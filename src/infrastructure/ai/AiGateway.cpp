#include "infrastructure/ai/AiGateway.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace PersonOS::Infrastructure {

namespace {
std::string sha256(const std::string &text)
{
    return QCryptographicHash::hash(QByteArray::fromStdString(text),
                                    QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}

// 网关元数据封装在 request_json 内（ai_jobs_v6 无独立列；迁移已冻结不新增）：
// {"_config_uid":..., "_contract_type":..., "_contract_version":...,
//  "_context_items":[{...}], "payload":{...}}
QString wrapRequest(const Application::AiGatewaySubmit &input)
{
    QJsonObject wrapped;
    wrapped.insert(QStringLiteral("_config_uid"),
                   QString::fromStdString(input.providerConfigUid.value()));
    wrapped.insert(QStringLiteral("_contract_type"), QString::fromStdString(input.contractType));
    wrapped.insert(QStringLiteral("_contract_version"),
                   QString::fromStdString(input.contractVersion));
    QJsonArray items;
    for (const auto &item : input.contextItems) {
        QJsonObject object;
        object.insert(QStringLiteral("context_type"), QString::fromStdString(item.contextType));
        object.insert(QStringLiteral("context_uid"), QString::fromStdString(item.contextUid));
        object.insert(QStringLiteral("version_uid"), QString::fromStdString(item.versionUid));
        object.insert(QStringLiteral("rank"), item.rank);
        object.insert(QStringLiteral("reason"), QString::fromStdString(item.reason));
        items.append(object);
    }
    wrapped.insert(QStringLiteral("_context_items"), items);
    wrapped.insert(QStringLiteral("payload"),
                   QJsonDocument::fromJson(QByteArray::fromStdString(input.requestJson)).object());
    return QString::fromUtf8(QJsonDocument(wrapped).toJson(QJsonDocument::Compact));
}

struct UnwrappedRequest
{
    Domain::Uid configUid;
    std::string contractType;
    std::string contractVersion;
    std::vector<Application::AiContextItem> contextItems;
    std::string payloadJson;
};

std::optional<UnwrappedRequest> unwrapRequest(const std::string &requestJson)
{
    const QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray::fromStdString(requestJson));
    if (!doc.isObject())
        return std::nullopt;
    const QJsonObject object = doc.object();
    const auto configUid = Domain::Uid::parse(
        object.value(QStringLiteral("_config_uid")).toString().toStdString());
    if (!configUid)
        return std::nullopt;
    UnwrappedRequest request;
    request.configUid = *configUid;
    request.contractType =
        object.value(QStringLiteral("_contract_type")).toString().toStdString();
    request.contractVersion =
        object.value(QStringLiteral("_contract_version")).toString().toStdString();
    for (const auto &value :
         object.value(QStringLiteral("_context_items")).toArray()) {
        const QJsonObject item = value.toObject();
        Application::AiContextItem context;
        context.contextType = item.value(QStringLiteral("context_type")).toString().toStdString();
        context.contextUid = item.value(QStringLiteral("context_uid")).toString().toStdString();
        context.versionUid = item.value(QStringLiteral("version_uid")).toString().toStdString();
        context.rank = item.value(QStringLiteral("rank")).toInt();
        context.reason = item.value(QStringLiteral("reason")).toString().toStdString();
        request.contextItems.push_back(std::move(context));
    }
    request.payloadJson = QString::fromUtf8(
                              QJsonDocument(object.value(QStringLiteral("payload")).toObject())
                                  .toJson(QJsonDocument::Compact))
                              .toStdString();
    return request;
}
} // namespace

AiGateway::AiGateway(SqlAiRepository &repo, Application::AiProviderPort &provider,
                     Application::UuidPort &uids, const Domain::Clock &clock)
    : m_repo(repo), m_provider(provider), m_uids(uids), m_clock(clock)
{}

Application::Result<Domain::AiJob, Application::ApplicationError> AiGateway::submit(
    const Application::AiGatewaySubmit &input)
{
    if (input.idempotencyKey.empty())
        return Application::Result<Domain::AiJob, Application::ApplicationError>::failure(
            {Application::ErrorCode::Validation, "idempotency key required", {}, false});
    if (m_repo.existsJobKey(input.idempotencyKey))
        return Application::Result<Domain::AiJob, Application::ApplicationError>::failure(
            {Application::ErrorCode::Conflict, "duplicate job idempotency key", {}, false});
    if (!m_repo.findEnabledConfig(input.providerConfigUid))
        return Application::Result<Domain::AiJob, Application::ApplicationError>::failure(
            {Application::ErrorCode::NotFound, "provider config not found or disabled", {},
             false});

    Domain::AiJob job;
    job.uid = m_uids.next();
    job.jobType = input.jobType;
    job.status = Domain::AiJobStatus::Queued;
    job.requestJson = wrapRequest(input).toStdString();
    job.schemaVersion = input.contractType + "/" + input.contractVersion;
    job.idempotencyKey = input.idempotencyKey;
    job.maxAttempts = input.maxAttempts;
    const auto saved = m_repo.insertJob(job);
    if (!saved.ok)
        return Application::Result<Domain::AiJob, Application::ApplicationError>::failure(
            saved.error);
    return Application::Result<Domain::AiJob, Application::ApplicationError>::success(
        std::move(job));
}

Application::Result<Domain::AiJob, Application::ApplicationError> AiGateway::execute(
    const Domain::Uid &jobUid)
{
    const auto current = m_repo.findJob(jobUid);
    if (!current)
        return Application::Result<Domain::AiJob, Application::ApplicationError>::failure(
            {Application::ErrorCode::NotFound, "job not found", {}, false});
    if (current->status == Domain::AiJobStatus::Completed)
        return Application::Result<Domain::AiJob, Application::ApplicationError>::success(
            *current);
    if (current->status == Domain::AiJobStatus::Cancelled
        || current->status == Domain::AiJobStatus::FailedTerminal)
        return Application::Result<Domain::AiJob, Application::ApplicationError>::failure(
            {Application::ErrorCode::Conflict, "job is not executable", {}, false});

    const auto unwrapped = unwrapRequest(current->requestJson);
    if (!unwrapped)
        return Application::Result<Domain::AiJob, Application::ApplicationError>::failure(
            {Application::ErrorCode::Validation, "job request metadata missing", {}, false});
    const auto config = m_repo.findEnabledConfig(unwrapped->configUid);
    if (!config)
        return Application::Result<Domain::AiJob, Application::ApplicationError>::failure(
            {Application::ErrorCode::NotFound, "provider config not found or disabled", {},
             false});

    Application::ProviderRequest request;
    request.systemPrompt = "遵循系统协议；重要结论引用知识库并标注支持程度";
    // 契约结构注入提示词:模型必须知道要输出的 JSON 形状,
    // 否则会返回散文导致结构提取失败(真实批次 14/14 复现)。
    const std::string schema =
        Application::ContractValidator::promptSchema(unwrapped->contractType,
                                                     unwrapped->contractVersion);
    if (!schema.empty()) {
        request.systemPrompt +=
            "\n\n你的最终回复必须是一个 JSON 对象,严格符合以下结构"
            "(不要输出任何其他文字,不要用 markdown 代码块包裹):\n";
        request.systemPrompt += schema;
    }
    request.userPrompt = unwrapped->payloadJson;
    request.contractType = unwrapped->contractType;
    request.contractVersion = unwrapped->contractVersion;

    const auto response = m_provider.submit(*config, request);
    const int attempt = current->attemptCount + 1;

    Domain::AiJob updated = *current;
    updated.attemptCount = attempt;
    updated.nextAttemptAt.reset();
    updated.errorJson.reset();

    std::optional<std::string> validationError;
    if (!response.ok) {
        updated.errorJson = "{\"error\":\"" + response.errorMessage + "\"}";
    } else {
        validationError = Application::ContractValidator::validate(
            unwrapped->contractType, response.structuredJson);
        if (validationError)
            updated.errorJson = "{\"validation\":\"" + *validationError + "\"}";
    }

    if (!response.ok || validationError) {
        if (attempt >= current->maxAttempts) {
            updated.status = Domain::AiJobStatus::FailedTerminal;
        } else {
            updated.status = Domain::AiJobStatus::FailedRetryable;
            const int backoffMinutes = 1 << (attempt - 1);   // 1m/2m/4m...
            updated.nextAttemptAt = m_clock.utcIsoPlusMinutes(backoffMinutes);
        }
        const auto saved = m_repo.updateJob(updated, current->revision);
        if (!saved.ok)
            return Application::Result<Domain::AiJob, Application::ApplicationError>::failure(
                saved.error);
        updated.revision = current->revision + 1;
        return Application::Result<Domain::AiJob, Application::ApplicationError>::success(
            std::move(updated));
    }

    updated.status = Domain::AiJobStatus::Completed;
    updated.resultJson = response.structuredJson;
    const auto saved = m_repo.updateJob(updated, current->revision);
    if (!saved.ok)
        return Application::Result<Domain::AiJob, Application::ApplicationError>::failure(
            saved.error);
    updated.revision = current->revision + 1;

    const Domain::Uid callUid = m_uids.next();
    const auto callSaved = m_repo.insertCall(
        callUid, jobUid, unwrapped->configUid, sha256(request.userPrompt),
        sha256("context:" + current->requestJson), sha256(response.structuredJson), 0,
        "completed");
    if (!callSaved.ok)
        return Application::Result<Domain::AiJob, Application::ApplicationError>::failure(
            callSaved.error);
    for (const auto &item : unwrapped->contextItems) {
        const auto itemSaved =
            m_repo.insertContextItem(callUid, item.contextType, item.contextUid,
                                     item.versionUid, item.rank, item.reason);
        if (!itemSaved.ok)
            return Application::Result<Domain::AiJob, Application::ApplicationError>::failure(
                itemSaved.error);
    }
    return Application::Result<Domain::AiJob, Application::ApplicationError>::success(
        std::move(updated));
}

} // namespace PersonOS::Infrastructure
