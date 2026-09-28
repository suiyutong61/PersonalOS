#include "presentation/viewmodels/DashboardViewModel.h"

#include "application/usecases/advice/AdviceUseCases.h"
#include "application/usecases/detection/DetectionUseCases.h"
#include "application/usecases/goal/ContentMapUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/ai/SqlAiRepository.h"
#include "infrastructure/persistence/SqlAssessmentRepository.h"
#include "infrastructure/persistence/SqlStateRepository.h"
#include "presentation/viewmodels/AiServices.h"
#include "domain/foundation/Uid.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/operations/SqlOperationsRepository.h"
#include "infrastructure/persistence/SqlContentMapRepository.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlMelRepository.h"
#include "presentation/viewmodels/VmSupport.h"

namespace PersonOS {

DashboardViewModel::DashboardViewModel(QObject *parent) : QObject(parent) {}

void DashboardViewModel::setState(const QString &state)
{
    if (m_pageState == state)
        return;
    m_pageState = state;
    emit pageStateChanged();
}

void DashboardViewModel::refresh()
{
    setState(QStringLiteral("loading"));
    m_lastError.clear();
    emit lastErrorChanged();

    // 只读聚合：活跃 MEL（标题 + 必做任务平均进度）与待投递提醒数、
    // 目标数与长期内容覆盖率摘要（口径见目标页）。
    // ViewModel 只通过端口/仓储读取，不写任何正式业务事实（DD-001 §11）。
    const auto database = DatabaseManager::instance().database();
    const auto userUid = Presentation::activeUserUid(database);
    m_activeMelTitle.clear();
    m_activeMelProgress.clear();
    m_dueReminderCount = 0;
    m_goalCount = 0;
    m_coverageText.clear();

    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlOperationsRepository opsRepo(database, clock);

    if (userUid) {
        Infrastructure::SqlMelRepository melRepo(database, clock);
        const auto activeMels = melRepo.findActive(*userUid, 1);
        if (!activeMels.empty()) {
            m_activeMelTitle = QString::fromStdString(activeMels.front().title);
            double progress = 0.0;
            int requiredCount = 0;
            const auto tasks = melRepo.tasksOf(activeMels.front().uid);
            for (const auto &task : tasks)
                if (task.required) {
                    progress += task.progress;
                    ++requiredCount;
                }
            m_activeMelProgress =
                Presentation::percentText(requiredCount > 0 ? progress / requiredCount : 0.0);
        }

        // 长期目标摘要：目标数 + 首个目标的覆盖率（口径可解释，不合并进度）
        Infrastructure::SqlGoalRepository goalsRepo(database, clock);
        Infrastructure::SqlContentMapRepository mapsRepo(database, clock);
        Application::ContentMapUseCases mapUseCases(mapsRepo, goalsRepo, uids, clock);
        const auto goals = goalsRepo.findByUser(*userUid);
        m_goalCount = static_cast<int>(goals.size());
        if (!goals.empty()) {
            const auto coverage = mapUseCases.coverageByGoal(goals.front().uid, *userUid);
            if (coverage)
                m_coverageText =
                    Presentation::percentText(coverage.value().coverage) + QStringLiteral(" · ")
                    + QString::fromStdString(coverage.value().caliberText);
        }
    }

    const auto pending = opsRepo.pendingDeliveries(clock.utcIso());
    m_dueReminderCount = static_cast<int>(pending.size());

    // R2 检测 + R5 建议评估（SQL 同步；参数与依据来自领域清单，不写死阈值；
    // 建议不自动执行，pending 待用户确认）
    if (userUid) {
        Infrastructure::SqlStateRepository stateRepo(database, clock);
        Infrastructure::SqlAssessmentRepository assessmentRepo(database, clock);
        Infrastructure::SqlMelRepository melRepo(database, clock);
        Infrastructure::SqlAiRepository decisionRepo(database, clock);
        Application::DetectionUseCases detection(stateRepo, melRepo, assessmentRepo, uids,
                                                 clock);
        Application::AdviceUseCases advice(decisionRepo, stateRepo, melRepo, uids, clock);
        const auto detectionConfiguration = Presentation::detectionConfig(database);
        if (detectionConfiguration)
            detection.detect(*userUid, clock.utcIso(), *detectionConfiguration);
        const auto adviceConfiguration = Presentation::adviceConfig(database);
        if (adviceConfiguration)
            advice.evaluate(*userUid, clock.utcIso(), *adviceConfiguration,
                            Domain::SourceMode::PartiallyGrounded);
    }

    emit dashboardChanged();
    setState(QStringLiteral("ready"));
}

} // namespace PersonOS
