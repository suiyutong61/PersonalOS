#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "presentation/viewmodels/ListItemModel.h"

// 目标与路线页 ViewModel（DD-001 §11）：目标/路线/内容地图只读投影与用户命令；
// 只通过应用用例写入，不保存正式业务事实、不执行业务判定。
namespace PersonOS {

class GoalRouteViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pageState READ pageState NOTIFY pageStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(ListItemModel *goalsModel READ goalsModel CONSTANT)
    Q_PROPERTY(ListItemModel *mapsModel READ mapsModel CONSTANT)
    Q_PROPERTY(ListItemModel *routesModel READ routesModel CONSTANT)
    Q_PROPERTY(QString selectedGoalTitle READ selectedGoalTitle NOTIFY selectionChanged)
    Q_PROPERTY(QString coverageText READ coverageText NOTIFY selectionChanged)
    Q_PROPERTY(QString aiState READ aiState NOTIFY aiStateChanged)
    QML_ELEMENT

public:
    explicit GoalRouteViewModel(QObject *parent = nullptr);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectGoal(const QString &goalUid);

    // 命令（全部经应用用例；失败写入 lastError + conflict/validation 状态）
    Q_INVOKABLE void createGoal(const QString &title);
    Q_INVOKABLE void createMap(const QString &title);
    Q_INVOKABLE void addNodes(const QString &lines);        // 每行一个节点（等权）
    Q_INVOKABLE void confirmMap(const QString &mapUid);
    Q_INVOKABLE void proposeRoute(const QString &rationale, const QString &stageLines);
    Q_INVOKABLE void confirmRoute(const QString &routeUid);
    // AI 端到端生成路线候选（后台线程执行；aiState: idle/ai_waiting/error）
    Q_INVOKABLE void aiGenerateRoute();

    QString pageState() const { return m_pageState; }
    QString lastError() const { return m_lastError; }
    ListItemModel *goalsModel() { return &m_goalsModel; }
    ListItemModel *mapsModel() { return &m_mapsModel; }
    ListItemModel *routesModel() { return &m_routesModel; }
    QString selectedGoalTitle() const { return m_selectedGoalTitle; }
    QString coverageText() const { return m_coverageText; }
    QString aiState() const { return m_aiState; }

signals:
    void pageStateChanged();
    void lastErrorChanged();
    void selectionChanged();
    void dataChanged();
    void aiStateChanged();

private:
    void setState(const QString &state);
    void setError(const QString &message);
    void refreshSelection();

    QString m_pageState = QStringLiteral("idle");
    QString m_lastError;
    QString m_selectedGoalUid;
    QString m_selectedGoalTitle;
    QString m_coverageText;
    QString m_aiState = QStringLiteral("idle");
    ListItemModel m_goalsModel;
    ListItemModel m_mapsModel;
    ListItemModel m_routesModel;
};

} // namespace PersonOS
