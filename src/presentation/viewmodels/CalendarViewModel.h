#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "presentation/viewmodels/ListItemModel.h"

// 日历与提醒页 ViewModel（DD-001 §12；DR-024）：提醒规则/待投递/Deadline 只读投影。
// 提醒不得自动追加任务或改变 MEL；投递失败不改变业务状态。
namespace PersonOS {

class CalendarViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pageState READ pageState NOTIFY pageStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(ListItemModel *remindersModel READ remindersModel CONSTANT)
    Q_PROPERTY(ListItemModel *dueModel READ dueModel CONSTANT)
    Q_PROPERTY(ListItemModel *deliveriesModel READ deliveriesModel CONSTANT)
    QML_ELEMENT

public:
    explicit CalendarViewModel(QObject *parent = nullptr);

    Q_INVOKABLE void refresh();

    QString pageState() const { return m_pageState; }
    QString lastError() const { return m_lastError; }
    ListItemModel *remindersModel() { return &m_remindersModel; }
    ListItemModel *dueModel() { return &m_dueModel; }
    ListItemModel *deliveriesModel() { return &m_deliveriesModel; }

signals:
    void pageStateChanged();
    void lastErrorChanged();
    void dataChanged();

private:
    void setState(const QString &state);

    QString m_pageState = QStringLiteral("idle");
    QString m_lastError;
    ListItemModel m_remindersModel;
    ListItemModel m_dueModel;
    ListItemModel m_deliveriesModel;
};

} // namespace PersonOS
