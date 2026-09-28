#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// 首页总览 ViewModel（DD-001 §11）：只读状态展示，不保存正式业务事实；
// 暴露 idle/loading/ready/error 页面状态；命令通过应用用例执行。
// 长期覆盖率与 MEL 执行率分开展示（DR-020），不合并为一个百分比。
namespace PersonOS {

class DashboardViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pageState READ pageState NOTIFY pageStateChanged)
    Q_PROPERTY(QString activeMelTitle READ activeMelTitle NOTIFY dashboardChanged)
    Q_PROPERTY(QString activeMelProgress READ activeMelProgress NOTIFY dashboardChanged)
    Q_PROPERTY(int dueReminderCount READ dueReminderCount NOTIFY dashboardChanged)
    Q_PROPERTY(int goalCount READ goalCount NOTIFY dashboardChanged)
    Q_PROPERTY(QString coverageText READ coverageText NOTIFY dashboardChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    QML_ELEMENT

public:
    explicit DashboardViewModel(QObject *parent = nullptr);

    Q_INVOKABLE void refresh();

    QString pageState() const { return m_pageState; }
    QString activeMelTitle() const { return m_activeMelTitle; }
    QString activeMelProgress() const { return m_activeMelProgress; }
    int dueReminderCount() const { return m_dueReminderCount; }
    int goalCount() const { return m_goalCount; }
    QString coverageText() const { return m_coverageText; }
    QString lastError() const { return m_lastError; }

signals:
    void pageStateChanged();
    void dashboardChanged();
    void lastErrorChanged();

private:
    void setState(const QString &state);

    QString m_pageState = QStringLiteral("idle");
    QString m_activeMelTitle;
    QString m_activeMelProgress;
    int m_dueReminderCount = 0;
    int m_goalCount = 0;
    QString m_coverageText;
    QString m_lastError;
};

} // namespace PersonOS
