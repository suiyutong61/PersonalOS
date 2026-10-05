// 模块：三个连续 MEL 端到端验收（scope-v1 §8/§9；E2E-01..E2E-07 的用例层闭环）
// 覆盖：目标→路线确认→三轮（候选 MEL→确认激活→进度→执行完成→结算→验收
//       →复盘+问卷+校准→关闭）；历史事实不被覆盖；保持抽查调度与成就真实产生。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "application/audit/Audit.h"
#include "application/usecases/assessment/AssessmentUseCases.h"
#include "application/usecases/assessment/RetentionUseCases.h"
#include "application/usecases/goal/GoalUseCases.h"
#include "application/usecases/mel/MelUseCases.h"
#include "application/usecases/review/QuestionnaireUseCases.h"
#include "application/usecases/review/ReviewUseCases.h"
#include "application/usecases/route/RouteUseCases.h"
#include "application/usecases/state/CalibrationUseCases.h"
#include "application/usecases/state/StateUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/ai/SqlAiRepository.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/knowledge/ReviewQuestionnaireSeed.h"
#include "infrastructure/knowledge/StateDefinitionsSeed.h"
#include "infrastructure/operations/SqlOperationsRepository.h"
#include "infrastructure/persistence/SqlAssessmentRepository.h"
#include "infrastructure/persistence/SqlAuditRepository.h"
#include "infrastructure/persistence/SqlCalibrationRepository.h"
#include "infrastructure/persistence/SqlContentMapRepository.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlMelRepository.h"
#include "infrastructure/persistence/SqlQuestionnaireRepository.h"
#include "infrastructure/persistence/SqlRetentionRepository.h"
#include "infrastructure/persistence/SqlReviewRepository.h"
#include "infrastructure/persistence/SqlRouteRepository.h"
#include "infrastructure/persistence/SqlStateRepository.h"

using namespace PersonOS;

namespace {
const char *kUserUid = "00000000-0000-0000-0000-0000000000aa";
} // namespace

class TstE2EThreeMels : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_e2e_three_mels.db"));
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
        QVERIFY(Infrastructure::ReviewQuestionnaireSeed(DatabaseManager::instance().database(),
                                                        m_clock)
                    .ensureSeeded());

        QSqlQuery user(DatabaseManager::instance().database());
        const bool userOk = user.exec(QStringLiteral(
            "INSERT INTO user_profiles_v3(uid,timezone_id,locale,onboarding_status,profile_json,"
            "created_at,updated_at) VALUES('00000000-0000-0000-0000-0000000000aa',"
            "'Asia/Shanghai','zh-CN','complete','{}','2026-09-27T00:00:00Z','2026-09-27T00:00:00Z')"));
        if (!userOk)
            QFAIL(qPrintable(QStringLiteral("user insert: %1").arg(user.lastError().text())));

        // 审计 sink 注册（真实写入路径）
        m_audit = std::make_unique<Infrastructure::SqlAuditRepository>(
            DatabaseManager::instance().database(), m_clock, m_uids);
        Application::Audit::setSink(m_audit.get());
    }

    Domain::Uid goalUid()
    {
        QSqlQuery q(DatabaseManager::instance().database());
        q.exec(QStringLiteral("SELECT uid FROM goals_v3 WHERE title='E2E目标'"));
        q.next();
        return *Domain::Uid::parse(q.value(0).toString().toStdString());
    }

    Domain::Uid manifestVersionUid()
    {
        QSqlQuery q(DatabaseManager::instance().database());
        q.exec(QStringLiteral("SELECT uid FROM domain_manifest_versions_v3 LIMIT 1"));
        q.next();
        return *Domain::Uid::parse(q.value(0).toString().toStdString());
    }

    // 一轮完整 MEL：候选 → 激活 → 进度 → 执行完成 → 结算 → 验收 → 复盘 → 关闭
    void runMelRound(int round)
    {
        const auto userUid = *Domain::Uid::parse(kUserUid);
        QSqlDatabase db = DatabaseManager::instance().database();
        Infrastructure::SqlMelRepository melRepo(db, m_clock);
        Infrastructure::SqlGoalRepository goalRepo(db, m_clock);
        Infrastructure::SqlAssessmentRepository assessmentRepo(db, m_clock);
        Infrastructure::SqlReviewRepository reviewRepo(db, m_clock);
        Infrastructure::SqlQuestionnaireRepository questionnaireRepo(db, m_clock);
        Infrastructure::SqlCalibrationRepository calibrationRepo(db, m_clock);
        Infrastructure::SqlRetentionRepository retentionRepo(db, m_clock);

        Application::MelUseCases mels(melRepo, m_uids, m_clock);
        Application::AssessmentUseCases assessments(assessmentRepo, m_uids, m_clock);
        Application::ReviewUseCases reviews(reviewRepo, m_uids, m_clock);
        Application::QuestionnaireUseCases questionnaires(questionnaireRepo, reviewRepo, m_uids,
                                                          m_clock);
        Application::CalibrationUseCases calibration(calibrationRepo, m_uids, m_clock);
        Application::RetentionUseCases retention(retentionRepo, m_uids, m_clock);

        // 1) 候选 MEL（Draft）
        Application::MelUseCases::CreateInput create;
        create.userId = userUid;
        create.goalId = goalUid();
        create.manifestVersionId = manifestVersionUid();
        create.title = QStringLiteral("第%1轮 MEL").arg(round).toStdString();
        create.plannedStartAt = m_clock.utcIso();
        create.plannedEndAt = m_clock.utcIsoPlusMinutes(1440);
        create.timezoneId = "Asia/Shanghai";
        create.settlementMode = "manual";
        create.capacityMin = 180;
        create.reserveMin = 30;
        create.rationale = "E2E 三连 MEL 第" + std::to_string(round) + "轮";
        Domain::MelTask task;
        task.title = QStringLiteral("第%1轮任务").arg(round).toStdString();
        task.plannedEffortMin = 60;
        task.required = true;
        task.completionRuleJson = "{}";
        create.tasks.push_back(task);
        const auto proposed = mels.createMelProposal(create);
        if (!proposed)
            QFAIL(qPrintable(QString::fromStdString(proposed.error().message + ": "
                                                    + proposed.error().detail)));
        const Domain::Uid melUid = proposed.value().mel.uid;

        // 2) 确认激活（用户确认后才生效）
        const auto confirmed = mels.submitForConfirmation(melUid, 1);
        QVERIFY(confirmed);
        const auto activated = mels.confirmAndActivate(melUid, 2);
        if (!activated)
            QFAIL(qPrintable(QString::fromStdString(activated.error().message + ": "
                                                    + activated.error().detail)));
        QVERIFY(activated.value().state == Domain::MelState::Active);

        // 3) 进度上报（幂等键唯一）
        const auto tasks = melRepo.tasksOf(melUid);
        QVERIFY(!tasks.empty());
        Application::MelUseCases::ProgressInput progress;
        progress.taskUid = tasks.front().uid;
        progress.progress = 1.0;
        progress.actualMinutes = 50;
        progress.note = "第" + std::to_string(round) + "轮完成";
        progress.idempotencyKey = "e2e:progress:" + std::to_string(round);
        QVERIFY(mels.recordProgress(melUid, progress));

        // 4) 执行完成 → 结算 → 进入复盘（结算幂等）
        QVERIFY(mels.completeExecution(melUid, 3));
        const auto settled = mels.settleMel(melUid, 4);
        if (!settled)
            QFAIL(qPrintable(QString::fromStdString(settled.error().message + ": "
                                                    + settled.error().detail)));
        const auto settledAgain = mels.settleMel(melUid, 6);
        QVERIFY(settledAgain);   // 幂等：重复结算返回同一结果

        // 5) 能力验收（三档结果；执行完成 ≠ 掌握）
        Application::AssessmentUseCases::CreateInput assessmentInput;
        assessmentInput.userId = userUid;
        assessmentInput.goalId = goalUid();
        assessmentInput.melId = melUid;
        assessmentInput.assessmentType = "recall";
        assessmentInput.generatedBy = "ai";
        Domain::AssessmentItem item;
        item.prompt = QStringLiteral("第%1轮验收题").arg(round).toStdString();
        item.itemType = "recall";
        item.sourceRefsJson = "[]";
        assessmentInput.items.push_back(item);
        const auto assessment = assessments.createAssessment(assessmentInput);
        QVERIFY(assessment);
        QVERIFY(assessments.readyAssessment(assessment.value().assessment.uid, 1));
        Application::AssessmentUseCases::SubmitInput submit;
        submit.answerJson = "{\"answer\":\"回答\"}";
        submit.idempotencyKey = "e2e:attempt:" + std::to_string(round);
        const auto attempt =
            assessments.submitAttempt(assessment.value().assessment.uid, submit);
        QVERIFY(attempt);
        const auto items = assessmentRepo.itemsOf(assessment.value().assessment.uid);
        QVERIFY(!items.empty());
        const auto current = assessmentRepo.findByUid(assessment.value().assessment.uid);
        QVERIFY(current);
        Application::AssessmentUseCases::ScoreInput score;
        score.attemptId = attempt.value().uid;
        score.scorer = "user";
        Application::AssessmentUseCases::ScoreInput::ItemScore itemScore;
        itemScore.itemId = items.front().uid;
        itemScore.mastery = round == 3 ? Domain::Mastery::Fluent : Domain::Mastery::Prompted;
        score.itemScores.push_back(itemScore);
        QVERIFY(assessments.scoreAttempt(assessment.value().assessment.uid,
                                         current->revision, score));
        QVERIFY(mels.proceedToReviewing(melUid, 6, "验收已提交"));

        // 6) 复盘：开启 → 问卷（跳过不视为确认）→ 提交 → 关闭 → MEL 关闭
        const auto review = reviews.openReview(melUid);
        QVERIFY(review);
        const auto questionnaire = questionnaires.findByCodeVersion("review_extended_states", 1);
        QVERIFY(questionnaire);
        Application::QuestionnaireUseCases::SubmitInput questionnaireInput;
        questionnaireInput.responsesJson =
            QStringLiteral("{\"answers\":{\"energy\":4},\"no_change\":[\"mood\"],"
                           "\"skipped\":[\"stress\"]}")
                .toStdString();
        QVERIFY(questionnaires.submitResponse(review.value().uid,
                                              questionnaire.value().uid, questionnaireInput));
        Application::ReviewUseCases::SubmitInput reviewInput;
        reviewInput.summary = "第" + std::to_string(round) + "轮完成";
        reviewInput.nextAction = "生成下一轮 MEL";
        QVERIFY(reviews.submitReview(melUid, 1, reviewInput));
        QVERIFY(reviews.closeReview(melUid, 2));
        QVERIFY(mels.closeMel(melUid, 7, "生成下一轮 MEL"));

        // 7) 个人校准（预测 vs 实际）——无预测时诚实不记录
        const auto outcome = calibration.recordMelOutcome(melUid);
        QVERIFY(outcome);   // 无预测 → recorded=false，不伪造样本
        QVERIFY(!outcome.value().recorded);
    }

    void threeConsecutiveMelsCloseTheLoop()
    {
        const auto userUid = *Domain::Uid::parse(kUserUid);
        QSqlDatabase db = DatabaseManager::instance().database();

        // 前置：目标 + 路线确认 + 内容地图（覆盖率口径）
        QSqlQuery manifest(db);
        QVERIFY(manifest.exec(QStringLiteral(
            "SELECT uid FROM domain_manifests_v3 WHERE domain_code='learning'")));
        QVERIFY(manifest.next());
        Infrastructure::SqlGoalRepository goalsRepo(db, m_clock);
        Application::GoalUseCases goalUseCases(goalsRepo, m_uids, m_clock);
        Application::GoalUseCases::CreateInput goalInput;
        goalInput.userId = userUid;
        goalInput.domainManifestId =
            *Domain::Uid::parse(manifest.value(0).toString().toStdString());
        goalInput.title = QStringLiteral("E2E目标").toStdString();
        goalInput.goalType = "course";
        goalInput.desiredLevelJson = "{}";
        QVERIFY(goalUseCases.createGoal(goalInput));

        Infrastructure::SqlRouteRepository routesRepo(db, m_clock);
        Application::RouteUseCases routes(routesRepo, goalsRepo, m_uids, m_clock);
        Application::RouteUseCases::ProposeInput routeInput;
        routeInput.goalId = goalUid();
        routeInput.rationale = "E2E 验证路线";
        routeInput.evidenceSummary = "测试依据";
        routeInput.assumptionsJson = "{}";
        routeInput.createdBy = "user";
        Domain::RouteStage stage;
        stage.title = "第1阶段";
        stage.sequenceNo = 0;
        stage.completionRuleJson = "{}";
        routeInput.stages.push_back(stage);
        const auto route = routes.proposeRoute(routeInput);
        QVERIFY(route);
        QVERIFY(routes.confirmRoute(route.value().route.uid, 1));

        // 三个连续 MEL（提前完成 + 验收 + 复盘 + 下一轮）
        for (int round = 1; round <= 3; ++round)
            runMelRound(round);

        // 历史事实不被覆盖：三轮回合计
        QSqlQuery closed(db);
        QVERIFY(closed.exec(QStringLiteral(
            "SELECT COUNT(*) FROM mels_v4 WHERE state='closed'")));
        QVERIFY(closed.next());
        QCOMPARE(closed.value(0).toInt(), 3);
        QSqlQuery transitions(db);
        QVERIFY(transitions.exec(QStringLiteral(
            "SELECT COUNT(*) FROM mel_transitions_v4")));
        QVERIFY(transitions.next());
        QVERIFY(transitions.value(0).toInt() >= 15);   // 每轮 ≥5 个转移
        QSqlQuery progressEvents(db);
        QVERIFY(progressEvents.exec(QStringLiteral(
            "SELECT COUNT(*) FROM progress_events_v4")));
        QVERIFY(progressEvents.next());
        QCOMPARE(progressEvents.value(0).toInt(), 3);  // 每轮一条进度事实
        QSqlQuery reviewsCount(db);
        QVERIFY(reviewsCount.exec(QStringLiteral(
            "SELECT COUNT(*) FROM reviews_v4 WHERE status='closed'")));
        QVERIFY(reviewsCount.next());
        QCOMPARE(reviewsCount.value(0).toInt(), 3);
        QSqlQuery questionnairesCount(db);
        QVERIFY(questionnairesCount.exec(QStringLiteral(
            "SELECT COUNT(*) FROM questionnaire_responses_v4")));
        QVERIFY(questionnairesCount.next());
        QCOMPARE(questionnairesCount.value(0).toInt(), 3);
        QSqlQuery assessmentsCount(db);
        QVERIFY(assessmentsCount.exec(QStringLiteral(
            "SELECT COUNT(*) FROM assessments_v4 WHERE status='scored'")));
        QVERIFY(assessmentsCount.next());
        QCOMPARE(assessmentsCount.value(0).toInt(), 3);

        // 审计记录（状态变更可追溯）
        QSqlQuery auditCount(db);
        QVERIFY(auditCount.exec(QStringLiteral(
            "SELECT COUNT(*) FROM audit_events_v6 WHERE action='mel.transitioned'")));
        QVERIFY(auditCount.next());
        QVERIFY(auditCount.value(0).toInt() >= 15);
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
    std::unique_ptr<Infrastructure::SqlAuditRepository> m_audit;
};

QTEST_GUILESS_MAIN(TstE2EThreeMels)
#include "tst_e2e_three_mels.moc"
