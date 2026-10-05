#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "presentation/viewmodels/ListItemModel.h"

// 当前 MEL 页 ViewModel（DD-001 §11）：活跃 MEL 与任务投影、进度上报与状态命令。
// MEL 执行率与长期覆盖率/能力验收分开展示（DR-020/022）；进度条不暗示掌握。
namespace PersonOS {

class MelViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pageState READ pageState NOTIFY pageStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(ListItemModel *tasksModel READ tasksModel CONSTANT)
    Q_PROPERTY(QString melTitle READ melTitle NOTIFY melChanged)
    Q_PROPERTY(QString melState READ melState NOTIFY melChanged)
    Q_PROPERTY(QString melStateLabel READ melStateLabel NOTIFY melChanged)
    Q_PROPERTY(QString melDeadline READ melDeadline NOTIFY melChanged)
    Q_PROPERTY(QString melProgress READ melProgress NOTIFY melChanged)
    Q_PROPERTY(QString melUid READ melUid NOTIFY melChanged)
    // aiState 必须作为属性暴露:QML 用它门控 AI 按钮(防连点并发管线)
    Q_PROPERTY(QString aiState READ aiState NOTIFY aiStateChanged)
    // 进度汇报 → AI 调整建议（用户决策 2026-09-30：MEL 页主循环）
    Q_PROPERTY(ListItemModel *reviewModel READ reviewModel CONSTANT)
    Q_PROPERTY(QString reviewNotice READ reviewNotice NOTIFY reviewChanged)
    Q_PROPERTY(QString reviewUid READ reviewUid NOTIFY reviewChanged)
    Q_PROPERTY(bool reviewVisible READ reviewVisible NOTIFY reviewChanged)
    QML_ELEMENT

public:
    explicit MelViewModel(QObject *parent = nullptr);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void completeExecution();
    Q_INVOKABLE void settle();
    Q_INVOKABLE void pauseResume();
    // 确认并激活候选 MEL（首个 MEL 流程；draft/awaiting_confirmation 可见）
    Q_INVOKABLE void confirmMel();
    // 自然语言汇报 → AI 生成正式更新候选；用户不手填百分比/状态。
    Q_INVOKABLE void reportProgress(const QString &text);
    // 用户确认后才写入正式进度。
    Q_INVOKABLE void adoptReview();
    Q_INVOKABLE void abandonReview();
    // AI 端到端（后台线程执行；aiState: idle/ai_waiting/error）
    Q_INVOKABLE void aiGenerateMel();
    Q_INVOKABLE void aiSuggestMethods();

    QString pageState() const { return m_pageState; }
    QString lastError() const { return m_lastError; }
    ListItemModel *tasksModel() { return &m_tasksModel; }
    QString melTitle() const { return m_melTitle; }
    QString melState() const { return m_melState; }
    QString melStateLabel() const { return m_melStateLabel; }
    QString melDeadline() const { return m_melDeadline; }
    QString melProgress() const { return m_melProgress; }
    QString melUid() const { return m_melUid; }
    QString aiState() const { return m_aiState; }
    ListItemModel *reviewModel() { return &m_reviewModel; }
    QString reviewNotice() const { return m_reviewNotice; }
    QString reviewUid() const { return m_reviewUid; }
    bool reviewVisible() const { return m_reviewVisible; }

signals:
    void pageStateChanged();
    void lastErrorChanged();
    void melChanged();
    void dataChanged();
    void aiStateChanged();
    void reviewChanged();
    // 仅在进度审查候选已成功落库并刷新后发出；QML 据此清空输入，
    // 避免离线/校验失败时丢失用户刚写的汇报。
    void progressReviewGenerated();

private:
    void setState(const QString &state);
    void setError(const QString &message);
    void runAi(const QString &purpose, const QString &jobType);
    void refreshReview();

    QString m_pageState = QStringLiteral("idle");
    QString m_lastError;
    QString m_melUid;
    QString m_melTitle;
    QString m_melState;
    QString m_melStateLabel;
    QString m_melDeadline;
    QString m_melProgress;
    int m_melRevision = 1;
    QString m_aiState = QStringLiteral("idle");
    ListItemModel m_tasksModel;
    ListItemModel m_reviewModel;
    QString m_reviewNotice;
    QString m_reviewUid;
    bool m_reviewVisible = false;
};

} // namespace PersonOS
