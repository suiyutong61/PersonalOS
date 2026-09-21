#include "services/ApplicationService.h"

#include <algorithm>
#include <functional>

#include <QDate>
#include <QHash>
#include <QSet>

#include "database/TaskRepository.h"
#include "models/Review.h"
#include "models/StateSnapshot.h"
#include "models/Task.h"
#include "services/Items.h"

namespace PersonOS {

namespace {

QString todayString()
{
    return QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
}

} // namespace

ApplicationService::ApplicationService(QObject *parent)
    : QObject(parent)
{
    m_todayDate = todayString();
    m_ai.setConfig(AiConfig::load());
}

// ---------------------------------------------------------------- 初始化

void ApplicationService::init()
{
    m_planning.ensureDailyPlan(m_todayDate);
    refreshTodayInternal();
    refreshGoalsInternal();
    refreshPending();
    loadTodayState();
}

// ---------------------------------------------------------------- 今日任务

void ApplicationService::refreshTodayInternal()
{
    qDeleteAll(m_todayItems);
    m_todayItems.clear();

    for (const Task &t : TaskRepository().getByDate(m_todayDate))
        m_todayItems.append(new TaskItem(t, this));

    int total = m_todayItems.size();
    int done = 0;
    for (QObject *obj : std::as_const(m_todayItems))
        if (static_cast<TaskItem *>(obj)->m_done)
            ++done;
    m_taskSummary = QStringLiteral("%1 项任务 · 完成 %2 项").arg(total).arg(done);
    emit todayChanged();
}

qint64 ApplicationService::addTask(const QString &title, int plannedMinutes)
{
    Task t;
    t.title = title.trimmed();
    if (plannedMinutes > 0)
        t.plannedMinutes = plannedMinutes;
    QString error;
    const qint64 id = m_planning.addTaskToPlan(m_todayDate, t, &error);
    if (id == 0)
        setError(error);
    else {
        setError({});
        refreshTodayInternal();
    }
    return id;
}

bool ApplicationService::startTask(qint64 id)
{
    QString error;
    const bool ok = m_execution.startTask(id, &error);
    if (!ok)
        setError(error);
    else {
        setError({});
        refreshTodayInternal();
    }
    return ok;
}

bool ApplicationService::completeTask(qint64 id, int actualMinutes)
{
    QString error;
    const bool ok = m_execution.completeTask(id, actualMinutes, &error);
    if (!ok)
        setError(error);
    else {
        setError({});
        refreshTodayInternal();
    }
    return ok;
}

bool ApplicationService::skipTask(qint64 id, const QString &reason)
{
    QString error;
    const bool ok = m_execution.skipTask(id, reason, &error);
    if (!ok)
        setError(error);
    else {
        setError({});
        refreshTodayInternal();
    }
    return ok;
}

bool ApplicationService::cancelTask(qint64 id, const QString &reason)
{
    QString error;
    const bool ok = m_execution.cancelTask(id, reason, &error);
    if (!ok)
        setError(error);
    else {
        setError({});
        refreshTodayInternal();
    }
    return ok;
}

// ---------------------------------------------------------------- 目标

void ApplicationService::refreshGoalsInternal()
{
    qDeleteAll(m_goalItems);
    m_goalItems.clear();

    // 按父链计算深度：层级树以缩进展示（MVP 简化）
    const QList<Goal> all = m_goalsSvc.goalHierarchy();
    QHash<qint64, int> depth;
    std::function<int(qint64)> depthOf = [&](qint64 id) -> int {
        if (depth.contains(id))
            return depth[id];
        int d = 0;
        qint64 cursor = id;
        QSet<qint64> visited;
        while (cursor != 0 && !visited.contains(cursor)) {
            visited.insert(cursor);
            ++d;
            const auto parent = m_goalsSvc.goal(cursor);
            cursor = parent ? parent->parentId : 0;
        }
        depth[id] = d - 1; // 根目标深度 0
        return depth[id];
    };

    QList<Goal> sorted = all;
    std::sort(sorted.begin(), sorted.end(), [](const Goal &a, const Goal &b) {
        return a.id < b.id;
    });
    for (const Goal &g : sorted) {
        auto *item = new GoalItem(this);
        item->m_id = g.id;
        item->m_title = g.title;
        item->m_levelText = QStringLiteral("%1 · 优先级 %2").arg(g.level).arg(g.priority);
        item->m_priority = g.priority;
        item->m_depth = depthOf(g.id);
        m_goalItems.append(item);
    }
    emit goalsChanged();
}

void ApplicationService::refreshGoals()
{
    refreshGoalsInternal();
}

qint64 ApplicationService::createGoal(const QString &title, const QString &level, qint64 parentId,
                                      int priority)
{
    Goal g;
    g.title = title.trimmed();
    g.level = level;
    g.parentId = parentId;
    g.priority = qBound(0, priority, 100);
    QString error;
    const qint64 id = m_goalsSvc.createGoal(g, &error);
    if (id == 0)
        setError(error);
    else {
        setError({});
        refreshGoalsInternal();
    }
    return id;
}

// ---------------------------------------------------------------- 状态

void ApplicationService::loadTodayState()
{
    const auto s = m_states.today();
    m_stateSleep = s && s->sleepHours ? QString::number(*s->sleepHours) : QString();
    m_stateEnergy = s && s->energy ? *s->energy : 0;
    m_stateFocus = s && s->focus ? *s->focus : 0;
    m_stateMood = s && s->mood ? *s->mood : 0;
    m_stateNote = s ? s->note : QString();
    emit stateChanged();
}

bool ApplicationService::saveTodayState(const QString &sleep, int energy, int focus, int mood,
                                        const QString &note)
{
    StateSnapshot s;
    s.date = m_todayDate;
    bool ok = false;
    if (!sleep.trimmed().isEmpty())
        s.sleepHours = sleep.trimmed().toDouble(&ok);
    if (!sleep.trimmed().isEmpty() && !ok) {
        setError(QStringLiteral("睡眠时长必须是数字（小时）"));
        return false;
    }
    if (energy >= 1 && energy <= 5)
        s.energy = energy;
    if (focus >= 1 && focus <= 5)
        s.focus = focus;
    if (mood >= 1 && mood <= 5)
        s.mood = mood;
    s.note = note;

    QString error;
    const bool saved = m_states.record(s, &error);
    if (!saved)
        setError(error);
    else {
        setError({});
        loadTodayState();
    }
    return saved;
}

// ---------------------------------------------------------------- 复盘

void ApplicationService::refreshPending()
{
    m_pendingDates = m_reviews.pendingReviewDates(7);
    emit pendingChanged();
}

void ApplicationService::draftTodayReview()
{
    const Review draft = m_reviews.draftDailyReview(m_todayDate);
    m_reviewSummary = draft.summary;
    m_reviewProblems = draft.problems;
    m_reviewCauses = draft.causes;
    m_reviewNextActions = draft.nextActions;
    setError({});
    emit reviewChanged();
}

void ApplicationService::aiAnalyzeTodayReview()
{
    const QString date = m_todayDate;
    m_ai.analyzeDailyReview(date, [this](bool ok, const Review &review, const QString &error) {
        if (!ok) {
            setError(error);
            emit aiReviewReady(false, error);
            return;
        }
        m_reviewSummary = review.summary;
        m_reviewProblems = review.problems;
        m_reviewCauses = review.causes;
        m_reviewNextActions = review.nextActions;
        setError({});
        emit reviewChanged();
        emit aiReviewReady(true, {});
    });
}

bool ApplicationService::saveTodayReview()
{
    Review r;
    r.reviewType = QStringLiteral("daily");
    r.periodStart = m_todayDate;
    r.summary = m_reviewSummary;
    r.problems = m_reviewProblems;
    r.causes = m_reviewCauses;
    r.nextActions = m_reviewNextActions;

    QString error;
    if (!m_reviews.saveDailyReview(r, &error)) {
        setError(error);
        return false;
    }
    m_planning.closeDailyPlan(m_todayDate, &error); // 复盘完成 → 关闭计划（失败不阻断）
    setError({});
    refreshPending();
    return true;
}

// ---------------------------------------------------------------- AI 提案

void ApplicationService::aiGenerateProposal(const QString &description)
{
    m_proposalText = QStringLiteral("AI 正在生成提案…");
    emit proposalChanged();

    m_ai.proposeChange(description, [this](bool ok, const Proposal &proposal, const QString &error) {
        if (!ok) {
            setError(error);
            m_proposalText.clear();
            emit proposalChanged();
            return;
        }
        m_currentProposal = proposal;
        m_proposalText = QStringLiteral("对象: %1\n现状: %2\n建议: %3\n理由: %4\n风险: %5")
                             .arg(proposal.targetType, proposal.currentValue,
                                  proposal.proposedValue, proposal.reason,
                                  proposal.risk.isEmpty() ? QStringLiteral("（未说明）") : proposal.risk);
        setError({});
        emit proposalChanged();
    });
}

bool ApplicationService::approveProposal()
{
    if (m_currentProposal.id > 0) {
        // AI 草案尚未落库：重新提交为正式提案
        m_currentProposal.id = 0;
        QString error;
        const qint64 id = m_evolution.propose(m_currentProposal, &error);
        if (id == 0) {
            setError(error);
            return false;
        }
        m_currentProposal.id = id;
        if (!m_evolution.decide(id, true, &error)) {
            setError(error);
            return false;
        }
        if (!m_evolution.applyApproved(id, &error)) {
            setError(error);
            return false;
        }
        const auto version = m_evolution.currentVersion();
        m_proposalText += QStringLiteral("\n\n✅ 已批准并生效（版本 %1）")
                              .arg(version ? version->versionNumber : QStringLiteral("?"));
        m_currentProposal.id = 0; // 提案已处理
        setError({});
        emit proposalChanged();
        return true;
    }
    setError(QStringLiteral("没有待批准的提案"));
    return false;
}

bool ApplicationService::rejectProposal()
{
    if (m_currentProposal.id > 0) {
        QString error;
        const qint64 id = m_evolution.propose(m_currentProposal, &error);
        if (id == 0) {
            setError(error);
            return false;
        }
        if (!m_evolution.decide(id, false, &error)) {
            setError(error);
            return false;
        }
        m_proposalText += QStringLiteral("\n\n✖ 已拒绝");
        m_currentProposal.id = 0;
        setError({});
        emit proposalChanged();
        return true;
    }
    setError(QStringLiteral("没有待决定的提案"));
    return false;
}

// ---------------------------------------------------------------- 通用

void ApplicationService::setError(const QString &e)
{
    if (m_lastError == e)
        return;
    m_lastError = e;
    emit lastErrorChanged();
}

} // namespace PersonOS
