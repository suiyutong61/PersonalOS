#include "presentation/viewmodels/MelViewModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include <QtConcurrent/QtConcurrent>

#include <algorithm>

#include "application/usecases/mel/MelUseCases.h"
#include "application/usecases/planning/AiPlanningUseCases.h"
#include "application/usecases/review/ReviewUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/ai/SqlAiRepository.h"
#include "infrastructure/persistence/DatabaseConnectionFactory.h"
#include "presentation/viewmodels/AiServices.h"
#include "domain/foundation/Uid.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/persistence/SqlMelRepository.h"
#include "infrastructure/persistence/SqlReviewRepository.h"
#include "presentation/viewmodels/VmSupport.h"

namespace PersonOS {

namespace {

QVariantMap taskRow(const QString &uid, const QString &title, double progress, bool required,
                    const QString &state)
{
    return {{QStringLiteral("uid"), uid},
            {QStringLiteral("title"), title},
            {QStringLiteral("subtitle"), Presentation::melTaskStateLabel(state)},
            {QStringLiteral("badge"), PersonOS::Presentation::percentText(progress)},
            {QStringLiteral("badgeTone"), progress >= 1.0 ? QStringLiteral("success")
                                                          : QStringLiteral("info")},
            {QStringLiteral("detail"),
             required ? QStringLiteral("必做") : QStringLiteral("可选")}};
}

} // namespace

MelViewModel::MelViewModel(QObject *parent) : QObject(parent) {}

void MelViewModel::setState(const QString &state)
{
    if (m_pageState == state)
        return;
    m_pageState = state;
    emit pageStateChanged();
}

void MelViewModel::setError(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
}

void MelViewModel::refresh()
{
    setState(QStringLiteral("loading"));
    setError({});

    const auto database = DatabaseManager::instance().database();
    const auto userUid = Presentation::activeUserUid(database);
    if (!userUid) {
        m_tasksModel.clear();
        m_melUid.clear();
        refreshReview();
        setState(QStringLiteral("empty"));
        return;
    }

    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlMelRepository melRepo(database, clock);
    auto activeMels = melRepo.findActive(*userUid, 1);
    // 无活跃 MEL 时回退展示最新"候选/待确认"MEL（首个 MEL 流程：
    // AI 生成的是候选，必须让用户看到并确认，否则首个 MEL 是死路；
    // 已关闭/取消的历史 MEL 不展示）
    if (activeMels.empty()) {
        // 执行完成后仍必须展示同一个 MEL，用户才能继续结算/验收/复盘。
        // 旧实现只回退 draft/awaiting_confirmation，导致 completeExecution()
        // 成功后页面立刻 empty，结算入口永久不可达。
        for (const auto &candidate : melRepo.findByUser(*userUid, 20)) {
            if (!Domain::MelStateMachine::isTerminal(candidate.state)) {
                activeMels.push_back(candidate);
                break;
            }
        }
    }
    if (activeMels.empty()) {
        m_tasksModel.clear();
        m_melUid.clear();
        m_melTitle.clear();
        m_melState.clear();
        m_melStateLabel.clear();
        m_melDeadline.clear();
        m_melProgress.clear();
        refreshReview();
        setState(QStringLiteral("empty"));
        emit melChanged();
        return;
    }

    const Domain::Mel &mel = activeMels.front();
    m_melUid = QString::fromStdString(mel.uid.value());
    m_melTitle = QString::fromStdString(mel.title);
    m_melState = QString::fromStdString(Domain::toString(mel.state));
    m_melStateLabel = Presentation::melStateLabel(m_melState);
    m_melDeadline = QString::fromStdString(mel.plannedEndAt);
    m_melRevision = mel.revision;

    const auto tasks = melRepo.tasksOf(mel.uid);
    double progressSum = 0.0;
    int requiredCount = 0;
    QVariantList rows;
    for (const auto &task : tasks) {
        if (task.required) {
            progressSum += task.progress;
            ++requiredCount;
        }
        rows.append(taskRow(QString::fromStdString(task.uid.value()),
                            QString::fromStdString(task.title), task.progress,
                            task.required, QString::fromStdString(Domain::toString(task.state))));
    }
    m_tasksModel.replace(rows);
    m_melProgress = requiredCount > 0
                        ? Presentation::percentText(progressSum / requiredCount)
                        : Presentation::percentText(0.0);
    setState(QStringLiteral("ready"));
    refreshReview();
    emit melChanged();
    emit dataChanged();
}

void MelViewModel::completeExecution()
{
    const auto parsed = Domain::Uid::parse(m_melUid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("当前没有可操作的 MEL"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlMelRepository melRepo(database, clock);
    Application::MelUseCases useCases(melRepo, uids, clock);
    const auto completed = useCases.completeExecution(*parsed, m_melRevision);
    if (!completed) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(completed.error().message, completed.error().detail));
        return;
    }
    refresh();
}

void MelViewModel::settle()
{
    const auto parsed = Domain::Uid::parse(m_melUid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("当前没有可结算的 MEL"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlMelRepository melRepo(database, clock);
    Application::MelUseCases useCases(melRepo, uids, clock);
    const auto settled = useCases.settleMel(*parsed, m_melRevision);
    if (!settled) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(settled.error().message, settled.error().detail));
        return;
    }
    // 结算时开启复盘(R3 闭环:复盘页此前是死路——全系统无人调用
    // openReview)。每个 MEL 至多一个复盘(UNIQUE 兜底),失败不阻断结算。
    {
        Infrastructure::SqlReviewRepository reviewRepo(database, clock);
        Application::ReviewUseCases reviewUseCases(reviewRepo, uids, clock);
        if (!reviewUseCases.openReview(*parsed))
            qWarning("结算后开启复盘失败(下次结算重试): mel=%s",
                     m_melUid.toStdString().c_str());
    }
    refresh();
}

void MelViewModel::pauseResume()
{
    const auto parsed = Domain::Uid::parse(m_melUid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("当前没有可暂停/恢复的 MEL"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlMelRepository melRepo(database, clock);
    Application::MelUseCases useCases(melRepo, uids, clock);

    const auto current = melRepo.findByUid(*parsed);
    if (!current) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("MEL 不存在"));
        return;
    }
    const auto updated = current->state == Domain::MelState::Paused
                             ? useCases.resumeMel(*parsed, m_melRevision)
                             : useCases.pauseMel(*parsed, m_melRevision);
    if (!updated) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(updated.error().message, updated.error().detail));
        return;
    }
    refresh();
}


void MelViewModel::runAi(const QString &purpose, const QString &jobType)
{
    // jobType: "mel" | "methods"；每线程独立数据库连接
    const auto melUid = Domain::Uid::parse(m_melUid.toStdString());
    const auto userUid = Presentation::activeUserUid(DatabaseManager::instance().database());
    if (!userUid) {
        // error 状态才能让 StateViews 渲染错误横幅(empty 下错误不可见)
        setState(QStringLiteral("error"));
        setError(QStringLiteral("缺少用户档案"));
        return;
    }
    // 诚实降级预检:未配置可用模型直接给出可行动指引(不进入后台管线)
    {
        Infrastructure::QtSystemClock clock;
        Infrastructure::SqlAiRepository aiRepo(DatabaseManager::instance().database(), clock);
        if (!aiRepo.findFirstEnabledConfig()) {
            setState(QStringLiteral("offline"));
            setError(QStringLiteral("未配置可用的模型连接：请到设置页添加、测试并启用连接"));
            return;
        }
    }
    m_aiState = QStringLiteral("ai_waiting");
    emit aiStateChanged();

    const QString dbPath = DatabaseManager::instance().databasePath();
    QtConcurrent::run([this, purpose, jobType, userUid = *userUid,
                       melUid, dbPath]() {
        QString error;
        Infrastructure::DatabaseConnectionFactory factory(dbPath);
        QString openError;
        QSqlDatabase workerDb = factory.openForCurrentThread(purpose, &openError);
        if (!workerDb.isOpen())
            return QStringLiteral("数据库连接失败：") + openError;
        const auto pipeline = Presentation::buildPlanningPipeline(workerDb);
        if (jobType == QStringLiteral("mel")) {
            // 取首个目标生成候选 MEL
            const auto goals = pipeline->goalsRepo->findByUser(userUid);
            if (goals.empty()) {
                Infrastructure::DatabaseConnectionFactory::closeCurrentThreadConnection(
                    purpose);
                return QStringLiteral("请先创建学习目标");
            }
            const auto result =
                pipeline->useCases->generateMelProposal(userUid, goals.front().uid,
                                                        std::nullopt);
            if (!result)
                error = Presentation::friendlyError(result.error().message,
                                                    result.error().detail);
        } else if (melUid) {
            const auto bound = pipeline->useCases->generateMethodSuggestions(userUid, *melUid);
            if (!bound)
                error = Presentation::friendlyError(bound.error().message,
                                                    bound.error().detail);
            else if (bound.value() == 0)
                // 无召回/全部候选被拒:明确提示,不再"什么都没发生"
                error = QStringLiteral("OK0:AI 未绑定任何方法：检索无召回或全部候选被硬约束拒绝");
        } else {
            error = QStringLiteral("当前没有可关联方法的 MEL");
        }
        Infrastructure::DatabaseConnectionFactory::closeCurrentThreadConnection(purpose);
        return error;
    }).then([this](QString error) {
        if (error.startsWith(QStringLiteral("OK0:"))) {
            setState(QStringLiteral("conflict"));
            setError(error.mid(4));
        } else if (error.isEmpty()) {
            refresh();
        } else {
            setState(QStringLiteral("error"));
            setError(error);
        }
        m_aiState = QStringLiteral("idle");
        emit aiStateChanged();
    });
}

void MelViewModel::aiGenerateMel()
{
    runAi(QStringLiteral("ai_mel"), QStringLiteral("mel"));
}

void MelViewModel::reportProgress(const QString &text)
{
    const auto melUid = Domain::Uid::parse(m_melUid.toStdString());
    const auto userUid = Presentation::activeUserUid(DatabaseManager::instance().database());
    if (!melUid || !userUid) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("请先生成或确认一个 MEL"));
        return;
    }
    if (text.trimmed().isEmpty()) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("请直接告诉 AI 这次做了什么、结果怎样"));
        return;
    }
    // 诚实降级预检：未配置可用模型直接给出可行动指引
    {
        Infrastructure::QtSystemClock clock;
        Infrastructure::SqlAiRepository aiRepo(DatabaseManager::instance().database(), clock);
        if (!aiRepo.findFirstEnabledConfig()) {
            setState(QStringLiteral("offline"));
            setError(QStringLiteral("未配置可用的模型连接：请到设置页添加、测试并启用连接"));
            return;
        }
    }
    m_aiState = QStringLiteral("ai_waiting");
    emit aiStateChanged();

    const QString dbPath = DatabaseManager::instance().databasePath();
    QtConcurrent::run([melUid = *melUid, userUid = *userUid,
                       report = text.trimmed().toStdString(), dbPath]() {
        QString error;
        Infrastructure::DatabaseConnectionFactory factory(dbPath);
        QString openError;
        QSqlDatabase workerDb = factory.openForCurrentThread(
            QStringLiteral("ai_progress_review"), &openError);
        if (!workerDb.isOpen())
            return QStringLiteral("数据库连接失败：") + openError;
        const auto pipeline = Presentation::buildPlanningPipeline(workerDb);
        const auto result =
            pipeline->useCases->reviewProgress(userUid, melUid, report);
        if (!result)
            error = Presentation::friendlyError(result.error().message,
                                                result.error().detail);
        Infrastructure::DatabaseConnectionFactory::closeCurrentThreadConnection(
            QStringLiteral("ai_progress_review"));
        return error;
    }).then(this, [this](QString error) {
        if (error.isEmpty()) {
            refresh();
            emit progressReviewGenerated();
        } else {
            setState(QStringLiteral("error"));
            setError(error);
        }
        m_aiState = QStringLiteral("idle");
        emit aiStateChanged();
    });
}

void MelViewModel::adoptReview()
{
    const auto reviewUid = Domain::Uid::parse(m_reviewUid.toStdString());
    if (!reviewUid) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("没有可采用的调整建议"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    const auto pipeline = Presentation::buildPlanningPipeline(database);
    const auto applied = pipeline->useCases->adoptProgressReview(*reviewUid);
    if (!applied) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(applied.error().message,
                                             applied.error().detail));
        return;
    }
    refresh();
}

void MelViewModel::abandonReview()
{
    const auto reviewUid = Domain::Uid::parse(m_reviewUid.toStdString());
    if (!reviewUid)
        return;
    const auto pipeline = Presentation::buildPlanningPipeline(
        DatabaseManager::instance().database());
    const auto result = pipeline->useCases->markDecision(
        reviewUid->value(), "rejected", std::nullopt);
    if (!result) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(result.error().message, result.error().detail));
        return;
    }
    refresh();
}

void MelViewModel::refreshReview()
{
    m_reviewModel.clear();
    m_reviewNotice.clear();
    m_reviewUid.clear();
    m_reviewVisible = false;

    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlAiRepository aiRepo(database, clock);
    if (m_melUid.isEmpty()) {
        emit reviewChanged();
        return;
    }
    // 当前 MEL 的最新一条未采用进度审查（listDecisions 最新在前）。
    // 不按 aggregate 过滤会把另一个 MEL 的建议显示在当前页面，甚至误采用。
    for (const auto &decision : aiRepo.listDecisions("progress_review", 50)) {
        if (decision.userStatus != Domain::DecisionUserStatus::Pending)
            continue;
        if (decision.aggregateType != "mel" || decision.aggregateUid != m_melUid.toStdString())
            continue;
        m_reviewUid = QString::fromStdString(decision.uid.value());
        const QJsonDocument doc = QJsonDocument::fromJson(
            QByteArray::fromStdString(decision.candidateJson));
        m_reviewNotice =
            doc.object().value(QStringLiteral("user_text")).toString();
        QVariantList rows;
        Infrastructure::SqlMelRepository melRepo(database, clock);
        const auto parsedMel = Domain::Uid::parse(m_melUid.toStdString());
        const auto tasks = parsedMel ? melRepo.tasksOf(*parsedMel) : std::vector<Domain::MelTask>{};
        for (const auto &value : doc.object().value(QStringLiteral("task_updates")).toArray()) {
            const QJsonObject suggestion = value.toObject();
            const QString taskUid = suggestion.value(QStringLiteral("task_uid")).toString();
            const auto found = std::find_if(tasks.begin(), tasks.end(), [&](const auto &task) {
                return task.uid.value() == taskUid.toStdString();
            });
            if (found == tasks.end())
                continue;
            QVariantMap map;
            map.insert(QStringLiteral("uid"), QString::number(rows.size()));
            map.insert(QStringLiteral("kind"), QStringLiteral("progress"));
            map.insert(QStringLiteral("title"), QString::fromStdString(found->title));
            const int before = qRound(found->progress * 100.0);
            const int after = qRound(suggestion.value(QStringLiteral("progress")).toDouble() * 100.0);
            map.insert(QStringLiteral("subtitle"),
                       QStringLiteral("当前 %1% → 确认后 %2% · %3")
                           .arg(before).arg(after)
                           .arg(suggestion.value(QStringLiteral("rationale")).toString()));
            map.insert(QStringLiteral("badge"), QStringLiteral("%1% → %2%").arg(before).arg(after));
            map.insert(QStringLiteral("badgeTone"), QStringLiteral("info"));
            rows.append(map);
        }
        m_reviewModel.replace(rows);
        m_reviewVisible = true;
        break;
    }
    emit reviewChanged();
}

void MelViewModel::confirmMel()
{
    const auto melUid = Domain::Uid::parse(m_melUid.toStdString());
    const auto database = DatabaseManager::instance().database();
    if (!melUid) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("无效的 MEL 标识"));
        return;
    }
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlMelRepository melRepo(database, clock);
    Application::MelUseCases useCases(melRepo, uids, clock);
    int activationRevision = m_melRevision;
    // AI 候选历史上以 draft 落库；确认动作必须完整执行
    // draft -> awaiting_confirmation -> active，不能直接跳状态机。
    // 同时兼容已经是 awaiting_confirmation 的候选。
    if (m_melState == QStringLiteral("draft")) {
        const auto submitted = useCases.submitForConfirmation(*melUid, activationRevision);
        if (!submitted) {
            setState(QStringLiteral("error"));
            setError(Presentation::friendlyError(submitted.error().message,
                                                 submitted.error().detail));
            return;
        }
        activationRevision = submitted.value().revision;
    }
    const auto activated = useCases.confirmAndActivate(*melUid, activationRevision);
    if (!activated) {
        setState(QStringLiteral("error"));
        setError(Presentation::friendlyError(activated.error().message,
                                             activated.error().detail));
        return;
    }
    refresh();
}

void MelViewModel::aiSuggestMethods()
{
    runAi(QStringLiteral("ai_methods"), QStringLiteral("methods"));
}

} // namespace PersonOS
