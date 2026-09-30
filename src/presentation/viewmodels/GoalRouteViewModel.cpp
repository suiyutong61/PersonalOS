#include "presentation/viewmodels/GoalRouteViewModel.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlQuery>
#include <QStringList>
#include <QVariantMap>

#include <QtConcurrent/QtConcurrent>

#include "application/usecases/goal/ContentMapUseCases.h"
#include "application/usecases/goal/GoalUseCases.h"
#include "application/usecases/planning/AiPlanningUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/ai/SqlAiRepository.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/persistence/DatabaseConnectionFactory.h"
#include "presentation/viewmodels/AiServices.h"
#include "application/usecases/route/RouteStageDetailUseCases.h"
#include "application/usecases/route/RouteUseCases.h"
#include "database/DatabaseManager.h"
#include "domain/foundation/Uid.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/persistence/SqlContentMapRepository.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlKnowledgeRepository.h"
#include "infrastructure/persistence/SqlRouteRepository.h"
#include "presentation/viewmodels/VmSupport.h"

namespace PersonOS {

namespace {
using namespace PersonOS::Presentation;

std::optional<Domain::Uid> manifestUid(QSqlDatabase database)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral(
            "SELECT uid FROM domain_manifests_v3 WHERE domain_code='learning' ORDER BY id"))
        || !query.next())
        return std::nullopt;
    return Domain::Uid::parse(query.value(0).toString().toStdString());
}

QVariantMap row(const QString &uid, const QString &title, const QString &subtitle,
                const QString &badge, const QString &badgeTone, const QString &detail = {},
                bool retractable = false)
{
    return {{QStringLiteral("uid"), uid},
            {QStringLiteral("title"), title},
            {QStringLiteral("subtitle"), subtitle},
            {QStringLiteral("detail"), detail},
            {QStringLiteral("badge"), badge},
            {QStringLiteral("badgeTone"), badgeTone},
            {QStringLiteral("retractable"), retractable}};
}

} // namespace

GoalRouteViewModel::GoalRouteViewModel(QObject *parent) : QObject(parent) {}

void GoalRouteViewModel::setState(const QString &state)
{
    if (m_pageState == state)
        return;
    m_pageState = state;
    emit pageStateChanged();
}

void GoalRouteViewModel::setError(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
}

void GoalRouteViewModel::refresh()
{
    setState(QStringLiteral("loading"));
    setError({});

    const auto database = DatabaseManager::instance().database();
    const auto userUid = Presentation::activeUserUid(database);
    if (!userUid) {
        m_goalsModel.clear();
        m_mapsModel.clear();
        m_routesModel.clear();
        m_subjectsModel.clear();
        setState(QStringLiteral("empty"));
        return;
    }

    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlGoalRepository goalsRepo(database, clock);
    Infrastructure::SqlRouteRepository routesRepo(database, clock);

    // 宏伟目标列表。教材内容覆盖率属于后续学习材料进度，不能作为
    // “是否已经完成目标规划”的主徽标，避免把内容地图误当成目标层级。
    QVariantList goalRows;
    for (const auto &goal : goalsRepo.findByUser(*userUid)) {
        const bool hasConfirmedRoute = [&]() {
            for (const auto &route : routesRepo.findByGoal(goal.uid))
                if (route.status == Domain::RouteStatus::Confirmed
                    || route.status == Domain::RouteStatus::Active)
                    return true;
            return false;
        }();
        goalRows.append(row(QString::fromStdString(goal.uid.value()),
                            QString::fromStdString(goal.title),
                            QString::fromStdString(goal.description),
                            hasConfirmedRoute ? QStringLiteral("路线已确认")
                                              : QStringLiteral("待规划"),
                            hasConfirmedRoute ? QStringLiteral("success")
                                              : QStringLiteral("neutral")));
    }
    m_goalsModel.replace(goalRows);

    // 保持当前选择；无选择时选第一个
    if (m_selectedGoalUid.isEmpty() && !goalRows.isEmpty())
        m_selectedGoalUid = goalRows.first().toMap().value(QStringLiteral("uid")).toString();
    refreshSelection();

    setState(goalRows.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready"));
    emit dataChanged();
}

void GoalRouteViewModel::selectGoal(const QString &goalUid)
{
    if (m_selectedGoalUid == goalUid)
        return;
    m_selectedGoalUid = goalUid;
    refreshSelection();
    emit selectionChanged();
}

void GoalRouteViewModel::refreshSelection()
{
    m_mapsModel.clear();
    m_routesModel.clear();
    m_subjectsModel.clear();
    m_selectedGoalTitle.clear();
    m_selectedGoalDescription.clear();
    m_routeGraphJson = QStringLiteral("[]");
    m_coverageText.clear();
    if (m_selectedGoalUid.isEmpty())
        return;

    const auto goalUid = Domain::Uid::parse(m_selectedGoalUid.toStdString());
    if (!goalUid)
        return;

    const auto database = DatabaseManager::instance().database();
    const auto userUid = Presentation::activeUserUid(database);
    if (!userUid)
        return;

    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlGoalRepository goalsRepo(database, clock);
    Infrastructure::SqlContentMapRepository mapsRepo(database, clock);
    Infrastructure::SqlRouteRepository routesRepo(database, clock);
    Application::ContentMapUseCases mapUseCases(mapsRepo, goalsRepo, uids, clock);

    if (const auto goal = goalsRepo.findByUid(*goalUid)) {
        m_selectedGoalTitle = QString::fromStdString(goal->title);
        m_selectedGoalDescription = QString::fromStdString(goal->description);
    }

    // 内容地图 + 覆盖率（未确认地图不进入进度口径）
    QVariantList mapRows;
    const auto coverage = mapUseCases.coverageByGoal(*goalUid, *userUid);
    if (coverage)
        m_coverageText = percentText(coverage.value().coverage) + QStringLiteral(" · ")
                         + QString::fromStdString(coverage.value().caliberText);
    for (const auto &map : mapsRepo.mapsOfGoal(*goalUid)) {
        const QString raw = QString::fromStdString(Domain::toString(map.status));
        const QString badge = Presentation::contentMapStatusLabel(raw);
        const QString tone = map.status == Domain::ContentMapStatus::Confirmed
                                 ? QStringLiteral("success")
                                 : (map.status == Domain::ContentMapStatus::Retired
                                        ? QStringLiteral("neutral")
                                        : QStringLiteral("info"));
        mapRows.append(QVariantMap{
            {QStringLiteral("uid"), QString::fromStdString(map.uid.value())},
            {QStringLiteral("title"), QString::fromStdString(map.title)},
            {QStringLiteral("subtitle"),
             Presentation::sourceTypeLabel(QString::fromStdString(map.sourceType))},
            {QStringLiteral("badge"), badge},
            {QStringLiteral("badgeTone"), tone},
            // retractable 角色复用为"可确认"标记:仅草稿且已有节点
            // (空地图确认必被后端拒绝,不给用户必失败的操作入口)
            {QStringLiteral("retractable"),
             map.status == Domain::ContentMapStatus::Draft
                 && !mapsRepo.nodesOfMap(map.uid).empty()}});
    }
    m_mapsModel.replace(mapRows);

    // 路线（候选 → 用户确认；确认后为当前路线）。把阶段投影为用户能读懂的
    // “粗阶段与先后关系”，而不是只显示内部版本数量。
    QVariantList routeRows;
    QVariantList subjectRows;
    QJsonArray graphNodes;
    const auto goalRoutes = routesRepo.findByGoal(*goalUid);
    // 主界面只展示最新路线。旧候选和旧版本继续保留用于审计与追溯，
    // 但不能叠加到当前路线里造成阶段重复。
    if (!goalRoutes.empty()) {
        const auto &route = goalRoutes.back();
        const auto versions = routesRepo.versionsOf(route.uid);
        const QString badge = Presentation::routeStatusLabel(
            QString::fromStdString(Domain::toString(route.status)));
        const bool canConfirm = route.status == Domain::RouteStatus::Draft
                                || route.status == Domain::RouteStatus::Proposed;
        const QString tone = canConfirm
                                 ? QStringLiteral("warning")
                                 : (route.status == Domain::RouteStatus::Confirmed
                                        ? QStringLiteral("success")
                                        : QStringLiteral("neutral"));
        QStringList subjects;
        QString rationale;
        if (!versions.empty()) {
            const auto &latest = versions.back();
            rationale = QString::fromStdString(latest.rationale);
            QSqlQuery stages(database);
            stages.prepare(QStringLiteral(
                "SELECT uid, title, description, completion_rule_json FROM route_stages_v3 "
                "WHERE route_version_id=(SELECT id FROM route_versions_v3 WHERE uid=?) "
                "ORDER BY sequence_no"));
            stages.addBindValue(QString::fromStdString(latest.uid));
            if (stages.exec()) {
                while (stages.next()) {
                    const QString stageUid = stages.value(0).toString();
                    QString subject = stages.value(1).toString();
                    const QString description = stages.value(2).toString();
                    const QJsonObject relation = QJsonDocument::fromJson(
                        stages.value(3).toString().toUtf8()).object();
                    const QString relationship = relation.value(QStringLiteral("relationship")).toString();
                    if (relationship == QStringLiteral("parallel"))
                        subject += QStringLiteral("（可并行）");
                    else if (relationship == QStringLiteral("after"))
                        subject += QStringLiteral("（有前置）");
                    subjects.append(subject);
                    const QJsonArray dependencies =
                        relation.value(QStringLiteral("depends_on")).toArray();
                    QStringList dependencyNames;
                    for (const auto &dependency : dependencies)
                        dependencyNames.append(dependency.toString());
                    QStringList keyContents;
                    for (const auto &content : relation.value(QStringLiteral("key_contents")).toArray())
                        keyContents.append(content.toString());
                    const QString relationLabel = relationship == QStringLiteral("parallel")
                                                      ? QStringLiteral("可并行学习")
                                                      : (relationship == QStringLiteral("after")
                                                             ? QStringLiteral("需先完成前置")
                                                             : QStringLiteral("可从这里开始"));
                    subjectRows.append(row(
                        stageUid,   // 真实阶段 uid：点击节点定位 route_stages_v3 记录
                        stages.value(1).toString(),
                        dependencyNames.isEmpty()
                            ? relationLabel
                            : QStringLiteral("%1：%2").arg(relationLabel,
                                                           dependencyNames.join(QStringLiteral("、"))),
                        relationLabel,
                        relationship == QStringLiteral("parallel") ? QStringLiteral("info")
                                                                    : QStringLiteral("neutral"),
                        keyContents.isEmpty()
                            ? description
                            : QStringLiteral("%1\n包含：%2")
                                  .arg(description, keyContents.join(QStringLiteral("、")))));
                    QJsonObject graphNode;
                    graphNode.insert(QStringLiteral("uid"), stageUid);
                    graphNode.insert(QStringLiteral("title"), stages.value(1).toString());
                    graphNode.insert(QStringLiteral("description"), description);
                    graphNode.insert(QStringLiteral("relationship"), relationship);
                    graphNode.insert(QStringLiteral("depends_on"), dependencies);
                    graphNode.insert(QStringLiteral("key_contents"),
                                     relation.value(QStringLiteral("key_contents")));
                    graphNodes.append(graphNode);
                }
            }
        }
        routeRows.append(row(QString::fromStdString(route.uid.value()),
                              subjects.isEmpty() ? QStringLiteral("路线方案")
                                                 : subjects.join(QStringLiteral(" → ")),
                              rationale, badge, tone, {},
                             // 仅候选路线可确认(经角色表达,徽标为中文标签后
                             // 不再用原始串比较)
                              canConfirm));
    }
    m_routesModel.replace(routeRows);
    m_subjectsModel.replace(subjectRows);
    m_routeGraphJson = QString::fromUtf8(
        QJsonDocument(graphNodes).toJson(QJsonDocument::Compact));
    // 阶段层与路线层同源刷新：路线重生成后旧阶段 uid 失效 → stageMissing
    refreshStageDetail();
    emit selectionChanged();
}

void GoalRouteViewModel::createGoal(const QString &title, const QString &description)
{
    const auto database = DatabaseManager::instance().database();
    const auto userUid = Presentation::activeUserUid(database);
    const auto manifest = manifestUid(database);
    if (!userUid || !manifest) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("缺少用户或学习领域配置"));
        return;
    }
    if (title.trimmed().isEmpty()) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("目标标题不能为空"));
        return;
    }

    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlGoalRepository goalsRepo(database, clock);
    Application::GoalUseCases useCases(goalsRepo, uids, clock);
    Application::GoalUseCases::CreateInput input;
    input.userId = *userUid;
    input.domainManifestId = *manifest;
    input.title = title.trimmed().toStdString();
    input.description = description.trimmed().toStdString();
    input.goalType = "long_term_outcome";
    input.desiredLevelJson = std::string("{}");
    input.userDefinedLevel = false;
    const auto created = useCases.createGoal(input);
    if (!created) {
        setState(QStringLiteral("error"));
        setError(friendlyError(created.error().message, created.error().detail));
        return;
    }
    m_selectedGoalUid = QString::fromStdString(created.value().goal.uid.value());
    refresh();
}

void GoalRouteViewModel::createMap(const QString &title)
{
    const auto goalUid = Domain::Uid::parse(m_selectedGoalUid.toStdString());
    const auto database = DatabaseManager::instance().database();
    const auto userUid = Presentation::activeUserUid(database);
    if (!goalUid || !userUid || title.trimmed().isEmpty()) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("请先选择目标并填写目录名称"));
        return;
    }

    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlGoalRepository goalsRepo(database, clock);
    Infrastructure::SqlContentMapRepository mapsRepo(database, clock);
    Application::ContentMapUseCases useCases(mapsRepo, goalsRepo, uids, clock);
    Application::ContentMapUseCases::CreateMapInput input;
    input.userId = *userUid;
    input.goalId = *goalUid;
    input.title = title.trimmed().toStdString();
    input.sourceType = "user_document";
    const auto created = useCases.createMap(input);
    if (!created) {
        setState(QStringLiteral("error"));
        setError(friendlyError(created.error().message, created.error().detail));
        return;
    }
    refreshSelection();
}

void GoalRouteViewModel::addNodes(const QString &lines)
{
    // 需要一张草稿地图：取当前目标的最近草稿地图；没有则提示先创建
    const auto goalUid = Domain::Uid::parse(m_selectedGoalUid.toStdString());
    const auto database = DatabaseManager::instance().database();
    const auto userUid = Presentation::activeUserUid(database);
    if (!goalUid || !userUid) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("请先选择目标"));
        return;
    }

    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlGoalRepository goalsRepo(database, clock);
    Infrastructure::SqlContentMapRepository mapsRepo(database, clock);
    Application::ContentMapUseCases useCases(mapsRepo, goalsRepo, uids, clock);

    std::optional<Domain::Uid> draftMap;
    for (const auto &map : mapsRepo.mapsOfGoal(*goalUid))
        if (map.status == Domain::ContentMapStatus::Draft)
            draftMap = map.uid;
    if (!draftMap) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("请先创建目录地图，再添加章/节节点"));
        return;
    }

    Application::ContentMapUseCases::AddNodesInput input;
    int sequence = 0;
    for (const auto &line : lines.split(u'\n', Qt::SkipEmptyParts)) {
        const QString title = line.trimmed();
        if (title.isEmpty())
            continue;
        Domain::ContentNode node;
        node.title = title.toStdString();
        node.nodeType = "chapter";
        node.sequenceNo = sequence++;
        node.weight = 1.0;
        input.nodes.push_back(std::move(node));
    }
    if (input.nodes.empty()) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("没有可添加的节点"));
        return;
    }
    const auto added = useCases.addNodes(*draftMap, input);
    if (!added) {
        setState(QStringLiteral("error"));
        setError(friendlyError(added.error().message, added.error().detail));
        return;
    }
    refreshSelection();
}

void GoalRouteViewModel::confirmMap(const QString &mapUid)
{
    const auto parsed = Domain::Uid::parse(mapUid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("无效的目录标识"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlGoalRepository goalsRepo(database, clock);
    Infrastructure::SqlContentMapRepository mapsRepo(database, clock);
    Application::ContentMapUseCases useCases(mapsRepo, goalsRepo, uids, clock);
    const auto current = mapsRepo.findMapByUid(*parsed);
    if (!current) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("目录不存在"));
        return;
    }
    const auto confirmed = useCases.confirmMap(*parsed, current->revision);
    if (!confirmed) {
        setState(QStringLiteral("error"));
        setError(friendlyError(confirmed.error().message, confirmed.error().detail));
        return;
    }
    refreshSelection();
}

void GoalRouteViewModel::proposeRoute(const QString &rationale, const QString &stageLines)
{
    const auto goalUid = Domain::Uid::parse(m_selectedGoalUid.toStdString());
    const auto database = DatabaseManager::instance().database();
    if (!goalUid || rationale.trimmed().isEmpty() || stageLines.trimmed().isEmpty()) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("请填写路线依据与阶段（每行一个阶段）"));
        return;
    }

    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlGoalRepository goalsRepo(database, clock);
    Infrastructure::SqlRouteRepository routesRepo(database, clock);
    Application::RouteUseCases useCases(routesRepo, goalsRepo, uids, clock);

    Application::RouteUseCases::ProposeInput input;
    input.goalId = *goalUid;
    input.rationale = rationale.trimmed().toStdString();
    input.evidenceSummary = std::string("用户手工提出；AI 生成与知识校准待 IMP-005/006");
    input.assumptionsJson = std::string("{}");
    input.createdBy = "user";
    int sequence = 0;
    for (const auto &line : stageLines.split(u'\n', Qt::SkipEmptyParts)) {
        const QString title = line.trimmed();
        if (title.isEmpty())
            continue;
        Domain::RouteStage stage;
        stage.title = title.toStdString();
        stage.sequenceNo = sequence++;
        stage.completionRuleJson = std::string("{}");
        input.stages.push_back(std::move(stage));
    }
    if (input.stages.empty()) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("路线至少需要一个阶段"));
        return;
    }
    const auto proposed = useCases.proposeRoute(input);
    if (!proposed) {
        setState(QStringLiteral("error"));
        setError(friendlyError(proposed.error().message, proposed.error().detail));
        return;
    }
    refreshSelection();
}

void GoalRouteViewModel::confirmRoute(const QString &routeUid)
{
    const auto parsed = Domain::Uid::parse(routeUid.toStdString());
    if (!parsed) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("无效的路线标识"));
        return;
    }
    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlGoalRepository goalsRepo(database, clock);
    Infrastructure::SqlRouteRepository routesRepo(database, clock);
    Infrastructure::SqlAiRepository decisions(database, clock);
    Application::RouteUseCases useCases(routesRepo, goalsRepo, uids, clock, &decisions);
    const auto current = routesRepo.findByUid(*parsed);
    if (!current) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("路线不存在"));
        return;
    }
    const auto confirmed = useCases.confirmRoute(*parsed, current->revision);
    if (!confirmed) {
        setState(QStringLiteral("error"));
        setError(friendlyError(confirmed.error().message, confirmed.error().detail));
        return;
    }
    refreshSelection();
}


void GoalRouteViewModel::aiGenerateRoute(const QString &userGuidance)
{
    const auto goalUid = Domain::Uid::parse(m_selectedGoalUid.toStdString());
    const auto userUid = Presentation::activeUserUid(DatabaseManager::instance().database());
    if (!goalUid || !userUid) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("请先选择目标"));
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

    // 后台线程执行：每线程独立数据库连接；结果回主线程刷新
    const QString dbPath = DatabaseManager::instance().databasePath();
    QtConcurrent::run([this, goalUid = *goalUid, userUid = *userUid,
                       guidance = userGuidance.trimmed(), dbPath]() {
        QString error;
        Infrastructure::DatabaseConnectionFactory factory(dbPath);
        QString openError;
        QSqlDatabase workerDb = factory.openForCurrentThread(QStringLiteral("ai_route"),
                                                             &openError);
        if (!workerDb.isOpen())
            return QStringLiteral("数据库连接失败：") + openError;
        const auto pipeline = Presentation::buildPlanningPipeline(workerDb);
        const auto result = pipeline->useCases->generateRouteProposal(
            userUid, goalUid, guidance.toStdString());
        if (!result)
            error = Presentation::friendlyError(result.error().message, result.error().detail);
        Infrastructure::DatabaseConnectionFactory::closeCurrentThreadConnection(
            QStringLiteral("ai_route"));
        return error;
    }).then([this](QString error) {
        if (error.isEmpty()) {
            refresh();
            refreshSelection();
        } else {
            setState(QStringLiteral("error"));
            setError(error);
        }
        m_aiState = QStringLiteral("idle");
        emit aiStateChanged();
    });
}

// ===== 阶段详情（viewIndex=4 层）=====

void GoalRouteViewModel::selectStage(const QString &stageUid)
{
    if (m_selectedStageUid == stageUid)
        return;
    m_selectedStageUid = stageUid;
    refreshStageDetail();
}

void GoalRouteViewModel::refreshStageDetail()
{
    m_stageDetailModel.clear();
    m_stageMaterialsModel.clear();
    m_stageInfoTitle.clear();
    m_stageInfoDescription.clear();
    m_stageInfoKeyContents.clear();
    m_stageDetailBadge.clear();
    m_stageDetailRationale.clear();
    m_stageDetailVersion = 0;
    m_stageDetailConfirmable = false;
    m_stageRouteConfirmed = false;
    m_stageMissing = false;

    if (m_selectedStageUid.isEmpty()) {
        emit stageChanged();
        return;
    }
    const auto stageUid = Domain::Uid::parse(m_selectedStageUid.toStdString());
    if (!stageUid) {
        m_stageMissing = true;
        emit stageChanged();
        return;
    }

    const auto database = DatabaseManager::instance().database();
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlRouteRepository routesRepo(database, clock);
    Infrastructure::SqlKnowledgeRepository knowledgeRepo(database, clock);

    const auto location = routesRepo.locateStage(*stageUid);
    if (!location) {
        // 路线重生成后旧阶段 uid 失效：如实标记，不给伪造内容
        m_stageMissing = true;
        emit stageChanged();
        return;
    }

    m_stageInfoTitle = QString::fromStdString(location->stage.title);
    m_stageInfoDescription = QString::fromStdString(location->stage.description);
    const QJsonObject relation = QJsonDocument::fromJson(
        QString::fromStdString(location->stage.completionRuleJson).toUtf8()).object();
    QStringList contents;
    for (const auto &content : relation.value(QStringLiteral("key_contents")).toArray())
        contents.append(content.toString());
    m_stageInfoKeyContents = contents.join(QStringLiteral("、"));
    m_stageRouteConfirmed = location->routeStatus == Domain::RouteStatus::Confirmed
                            || location->routeStatus == Domain::RouteStatus::Active;

    // 详情内容（最新版本；旧版本保留审计不叠加展示）
    const auto versions = routesRepo.stageDetailVersionsOf(*stageUid);
    if (!versions.empty()) {
        const auto &latest = versions.back();
        m_stageDetailVersion = latest.versionNo;
        m_stageDetailRationale = QString::fromStdString(latest.rationale);
        const bool confirmed = latest.userConfirmedAt.has_value();
        m_stageDetailBadge = confirmed
                                 ? QStringLiteral("已确认 v%1").arg(latest.versionNo)
                                 : QStringLiteral("候选 v%1 · 待确认").arg(latest.versionNo);
        // 已确认内容不再提供确认入口；路线未确认不给生成入口（QML 另行引导）
        m_stageDetailConfirmable = !confirmed && m_stageRouteConfirmed;

        const auto parseArray = [](const QString &json) {
            return QJsonDocument::fromJson(json.toUtf8()).array();
        };
        // kind 角色供 QML 分组（判断走原始键，展示文案在 QML 分组标题）
        QVariantList detailRows;
        const auto textRow = [&detailRows](const QString &kind, const QString &title,
                                           const QString &subtitle = {},
                                           const QString &detail = {}) {
            detailRows.append(QVariantMap{
                {QStringLiteral("uid"), QString::number(detailRows.size())},
                {QStringLiteral("kind"), kind},
                {QStringLiteral("title"), title},
                {QStringLiteral("subtitle"), subtitle},
                {QStringLiteral("detail"), detail},
                {QStringLiteral("badge"), {}},
                {QStringLiteral("badgeTone"), {}},
                {QStringLiteral("retractable"), false}});
        };
        for (const auto &value :
             parseArray(QString::fromStdString(latest.outcomesJson)))
            textRow(QStringLiteral("outcome"),
                    value.toObject().value(QStringLiteral("description")).toString());
        for (const auto &value : parseArray(QString::fromStdString(latest.tasksJson))) {
            const QJsonObject object = value.toObject();
            const int sequence = object.value(QStringLiteral("sequence_no")).toInt();
            const int effort = object.value(QStringLiteral("estimated_effort_min")).toInt();
            textRow(QStringLiteral("task"), object.value(QStringLiteral("title")).toString(),
                    effort > 0
                        ? QStringLiteral("任务 %1 · 约 %2 分钟").arg(sequence).arg(effort)
                        : QStringLiteral("任务 %1").arg(sequence),
                    object.value(QStringLiteral("description")).toString());
        }
        for (const auto &value : parseArray(QString::fromStdString(latest.projectsJson))) {
            const QJsonObject object = value.toObject();
            const QString verifiable =
                object.value(QStringLiteral("verifiable_result")).toString();
            const QString description = object.value(QStringLiteral("description")).toString();
            textRow(QStringLiteral("project"),
                    object.value(QStringLiteral("title")).toString(),
                    QStringLiteral("项目/练习"),
                    verifiable.isEmpty()
                        ? description
                        : QStringLiteral("%1\n可验证产出：%2").arg(description, verifiable));
        }
        for (const auto &value :
             parseArray(QString::fromStdString(latest.criteriaJson)))
            textRow(QStringLiteral("criterion"),
                    value.toObject().value(QStringLiteral("description")).toString());
        m_stageDetailModel.replace(detailRows);
    }

    // 资料绑定（LEFT JOIN 语义：知识条目可被物理删除，容错显示"资料已归档"）
    QVariantList materialRows;
    for (const auto &material : routesRepo.stageMaterialsOf(*stageUid)) {
        QString title = QString::fromStdString(material.knowledgeItemUid);
        QString typeLabel = QStringLiteral("资料已归档");
        const auto parsedItem = Domain::Uid::parse(material.knowledgeItemUid);
        if (parsedItem)
            if (const auto item = knowledgeRepo.findItem(*parsedItem)) {
                title = QString::fromStdString(item->title);
                typeLabel = Presentation::knowledgeTypeLabel(
                    QString::fromStdString(Domain::toString(item->libraryType)));
            }
        const QString choice = QString::fromStdString(material.userChoice);
        materialRows.append(QVariantMap{
            {QStringLiteral("uid"), QString::fromStdString(material.knowledgeItemUid)},
            // kind 保留原始键供 QML 判断（展示文案经 badge 中文标签）
            {QStringLiteral("kind"), choice},
            {QStringLiteral("title"), title},
            {QStringLiteral("subtitle"), QString::fromStdString(material.reason)},
            {QStringLiteral("detail"), typeLabel},
            {QStringLiteral("badge"), Presentation::stageMaterialChoiceLabel(choice)},
            {QStringLiteral("badgeTone"),
             choice == QStringLiteral("accepted")
                 ? QStringLiteral("success")
                 : (choice == QStringLiteral("rejected") ? QStringLiteral("neutral")
                                                         : QStringLiteral("warning"))},
            {QStringLiteral("retractable"), choice == QStringLiteral("pending")}});
    }
    m_stageMaterialsModel.replace(materialRows);
    emit stageChanged();
}

void GoalRouteViewModel::aiGenerateStageDetail(const QString &userGuidance)
{
    const auto stageUid = Domain::Uid::parse(m_selectedStageUid.toStdString());
    const auto userUid = Presentation::activeUserUid(DatabaseManager::instance().database());
    if (!stageUid || !userUid) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("请先选择路线阶段"));
        return;
    }
    if (!m_stageRouteConfirmed) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("路线确认后才能生成阶段详情"));
        return;
    }
    // 诚实降级预检：未配置可用模型直接给出可行动指引（同路线层口径）
    {
        Infrastructure::QtSystemClock clock;
        Infrastructure::SqlAiRepository aiRepo(DatabaseManager::instance().database(), clock);
        if (!aiRepo.findFirstEnabledConfig()) {
            setState(QStringLiteral("offline"));
            setError(QStringLiteral("未配置可用的模型连接：请到设置页添加、测试并启用连接"));
            return;
        }
    }
    m_stageAiState = QStringLiteral("ai_waiting");
    emit stageAiStateChanged();

    // 后台线程执行：每线程独立数据库连接；结果回主线程刷新（独立于 aiState，
    // 避免路线层 Connections 把阶段完成误读为路线生成完成）
    const QString dbPath = DatabaseManager::instance().databasePath();
    QtConcurrent::run([this, stageUid = *stageUid, userUid = *userUid,
                       guidance = userGuidance.trimmed(), dbPath]() {
        QString error;
        Infrastructure::DatabaseConnectionFactory factory(dbPath);
        QString openError;
        QSqlDatabase workerDb = factory.openForCurrentThread(QStringLiteral("ai_stage_detail"),
                                                             &openError);
        if (!workerDb.isOpen())
            return QStringLiteral("数据库连接失败：") + openError;
        const auto pipeline = Presentation::buildPlanningPipeline(workerDb);
        const auto result = pipeline->useCases->generateStageDetail(
            userUid, stageUid, guidance.toStdString());
        if (!result)
            error = Presentation::friendlyError(result.error().message, result.error().detail);
        Infrastructure::DatabaseConnectionFactory::closeCurrentThreadConnection(
            QStringLiteral("ai_stage_detail"));
        return error;
    }).then([this](QString error) {
        if (error.isEmpty()) {
            refreshStageDetail();
        } else {
            setState(QStringLiteral("error"));
            setError(error);
        }
        m_stageAiState = QStringLiteral("idle");
        emit stageAiStateChanged();
    });
}

void GoalRouteViewModel::confirmStageDetail()
{
    const auto stageUid = Domain::Uid::parse(m_selectedStageUid.toStdString());
    const auto database = DatabaseManager::instance().database();
    if (!stageUid) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("无效的阶段标识"));
        return;
    }
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlRouteRepository routesRepo(database, clock);
    Infrastructure::SqlAiRepository decisions(database, clock);
    Application::RouteStageDetailUseCases useCases(routesRepo, decisions, clock);
    const auto confirmed = useCases.confirmStageDetail(*stageUid, m_stageDetailVersion);
    if (!confirmed) {
        setState(QStringLiteral("error"));
        setError(friendlyError(confirmed.error().message, confirmed.error().detail));
        return;
    }
    refreshStageDetail();
}

void GoalRouteViewModel::respondStageMaterial(const QString &itemUid, const QString &choice)
{
    const auto stageUid = Domain::Uid::parse(m_selectedStageUid.toStdString());
    const auto database = DatabaseManager::instance().database();
    if (!stageUid || itemUid.isEmpty()) {
        setState(QStringLiteral("error"));
        setError(QStringLiteral("无效的阶段或资料标识"));
        return;
    }
    Infrastructure::QtSystemClock clock;
    Infrastructure::SqlRouteRepository routesRepo(database, clock);
    Infrastructure::SqlAiRepository decisions(database, clock);
    Application::RouteStageDetailUseCases useCases(routesRepo, decisions, clock);
    const auto responded = useCases.respondStageMaterial(
        *stageUid, itemUid.toStdString(), choice.toStdString());
    if (!responded) {
        setState(QStringLiteral("error"));
        setError(friendlyError(responded.error().message, responded.error().detail));
        return;
    }
    refreshStageDetail();
}

} // namespace PersonOS
