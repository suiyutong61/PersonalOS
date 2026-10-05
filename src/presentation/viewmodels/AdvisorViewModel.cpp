#include "presentation/viewmodels/AdvisorViewModel.h"

#include <QJsonDocument>
#include <QJsonObject>
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
    bool first = true;
    m_latestConflict = false;
    for (const auto &decision : repo.listDecisions("advisor_answer", 100)) {
        QVariantMap map;
        map.insert(QStringLiteral("uid"), QString::fromStdString(decision.uid.value()));
        map.insert(QStringLiteral("title"), QStringLiteral("咨询回答"));
        // 结构化字段解析：问题来自 input_snapshot_json.question，
        // 回答文本来自 candidate_json.user_text——原始 JSON 不直接展示
        const QJsonDocument inputDoc = QJsonDocument::fromJson(
            QByteArray::fromStdString(decision.inputSnapshotJson));
        QString question = inputDoc.object().value(QStringLiteral("question")).toString();
        if (question.isEmpty())
            question = QStringLiteral("（未记录问题）");
        const QJsonDocument candidateDoc = QJsonDocument::fromJson(
            QByteArray::fromStdString(decision.candidateJson));
        QString answer = candidateDoc.object().value(QStringLiteral("user_text")).toString();
        if (answer.isEmpty() && !decision.candidateJson.empty())
            answer = QStringLiteral("（回答内容无法解析，原始记录已保存于决策记录）");
        map.insert(QStringLiteral("subtitle"), question);
        const QString rawSourceMode =
            QString::fromStdString(
                decision.sourceMode == Domain::SourceMode::KnowledgeGrounded
                    ? "knowledge_grounded"
                    : (decision.sourceMode == Domain::SourceMode::PartiallyGrounded
                           ? "partially_grounded"
                           : "ungrounded"));
        // 知识冲突独立标志（DR-028；R4.8 冲突不混同知识不足）：
        // 徽标叠加"含冲突"文案并提级 warning 色调
        const bool conflict = QJsonDocument::fromJson(
                                  QByteArray::fromStdString(decision.warningJson))
                                  .object()
                                  .value(QStringLiteral("conflict"))
                                  .toBool();
        if (first)
            m_latestConflict = conflict;   // listDecisions 最新在前
        first = false;
        map.insert(QStringLiteral("badge"),
                   Presentation::sourceModeLabel(rawSourceMode)
                       + (conflict ? QStringLiteral(" · 含冲突") : QString()));
        map.insert(QStringLiteral("badgeTone"),
                   conflict ? QStringLiteral("warning")
                            : (decision.sourceMode == Domain::SourceMode::Ungrounded
                                   ? QStringLiteral("warning")
                                   : QStringLiteral("info")));
        map.insert(QStringLiteral("detail"), answer);
        // 仓储按最新在前返回；聊天界面按时间正序显示，最新问答落在底部。
        rows.prepend(map);
    }
    m_historyModel.replace(rows);
    setState(rows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"));
    emit dataChanged();
}

void AdvisorViewModel::send(const QString &question)
{
    if (m_pageState == QStringLiteral("ai_waiting")) {
        setError(QStringLiteral("AI 正在处理上一个问题，请稍候"));
        return;
    }
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
        // error 状态才能让 StateViews 渲染错误横幅（empty 状态下错误不可见，
        // 用户会以为"没有任何反应"）；输入区在 error 状态仍可见，可重试
        setState(QStringLiteral("error"));
        setError(QStringLiteral("缺少用户档案"));
        return;
    }

    // 后台线程执行完整管线：领域配置 → 状态上下文 → 检索/快照 → AI → 契约校验 → 决策记录
    m_pendingQuestion = question.trimmed();
    emit pendingQuestionChanged();
    setState(QStringLiteral("ai_waiting"));
    const QString dbPath = DatabaseManager::instance().databasePath();
    const std::string text = question.trimmed().toStdString();
    QtConcurrent::run([userUid = *userUid, dbPath, text]() {
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
    }).then(this, [this](QString error) {
        if (error.isEmpty()) {
            refresh();
            m_pendingQuestion.clear();
            emit pendingQuestionChanged();
            emit answerGenerated();
        } else {
            m_pendingQuestion.clear();
            emit pendingQuestionChanged();
            setState(QStringLiteral("error"));
            setError(error);
        }
    });
}

void AdvisorViewModel::openDetail(const QString &uid)
{
    const auto parsed = Domain::Uid::parse(uid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("无效的回答标识"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlAiRepository repo(database, clock);
    const auto decision = repo.findDecision(*parsed);
    if (!decision) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("回答不存在"));
        return;
    }
    const QJsonDocument inputDoc = QJsonDocument::fromJson(
        QByteArray::fromStdString(decision->inputSnapshotJson));
    m_detailQuestion = inputDoc.object().value(QStringLiteral("question")).toString();
    if (m_detailQuestion.isEmpty())
        m_detailQuestion = QStringLiteral("（未记录问题）");
    const QJsonDocument candidateDoc = QJsonDocument::fromJson(
        QByteArray::fromStdString(decision->candidateJson));
    m_detailAnswer = candidateDoc.object().value(QStringLiteral("user_text")).toString();
    if (m_detailAnswer.isEmpty() && !decision->candidateJson.empty())
        m_detailAnswer = QStringLiteral("（回答内容无法解析，原始记录已保存）");
    m_detailConflict = QJsonDocument::fromJson(
                           QByteArray::fromStdString(decision->warningJson))
                           .object()
                           .value(QStringLiteral("conflict"))
                           .toBool();
    m_detailVisible = true;
    emit detailChanged();
}

void AdvisorViewModel::closeDetail()
{
    m_detailVisible = false;
    emit detailChanged();
}

void AdvisorViewModel::deleteAnswer(const QString &uid)
{
    const auto parsed = Domain::Uid::parse(uid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("无效的回答标识"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlAiRepository repo(database, clock);
    const auto removed = repo.deleteDecision(*parsed);
    if (!removed.ok) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(removed.error.message, removed.error.detail));
        return;
    }
    closeDetail();
    refresh();
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
