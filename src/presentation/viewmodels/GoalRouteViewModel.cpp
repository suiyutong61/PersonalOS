#include "presentation/viewmodels/GoalRouteViewModel.h"

#include <QSqlQuery>
#include <QVariantMap>

#include <QtConcurrent/QtConcurrent>

#include "application/usecases/goal/ContentMapUseCases.h"
#include "application/usecases/goal/GoalUseCases.h"
#include "application/usecases/planning/AiPlanningUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/persistence/DatabaseConnectionFactory.h"
#include "presentation/viewmodels/AiServices.h"
#include "application/usecases/route/RouteUseCases.h"
#include "database/DatabaseManager.h"
#include "domain/foundation/Uid.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/persistence/SqlContentMapRepository.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
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
                const QString &badge, const QString &badgeTone, const QString &detail = {})
{
    return {{QStringLiteral("uid"), uid},
            {QStringLiteral("title"), title},
            {QStringLiteral("subtitle"), subtitle},
            {QStringLiteral("detail"), detail},
            {QStringLiteral("badge"), badge},
            {QStringLiteral("badgeTone"), badgeTone}};
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
        setState(QStringLiteral("empty"));
        return;
    }

    Infrastructure::QtSystemClock clock;
    Infrastructure::QtUidGenerator uids;
    Infrastructure::SqlGoalRepository goalsRepo(database, clock);
    Infrastructure::SqlContentMapRepository mapsRepo(database, clock);
    Infrastructure::SqlRouteRepository routesRepo(database, clock);
    Application::GoalUseCases goalUseCases(goalsRepo, uids, clock);
    Application::ContentMapUseCases mapUseCases(mapsRepo, goalsRepo, uids, clock);
    Application::RouteUseCases routeUseCases(routesRepo, goalsRepo, uids, clock);

    // 目标列表 + 每个目标的覆盖率
    QVariantList goalRows;
    for (const auto &goal : goalsRepo.findByUser(*userUid)) {
        double coverage = -1.0;
        QString caliber;
        const auto summary = mapUseCases.coverageByGoal(goal.uid, *userUid);
        if (summary) {
            coverage = summary.value().coverage;
            caliber = QString::fromStdString(summary.value().caliberText);
        }
        goalRows.append(row(QString::fromStdString(goal.uid.value()),
                            QString::fromStdString(goal.title),
                            QString::fromStdString(goal.description),
                            coverage >= 0.0 ? percentText(coverage) : QStringLiteral("—"),
                            coverage >= 0.0 ? QStringLiteral("info") : QStringLiteral("neutral"),
                            caliber));
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
    m_selectedGoalTitle.clear();
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

    if (const auto goal = goalsRepo.findByUid(*goalUid))
        m_selectedGoalTitle = QString::fromStdString(goal->title);

    // 内容地图 + 覆盖率（未确认地图不进入进度口径）
    QVariantList mapRows;
    const auto coverage = mapUseCases.coverageByGoal(*goalUid, *userUid);
    if (coverage)
        m_coverageText = percentText(coverage.value().coverage) + QStringLiteral(" · ")
                         + QString::fromStdString(coverage.value().caliberText);
    for (const auto &map : mapsRepo.mapsOfGoal(*goalUid)) {
        QString badge = QString::fromStdString(Domain::toString(map.status));
        QString tone = map.status == Domain::ContentMapStatus::Confirmed
                           ? QStringLiteral("success")
                           : QStringLiteral("neutral");
        mapRows.append(row(QString::fromStdString(map.uid.value()),
                           QString::fromStdString(map.title),
                           QString::fromStdString(map.sourceType), badge, tone));
    }
    m_mapsModel.replace(mapRows);

    // 路线（候选 → 用户确认；确认后为当前路线）
    QVariantList routeRows;
    for (const auto &route : routesRepo.findByGoal(*goalUid)) {
        const auto versions = routesRepo.versionsOf(route.uid);
        QString badge = QString::fromStdString(Domain::toString(route.status));
        QString tone = route.status == Domain::RouteStatus::Proposed
                           ? QStringLiteral("warning")
                           : QStringLiteral("neutral");
        routeRows.append(row(QString::fromStdString(route.uid.value()),
                             QStringLiteral("路线（%1 个版本）").arg(versions.size()),
                             QString::fromStdString(Domain::toString(route.status)), badge,
                             tone));
    }
    m_routesModel.replace(routeRows);
    emit selectionChanged();
}

void GoalRouteViewModel::createGoal(const QString &title)
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
    input.goalType = "course";
    input.desiredLevelJson = std::string("{}");
    input.userDefinedLevel = false;
    const auto created = useCases.createGoal(input);
    if (!created) {
        setState(QStringLiteral("error"));
        setError(friendlyError(created.error().message, created.error().detail));
        return;
    }
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
    Application::RouteUseCases useCases(routesRepo, goalsRepo, uids, clock);
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


void GoalRouteViewModel::aiGenerateRoute()
{
    const auto goalUid = Domain::Uid::parse(m_selectedGoalUid.toStdString());
    const auto userUid = Presentation::activeUserUid(DatabaseManager::instance().database());
    if (!goalUid || !userUid) {
        setState(QStringLiteral("conflict"));
        setError(QStringLiteral("请先选择目标"));
        return;
    }
    m_aiState = QStringLiteral("ai_waiting");
    emit aiStateChanged();

    // 后台线程执行：每线程独立数据库连接；结果回主线程刷新
    const QString dbPath = DatabaseManager::instance().databasePath();
    QtConcurrent::run([this, goalUid = *goalUid, userUid = *userUid, dbPath]() {
        QString error;
        Infrastructure::DatabaseConnectionFactory factory(dbPath);
        QString openError;
        QSqlDatabase workerDb = factory.openForCurrentThread(QStringLiteral("ai_route"),
                                                             &openError);
        if (!workerDb.isOpen())
            return QStringLiteral("数据库连接失败：") + openError;
        const auto pipeline = Presentation::buildPlanningPipeline(workerDb);
        const auto result = pipeline->useCases->generateRouteProposal(userUid, goalUid);
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

} // namespace PersonOS
