// 模块：R2 子问题检测 + R5 主动建议（requirements R2/R5；架构 4.3.2/4.8）
// 覆盖：进度落后/耗时超预计/长期无记录/弱验收四条检测（多原因假设、中性表述、
//       幂等去重、状态事件落库）；建议触发/安静窗口静默/上限/接受拒绝稍后处理。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>

#include "application/usecases/advice/AdviceUseCases.h"
#include "application/usecases/assessment/AssessmentUseCases.h"
#include "application/usecases/detection/DetectionUseCases.h"
#include "application/usecases/goal/GoalUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/ai/SqlAiRepository.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/knowledge/StateDefinitionsSeed.h"
#include "infrastructure/persistence/SqlAssessmentRepository.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlMelRepository.h"
#include "infrastructure/persistence/SqlStateRepository.h"

using namespace PersonOS;

namespace {
const char *kUserUid = "00000000-0000-0000-0000-0000000000aa";
const char *kMelUid = "00000000-0000-0000-0000-0000000000bb";
const char *kNowIso = "2026-09-28T12:00:00Z";
} // namespace

class TstDetectionAdvice : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_detection.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
        QVERIFY(Infrastructure::LearningManifestSeed(DatabaseManager::instance().database(),
                                                     m_clock)
                    .ensureSeeded());
        QVERIFY(Infrastructure::StateDefinitionsSeed(DatabaseManager::instance().database(),
                                                     m_clock)
                    .ensureSeeded());

        QSqlQuery user(DatabaseManager::instance().database());
        const bool userOk = user.exec(QStringLiteral(
            "INSERT INTO user_profiles_v3(uid,timezone_id,locale,onboarding_status,profile_json,"
            "created_at,updated_at) VALUES('00000000-0000-0000-0000-0000000000aa',"
            "'Asia/Shanghai','zh-CN','complete','{}','2026-09-27T00:00:00Z','2026-09-27T00:00:00Z')"));
        if (!userOk)
            QFAIL(qPrintable(QStringLiteral("user insert: %1").arg(user.lastError().text())));

        QSqlQuery goal(DatabaseManager::instance().database());
        const bool goalOk = goal.exec(QStringLiteral(
            "INSERT INTO goals_v3(uid,user_id,domain_manifest_id,title,description,goal_type,"
            "status,priority,desired_level_json,user_defined_level,sort_order,created_at,"
            "updated_at) SELECT '00000000-0000-0000-0000-0000000000ca',u.id,dm.id,'检测目标',"
            "'','course','active',1,'{}',1,0,'2026-09-27T00:00:00Z','2026-09-27T00:00:00Z' "
            "FROM user_profiles_v3 u, domain_manifests_v3 dm WHERE "
            "u.uid='00000000-0000-0000-0000-0000000000aa' AND dm.domain_code='learning'"));
        if (!goalOk)
            QFAIL(qPrintable(QStringLiteral("goal insert: %1").arg(goal.lastError().text())));

        // 活跃 MEL：三天窗口，进度 0；激活时间早于观察窗口
        QSqlQuery mel(DatabaseManager::instance().database());
        const bool melOk = mel.exec(QStringLiteral(
            "INSERT INTO mels_v4(uid,user_id,goal_id,manifest_version_id,title,state,"
            "planned_start_at,planned_end_at,timezone_id,settlement_mode,capacity_min,"
            "reserve_min,rationale,confirmed_at,activated_at,created_at,updated_at) "
            "SELECT '%1',u.id,g.id,mv.id,'检测MEL','active','2026-09-27T00:00:00Z',"
            "'2026-09-30T00:00:00Z','Asia/Shanghai','deadline',300,30,'测试',"
            "'2026-09-27T00:10:00Z','2026-09-27T00:20:00Z','2026-09-27T00:20:00Z',"
            "'2026-09-27T00:20:00Z' FROM user_profiles_v3 u, goals_v3 g, "
            "domain_manifest_versions_v3 mv WHERE "
            "u.uid='00000000-0000-0000-0000-0000000000aa' AND g.title='检测目标' AND mv.version_no=1")
                                       .arg(QString::fromLatin1(kMelUid)));
        if (!melOk)
            QFAIL(qPrintable(QStringLiteral("mel insert: %1").arg(mel.lastError().text())));

        // 一个必做任务：计划 100 分钟；实际上报 200 分钟（超 1.5 倍阈值）
        QSqlQuery task(DatabaseManager::instance().database());
        const bool taskOk = task.exec(QStringLiteral(
            "INSERT INTO mel_tasks_v4(uid,mel_id,title,description,sequence_no,required,"
            "planned_effort_min,completion_rule_json,state,progress,completed_at,created_at,"
            "updated_at) SELECT '00000000-0000-0000-0000-0000000000da',m.id,'任务',NULL,0,1,"
            "100,'{}','active',0.0,NULL,'2026-09-27T00:00:00Z','2026-09-27T00:00:00Z' "
            "FROM mels_v4 m WHERE m.uid='%1'").arg(QString::fromLatin1(kMelUid)));
        if (!taskOk)
            QFAIL(qPrintable(QStringLiteral("task insert: %1").arg(task.lastError().text())));

        QSqlQuery events(DatabaseManager::instance().database());
        const bool eventsOk = events.exec(QStringLiteral(
            "INSERT INTO progress_events_v4(uid,user_id,mel_id,goal_id,task_id,event_type,"
            "amount,unit,note,evidence_asset_uid,occurred_at,recorded_at,actor_type,"
            "idempotency_key) SELECT '00000000-0000-0000-0000-0000000000ea',u.id,m.id,g.id,"
            "t.id,'incremented',200,'minutes','学习记录',NULL,'2026-09-27T09:00:00Z',"
            "'2026-09-27T09:00:00Z','user','detect:ev:1' FROM user_profiles_v3 u, mels_v4 m, "
            "goals_v3 g, mel_tasks_v4 t WHERE "
            "u.uid='00000000-0000-0000-0000-0000000000aa' AND m.uid='%1' AND g.title='检测目标' "
            "AND t.title='任务'").arg(QString::fromLatin1(kMelUid)));
        if (!eventsOk)
            QFAIL(qPrintable(QStringLiteral("events insert: %1").arg(events.lastError().text())));
    }

    Application::DetectionUseCases::DetectionConfig appConfig()
    {
        Application::DetectionUseCases::DetectionConfig c;
        c.progressLagTolerance = 0.3;
        c.inactivityMinutes = 60;
        c.effortRatioThreshold = 1.5;
        c.weakAssessmentWindowMinutes = 100000;
        c.basisSource = "test:detection-config:v1";
        return c;
    }

    void detectionFindsMultipleCausesNeutrally()
    {
        Infrastructure::SqlStateRepository stateRepo(DatabaseManager::instance().database(),
                                                     m_clock);
        Infrastructure::SqlMelRepository melRepo(DatabaseManager::instance().database(),
                                                 m_clock);
        Infrastructure::SqlAssessmentRepository assessmentRepo(
            DatabaseManager::instance().database(), m_clock);
        Application::DetectionUseCases useCases(stateRepo, melRepo, assessmentRepo, m_uids,
                                                m_clock);
        const auto userUid = *Domain::Uid::parse(kUserUid);

        // 无依据配置 → Validation
        Application::DetectionUseCases::DetectionConfig bad = appConfig();
        bad.basisSource.clear();
        QVERIFY(!useCases.detect(userUid, kNowIso, bad));

        const auto result = useCases.detect(userUid, kNowIso, appConfig());
        if (!result)
            QFAIL(qPrintable(QString::fromStdString(result.error().message + ": "
                                                    + result.error().detail)));
        // 进度落后 + 耗时超预计 + 长期无记录（最近记录 09-27T09:00，now 09-28T12:00 > 60 分钟）
        QCOMPARE(result.value(), 3);

        // 幂等：同一时点重复检测不重复落事件
        const auto again = useCases.detect(userUid, kNowIso, appConfig());
        QVERIFY(again);
        QCOMPARE(again.value(), 0);

        // 中性表述：多原因假设且不含羞辱性标签
        const auto recent = useCases.recentDetections(userUid, 20);
        QVERIFY(recent);
        QVERIFY(recent.value().size() >= 3);
        bool sawLagCauses = false;
        for (const auto &event : recent.value()) {
            if (event.definitionCode != "subproblem_progress_lag")
                continue;
            const auto value = QJsonDocument::fromJson(
                QString::fromStdString(event.valueJson).toUtf8());
            const QJsonArray causes = value.object().value(QStringLiteral("causes")).toArray();
            QVERIFY(causes.size() >= 3);   // 多原因假设
            sawLagCauses = true;
            // 明确不贴"不自律"标签
            QVERIFY(event.valueJson.find("不自律") == std::string::npos
                    || event.valueJson.find("非自律问题") != std::string::npos);
        }
        QVERIFY(sawLagCauses);
    }

    void weakAssessmentTriggersDetection()
    {
        Infrastructure::SqlStateRepository stateRepo(DatabaseManager::instance().database(),
                                                     m_clock);
        Infrastructure::SqlMelRepository melRepo(DatabaseManager::instance().database(),
                                                 m_clock);
        Infrastructure::SqlAssessmentRepository assessmentRepo(
            DatabaseManager::instance().database(), m_clock);
        Application::DetectionUseCases detection(stateRepo, melRepo, assessmentRepo, m_uids,
                                                 m_clock);
        Application::AssessmentUseCases assessmentUseCases(assessmentRepo, m_uids, m_clock);
        const auto userUid = *Domain::Uid::parse(kUserUid);

        // 真实验收：not_recalled 结果
        QSqlQuery goal(DatabaseManager::instance().database());
        QVERIFY(goal.exec(QStringLiteral(
            "SELECT uid FROM goals_v3 WHERE title='检测目标'")));
        QVERIFY(goal.next());
        const auto goalUid = *Domain::Uid::parse(goal.value(0).toString().toStdString());

        Application::AssessmentUseCases::CreateInput input;
        input.userId = userUid;
        input.goalId = goalUid;
        input.assessmentType = "recall";
        input.generatedBy = "user";
        Domain::AssessmentItem item;
        item.prompt = QStringLiteral("检测题").toStdString();
        item.itemType = "recall";
        item.sourceRefsJson = std::string("[]");
        input.items.push_back(item);
        const auto created = assessmentUseCases.createAssessment(input);
        QVERIFY(created);
        QVERIFY(assessmentUseCases.readyAssessment(created.value().assessment.uid, 1));
        Application::AssessmentUseCases::SubmitInput submit;
        submit.answerJson = std::string("{\"answer\":\"不会\"}");
        submit.idempotencyKey = std::string("detect:attempt:1");
        const auto attempt =
            assessmentUseCases.submitAttempt(created.value().assessment.uid, submit);
        QVERIFY(attempt);
        const auto items = assessmentRepo.itemsOf(created.value().assessment.uid);
        QVERIFY(!items.empty());
        const auto current = assessmentRepo.findByUid(created.value().assessment.uid);
        QVERIFY(current.has_value());
        Application::AssessmentUseCases::ScoreInput score;
        score.attemptId = attempt.value().uid;
        score.scorer = "user";
        Application::AssessmentUseCases::ScoreInput::ItemScore itemScore;
        itemScore.itemId = items.front().uid;
        itemScore.mastery = Domain::Mastery::NotRecalled;
        score.itemScores.push_back(itemScore);
        QVERIFY(assessmentUseCases.scoreAttempt(created.value().assessment.uid,
                                                current->revision, score));

        // 现在时点窗口内应新增弱验收检测
        const auto result = detection.detect(userUid, kNowIso, appConfig());
        QVERIFY(result);
        // 之前三类已有有效事件（去重跳过），弱验收新增 1
        QCOMPARE(result.value(), 1);
    }

    void adviceTriggersQuietWindowAndFeedback()
    {
        Infrastructure::SqlStateRepository stateRepo(DatabaseManager::instance().database(),
                                                     m_clock);
        Infrastructure::SqlMelRepository melRepo(DatabaseManager::instance().database(),
                                                 m_clock);
        Infrastructure::SqlAiRepository decisionRepo(DatabaseManager::instance().database(),
                                                    m_clock);
        Application::AdviceUseCases useCases(decisionRepo, stateRepo, melRepo, m_uids, m_clock);
        const auto userUid = *Domain::Uid::parse(kUserUid);

        Application::AdviceUseCases::AdviceConfig config;
        config.quietWindowMinutes = 1440;
        config.maxActiveAdvice = 5;
        config.basisSource = "test:advice-config:v1";

        // 无依据 → Validation
        Application::AdviceUseCases::AdviceConfig bad = config;
        bad.basisSource.clear();
        QVERIFY(!useCases.evaluate(userUid, kNowIso, bad, Domain::SourceMode::Ungrounded));

        const auto created = useCases.evaluate(userUid, kNowIso, config,
                                               Domain::SourceMode::Ungrounded);
        QVERIFY(created);
        QVERIFY(created.value() >= 1);

        // 安静窗口内重复评估不新增
        const auto again = useCases.evaluate(userUid, kNowIso, config,
                                             Domain::SourceMode::Ungrounded);
        QVERIFY(again);
        QCOMPARE(again.value(), 0);

        // 待处理列表 + 用户反馈
        const auto pending = useCases.pendingAdvice(20);
        QVERIFY(pending && !pending.value().empty());
        const auto adviceUid = pending.value().front().uid;
        const auto accepted = useCases.respond(adviceUid, "accepted", "已按建议调整");
        QVERIFY(accepted);
        QVERIFY(accepted.value().userStatus == Domain::DecisionUserStatus::Accepted);
        QVERIFY(accepted.value().confirmedAt.has_value());

        const auto invalid = useCases.respond(adviceUid, "maybe", "");
        QVERIFY(!invalid);

        // snoozed 保持待处理；rejected 终态
        const auto second = pending.value()[1];
        QVERIFY(useCases.respond(second.uid, "snoozed", "稍后处理"));
        const auto afterSnooze = decisionRepo.findDecision(second.uid);
        QVERIFY(afterSnooze);
        QVERIFY(afterSnooze->userStatus == Domain::DecisionUserStatus::Pending);
        QVERIFY(useCases.respond(second.uid, "rejected", "不适用"));
        const auto afterReject = decisionRepo.findDecision(second.uid);
        QVERIFY(afterReject);
        QVERIFY(afterReject->userStatus == Domain::DecisionUserStatus::Rejected);
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstDetectionAdvice)
#include "tst_detection_advice.moc"
