// IMP-003c：能力验收（DD-001 §5.2；DR-022 三档结果；requirements R3.4）
// 覆盖：创建/发布/作答/评分（三档）/用户确认、幂等、来源标注、重复评分拒绝。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "application/usecases/assessment/AssessmentUseCases.h"
#include "application/usecases/goal/GoalUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/persistence/SqlAssessmentRepository.h"
#include "infrastructure/persistence/SqlGoalRepository.h"

using namespace PersonOS;

namespace {
const char *kUserUid = "00000000-0000-0000-0000-0000000000aa";
} // namespace

class TstAssessment : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_assessment.db"));
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

        QSqlQuery manifest(DatabaseManager::instance().database());
        QVERIFY(manifest.exec(QStringLiteral(
            "SELECT uid FROM domain_manifests_v3 WHERE domain_code='learning'")));
        QVERIFY(manifest.next());
        const QString manifestUid = manifest.value(0).toString();

        Infrastructure::SqlGoalRepository goalsRepo(DatabaseManager::instance().database(),
                                                    m_clock);
        Application::GoalUseCases goalUseCases(goalsRepo, m_uids, m_clock);
        Application::GoalUseCases::CreateInput goalInput;
        goalInput.userId = *Domain::Uid::parse(kUserUid);
        goalInput.domainManifestId = *Domain::Uid::parse(manifestUid.toStdString());
        goalInput.title = QStringLiteral("验收测试目标").toStdString();
        goalInput.desiredLevelJson = std::string("{}");
        goalInput.userDefinedLevel = true;
        const auto goalResult = goalUseCases.createGoal(goalInput);
        if (!goalResult)
            QFAIL(qPrintable(QString::fromStdString(goalResult.error().message + ": "
                                                    + goalResult.error().detail)));
        m_goalUid = QString::fromStdString(goalResult.value().goal.uid.value());
    }

    void assessmentLifecycle()
    {
        Infrastructure::SqlAssessmentRepository repo(
            DatabaseManager::instance().database(), m_clock);
        Application::AssessmentUseCases useCases(repo, m_uids, m_clock);
        const auto userUid = *Domain::Uid::parse(kUserUid);
        const auto goalUid = *Domain::Uid::parse(m_goalUid.toStdString());

        // 创建验收（含题目，来源标注 AI 生成）
        Application::AssessmentUseCases::CreateInput input;
        input.userId = userUid;
        input.goalId = goalUid;
        input.assessmentType = "recall";
        input.generatedBy = "ai";
        input.scopeJson = std::string("{\"nodes\":[\"进程模型\"]}");
        input.rubricJson = std::string("{\"prompt_threshold\":1}");

        Domain::AssessmentItem item1;
        item1.itemType = "recall";
        item1.prompt = QStringLiteral("解释进程状态转换").toStdString();
        item1.sourceRefsJson =
            QStringLiteral("[\"ai_generated:2026-09-27\"]").toStdString();
        Domain::AssessmentItem item2;
        item2.itemType = "explanation";
        item2.prompt = QStringLiteral("为什么需要进程控制块").toStdString();
        item2.sourceRefsJson =
            QStringLiteral("[\"ai_generated:2026-09-27\"]").toStdString();
        input.items = {item1, item2};

        const auto created = useCases.createAssessment(input);
        if (!created)
            QFAIL(qPrintable(QString::fromStdString(created.error().message + ": "
                                                    + created.error().detail)));
        const auto assessmentUid = created.value().assessment.uid;
        QVERIFY(created.value().assessment.status == Domain::AssessmentStatus::Draft);
        QCOMPARE(repo.itemsOf(assessmentUid).size(), 2);

        // 发布
        const auto ready = useCases.readyAssessment(assessmentUid, 1);
        if (!ready)
            QFAIL(qPrintable(QString::fromStdString(ready.error().message + ": "
                                                    + ready.error().detail)));
        QVERIFY(ready.value().status == Domain::AssessmentStatus::Ready);

        // 提交作答（幂等键）
        Application::AssessmentUseCases::SubmitInput submit;
        submit.answerJson = std::string("{\"answers\":{\"0\":\"就绪/运行/等待\",\"1\":\"PCB 保存上下文\"}}");
        submit.idempotencyKey = "attempt:1:first";
        const auto attempt = useCases.submitAttempt(assessmentUid, submit);
        if (!attempt)
            QFAIL(qPrintable(QString::fromStdString(attempt.error().message + ": "
                                                    + attempt.error().detail)));
        const auto attemptUid = attempt.value().uid;

        // 幂等：重复键拒绝
        const auto dup = useCases.submitAttempt(assessmentUid, submit);
        QVERIFY(!dup);
        QVERIFY(dup.error().code == Application::ErrorCode::Conflict);

        // AI 评分（三档）。提交作答已推进 assessment 状态（revision 2→3），
        // 评分用当前 revision。
        const auto currentAssessment = repo.findByUid(assessmentUid);
        QVERIFY(currentAssessment.has_value());
        const auto items = repo.itemsOf(assessmentUid);
        Application::AssessmentUseCases::ScoreInput score;
        score.attemptId = attemptUid;
        score.scorer = "ai";
        score.confidence = 0.9;
        score.itemScores = {
            {items[0].uid, Domain::Mastery::Fluent, 0.95, "流利回忆"},
            {items[1].uid, Domain::Mastery::Prompted, 0.7, "提示后完成"},
        };
        const auto results =
            useCases.scoreAttempt(assessmentUid, currentAssessment->revision, score);
        if (!results)
            QFAIL(qPrintable(QString::fromStdString(results.error().message + ": "
                                                    + results.error().detail)));
        QCOMPARE(results.value().size(), 2);
        QVERIFY(results.value()[0].mastery == Domain::Mastery::Fluent);
        QVERIFY(results.value()[1].mastery == Domain::Mastery::Prompted);

        // 同一 scorer 重复评分拒绝
        const auto rescore =
            useCases.scoreAttempt(assessmentUid, currentAssessment->revision + 1, score);
        QVERIFY(!rescore);

        // 用户确认结果
        const auto confirmed = useCases.confirmResult(results.value()[1].uid);
        if (!confirmed)
            QFAIL(qPrintable(QString::fromStdString(confirmed.error().message + ": "
                                                    + confirmed.error().detail)));
        const auto after = repo.resultsOfAttempt(attemptUid);
        bool foundConfirmed = false;
        for (const auto &result : after)
            if (result.uid == results.value()[1].uid && result.confirmedByUser)
                foundConfirmed = true;
        QVERIFY(foundConfirmed);

        // assessment 状态推进到 scored
        const auto finalAssessment = repo.findByUid(assessmentUid);
        QVERIFY(finalAssessment
                && finalAssessment->status == Domain::AssessmentStatus::Scored);
    }

private:
    QString m_path;
    QString m_goalUid;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstAssessment)
#include "tst_assessment.moc"
