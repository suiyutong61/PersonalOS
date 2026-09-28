#include "infrastructure/knowledge/LearningManifestSeed.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

namespace {

QJsonObject manifestJson()
{
    QJsonObject manifest;
    manifest.insert(QStringLiteral("schema_version"), QStringLiteral("1.1"));
    manifest.insert(QStringLiteral("domain_id"), QStringLiteral("learning"));
    manifest.insert(QStringLiteral("manifest_version"), QStringLiteral("1.0"));
    manifest.insert(QStringLiteral("name"), QStringLiteral("学习"));
    manifest.insert(QStringLiteral("description"),
                    QStringLiteral("v1 首发领域包：学习目标、路线、MEL、验收与复盘"));

    QJsonArray researchRefs;
    researchRefs.append(QStringLiteral("04-learning-science"));
    researchRefs.append(QStringLiteral("01-goal-self-regulation"));
    researchRefs.append(QStringLiteral("02-plan-feasibility"));
    researchRefs.append(QStringLiteral("03-time-management"));
    researchRefs.append(QStringLiteral("13-procrastination"));
    manifest.insert(QStringLiteral("research_refs"), researchRefs);

    QJsonArray roles;
    roles.append(QStringLiteral("domain_pack"));
    manifest.insert(QStringLiteral("roles"), roles);

    QJsonArray workflows;
    workflows.append(QStringLiteral("WF-1"));
    workflows.append(QStringLiteral("WF-2"));
    workflows.append(QStringLiteral("WF-3"));
    workflows.append(QStringLiteral("WF-5"));
    manifest.insert(QStringLiteral("workflow_refs"), workflows);

    QJsonArray capabilities;
    capabilities.append(QStringLiteral("goal_route"));
    capabilities.append(QStringLiteral("mel_loop"));
    capabilities.append(QStringLiteral("state_record"));
    capabilities.append(QStringLiteral("assessment"));
    capabilities.append(QStringLiteral("knowledge_retrieval"));
    capabilities.append(QStringLiteral("advisory"));
    manifest.insert(QStringLiteral("capabilities"), capabilities);

    QJsonArray stateBindings; // 状态定义引用在 IMP-004 状态库实现后固化
    manifest.insert(QStringLiteral("state_bindings"), stateBindings);

    QJsonObject knowledgeSelectors;
    QJsonArray knowledgeTags;
    knowledgeTags.append(QStringLiteral("learning"));
    knowledgeTags.append(QStringLiteral("study"));
    knowledgeTags.append(QStringLiteral("memory"));
    knowledgeTags.append(QStringLiteral("planning"));
    knowledgeSelectors.insert(QStringLiteral("preferred_tags"), knowledgeTags);
    knowledgeSelectors.insert(QStringLiteral("cross_domain_allowed"), true);
    manifest.insert(QStringLiteral("knowledge_selectors"), knowledgeSelectors);

    QJsonArray decisionPoints;
    QJsonObject createPlan;
    createPlan.insert(QStringLiteral("decision_code"), QStringLiteral("create_plan"));
    createPlan.insert(QStringLiteral("sequence_no"), 1);
    createPlan.insert(QStringLiteral("policy_ref"), QStringLiteral("pol-plan-confirm"));
    decisionPoints.append(createPlan);
    QJsonObject review;
    review.insert(QStringLiteral("decision_code"), QStringLiteral("review"));
    review.insert(QStringLiteral("sequence_no"), 2);
    review.insert(QStringLiteral("policy_ref"), QStringLiteral("pol-review"));
    decisionPoints.append(review);
    QJsonObject userHelp;
    userHelp.insert(QStringLiteral("decision_code"), QStringLiteral("user_help"));
    userHelp.insert(QStringLiteral("sequence_no"), 3);
    userHelp.insert(QStringLiteral("policy_ref"), QStringLiteral("pol-advice"));
    decisionPoints.append(userHelp);
    manifest.insert(QStringLiteral("decision_points"), decisionPoints);

    // MEL 周期作为参数规格：默认 3 天、允许 1~14 天、用户可调 —— 公共核心不写死。
    QJsonArray parameterSpecs;
    QJsonObject melPeriod;
    melPeriod.insert(QStringLiteral("parameter_id"), QStringLiteral("mel_period_days"));
    melPeriod.insert(QStringLiteral("value_type"), QStringLiteral("integer"));
    melPeriod.insert(QStringLiteral("unit"), QStringLiteral("days"));
    melPeriod.insert(QStringLiteral("default_value"), 3);
    melPeriod.insert(QStringLiteral("min"), 1);
    melPeriod.insert(QStringLiteral("max"), 14);
    melPeriod.insert(QStringLiteral("user_adjustable"), true);
    melPeriod.insert(QStringLiteral("missing_policy"), QStringLiteral("use_default"));
    parameterSpecs.append(melPeriod);
    // R2 子问题检测 / R5 建议频控参数（阈值由配置决定并携带依据，不在代码写死）
    const struct
    {
        const char *id;
        const char *type;
        const char *unit;
        double defaultValue;
        double min;
        double max;
        bool adjustable;
    } extraParams[] = {
        {"detection_progress_lag_tolerance", "number", "ratio", 0.3, 0.0, 0.6, true},
        {"detection_inactivity_minutes", "number", "minutes", 4320, 60, 20160, true},
        {"detection_effort_ratio_threshold", "number", "ratio", 1.5, 1.0, 5.0, true},
        {"detection_weak_assessment_window_minutes", "number", "minutes", 10080, 60, 43200,
         true},
        {"advice_quiet_window_minutes", "number", "minutes", 1440, 60, 10080, true},
        {"advice_max_active", "number", "count", 5, 1, 20, true},
    };
    for (const auto &param : extraParams) {
        QJsonObject object;
        object.insert(QStringLiteral("parameter_id"), QString::fromLatin1(param.id));
        object.insert(QStringLiteral("value_type"), QString::fromLatin1(param.type));
        object.insert(QStringLiteral("unit"), QString::fromLatin1(param.unit));
        object.insert(QStringLiteral("default_value"), param.defaultValue);
        object.insert(QStringLiteral("min"), param.min);
        object.insert(QStringLiteral("max"), param.max);
        object.insert(QStringLiteral("user_adjustable"), param.adjustable);
        object.insert(QStringLiteral("missing_policy"), QStringLiteral("use_default"));
        parameterSpecs.append(object);
    }
    manifest.insert(QStringLiteral("parameter_specs"), parameterSpecs);

    manifest.insert(QStringLiteral("outcome_specs"), QJsonArray());
    manifest.insert(QStringLiteral("policy_refs"), QJsonArray());

    QJsonObject uiBindings;
    uiBindings.insert(QStringLiteral("progress_view"), QStringLiteral("content_map"));
    uiBindings.insert(QStringLiteral("mel_view"), QStringLiteral("mel_loop"));
    uiBindings.insert(QStringLiteral("review_view"), QStringLiteral("three_level"));
    manifest.insert(QStringLiteral("ui_bindings"), uiBindings);

    QJsonObject provenance;
    provenance.insert(QStringLiteral("author"), QStringLiteral("Personal OS 项目"));
    provenance.insert(QStringLiteral("basis"),
                      QStringLiteral("requirements/scope/architecture 基线 v1.1"));
    provenance.insert(QStringLiteral("change_reason"), QStringLiteral("v1 首发领域包"));
    manifest.insert(QStringLiteral("provenance"), provenance);

    return manifest;
}

} // namespace

LearningManifestSeed::LearningManifestSeed(QSqlDatabase database, const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::Result<void, Application::ApplicationError> LearningManifestSeed::ensureSeeded()
{
    QSqlQuery exists(m_database);
    exists.prepare(QStringLiteral(
        "SELECT id FROM domain_manifests_v3 WHERE domain_code='learning'"));
    if (!exists.exec())
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "manifest query failed",
             exists.lastError().text().toStdString(), false});
    if (exists.next())
        return Application::Result<void, Application::ApplicationError>::success(); // 幂等

    const std::string now = formatUtcIso(m_clock.now());
    const std::string manifestUid =
        QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const std::string versionUid =
        QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const QJsonObject manifest = manifestJson();
    const QByteArray manifestBytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);

    QSqlQuery insertManifest(m_database);
    insertManifest.prepare(QStringLiteral(
        "INSERT INTO domain_manifests_v3(uid, domain_code, name, current_version_uid, status, "
        "created_at, updated_at, revision) VALUES(?,?,?,?,?,?,?,1)"));
    insertManifest.addBindValue(QString::fromStdString(manifestUid));
    insertManifest.addBindValue(QStringLiteral("learning"));
    insertManifest.addBindValue(QStringLiteral("学习"));
    insertManifest.addBindValue(QString::fromStdString(versionUid));
    insertManifest.addBindValue(QStringLiteral("active"));
    insertManifest.addBindValue(QString::fromStdString(now));
    insertManifest.addBindValue(QString::fromStdString(now));
    if (!insertManifest.exec())
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "manifest insert failed",
             insertManifest.lastError().text().toStdString(), false});
    const qint64 manifestPk = insertManifest.lastInsertId().toLongLong();

    QSqlQuery insertVersion(m_database);
    insertVersion.prepare(QStringLiteral(
        "INSERT INTO domain_manifest_versions_v3(uid, manifest_id, version_no, status, "
        "schema_version, manifest_json, content_hash, valid_from, created_at, created_by) "
        "VALUES(?,?,?,?,?,?,?,?,?,?)"));
    insertVersion.addBindValue(QString::fromStdString(versionUid));
    insertVersion.addBindValue(manifestPk);
    insertVersion.addBindValue(1);
    insertVersion.addBindValue(QStringLiteral("active"));
    insertVersion.addBindValue(QStringLiteral("1.1"));
    insertVersion.addBindValue(QString::fromStdString(manifestBytes.toStdString()));
    insertVersion.addBindValue(QString::fromLatin1(
        QCryptographicHash::hash(manifestBytes, QCryptographicHash::Sha256).toHex()));
    insertVersion.addBindValue(QString::fromStdString(now));
    insertVersion.addBindValue(QString::fromStdString(now));
    insertVersion.addBindValue(QStringLiteral("system"));
    if (!insertVersion.exec())
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "manifest version insert failed",
             insertVersion.lastError().text().toStdString(), false});
    const qint64 versionPk = insertVersion.lastInsertId().toLongLong();

    // 能力与决策点（与清单 JSON 中声明的 capabilities/decision_points 一致）
    const QStringList capabilities{QStringLiteral("goal_route"), QStringLiteral("mel_loop"),
                                   QStringLiteral("state_record"),
                                   QStringLiteral("assessment"),
                                   QStringLiteral("knowledge_retrieval"),
                                   QStringLiteral("advisory")};
    for (const auto &capability : capabilities) {
        QSqlQuery insertCapability(m_database);
        insertCapability.prepare(QStringLiteral(
            "INSERT INTO domain_capabilities_v3(manifest_version_id, capability_code, required, "
            "config_json) VALUES(?,?,?,?)"));
        insertCapability.addBindValue(versionPk);
        insertCapability.addBindValue(capability);
        insertCapability.addBindValue(1);
        insertCapability.addBindValue(QStringLiteral("{}"));
        if (!insertCapability.exec())
            return Application::Result<void, Application::ApplicationError>::failure(
                {Application::ErrorCode::Storage, "capability insert failed",
                 insertCapability.lastError().text().toStdString(), false});
    }

    const struct
    {
        const char *code;
        int sequence;
        const char *policy;
    } decisionPoints[] = {{"create_plan", 1, "pol-plan-confirm"},
                          {"review", 2, "pol-review"},
                          {"user_help", 3, "pol-advice"}};
    for (const auto &point : decisionPoints) {
        QSqlQuery insertPoint(m_database);
        insertPoint.prepare(QStringLiteral(
            "INSERT INTO domain_decision_points_v3(manifest_version_id, decision_code, "
            "sequence_no, input_schema, output_schema, policy_ref) VALUES(?,?,?,?,?,?)"));
        insertPoint.addBindValue(versionPk);
        insertPoint.addBindValue(QString::fromLatin1(point.code));
        insertPoint.addBindValue(point.sequence);
        insertPoint.addBindValue(QStringLiteral("{}"));
        insertPoint.addBindValue(QStringLiteral("{}"));
        insertPoint.addBindValue(QString::fromLatin1(point.policy));
        if (!insertPoint.exec())
            return Application::Result<void, Application::ApplicationError>::failure(
                {Application::ErrorCode::Storage, "decision point insert failed",
                 insertPoint.lastError().text().toStdString(), false});
    }

    return Application::Result<void, Application::ApplicationError>::success();
}

} // namespace PersonOS::Infrastructure
