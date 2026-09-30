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
    Q_PROPERTY(ListItemModel *subjectsModel READ subjectsModel CONSTANT)
    Q_PROPERTY(QString selectedGoalTitle READ selectedGoalTitle NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedGoalDescription READ selectedGoalDescription NOTIFY selectionChanged)
    Q_PROPERTY(QString routeGraphJson READ routeGraphJson NOTIFY selectionChanged)
    Q_PROPERTY(QString coverageText READ coverageText NOTIFY selectionChanged)
    Q_PROPERTY(QString aiState READ aiState NOTIFY aiStateChanged)
    // ---- 阶段详情（viewIndex=4 层；2026-09-30）----
    Q_PROPERTY(ListItemModel *stageDetailModel READ stageDetailModel CONSTANT)
    Q_PROPERTY(ListItemModel *stageMaterialsModel READ stageMaterialsModel CONSTANT)
    Q_PROPERTY(QString stageInfoTitle READ stageInfoTitle NOTIFY stageChanged)
    Q_PROPERTY(QString stageInfoDescription READ stageInfoDescription NOTIFY stageChanged)
    Q_PROPERTY(QString stageInfoKeyContents READ stageInfoKeyContents NOTIFY stageChanged)
    Q_PROPERTY(QString stageDetailBadge READ stageDetailBadge NOTIFY stageChanged)
    Q_PROPERTY(QString stageDetailRationale READ stageDetailRationale NOTIFY stageChanged)
    Q_PROPERTY(int stageDetailVersion READ stageDetailVersion NOTIFY stageChanged)
    Q_PROPERTY(bool stageDetailConfirmable READ stageDetailConfirmable NOTIFY stageChanged)
    Q_PROPERTY(bool stageRouteConfirmed READ stageRouteConfirmed NOTIFY stageChanged)
    Q_PROPERTY(bool stageMissing READ stageMissing NOTIFY stageChanged)
    Q_PROPERTY(QString stageAiState READ stageAiState NOTIFY stageAiStateChanged)
    QML_ELEMENT

public:
    explicit GoalRouteViewModel(QObject *parent = nullptr);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectGoal(const QString &goalUid);

    // 命令（全部经应用用例；失败写入 lastError + conflict/validation 状态）
    Q_INVOKABLE void createGoal(const QString &title, const QString &description);
    Q_INVOKABLE void createMap(const QString &title);
    Q_INVOKABLE void addNodes(const QString &lines);        // 每行一个节点（等权）
    Q_INVOKABLE void confirmMap(const QString &mapUid);
    Q_INVOKABLE void proposeRoute(const QString &rationale, const QString &stageLines);
    Q_INVOKABLE void confirmRoute(const QString &routeUid);
    // AI 端到端生成路线候选（后台线程执行；aiState: idle/ai_waiting/error）
    Q_INVOKABLE void aiGenerateRoute(const QString &userGuidance);

    // 阶段详情：进入/刷新详情层、AI 生成候选、整版确认、资料逐条决定
    Q_INVOKABLE void selectStage(const QString &stageUid);
    Q_INVOKABLE void aiGenerateStageDetail(const QString &userGuidance);
    Q_INVOKABLE void confirmStageDetail();
    Q_INVOKABLE void respondStageMaterial(const QString &itemUid, const QString &choice);

    QString pageState() const { return m_pageState; }
    QString lastError() const { return m_lastError; }
    ListItemModel *goalsModel() { return &m_goalsModel; }
    ListItemModel *mapsModel() { return &m_mapsModel; }
    ListItemModel *routesModel() { return &m_routesModel; }
    ListItemModel *subjectsModel() { return &m_subjectsModel; }
    QString selectedGoalTitle() const { return m_selectedGoalTitle; }
    QString selectedGoalDescription() const { return m_selectedGoalDescription; }
    QString routeGraphJson() const { return m_routeGraphJson; }
    QString coverageText() const { return m_coverageText; }
    QString aiState() const { return m_aiState; }

    ListItemModel *stageDetailModel() { return &m_stageDetailModel; }
    ListItemModel *stageMaterialsModel() { return &m_stageMaterialsModel; }
    QString stageInfoTitle() const { return m_stageInfoTitle; }
    QString stageInfoDescription() const { return m_stageInfoDescription; }
    QString stageInfoKeyContents() const { return m_stageInfoKeyContents; }
    QString stageDetailBadge() const { return m_stageDetailBadge; }
    QString stageDetailRationale() const { return m_stageDetailRationale; }
    int stageDetailVersion() const { return m_stageDetailVersion; }
    bool stageDetailConfirmable() const { return m_stageDetailConfirmable; }
    bool stageRouteConfirmed() const { return m_stageRouteConfirmed; }
    bool stageMissing() const { return m_stageMissing; }
    QString stageAiState() const { return m_stageAiState; }

signals:
    void pageStateChanged();
    void lastErrorChanged();
    void selectionChanged();
    void dataChanged();
    void aiStateChanged();
    void stageChanged();
    void stageAiStateChanged();

private:
    void setState(const QString &state);
    void setError(const QString &message);
    void refreshSelection();
    void refreshStageDetail();

    QString m_pageState = QStringLiteral("idle");
    QString m_lastError;
    QString m_selectedGoalUid;
    QString m_selectedGoalTitle;
    QString m_selectedGoalDescription;
    QString m_routeGraphJson = QStringLiteral("[]");
    QString m_coverageText;
    QString m_aiState = QStringLiteral("idle");
    ListItemModel m_goalsModel;
    ListItemModel m_mapsModel;
    ListItemModel m_routesModel;
    ListItemModel m_subjectsModel;

    QString m_selectedStageUid;
    QString m_stageInfoTitle;
    QString m_stageInfoDescription;
    QString m_stageInfoKeyContents;
    QString m_stageDetailBadge;
    QString m_stageDetailRationale;
    int m_stageDetailVersion = 0;
    bool m_stageDetailConfirmable = false;
    bool m_stageRouteConfirmed = false;
    bool m_stageMissing = false;
    QString m_stageAiState = QStringLiteral("idle");
    ListItemModel m_stageDetailModel;
    ListItemModel m_stageMaterialsModel;
};

} // namespace PersonOS
