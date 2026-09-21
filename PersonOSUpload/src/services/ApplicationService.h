#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "ai/AiConfig.h"
#include "ai/AiEngine.h"
#include "evolution/EvolutionService.h"
#include "execution/ExecutionService.h"
#include "feedback/FeedbackService.h"
#include "goals/CoreAndGoalService.h"
#include "models/Proposal.h"
#include "planning/PlanningService.h"
#include "review/ReviewService.h"
#include "state/StateService.h"

namespace PersonOS {

// ApplicationService：QML 的唯一入口门面（README 3.3.1）
// 编排 7 个子系统与 AI Engine；跨子系统协作都在这里，子系统之间不互相调用。
class ApplicationService : public QObject
{
    Q_OBJECT
    // ---- 今日 ----
    Q_PROPERTY(QList<QObject *> todayTasks READ todayTasks NOTIFY todayChanged)
    Q_PROPERTY(QString todayDate READ todayDate CONSTANT)
    Q_PROPERTY(QString taskSummary READ taskSummary NOTIFY todayChanged)
    // ---- 目标 ----
    Q_PROPERTY(QList<QObject *> goals READ goals NOTIFY goalsChanged)
    // ---- 状态 ----
    Q_PROPERTY(QString stateSleep READ stateSleep NOTIFY stateChanged)
    Q_PROPERTY(int stateEnergy READ stateEnergy NOTIFY stateChanged)
    Q_PROPERTY(int stateFocus READ stateFocus NOTIFY stateChanged)
    Q_PROPERTY(int stateMood READ stateMood NOTIFY stateChanged)
    Q_PROPERTY(QString stateNote READ stateNote NOTIFY stateChanged)
    // ---- 复盘 ----
    Q_PROPERTY(QString reviewSummary READ reviewSummary WRITE setReviewSummary NOTIFY reviewChanged)
    Q_PROPERTY(QString reviewProblems READ reviewProblems WRITE setReviewProblems NOTIFY reviewChanged)
    Q_PROPERTY(QString reviewCauses READ reviewCauses WRITE setReviewCauses NOTIFY reviewChanged)
    Q_PROPERTY(QString reviewNextActions READ reviewNextActions WRITE setReviewNextActions
                   NOTIFY reviewChanged)
    Q_PROPERTY(QStringList pendingReviewDates READ pendingReviewDates NOTIFY pendingChanged)
    // ---- 提案 ----
    Q_PROPERTY(QString proposalText READ proposalText NOTIFY proposalChanged)
    Q_PROPERTY(bool hasPendingProposal READ hasPendingProposal NOTIFY proposalChanged)
    // ---- 通用 ----
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit ApplicationService(QObject *parent = nullptr);

    // 启动初始化：ensure 今日计划 + 刷新全部视图 + 补录检查（3.0.3 循环触发时机）
    Q_INVOKABLE void init();

    // ---- 今日任务 ----
    Q_INVOKABLE qint64 addTask(const QString &title, int plannedMinutes);
    Q_INVOKABLE bool startTask(qint64 id);
    Q_INVOKABLE bool completeTask(qint64 id, int actualMinutes);
    Q_INVOKABLE bool skipTask(qint64 id, const QString &reason);
    Q_INVOKABLE bool cancelTask(qint64 id, const QString &reason);

    // ---- 目标 ----
    Q_INVOKABLE qint64 createGoal(const QString &title, const QString &level, qint64 parentId,
                                  int priority);
    void refreshGoals();

    // ---- 状态 ----
    Q_INVOKABLE void loadTodayState();
    Q_INVOKABLE bool saveTodayState(const QString &sleep, int energy, int focus, int mood,
                                    const QString &note);

    // ---- 复盘 ----
    Q_INVOKABLE void draftTodayReview();               // 本地草案（2.5.6）
    Q_INVOKABLE void aiAnalyzeTodayReview();           // AI 分析（异步 → aiReviewReady）
    Q_INVOKABLE bool saveTodayReview();                // 保存 + 关闭当日计划
    void refreshPending();

    // ---- AI 提案（2.7.17：draft → 用户批准 → 生效）----
    Q_INVOKABLE void aiGenerateProposal(const QString &description); // 异步 → proposalChanged
    Q_INVOKABLE bool approveProposal();                // 批准 + 生效（EvolutionService）
    Q_INVOKABLE bool rejectProposal();

    // ---- 属性访问器 ----
    QList<QObject *> todayTasks() const { return m_todayItems; }
    QString todayDate() const { return m_todayDate; }
    QString taskSummary() const { return m_taskSummary; }
    QList<QObject *> goals() const { return m_goalItems; }
    QString stateSleep() const { return m_stateSleep; }
    int stateEnergy() const { return m_stateEnergy; }
    int stateFocus() const { return m_stateFocus; }
    int stateMood() const { return m_stateMood; }
    QString stateNote() const { return m_stateNote; }
    QString reviewSummary() const { return m_reviewSummary; }
    QString reviewProblems() const { return m_reviewProblems; }
    QString reviewCauses() const { return m_reviewCauses; }
    QString reviewNextActions() const { return m_reviewNextActions; }
    QStringList pendingReviewDates() const { return m_pendingDates; }
    QString proposalText() const { return m_proposalText; }
    bool hasPendingProposal() const { return m_currentProposal.id > 0; }
    QString lastError() const { return m_lastError; }

    void setReviewSummary(const QString &v) { m_reviewSummary = v; }
    void setReviewProblems(const QString &v) { m_reviewProblems = v; }
    void setReviewCauses(const QString &v) { m_reviewCauses = v; }
    void setReviewNextActions(const QString &v) { m_reviewNextActions = v; }

signals:
    void todayChanged();
    void goalsChanged();
    void stateChanged();
    void reviewChanged();
    void pendingChanged();
    void proposalChanged();
    void lastErrorChanged();
    void aiReviewReady(bool ok, const QString &error);

private:
    void setError(const QString &e);
    void refreshTodayInternal();
    void refreshGoalsInternal();

    // ---- 子系统 ----
    PlanningService m_planning;
    ExecutionService m_execution;
    CoreAndGoalService m_goalsSvc;
    StateService m_states;
    ReviewService m_reviews;
    EvolutionService m_evolution;
    AiEngine m_ai;

    // ---- UI 数据 ----
    QList<QObject *> m_todayItems;
    QList<QObject *> m_goalItems;
    QString m_todayDate;
    QString m_taskSummary;
    QString m_stateSleep;
    int m_stateEnergy = 0;
    int m_stateFocus = 0;
    int m_stateMood = 0;
    QString m_stateNote;
    QString m_reviewSummary;
    QString m_reviewProblems;
    QString m_reviewCauses;
    QString m_reviewNextActions;
    QStringList m_pendingDates;
    Proposal m_currentProposal;
    QString m_proposalText;
    QString m_lastError;
};

} // namespace PersonOS
