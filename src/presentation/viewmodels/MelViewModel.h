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
    Q_PROPERTY(QString melDeadline READ melDeadline NOTIFY melChanged)
    Q_PROPERTY(QString melProgress READ melProgress NOTIFY melChanged)
    Q_PROPERTY(QString melUid READ melUid NOTIFY melChanged)
    QML_ELEMENT

public:
    explicit MelViewModel(QObject *parent = nullptr);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void recordProgress(const QString &taskUid, int percent,
                                    const QString &note);
    Q_INVOKABLE void completeExecution();
    Q_INVOKABLE void settle();
    Q_INVOKABLE void pauseResume();
    // AI 端到端（后台线程执行；aiState: idle/ai_waiting/error）
    Q_INVOKABLE void aiGenerateMel();
    Q_INVOKABLE void aiSuggestMethods();

    QString pageState() const { return m_pageState; }
    QString lastError() const { return m_lastError; }
    ListItemModel *tasksModel() { return &m_tasksModel; }
    QString melTitle() const { return m_melTitle; }
    QString melState() const { return m_melState; }
    QString melDeadline() const { return m_melDeadline; }
    QString melProgress() const { return m_melProgress; }
    QString melUid() const { return m_melUid; }
    QString aiState() const { return m_aiState; }

signals:
    void pageStateChanged();
    void lastErrorChanged();
    void melChanged();
    void dataChanged();
    void aiStateChanged();

private:
    void setState(const QString &state);
    void setError(const QString &message);
    void runAi(const QString &purpose, const QString &jobType);

    QString m_pageState = QStringLiteral("idle");
    QString m_lastError;
    QString m_melUid;
    QString m_melTitle;
    QString m_melState;
    QString m_melDeadline;
    QString m_melProgress;
    int m_melRevision = 1;
    QString m_aiState = QStringLiteral("idle");
    ListItemModel m_tasksModel;
};

} // namespace PersonOS
