#include "application/usecases/domain/DomainRegistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "application/foundation/JsonSchemaValidator.h"

namespace PersonOS::Application {

namespace {

// 清单结构元模式（JSON Schema 2020-12 子集；与 domain-configuration-design §2 对齐）
const char *kManifestSchema = R"JSON({
  "type": "object",
  "required": ["schema_version", "domain_id", "manifest_version", "name",
               "description", "research_refs", "roles", "workflow_refs",
               "capabilities", "state_bindings", "knowledge_selectors",
               "decision_points", "parameter_specs", "outcome_specs",
               "policy_refs", "provenance"],
  "additionalProperties": false,
  "properties": {
    "schema_version": {"type": "string", "minLength": 1},
    "domain_id": {"type": "string", "minLength": 1},
    "manifest_version": {"type": "string", "minLength": 1},
    "name": {"type": "string"},
    "description": {"type": "string"},
    "research_refs": {"type": "array", "items": {"type": "string"}},
    "roles": {"type": "array", "items": {"type": "string"}},
    "workflow_refs": {"type": "array", "items": {"type": "string"}},
    "capabilities": {"type": "array", "items": {"type": "string"}},
    "state_bindings": {"type": "array"},
    "knowledge_selectors": {"type": "object"},
    "decision_points": {"type": "array"},
    "parameter_specs": {"type": "array"},
    "outcome_specs": {"type": "array"},
    "policy_refs": {"type": "array", "items": {"type": "string"}},
    "ui_bindings": {"type": "object"},
    "provenance": {"type": "object"}
  }
})JSON";

bool knownWorkflow(const std::string &value)
{
    for (const char *wf : {"WF-1", "WF-2", "WF-3", "WF-4", "WF-5", "WF-6", "WF-7"})
        if (value == wf)
            return true;
    return false;
}

bool knownCapability(const std::string &value)
{
    for (const char *capability : kRegisteredCapabilities)
        if (value == capability)
            return true;
    return false;
}

} // namespace

std::optional<DomainParameterSpec> DomainConfig::parameter(const std::string &id) const
{
    for (const auto &spec : parameters)
        if (spec.id == id)
            return spec;
    return std::nullopt;
}

DomainRegistry::DomainRegistry(DomainManifestRepository &repo) : m_repo(repo) {}

Result<DomainConfig, ApplicationError> DomainRegistry::load(const std::string &domainCode)
{
    if (domainCode.empty())
        return Result<DomainConfig, ApplicationError>::failure(
            {ErrorCode::Validation, "domain code required", {}, false});
    const auto manifestUid = m_repo.findManifestByCode(domainCode);
    if (!manifestUid)
        return Result<DomainConfig, ApplicationError>::failure(
            {ErrorCode::NotFound, "domain manifest not found: " + domainCode, {}, false});
    const auto record = m_repo.latestActiveVersion(*manifestUid);
    if (!record)
        return Result<DomainConfig, ApplicationError>::failure(
            {ErrorCode::NotFound, "no active manifest version for: " + domainCode, {}, false});
    return validateAndBuild(domainCode, *record);
}

Result<DomainConfig, ApplicationError> DomainRegistry::loadVersion(
    const std::string &versionUid)
{
    const auto record = m_repo.versionByUid(versionUid);
    if (!record)
        return Result<DomainConfig, ApplicationError>::failure(
            {ErrorCode::NotFound, "manifest version not found", {}, false});

    // 从清单内容反查 domain_id（版本记录不携带 domain_code）
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromStdString(record->manifestJson));
    const QString domainCode =
        document.object().value(QStringLiteral("domain_id")).toString();
    if (domainCode.isEmpty())
        return Result<DomainConfig, ApplicationError>::failure(
            {ErrorCode::Validation, "manifest missing domain_id", {}, false});
    return validateAndBuild(domainCode.toStdString(), *record);
}

Result<DomainConfig, ApplicationError> DomainRegistry::validateAndBuild(
    const std::string &domainCode, const ManifestVersionRecord &record)
{
    // 1) JSON Schema 结构验证（本地解析，不访问网络）
    if (const auto error =
            JsonSchemaValidator::validate(kManifestSchema, record.manifestJson))
        return Result<DomainConfig, ApplicationError>::failure(
            {ErrorCode::Validation, "manifest schema invalid: " + *error, {}, false});

    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromStdString(record.manifestJson));
    const QJsonObject manifest = document.object();
    if (manifest.value(QStringLiteral("domain_id")).toString().toStdString() != domainCode)
        return Result<DomainConfig, ApplicationError>::failure(
            {ErrorCode::Validation, "manifest domain_id does not match requested domain", {},
             false});

    DomainConfig config;
    config.domainCode = domainCode;
    config.manifestVersionUid = record.versionUid;
    config.manifestVersion = manifest.value(QStringLiteral("manifest_version"))
                                 .toString()
                                 .toStdString();
    config.schemaVersion = record.schemaVersion;

    // 2) 引用与能力检查：workflow 必须引用 WF-1..7；能力必须已注册实现
    for (const auto &workflow : manifest.value(QStringLiteral("workflow_refs")).toArray())
        config.workflows.push_back(workflow.toString().toStdString());
    if (config.workflows.empty())
        return Result<DomainConfig, ApplicationError>::failure(
            {ErrorCode::Validation, "workflow_refs must not be empty", {}, false});
    for (const auto &workflow : config.workflows)
        if (!knownWorkflow(workflow))
            return Result<DomainConfig, ApplicationError>::failure(
                {ErrorCode::Validation, "unknown workflow reference: " + workflow, {}, false});

    for (const auto &capability : manifest.value(QStringLiteral("capabilities")).toArray())
        config.capabilities.push_back(capability.toString().toStdString());
    for (const auto &capability : config.capabilities)
        if (!knownCapability(capability))
            return Result<DomainConfig, ApplicationError>::failure(
                {ErrorCode::Validation, "capability not implemented: " + capability, {}, false});

    // 3) 参数规格：解析并检查范围合法性；值可为区间或 unknown
    for (const auto &value : manifest.value(QStringLiteral("parameter_specs")).toArray()) {
        const QJsonObject object = value.toObject();
        DomainParameterSpec spec;
        spec.id = object.value(QStringLiteral("parameter_id")).toString().toStdString();
        spec.valueType = object.value(QStringLiteral("value_type")).toString().toStdString();
        spec.unit = object.value(QStringLiteral("unit")).toString().toStdString();
        spec.missingPolicy =
            object.value(QStringLiteral("missing_policy")).toString().toStdString();
        spec.userAdjustable =
            object.value(QStringLiteral("user_adjustable")).toBool();
        if (object.contains(QStringLiteral("default_value")))
            spec.defaultValue = object.value(QStringLiteral("default_value")).toDouble();
        if (object.contains(QStringLiteral("min")))
            spec.min = object.value(QStringLiteral("min")).toDouble();
        if (object.contains(QStringLiteral("max")))
            spec.max = object.value(QStringLiteral("max")).toDouble();
        if (spec.id.empty() || spec.valueType.empty())
            return Result<DomainConfig, ApplicationError>::failure(
                {ErrorCode::Validation, "parameter spec missing id/value_type", {}, false});
        if (spec.min && spec.max && *spec.min > *spec.max)
            return Result<DomainConfig, ApplicationError>::failure(
                {ErrorCode::Validation, "parameter range invalid: " + spec.id, {}, false});
        if (spec.missingPolicy.empty())
            spec.missingPolicy = "use_default";
        config.parameters.push_back(std::move(spec));
    }

    // 4) 决策点：每个都必须携带 policy_ref（规则引用完整）
    for (const auto &value : manifest.value(QStringLiteral("decision_points")).toArray()) {
        const QJsonObject object = value.toObject();
        const QString code = object.value(QStringLiteral("decision_code")).toString();
        const QString policy = object.value(QStringLiteral("policy_ref")).toString();
        if (code.isEmpty())
            return Result<DomainConfig, ApplicationError>::failure(
                {ErrorCode::Validation, "decision point missing decision_code", {}, false});
        if (policy.isEmpty())
            return Result<DomainConfig, ApplicationError>::failure(
                {ErrorCode::Validation, "decision point missing policy_ref: "
                                            + code.toStdString(),
                 {}, false});
        config.decisionPoints.push_back(code.toStdString());
        config.policyRefs.push_back(policy.toStdString());
    }
    for (const auto &policy : manifest.value(QStringLiteral("policy_refs")).toArray())
        config.policyRefs.push_back(policy.toString().toStdString());

    // 5) 知识选择器（标签召回；允许有理由的跨领域召回）
    const QJsonObject selectors =
        manifest.value(QStringLiteral("knowledge_selectors")).toObject();
    for (const auto &tag : selectors.value(QStringLiteral("preferred_tags")).toArray())
        config.preferredTags.push_back(tag.toString().toStdString());
    config.crossDomainAllowed =
        selectors.value(QStringLiteral("cross_domain_allowed")).toBool();

    // 6) provenance 依据（可追溯）
    config.provenanceBasis = manifest.value(QStringLiteral("provenance"))
                                 .toObject()
                                 .value(QStringLiteral("basis"))
                                 .toString()
                                 .toStdString();

    return Result<DomainConfig, ApplicationError>::success(std::move(config));
}

} // namespace PersonOS::Application
