// IMP-003 补充项：复盘问卷提交 + 个人校准最小闭环
// 覆盖：问卷种子（基本+十类 16 项）、跳过/未回答不视为确认、完成比例口径、
//       未知题目与区块重叠拒绝、可修改（同一行更新）、复盘关闭后拒绝；
//       校准：预测 vs 实际（加权口径）、只追加、activeValue 取最新、无预测不伪造。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>

#include "application/usecases/review/QuestionnaireUseCases.h"
#include "application/usecases/review/ReviewUseCases.h"
#include "application/usecases/state/CalibrationUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/knowledge/ReviewQuestionnaireSeed.h"
#include "infrastructure/persistence/SqlCalibrationRepository.h"
#include "infrastructure/persistence/SqlQuestionnaireRepository.h"
#include "infrastructure/persistence/SqlReviewRepository.h"

using namespace PersonOS;

namespace {
const char *kUserUid = "00000000-0000-0000-0000-0000000000aa";
const char *kMelUid = "00000000-0000-0000-0000-0000000000bb";
const char *kMel2Uid = "00000000-0000-0000-0000-0000000000bc";
} // namespace

class TstQuestionnaireCalibration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_questionnaire.db"));
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

        // 目标 + 两个 MEL 行（复盘/校准用例的引用）
        QSqlQuery goal(DatabaseManager::instance().database());
        const bool goalOk = goal.exec(QStringLiteral(
            "INSERT INTO goals_v3(uid,user_id,domain_manifest_id,title,description,goal_type,"
            "status,priority,desired_level_json,user_defined_level,sort_order,created_at,"
            "updated_at) SELECT '00000000-0000-0000-0000-0000000000ca',u.id,dm.id,'问卷校准目标',"
            "'','course','active',1,'{}',1,0,'2026-09-27T00:00:00Z','2026-09-27T00:00:00Z' "
            "FROM user_profiles_v3 u, domain_manifests_v3 dm WHERE "
            "u.uid='00000000-0000-0000-0000-0000000000aa' AND dm.domain_code='learning'"));
        if (!goalOk)
            QFAIL(qPrintable(QStringLiteral("goal insert: %1").arg(goal.lastError().text())));

        for (const char *melUid : {kMelUid, kMel2Uid}) {
            QSqlQuery mel(DatabaseManager::instance().database());
            const bool melOk = mel.exec(QStringLiteral(
                "INSERT INTO mels_v4(uid,user_id,goal_id,manifest_version_id,title,state,"
                "planned_start_at,planned_end_at,timezone_id,settlement_mode,capacity_min,"
                "reserve_min,rationale,created_at,updated_at) "
                "SELECT '%1',u.id,g.id,mv.id,'问卷校准测试','active','2026-10-01T00:00:00Z',"
                "'2026-10-04T00:00:00Z','Asia/Shanghai','deadline',300,30,'测试',"
                "'2026-09-27T00:00:00Z','2026-09-27T00:00:00Z' "
                "FROM user_profiles_v3 u, goals_v3 g, domain_manifest_versions_v3 mv "
                "WHERE u.uid='00000000-0000-0000-0000-0000000000aa' AND g.title='问卷校准目标' "
                "AND mv.version_no=1")
                                       .arg(QString::fromLatin1(melUid)));
            if (!melOk)
                QFAIL(qPrintable(QStringLiteral("mel insert: %1").arg(mel.lastError().text())));
        }
    }

    void questionnaireSeedAndSubmit()
    {
        Infrastructure::ReviewQuestionnaireSeed seed(DatabaseManager::instance().database(),
                                                     m_clock);
        QVERIFY(seed.ensureSeeded());
        QVERIFY(seed.ensureSeeded());   // 幂等

        Infrastructure::SqlQuestionnaireRepository repo(DatabaseManager::instance().database(),
                                                        m_clock);
        Infrastructure::SqlReviewRepository reviewRepo(DatabaseManager::instance().database(),
                                                       m_clock);
        Application::QuestionnaireUseCases useCases(repo, reviewRepo, m_uids, m_clock);
        Application::ReviewUseCases reviewUseCases(reviewRepo, m_uids, m_clock);

        // 种子问卷存在，题目 16 项（基本+十类）
        const auto questionnaire = useCases.findByCodeVersion("review_extended_states", 1);
        if (!questionnaire)
            QFAIL(qPrintable(QString::fromStdString(questionnaire.error().message)));
        QCOMPARE(repo.itemCodesOf(questionnaire.value().uid).size(), 16);

        // 开启复盘 → 提交问卷
        const auto melUid = *Domain::Uid::parse(kMelUid);
        const auto review = reviewUseCases.openReview(melUid);
        QVERIFY(review);

        Application::QuestionnaireUseCases::SubmitInput submit;
        submit.responsesJson = QStringLiteral(
            "{\"answers\":{\"energy\":4,\"focus\":3},\"no_change\":[\"mood\",\"stress\"],"
            "\"skipped\":[\"sleep_hours\"]}")
                                   .toStdString();
        const auto response = useCases.submitResponse(review.value().uid,
                                                      questionnaire.value().uid, submit);
        if (!response)
            QFAIL(qPrintable(QString::fromStdString(response.error().message + ": "
                                                    + response.error().detail)));
        QCOMPARE(response.value().completionRatio, 4.0 / 16.0);   // answers+no_change 计入
        QVERIFY(response.value().submittedAt.has_value());

        // 可修改：同一行更新，比例重算（3 answers + 2 no_change = 5/16）
        Application::QuestionnaireUseCases::SubmitInput resubmit;
        resubmit.responsesJson = QStringLiteral(
            "{\"answers\":{\"energy\":4,\"focus\":3,\"fatigue\":2},\"no_change\":[\"mood\","
            "\"stress\"],\"skipped\":[\"sleep_hours\"]}")
                                     .toStdString();
        const auto updated = useCases.submitResponse(review.value().uid,
                                                     questionnaire.value().uid, resubmit);
        QVERIFY(updated);
        QCOMPARE(updated.value().completionRatio, 5.0 / 16.0);
        QCOMPARE(updated.value().uid, response.value().uid);   // 同一行

        // 全部跳过：接受（可跳过），比例为 0，跳过不视为确认
        Application::QuestionnaireUseCases::SubmitInput skipAll;
        skipAll.responsesJson = QStringLiteral(
            "{\"answers\":{},\"no_change\":[],\"skipped\":[\"energy\",\"mood\",\"stress\"]}")
                                    .toStdString();
        const auto skippedOnly = useCases.submitResponse(review.value().uid,
                                                         questionnaire.value().uid, skipAll);
        QVERIFY(skippedOnly);
        QCOMPARE(skippedOnly.value().completionRatio, 0.0);

        // 未知题目 → Validation
        Application::QuestionnaireUseCases::SubmitInput unknown;
        unknown.responsesJson = QStringLiteral(
            "{\"answers\":{\"not_exist\":1},\"no_change\":[],\"skipped\":[]}")
                                    .toStdString();
        QVERIFY(!useCases.submitResponse(review.value().uid, questionnaire.value().uid, unknown));

        // 区块重叠 → Validation
        Application::QuestionnaireUseCases::SubmitInput overlap;
        overlap.responsesJson = QStringLiteral(
            "{\"answers\":{\"energy\":4},\"no_change\":[\"energy\"],\"skipped\":[]}")
                                    .toStdString();
        QVERIFY(!useCases.submitResponse(review.value().uid, questionnaire.value().uid, overlap));

        // 格式不合法 → Validation
        Application::QuestionnaireUseCases::SubmitInput malformed;
        malformed.responsesJson = std::string("not json");
        QVERIFY(!useCases.submitResponse(review.value().uid, questionnaire.value().uid,
                                         malformed));

        // 复盘关闭后拒绝提交
        QVERIFY(reviewUseCases.closeReview(melUid, 1));
        Application::QuestionnaireUseCases::SubmitInput late;
        late.responsesJson = QStringLiteral(
            "{\"answers\":{\"energy\":4},\"no_change\":[],\"skipped\":[]}")
                                 .toStdString();
        QVERIFY(!useCases.submitResponse(review.value().uid, questionnaire.value().uid, late));
    }

    void calibrationMinimalLoop()
    {
        Infrastructure::SqlCalibrationRepository repo(DatabaseManager::instance().database(),
                                                      m_clock);
        Application::CalibrationUseCases useCases(repo, m_uids, m_clock);
        const auto userUid = *Domain::Uid::parse(kUserUid);
        const auto melUid = *Domain::Uid::parse(kMel2Uid);

        // 无预测的 MEL：不伪造校准
        const auto nothing = useCases.recordMelOutcome(melUid);
        QVERIFY(nothing);
        QVERIFY(!nothing.value().recorded);

        // 预测 + 执行事实：预测完成 0.9 / 300 分钟；
        // 两个必做任务（progress 1.0/0.5，计划耗时 100/200）→ 加权完成 = 200/300
        QSqlQuery prediction(DatabaseManager::instance().database());
        const bool predictionOk = prediction.exec(QStringLiteral(
            "INSERT INTO mel_predictions_v4(uid,mel_id,predicted_completion,predicted_effort_min,"
            "risk_level,basis_json,created_at,superseded_at) "
            "SELECT '00000000-0000-0000-0000-0000000000da',m.id,0.9,300,'low','{}',"
            "'2026-09-27T00:00:00Z',NULL FROM mels_v4 m WHERE m.uid='%1'")
                                                   .arg(QString::fromLatin1(kMel2Uid)));
        if (!predictionOk)
            QFAIL(qPrintable(QStringLiteral("prediction: %1").arg(prediction.lastError().text())));

        QSqlQuery tasks(DatabaseManager::instance().database());
        const bool tasksOk = tasks.exec(QStringLiteral(
            "INSERT INTO mel_tasks_v4(uid,mel_id,title,description,sequence_no,required,"
            "planned_effort_min,completion_rule_json,state,progress,completed_at,created_at,"
            "updated_at) "
            "SELECT '00000000-0000-0000-0000-0000000000ea',m.id,'任务A',NULL,0,1,100,'{}',"
            "'completed',1.0,'2026-10-02T00:00:00Z','2026-09-27T00:00:00Z','2026-09-27T00:00:00Z' "
            "FROM mels_v4 m WHERE m.uid='%1' UNION ALL "
            "SELECT '00000000-0000-0000-0000-0000000000eb',m.id,'任务B',NULL,1,1,200,'{}',"
            "'completed',0.5,'2026-10-02T00:00:00Z','2026-09-27T00:00:00Z','2026-09-27T00:00:00Z' "
            "FROM mels_v4 m WHERE m.uid='%1'")
                                             .arg(QString::fromLatin1(kMel2Uid)));
        if (!tasksOk)
            QFAIL(qPrintable(QStringLiteral("tasks: %1").arg(tasks.lastError().text())));

        QSqlQuery events(DatabaseManager::instance().database());
        const bool eventsOk = events.exec(QStringLiteral(
            "INSERT INTO progress_events_v4(uid,user_id,mel_id,goal_id,task_id,event_type,"
            "amount,unit,note,evidence_asset_uid,occurred_at,recorded_at,actor_type,"
            "idempotency_key) "
            "SELECT '00000000-0000-0000-0000-0000000000fa',u.id,m.id,g.id,NULL,'incremented',"
            "120,'minutes','学习记录',NULL,'2026-10-02T00:00:00Z','2026-10-02T00:00:00Z',"
            "'user','cal:ev:1' FROM user_profiles_v3 u, mels_v4 m, goals_v3 g "
            "WHERE u.uid='00000000-0000-0000-0000-0000000000aa' AND m.uid='%1' "
            "AND g.title='问卷校准目标' UNION ALL "
            "SELECT '00000000-0000-0000-0000-0000000000fb',u.id,m.id,g.id,NULL,'incremented',"
            "60,'minutes','学习记录',NULL,'2026-10-02T00:00:00Z','2026-10-02T00:00:00Z',"
            "'user','cal:ev:2' FROM user_profiles_v3 u, mels_v4 m, goals_v3 g "
            "WHERE u.uid='00000000-0000-0000-0000-0000000000aa' AND m.uid='%1' "
            "AND g.title='问卷校准目标'")
                                           .arg(QString::fromLatin1(kMel2Uid)));
        if (!eventsOk)
            QFAIL(qPrintable(QStringLiteral("events: %1").arg(events.lastError().text())));

        const auto outcome = useCases.recordMelOutcome(melUid);
        if (!outcome)
            QFAIL(qPrintable(QString::fromStdString(outcome.error().message + ": "
                                                    + outcome.error().detail)));
        QVERIFY(outcome.value().recorded);
        QCOMPARE(outcome.value().records.size(), 2);

        // 完成率校准：actual = 200/300（按计划工作量加权）
        const auto completion = useCases.activeValue(userUid, "mel_completion_ratio");
        QVERIFY(completion && completion.value().has_value());
        const auto newValue = QJsonDocument::fromJson(
                                  QString::fromStdString(completion.value().value().newValueJson)
                                      .toUtf8())
                                  .object();
        QCOMPARE(newValue.value(QStringLiteral("actual")).toDouble(), 200.0 / 300.0);
        const auto oldValue = QJsonDocument::fromJson(
                                  QString::fromStdString(*completion.value().value().oldValueJson)
                                      .toUtf8())
                                  .object();
        QCOMPARE(oldValue.value(QStringLiteral("predicted")).toDouble(), 0.9);

        // 耗时校准：actual 180 分钟
        const auto effort = useCases.activeValue(userUid, "mel_effort_ratio");
        QVERIFY(effort && effort.value().has_value());
        const auto effortValue = QJsonDocument::fromJson(
                                     QString::fromStdString(effort.value().value().newValueJson)
                                         .toUtf8())
                                     .object();
        QCOMPARE(effortValue.value(QStringLiteral("actual_minutes")).toInt(), 180);

        // 只追加：第二次执行产生新记录，旧的仍在（activeValue 取最新）
        const auto again = useCases.recordMelOutcome(melUid);
        QVERIFY(again && again.value().recorded);
        QSqlQuery count(DatabaseManager::instance().database());
        QVERIFY(count.exec(QStringLiteral(
            "SELECT COUNT(*) FROM calibration_records_v4 WHERE parameter_code="
            "'mel_completion_ratio'")));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), 2);

        // 手动记录 + activeValue：非法 JSON 拒绝
        Application::CalibrationUseCases::RecordInput input;
        input.userId = userUid;
        input.parameterCode = "manual_param";
        input.newValueJson = std::string("{\"value\":7}");
        input.evidenceJson = std::string("{\"source\":\"user\"}");
        input.oldValueJson = std::string("{\"value\":6}");
        const auto manual = useCases.record(input);
        QVERIFY(manual);
        const auto active = useCases.activeValue(userUid, "manual_param");
        QVERIFY(active && active.value().has_value());
        QCOMPARE(active.value().value().newValueJson, std::string("{\"value\":7}"));

        Application::CalibrationUseCases::RecordInput bad = input;
        bad.newValueJson = std::string("not json");
        QVERIFY(!useCases.record(bad));
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstQuestionnaireCalibration)
#include "tst_questionnaire_calibration.moc"
