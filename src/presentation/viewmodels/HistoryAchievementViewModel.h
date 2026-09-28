#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "presentation/viewmodels/ListItemModel.h"

// 历史与成就页 ViewModel（DD-001 §11；DR-031）：真实业务事件驱动的成就与复盘时间线。
// 不使用签到/断签/排行榜等压力机制；可关闭庆祝动效（页面仅展示事实）。
namespace PersonOS {

class HistoryAchievementViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pageState READ pageState NOTIFY pageStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(ListItemModel *achievementsModel READ achievementsModel CONSTANT)
    Q_PROPERTY(ListItemModel *timelineModel READ timelineModel CONSTANT)
    QML_ELEMENT

public:
    explicit HistoryAchievementViewModel(QObject *parent = nullptr);

    Q_INVOKABLE void refresh();

    QString pageState() const { return m_pageState; }
    QString lastError() const { return m_lastError; }
    ListItemModel *achievementsModel() { return &m_achievementsModel; }
    ListItemModel *timelineModel() { return &m_timelineModel; }

signals:
    void pageStateChanged();
    void lastErrorChanged();
    void dataChanged();

private:
    void setState(const QString &state);

    QString m_pageState = QStringLiteral("idle");
    QString m_lastError;
    ListItemModel m_achievementsModel;
    ListItemModel m_timelineModel;
};

} // namespace PersonOS
