// IMP-003 补充项：保持抽查/遗忘调度（架构 4.3.6、DR-021；requirements R3.4.1）
// 覆盖：调度建立（参数须带依据）、到期候选（active 过滤/到期过滤/弱表现优先）、
//       三档结果驱动下一次间隔、参数不写死（不同参数不同间隔）、
//       推迟不算能力下降、乐观并发、关闭非关键抽查。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "application/usecases/assessment/AssessmentUseCases.h"
#include "application/usecases/assessment/RetentionUseCases.h"
#include "application/usecases/goal/GoalUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/persistence/SqlAssessmentRepository.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlRetentionRepository.h"

using namespace PersonOS;

namespace {
const char *kUserUid = "00000000-0000-0000-0000-0000000000aa";
const char *kMapUid = "00000000-0000-0000-0000-0000000000cc";
const char *kNodeUid = "00000000-0000-0000-0000-0000000000dd";
const char *kNode2Uid = "00000000-0000-0000-0000-0000000000ee";
} // namespace

class TstRetention : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_retention.db"));
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
        const bool userOk = user.exec(QStringLiteral(
            "INSERT INTO user_profiles_v3(uid,timezone_id,locale,onboarding_status,profile_json,"
            "created_at,updated_at) VALUES('00000000-0000-0000-0000-0000000000aa',"
            "'Asia/Shanghai','zh-CN','complete','{}','2026-09-27T00:00:00Z','2026-09-27T00:00:00Z')"));
        if (!userOk)
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
        goalInput.title = QStringLiteral("保持抽查目标").toStdString();
        goalInput.desiredLevelJson = std::string("{}");
        goalInput.userDefinedLevel = true;
        QVERIFY(goalUseCases.createGoal(goalInput));

        // 已确认地图与两个章节节点（保持抽查的目标内容）
        QSqlQuery map(DatabaseManager::instance().database());
        const bool mapOk = map.exec(QStringLiteral(
            "INSERT INTO content_maps_v3(uid,goal_id,title,source_type,status,created_at,"
            "updated_at) SELECT '%1',g.id,'测试教材','user_document','confirmed',"
            "'2026-09-27T00:00:00Z','2026-09-27T00:00:00Z' FROM goals_v3 g "
            "WHERE g.title='保持抽查目标'")
                                    .arg(QString::fromLatin1(kMapUid)));
        if (!mapOk)
            QFAIL(qPrintable(QStringLiteral("map insert: %1").arg(map.lastError().text())));
        QSqlQuery nodes(DatabaseManager::instance().database());
        const bool nodesOk = nodes.exec(QStringLiteral(
            "INSERT INTO content_nodes_v3(uid,content_map_id,parent_node_id,title,node_type,"
            "sequence_no,weight,locator_json,created_at,updated_at) "
            "SELECT '%1',cm.id,NULL,'第一章','chapter',0,1,'{}','2026-09-27T00:00:00Z',"
            "'2026-09-27T00:00:00Z' FROM content_maps_v3 cm WHERE cm.uid='%2'")
                                           .arg(QString::fromLatin1(kNodeUid),
                                                QString::fromLatin1(kMapUid)));
        if (!nodesOk)
            QFAIL(qPrintable(QStringLiteral("node insert: %1").arg(nodes.lastError().text())));
        QSqlQuery nodes2(DatabaseManager::instance().database());
        const bool nodes2Ok = nodes2.exec(QStringLiteral(
            "INSERT INTO content_nodes_v3(uid,content_map_id,parent_node_id,title,node_type,"
            "sequence_no,weight,locator_json,created_at,updated_at) "
            "SELECT '%1',cm.id,NULL,'第二章','chapter',1,1,'{}','2026-09-27T00:00:00Z',"
            "'2026-09-27T00:00:00Z' FROM content_maps_v3 cm WHERE cm.uid='%2'")
                                              .arg(QString::fromLatin1(kNode2Uid),
                                                   QString::fromLatin1(kMapUid)));
        if (!nodes2Ok)
            QFAIL(qPrintable(QStringLiteral("node2 insert: %1").arg(nodes2.lastError().text())));
    }

    Domain::RetentionParameters params()
    {
        Domain::RetentionParameters p;
        p.baseIntervalMin = 600;            // 10 小时
        p.fluentMultiplier = 1.5;
        p.promptedMultiplier = 1.0;
        p.notRecalledMultiplier = 0.6;
        p.minIntervalMin = 60;
        p.maxIntervalMin = 10080;
        p.basisSource = "method:spaced-retrieval:v1";   // 方法库来源标识
        return p;
    }

    void createScheduleAndValidation()
    {
        Infrastructure::SqlRetentionRepository repo(DatabaseManager::instance().database(),
                                                    m_clock);
        Application::RetentionUseCases useCases(repo, m_uids, m_clock);
        const auto userUid = *Domain::Uid::parse(kUserUid);
        const auto nodeUid = *Domain::Uid::parse(kNodeUid);

        Application::RetentionUseCases::CreateInput input;
        input.userId = userUid;
        input.contentNodeUid = nodeUid;
        input.algorithmCode = "multiplier_schedule";
        input.algorithmVersion = "1";
        input.parameters = params();
        const auto created = useCases.createSchedule(input);
        if (!created)
            QFAIL(qPrintable(QString::fromStdString(created.error().message + ": "
                                                    + created.error().detail)));
        QCOMPARE(created.value().intervalMin, 600);
        QCOMPARE(QString::fromStdString(created.value().nextDueAt),
                 QString::fromStdString(m_clock.utcIsoPlusMinutes(600)));
        QVERIFY(created.value().active);

        // 缺依据 → Validation（算法不得无来源运行）
        Application::RetentionUseCases::CreateInput noBasis = input;
        noBasis.parameters.basisSource.clear();
        QVERIFY(!useCases.createSchedule(noBasis));

        // 非法参数 → Validation
        Application::RetentionUseCases::CreateInput bad = input;
        bad.parameters.baseIntervalMin = 0;
        QVERIFY(!useCases.createSchedule(bad));

        // 目标互斥：同时给 node+item → Validation
        Application::RetentionUseCases::CreateInput both = input;
        both.assessmentItemUid = nodeUid;
        QVERIFY(!useCases.createSchedule(both));

        // 都不给 → Validation
        Application::RetentionUseCases::CreateInput none = input;
        none.contentNodeUid.reset();
        QVERIFY(!useCases.createSchedule(none));
    }

    void dueCandidatesFilterAndOrder()
    {
        Infrastructure::SqlRetentionRepository repo(DatabaseManager::instance().database(),
                                                    m_clock);
        Application::RetentionUseCases useCases(repo, m_uids, m_clock);
        const auto userUid = *Domain::Uid::parse(kUserUid);
        const auto nodeUid = *Domain::Uid::parse(kNodeUid);
        const auto node2Uid = *Domain::Uid::parse(kNode2Uid);

        // 未来到期的调度：不应出现在"现在"的到期候选中
        Application::RetentionUseCases::CreateInput futureInput;
        futureInput.userId = userUid;
        futureInput.contentNodeUid = nodeUid;
        futureInput.algorithmCode = "multiplier_schedule";
        futureInput.algorithmVersion = "1";
        futureInput.parameters = params();
        const auto future = useCases.createSchedule(futureInput);
        QVERIFY(future);

        // 已到期调度（直接改库到过去时间）
        Application::RetentionUseCases::CreateInput dueInput = futureInput;
        dueInput.contentNodeUid = node2Uid;
        const auto due = useCases.createSchedule(dueInput);
        QVERIFY(due);
        QSqlQuery setDue(DatabaseManager::instance().database());
        setDue.prepare(QStringLiteral(
            "UPDATE retention_schedules_v4 SET next_due_at='2026-09-01T00:00:00Z' WHERE uid=?"));
        setDue.addBindValue(QString::fromStdString(due.value().uid.value()));
        QVERIFY(setDue.exec());

        const auto candidates =
            useCases.dueCandidates(userUid, m_clock.utcIso(), 10);
        QVERIFY(candidates);
        QCOMPARE(candidates.value().size(), 1);   // 只有已到期的一个
        QCOMPARE(candidates.value().front().schedule.uid, due.value().uid);

        // 关闭非关键抽查：active=0 后不再进入到期候选
        const auto disabled = useCases.setActive(due.value().uid, false, 1);
        QVERIFY(disabled);
        const auto afterDisable = useCases.dueCandidates(userUid, m_clock.utcIso(), 10);
        QVERIFY(afterDisable);
        QCOMPARE(afterDisable.value().size(), 0);
        QVERIFY(useCases.setActive(due.value().uid, true, 2));
    }

    void applyResultDrivesInterval()
    {
        Infrastructure::SqlRetentionRepository repo(DatabaseManager::instance().database(),
                                                    m_clock);
        Infrastructure::SqlAssessmentRepository assessmentRepo(
            DatabaseManager::instance().database(), m_clock);
        Application::RetentionUseCases useCases(repo, m_uids, m_clock);
        Application::AssessmentUseCases assessmentUseCases(assessmentRepo, m_uids, m_clock);
        const auto userUid = *Domain::Uid::parse(kUserUid);
        const auto nodeUid = *Domain::Uid::parse(kNodeUid);

        // 建立调度（10 小时基准）
        Application::RetentionUseCases::CreateInput input;
        input.userId = userUid;
        input.contentNodeUid = nodeUid;
        input.algorithmCode = "multiplier_schedule";
        input.algorithmVersion = "1";
        input.parameters = params();
        const auto created = useCases.createSchedule(input);
        QVERIFY(created);

        // 一次真实抽查：验收（三档 fluent）→ 应用到调度
        const auto assessment = makeScoredAssessment(userUid, Domain::Mastery::Fluent);
        QVERIFY(assessment.has_value());
        Application::RetentionUseCases::ApplyResultInput apply;
        apply.resultUid = assessment->first;
        apply.mastery = Domain::Mastery::Fluent;
        apply.parameters = params();
        const auto fluent = useCases.applyResult(created.value().uid, apply, 1);
        if (!fluent)
            QFAIL(qPrintable(QString::fromStdString(fluent.error().message + ": "
                                                    + fluent.error().detail)));
        QCOMPARE(fluent.value().intervalMin, 900);   // 600 × 1.5
        QCOMPARE(QString::fromStdString(fluent.value().nextDueAt),
                 QString::fromStdString(m_clock.utcIsoPlusMinutes(900)));
        QVERIFY(fluent.value().lastResultUid.has_value());
        QCOMPARE(fluent.value().revision, 2);

        // 陈旧 revision → Conflict
        const auto stale = useCases.applyResult(created.value().uid, apply, 1);
        QVERIFY(!stale);
        QVERIFY(stale.error().code == Application::ErrorCode::Conflict);

        // 不同参数 → 不同间隔（参数不写死；乘性调度按当前间隔复利）
        auto tighter = params();
        tighter.fluentMultiplier = 2.0;
        Application::RetentionUseCases::ApplyResultInput tighterApply = apply;
        tighterApply.parameters = tighter;
        const auto reapply = useCases.applyResult(created.value().uid, tighterApply, 2);
        QVERIFY(reapply);
        QCOMPARE(reapply.value().intervalMin, 1800);   // 900 × 2.0

        // 提示后仍不能 → 缩短间隔（0.6 倍）
        const auto weak = makeScoredAssessment(userUid, Domain::Mastery::NotRecalled);
        QVERIFY(weak.has_value());
        Application::RetentionUseCases::ApplyResultInput weakApply;
        weakApply.resultUid = weak->first;
        weakApply.mastery = Domain::Mastery::NotRecalled;
        weakApply.parameters = params();
        const auto weakened = useCases.applyResult(created.value().uid, weakApply, 3);
        QVERIFY(weakened);
        QCOMPARE(weakened.value().intervalMin, 1080);   // 1800 × 0.6

        // 参数越界（min>max）→ Validation，不猜测
        auto badParams = params();
        badParams.minIntervalMin = 99999;
        badParams.maxIntervalMin = 1;
        Application::RetentionUseCases::ApplyResultInput badApply = weakApply;
        badApply.parameters = badParams;
        QVERIFY(!useCases.applyResult(created.value().uid, badApply, 4));
    }

    void postponeIsNotDecline()
    {
        Infrastructure::SqlRetentionRepository repo(DatabaseManager::instance().database(),
                                                    m_clock);
        Application::RetentionUseCases useCases(repo, m_uids, m_clock);
        const auto userUid = *Domain::Uid::parse(kUserUid);
        const auto nodeUid = *Domain::Uid::parse(kNode2Uid);

        Application::RetentionUseCases::CreateInput input;
        input.userId = userUid;
        input.contentNodeUid = nodeUid;
        input.algorithmCode = "multiplier_schedule";
        input.algorithmVersion = "1";
        input.parameters = params();
        const auto created = useCases.createSchedule(input);
        QVERIFY(created);
        const int intervalBefore = created.value().intervalMin;

        // 推迟到未来：只改到期时间，不改变间隔与历史表现
        const auto postponed = useCases.postpone(created.value().uid,
                                                 m_clock.utcIsoPlusMinutes(120), 1);
        QVERIFY(postponed);
        QCOMPARE(QString::fromStdString(postponed.value().nextDueAt),
                 QString::fromStdString(m_clock.utcIsoPlusMinutes(120)));
        QCOMPARE(postponed.value().intervalMin, intervalBefore);
        QVERIFY(!postponed.value().lastResultUid.has_value());

        // 推迟到过去/格式错误 → Validation
        QVERIFY(!useCases.postpone(created.value().uid, "2026-09-01T00:00:00Z", 2));
        QVERIFY(!useCases.postpone(created.value().uid, "tomorrow", 2));
    }

    void dueOrderingPrefersWeakMastery()
    {
        Infrastructure::SqlRetentionRepository repo(DatabaseManager::instance().database(),
                                                    m_clock);
        Infrastructure::SqlAssessmentRepository assessmentRepo(
            DatabaseManager::instance().database(), m_clock);
        Application::RetentionUseCases useCases(repo, m_uids, m_clock);
        Application::AssessmentUseCases assessmentUseCases(assessmentRepo, m_uids, m_clock);
        const auto userUid = *Domain::Uid::parse(kUserUid);

        // 两个调度，同一到期时间：表现弱（not_recalled）的应排在前
        const auto weakResult = makeScoredAssessment(userUid, Domain::Mastery::NotRecalled);
        const auto strongResult = makeScoredAssessment(userUid, Domain::Mastery::Fluent);
        QVERIFY(weakResult.has_value() && strongResult.has_value());

        Application::RetentionUseCases::CreateInput weakInput;
        weakInput.userId = userUid;
        weakInput.assessmentItemUid = Domain::Uid::parse(weakResult->second); // 挂在验收题项上
        weakInput.algorithmCode = "multiplier_schedule";
        weakInput.algorithmVersion = "1";
        weakInput.parameters = params();
        const auto weak = useCases.createSchedule(weakInput);
        QVERIFY(weak);
        Application::RetentionUseCases::ApplyResultInput weakApply;
        weakApply.resultUid = weakResult->first;
        weakApply.mastery = Domain::Mastery::NotRecalled;
        weakApply.parameters = params();
        QVERIFY(useCases.applyResult(weak.value().uid, weakApply, 1));

        Application::RetentionUseCases::CreateInput strongInput = weakInput;
        strongInput.assessmentItemUid = Domain::Uid::parse(strongResult->second);
        const auto strong = useCases.createSchedule(strongInput);
        QVERIFY(strong);
        Application::RetentionUseCases::ApplyResultInput strongApply;
        strongApply.resultUid = strongResult->first;
        strongApply.mastery = Domain::Mastery::Fluent;
        strongApply.parameters = params();
        QVERIFY(useCases.applyResult(strong.value().uid, strongApply, 1));

        QSqlQuery align(DatabaseManager::instance().database());
        const bool alignOk = align.exec(QStringLiteral(
            "UPDATE retention_schedules_v4 SET next_due_at='2026-09-01T00:00:00Z' "
            "WHERE user_id=(SELECT id FROM user_profiles_v3 WHERE "
            "uid='00000000-0000-0000-0000-0000000000aa')"));
        if (!alignOk)
            QFAIL(qPrintable(QStringLiteral("align: %1").arg(align.lastError().text())));

        const auto candidates = useCases.dueCandidates(userUid, m_clock.utcIso(), 10);
        QVERIFY(candidates);
        QVERIFY(candidates.value().size() >= 2);
        // 找弱/强两个候选的相对顺序：not_recalled 在前
        int weakIndex = -1;
        int strongIndex = -1;
        for (int i = 0; i < static_cast<int>(candidates.value().size()); ++i) {
            if (candidates.value()[i].schedule.uid == weak.value().uid)
                weakIndex = i;
            if (candidates.value()[i].schedule.uid == strong.value().uid)
                strongIndex = i;
        }
        QVERIFY(weakIndex >= 0 && strongIndex >= 0);
        QVERIFY(weakIndex < strongIndex);
    }

private:
    // 跑一次真实验收：创建 1 题验收 → 作答 → 评分，返回 (resultUid, itemUid)
    std::optional<std::pair<std::string, std::string>> makeScoredAssessment(
        const Domain::Uid &userId, Domain::Mastery mastery)
    {
        Infrastructure::SqlAssessmentRepository repo(DatabaseManager::instance().database(),
                                                     m_clock);
        Application::AssessmentUseCases useCases(repo, m_uids, m_clock);
        const auto goalUid = [&]() {
            QSqlQuery q(DatabaseManager::instance().database());
            q.exec(QStringLiteral("SELECT uid FROM goals_v3 WHERE title='保持抽查目标'"));
            q.next();
            return *Domain::Uid::parse(q.value(0).toString().toStdString());
        }();

        Application::AssessmentUseCases::CreateInput input;
        input.userId = userId;
        input.goalId = goalUid;
        input.assessmentType = "recall";
        input.generatedBy = "user";
        Domain::AssessmentItem item;
        item.prompt = QStringLiteral("抽查：请复述第一章要点").toStdString();
        item.itemType = "recall";
        item.sourceRefsJson = std::string("[]");
        input.items.push_back(item);
        const auto created = useCases.createAssessment(input);
        if (!created)
            return std::nullopt;
        if (!useCases.readyAssessment(created.value().assessment.uid, 1))
            return std::nullopt;

        Application::AssessmentUseCases::SubmitInput submit;
        submit.answerJson = std::string("{\"answer\":\"要点\"}");
        submit.idempotencyKey = std::string("retention:") + m_uids.next().value();
        const auto attempt = useCases.submitAttempt(created.value().assessment.uid, submit);
        if (!attempt)
            return std::nullopt;

        // 题项 uid 由用例在插入时生成，从仓储读回
        const auto items = repo.itemsOf(created.value().assessment.uid);
        if (items.empty())
            return std::nullopt;

        // submitAttempt 已推进状态（ready→in_progress，revision+1），按当前 revision 评分
        const auto currentAssessment = repo.findByUid(created.value().assessment.uid);
        if (!currentAssessment)
            return std::nullopt;

        Application::AssessmentUseCases::ScoreInput score;
        score.attemptId = attempt.value().uid;
        score.scorer = "user";
        Application::AssessmentUseCases::ScoreInput::ItemScore itemScore;
        itemScore.itemId = items.front().uid;
        itemScore.mastery = mastery;
        itemScore.score = mastery == Domain::Mastery::Fluent ? 1.0 : 0.0;
        score.itemScores.push_back(itemScore);
        const auto results = useCases.scoreAttempt(created.value().assessment.uid,
                                                   currentAssessment->revision, score);
        if (!results || results.value().empty())
            return std::nullopt;
        return std::make_pair(results.value().front().uid.value(),
                              items.front().uid.value());
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstRetention)
#include "tst_retention.moc"
