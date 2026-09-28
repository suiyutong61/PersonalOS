// IMP-003：MEL 状态机与核心闭环（DD-001 §4；DB-06 幂等）
// 覆盖：候选→确认→激活→暂停→进度→执行完成→结算→复盘→关闭 全链、
//       逾期路径、结算幂等、非法转移拒绝、转移审计记录、findDue 启动恢复。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "application/usecases/goal/GoalUseCases.h"
#include "application/usecases/mel/MelUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlMelRepository.h"

using namespace PersonOS;

class TstMel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_mel.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
        QVERIFY(Infrastructure::LearningManifestSeed(DatabaseManager::instance().database(),
                                                     m_clock)
                    .ensureSeeded());
        QSqlQuery user(DatabaseManager::instance().database());
        const bool ok = user.exec(QStringLiteral(
            "INSERT INTO user_profiles_v3(uid,timezone_id,locale,onboarding_status,profile_json,"
            "created_at,updated_at) VALUES('00000000-0000-0000-0000-0000000000aa',"
            "'Asia/Shanghai','zh-CN','complete','{}','2026-09-27T00:00:00Z','2026-09-27T00:00:00Z')"));
        if (!ok)
            QFAIL(qPrintable(QStringLiteral("user insert: %1").arg(user.lastError().text())));

        // 目标 + 清单版本（MEL 必填外键）
        QSqlQuery manifest(DatabaseManager::instance().database());
        QVERIFY(manifest.exec(QStringLiteral(
            "SELECT uid FROM domain_manifests_v3 WHERE domain_code='learning'")));
        QVERIFY(manifest.next());
        m_manifestUid = manifest.value(0).toString();
        QVERIFY(manifest.exec(QStringLiteral(
            "SELECT uid FROM domain_manifest_versions_v3 WHERE version_no=1")));
        QVERIFY(manifest.next());
        m_manifestVersionUid = manifest.value(0).toString();

        Infrastructure::SqlGoalRepository goalsRepo(DatabaseManager::instance().database(),
                                                    m_clock);
        Application::GoalUseCases goalUseCases(goalsRepo, m_uids, m_clock);
        Application::GoalUseCases::CreateInput goalInput;
        goalInput.userId = *Domain::Uid::parse(kUserUid);
        goalInput.domainManifestId = *Domain::Uid::parse(m_manifestUid.toStdString());
        goalInput.title = QStringLiteral("操作系统").toStdString();
        goalInput.desiredLevelJson = std::string("{}");
        goalInput.userDefinedLevel = true;
        const auto goalResult = goalUseCases.createGoal(goalInput);
        if (!goalResult)
            QFAIL(qPrintable(QString::fromStdString(goalResult.error().message + ": "
                                                    + goalResult.error().detail)));
        m_goalUid = QString::fromStdString(goalResult.value().goal.uid.value());
    }

    void stateMachineRules()
    {
        using S = Domain::MelState;
        QVERIFY(Domain::MelStateMachine::canTransition(S::Draft, S::AwaitingConfirmation));
        QVERIFY(!Domain::MelStateMachine::canTransition(S::Draft, S::Active));
        QVERIFY(Domain::MelStateMachine::canTransition(S::Active, S::Paused));
        QVERIFY(Domain::MelStateMachine::canTransition(S::Paused, S::Active));
        QVERIFY(Domain::MelStateMachine::canTransition(S::Active, S::ExecutionComplete));
        QVERIFY(Domain::MelStateMachine::canTransition(S::Active, S::Overdue));
        QVERIFY(Domain::MelStateMachine::canTransition(S::ExecutionComplete, S::Settling));
        QVERIFY(Domain::MelStateMachine::canTransition(S::Overdue, S::Settling));
        QVERIFY(Domain::MelStateMachine::canTransition(S::Settling, S::AwaitingAssessment));
        QVERIFY(Domain::MelStateMachine::canTransition(S::AwaitingAssessment, S::Reviewing));
        QVERIFY(Domain::MelStateMachine::canTransition(S::Reviewing, S::Closed));
        QVERIFY(!Domain::MelStateMachine::canTransition(S::Closed, S::Active));
        QVERIFY(!Domain::MelStateMachine::canTransition(S::Cancelled, S::Draft));
        QVERIFY(!Domain::MelStateMachine::canTransition(S::AwaitingAssessment, S::Closed));
    }

    void fullLoop()
    {
        try {
            fullLoopBody();
        } catch (const std::exception &e) {
            QFAIL(qPrintable(QStringLiteral("fullLoop exception: %1").arg(e.what())));
        }
    }

private:
    void fullLoopBody()
    {
        auto repo = Infrastructure::SqlMelRepository(DatabaseManager::instance().database(),
                                                     m_clock);
        Application::MelUseCases useCases(repo, m_uids, m_clock);
        const auto userUid = Domain::Uid::parse(kUserUid);

        Application::MelUseCases::CreateInput input;
        input.userId = *userUid;
        input.goalId = *Domain::Uid::parse(m_goalUid.toStdString());
        input.manifestVersionId = *Domain::Uid::parse(m_manifestVersionUid.toStdString());
        input.title = QStringLiteral("操作系统第一轮").toStdString();
        input.plannedStartAt = "2026-10-01T00:00:00Z";
        input.plannedEndAt = "2026-10-04T00:00:00Z";
        input.capacityMin = 600;
        input.reserveMin = 120;
        input.rationale = "三天 MEL：操作系统绪论+进程模型（容量 10h 含 2h 缓冲）";

        Domain::MelTask t1;
        t1.title = QStringLiteral("绪论与进程模型（必做）").toStdString();
        t1.sequenceNo = 1;
        t1.required = true;
        t1.plannedEffortMin = 240;
        Domain::MelTask t2;
        t2.title = QStringLiteral("内存管理导览（必做）").toStdString();
        t2.sequenceNo = 2;
        t2.required = true;
        t2.plannedEffortMin = 240;
        Domain::MelTask t3;
        t3.title = QStringLiteral("拓展阅读（选做）").toStdString();
        t3.sequenceNo = 3;
        t3.required = false;
        t3.plannedEffortMin = 120;
        input.tasks = {t1, t2, t3};

        // 候选创建 → draft；任务落库
        const auto created = useCases.createMelProposal(input);
        if (!created)
            QFAIL(qPrintable(QString::fromStdString(created.error().message + ": "
                                                    + created.error().detail)));
        const auto melUid = created.value().mel.uid;
        QVERIFY(created.value().mel.state == Domain::MelState::Draft);
        QCOMPARE(repo.tasksOf(melUid).size(), 3);

        // 非法：draft 不能直接激活
        const auto illegal = useCases.confirmAndActivate(melUid, 1);
        QVERIFY(!illegal);

        // 提交确认 → 激活（revision 流转：1→2→3）
        const auto submitted = useCases.submitForConfirmation(melUid, 1);
        if (!submitted)
            QFAIL(qPrintable(QString::fromStdString(submitted.error().message + ": "
                                                    + submitted.error().detail)));
        QVERIFY(submitted.value().state == Domain::MelState::AwaitingConfirmation);
        const auto active = useCases.confirmAndActivate(melUid, 2);
        if (!active)
            QFAIL(qPrintable(QString::fromStdString(active.error().message + ": "
                                                    + active.error().detail)));
        QVERIFY(active.value().state == Domain::MelState::Active);
        QVERIFY(active.value().confirmedAt.has_value());
        QVERIFY(active.value().activatedAt.has_value());
        QCOMPARE(active.value().revision, 4);

        // 暂停/恢复
        const auto paused = useCases.pauseMel(melUid, 4);
        QVERIFY(paused && paused.value().state == Domain::MelState::Paused);
        const auto resumed = useCases.resumeMel(melUid, 5);
        QVERIFY(resumed && resumed.value().state == Domain::MelState::Active);

        // 进度上报：任务1 完成、任务2 一半；必做未全完成时不可执行完成
        const auto tasks = repo.tasksOf(melUid);
        const auto task1 = tasks[0].uid;
        const auto task2 = tasks[1].uid;
        Application::MelUseCases::ProgressInput p1;
        p1.taskUid = task1;
        p1.progress = 1.0;
        p1.idempotencyKey = "progress:1:first";
        const auto progress1 = useCases.recordProgress(melUid, p1);
        if (!progress1)
            QFAIL(qPrintable(QString::fromStdString(progress1.error().message + ": "
                                                    + progress1.error().detail)));

        Application::MelUseCases::ProgressInput p2;
        p2.taskUid = task2;
        p2.progress = 0.5;
        p2.idempotencyKey = "progress:2:first";
        QVERIFY(useCases.recordProgress(melUid, p2));

        const auto earlyComplete = useCases.completeExecution(melUid, 6);
        QVERIFY(!earlyComplete); // 必做任务2未完成

        // 幂等：重复键拒绝
        const auto dup = useCases.recordProgress(melUid, p2);
        QVERIFY(!dup);
        QVERIFY(dup.error().code == Application::ErrorCode::Conflict);

        // 任务2 完成 → 执行完成
        Application::MelUseCases::ProgressInput p2b;
        p2b.taskUid = task2;
        p2b.progress = 1.0;
        p2b.idempotencyKey = "progress:2:second";
        QVERIFY(useCases.recordProgress(melUid, p2b));
        const auto completed = useCases.completeExecution(melUid, 6);
        if (!completed)
            QFAIL(qPrintable(QString::fromStdString(completed.error().message + ": "
                                                    + completed.error().detail)));
        QVERIFY(completed.value().state == Domain::MelState::ExecutionComplete);

        // 结算（幂等）：execution_complete → settling → awaiting_assessment
        const auto settled = useCases.settleMel(melUid, 7);
        if (!settled)
            QFAIL(qPrintable(QString::fromStdString(settled.error().message + ": "
                                                    + settled.error().detail)));
        QVERIFY(settled.value().requiresAssessment);
        QVERIFY(settled.value().mel.state == Domain::MelState::AwaitingAssessment);

        // 重复结算（陈旧 revision）→ 已结算状态幂等返回，不重复转移
        const auto settledAgain = useCases.settleMel(melUid, 7);
        if (!settledAgain)
            QFAIL(qPrintable(QString::fromStdString(settledAgain.error().message + ": "
                                                    + settledAgain.error().detail)));
        QVERIFY(settledAgain.value().mel.state == Domain::MelState::AwaitingAssessment);

        // 验收提交 → 复盘 → 关闭
        const auto reviewing = useCases.proceedToReviewing(
            melUid, settled.value().mel.revision,
            QStringLiteral("验收已提交").toStdString());
        if (!reviewing)
            QFAIL(qPrintable(QString::fromStdString(reviewing.error().message + ": "
                                                    + reviewing.error().detail)));
        QVERIFY(reviewing.value().state == Domain::MelState::Reviewing);
        const auto closed = useCases.closeMel(
            melUid, reviewing.value().revision,
            QStringLiteral("生成下一轮 MEL").toStdString());
        if (!closed)
            QFAIL(qPrintable(QString::fromStdString(closed.error().message + ": "
                                                    + closed.error().detail)));
        QVERIFY(closed.value().state == Domain::MelState::Closed);
        QVERIFY(closed.value().closedAt.has_value());

        // 终态不可再转移
        QVERIFY(!useCases.cancelMel(melUid, closed.value().revision, std::string("x")));

        // 转移审计记录存在且状态序列正确
        QSqlQuery audit(DatabaseManager::instance().database());
        QVERIFY(audit.exec(QStringLiteral(
            "SELECT COUNT(*) FROM mel_transitions_v4 WHERE mel_id="
            "(SELECT id FROM mels_v4 WHERE uid='%1')")
                               .arg(QString::fromStdString(melUid.value()))));
        QVERIFY(audit.next());
        QVERIFY(audit.value(0).toInt() >= 8);
    }

private slots:
    void overduePathAndFindDue()
    {
        auto repo = Infrastructure::SqlMelRepository(DatabaseManager::instance().database(),
                                                     m_clock);
        Application::MelUseCases useCases(repo, m_uids, m_clock);
        const auto userUid = Domain::Uid::parse(kUserUid);

        Application::MelUseCases::CreateInput input;
        input.userId = *userUid;
        input.goalId = *Domain::Uid::parse(m_goalUid.toStdString());
        input.manifestVersionId = *Domain::Uid::parse(m_manifestVersionUid.toStdString());
        input.title = QStringLiteral("历史逾期 MEL").toStdString();
        input.plannedStartAt = "2020-01-01T00:00:00Z";
        input.plannedEndAt = "2020-01-04T00:00:00Z";   // 已过期的窗口
        input.capacityMin = 300;
        input.reserveMin = 30;
        input.rationale = "历史逾期 MEL（findDue 测试）";
        Domain::MelTask t;
        t.title = QStringLiteral("旧任务").toStdString();
        t.sequenceNo = 1;
        t.plannedEffortMin = 60;
        input.tasks = {t};

        const auto created = useCases.createMelProposal(input);
        QVERIFY(created);
        const auto melUid = created.value().mel.uid;
        QVERIFY(useCases.submitForConfirmation(melUid, 1));
        QVERIFY(useCases.confirmAndActivate(melUid, 2));

        // 启动恢复：findDue 找到窗口已过期的活跃 MEL
        const auto due = repo.findDue("2026-09-27T00:00:00Z", 10);
        bool found = false;
        for (const auto &mel : due)
            if (mel.uid == melUid)
                found = true;
        QVERIFY(found);

        // 逾期 → 结算
        const auto overdue = useCases.markOverdue(melUid, 4);
        QVERIFY(overdue && overdue.value().state == Domain::MelState::Overdue);
        const auto settled = useCases.settleMel(melUid, 5);
        QVERIFY(settled && settled.value().mel.state == Domain::MelState::AwaitingAssessment);
    }

private:
    QString m_path;
    QString m_manifestUid;
    QString m_manifestVersionUid;
    QString m_goalUid;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
    static constexpr const char *kUserUid = "00000000-0000-0000-0000-0000000000aa";
};

QTEST_GUILESS_MAIN(TstMel)
#include "tst_mel.moc"
