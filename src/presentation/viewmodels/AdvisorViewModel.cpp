#include "presentation/viewmodels/AdvisorViewModel.h"

#include <QVariantMap>

#include "database/DatabaseManager.h"
#include "domain/foundation/Uid.h"
#include <QtConcurrent/QtConcurrent>

#include "infrastructure/ai/SqlAiRepository.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/persistence/DatabaseConnectionFactory.h"
#include "presentation/viewmodels/AiServices.h"
#include "presentation/viewmodels/VmSupport.h"

namespace PersonOS {

namespace {

QVariantMap row(const Domain::AiJob &job)
{
    return {{QStringLiteral("uid"), QString::fromStdString(job.uid.value())},
            {QStringLiteral("title"), QString::fromStdString(job.jobType)},
            {QStringLiteral("subtitle"), QString::fromStdString(job.requestJson)},
            {QStringLiteral("badge"), QString::fromStdString(Domain::toString(job.status))},
            {QStringLiteral("badgeTone"),
             job.status == Domain::AiJobStatus::Completed ? QStringLiteral("success")
                                                          : QStringLiteral("neutral")},
            {QStringLiteral("detail"),
             job.resultJson ? QString::fromStdString(*job.resultJson) : QString()}};
}

} // namespace

AdvisorViewModel::AdvisorViewModel(QObject *parent) : QObject(parent) {}

void AdvisorViewModel::setState(const QString &state)
{
    if (m_pageState == state)
        return;
    m_pageState = state;
    emit pageStateChanged();
}

void AdvisorViewModel::setError(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
}

void AdvisorViewModel::refresh()
{
    setState(QStringLiteral("loading"));
    setError({});
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlAiRepository repo(database, clock);

    // 历史 = 咨询决策记录（知识支持状态可追溯）
    QVariantList rows;
    for (const auto &decision : repo.listDecisions("advisor_answer", 100)) {
        QVariantMap map;
        map.insert(QStringLiteral("uid"), QString::fromStdString(decision.uid.value()));
        map.insert(QStringLiteral("title"), QStringLiteral("咨询回答"));
        map.insert(QStringLiteral("subtitle"),
                   QString::fromStdString(decision.candidateJson));
        map.insert(QStringLiteral("badge"),
                   QString::fromStdString(
                       decision.sourceMode == Domain::SourceMode::KnowledgeGrounded
                           ? "knowledge_grounded"
                           : (decision.sourceMode == Domain::SourceMode::PartiallyGrounded
                                  ? "partially_grounded"
                                  : "ungrounded")));
        map.insert(QStringLiteral("badgeTone"),
                   decision.sourceMode == Domain::SourceMode::Ungrounded
                       ? QStringLiteral("warning")
                       : QStringLiteral("info"));
        map.insert(QStringLiteral("detail"), QString());
        rows.append(map);
    }
    m_historyModel.replace(rows);
    setState(rows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"));
    emit dataChanged();
}

void AdvisorViewModel::send(const QString &question)
{
    if (question.trimmed().isEmpty()) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("问题不能为空"));
        return;
    }

    // 诚实降级：未配置可用模型 → offline（不伪造结果）
    {
        Infrastructure::QtSystemClock clock;
        Infrastructure::SqlAiRepository repo(DatabaseManager::instance().database(), clock);
        if (!repo.findFirstEnabledConfig()) {
            setState(QStringLiteral("offline"));
            setError(QStringLiteral("未配置可用的模型连接：请到设置页添加、测试并启用连接"));
            return;
        }
    }

    const auto userUid = Presentation::activeUserUid(DatabaseManager::instance().database());
    if (!userUid) {
        setState(QStringLiteral("empty"));
        setError(QStringLiteral("缺少用户档案"));
        return;
    }

    // 后台线程执行完整管线：领域配置 → 状态上下文 → 检索/快照 → AI → 契约校验 → 决策记录
    setState(QStringLiteral("ai_waiting"));
    const QString dbPath = DatabaseManager::instance().databasePath();
    const std::string text = question.trimmed().toStdString();
    QtConcurrent::run([this, userUid = *userUid, dbPath, text]() {
        QString error;
        Infrastructure::DatabaseConnectionFactory factory(dbPath);
        QString openError;
        QSqlDatabase workerDb =
            factory.openForCurrentThread(QStringLiteral("ai_advisor"), &openError);
        if (!workerDb.isOpen())
            return QStringLiteral("数据库连接失败：") + openError;
        const auto pipeline = Presentation::buildPlanningPipeline(workerDb);
        const auto result = pipeline->useCases->askAdvisor(userUid, text);
        if (!result)
            error = Presentation::friendlyError(result.error().message, result.error().detail);
        Infrastructure::DatabaseConnectionFactory::closeCurrentThreadConnection(
            QStringLiteral("ai_advisor"));
        return error;
    }).then([this](QString error) {
        if (error.isEmpty()) {
            refresh();
        } else {
            setState(QStringLiteral("error"));
            setError(error);
        }
    });
}

void AdvisorViewModel::cancelLatestPending()
{
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlAiRepository repo(database, clock);

    // 协作式取消：只标记最新未完成任务为 cancelled（状态表达，不物理删除）
    std::optional<Domain::AiJob> target;
    for (const auto &job : repo.listJobs(100)) {
        if (job.status == Domain::AiJobStatus::Queued
            || job.status == Domain::AiJobStatus::Running) {
            target = job;
            break;
        }
    }
    if (!target)
        return;

    auto updated = *target;
    updated.status = Domain::AiJobStatus::Cancelled;
    const auto saved = repo.updateJob(updated, target->revision);
    if (!saved.ok) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(saved.error.message, saved.error.detail));
        return;
    }
    refresh();
}

} // namespace PersonOS
