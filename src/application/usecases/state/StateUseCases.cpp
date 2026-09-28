#include "application/usecases/state/StateUseCases.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "application/audit/Audit.h"

namespace PersonOS::Application {

StateUseCases::StateUseCases(StateRepository &repo, UuidPort &uids, const Domain::Clock &clock)
    : m_repo(repo), m_uids(uids), m_clock(clock)
{}

Result<Domain::StateEvent, ApplicationError> StateUseCases::observe(const ObserveInput &input)
{
    if (input.idempotencyKey.empty())
        return Result<Domain::StateEvent, ApplicationError>::failure(
            {ErrorCode::Validation, "idempotency key required", {}, false});
    if (m_repo.existsIdempotencyKey(input.idempotencyKey))
        return Result<Domain::StateEvent, ApplicationError>::failure(
            {ErrorCode::Conflict, "duplicate idempotency key", {}, false});

    const auto definition = m_repo.findDefinitionByCode(input.definitionCode);
    if (!definition)
        return Result<Domain::StateEvent, ApplicationError>::failure(
            {ErrorCode::NotFound, "state definition not found", {}, false});

    const std::string now = m_clock.utcIso();
    const int ttl = input.ttlMin.value_or(definition->defaultTtlMin);
    if (ttl <= 0)
        return Result<Domain::StateEvent, ApplicationError>::failure(
            {ErrorCode::Validation, "ttl must be positive", {}, false});

    Domain::StateEvent event;
    event.uid = m_uids.next();
    event.userId = input.userId;
    event.definitionId = definition->uid;
    event.valueJson = input.valueJson;
    event.source = input.source;
    event.confidence = input.confidence;
    event.observedAt = now;
    event.validUntil = m_clock.utcIsoPlusMinutes(ttl);   // 时效 = 观察时刻 + TTL
    event.consentScope = input.consentScope;
    event.recordedAt = now;
    event.idempotencyKey = input.idempotencyKey;

    const auto saved = m_repo.appendEvent(event);
    if (!saved.ok)
        return Result<Domain::StateEvent, ApplicationError>::failure(saved.error);
    Audit::record({"user", {}, "state.observed", "state_event", event.uid.value(),
                   "{\"definition\":\"" + event.definitionId.value() + "\"}"});
    return Result<Domain::StateEvent, ApplicationError>::success(std::move(event));
}

Result<std::optional<Domain::StateEvent>, ApplicationError> StateUseCases::current(
    const Domain::Uid &userId, const std::string &definitionCode)
{
    const auto event =
        m_repo.latestValid(userId, definitionCode, m_clock.utcIso());
    return Result<std::optional<Domain::StateEvent>, ApplicationError>::success(event);
}


Result<void, ApplicationError> StateUseCases::setChatExtractionEnabled(
    const Domain::Uid &userId, bool enabled)
{
    const auto saved = m_repo.setPreference(userId, "chat_state_extraction",
                                            enabled ? "{\"enabled\":true}"
                                                    : "{\"enabled\":false}");
    if (!saved.ok)
        return Result<void, ApplicationError>::failure(saved.error);
    Audit::record({"user", {}, "state.chat_extraction_setting", "preference",
                   userId.value(), "{\"enabled\":" + std::string(enabled ? "true" : "false")
                       + "}"});
    return Result<void, ApplicationError>::success();
}

Result<bool, ApplicationError> StateUseCases::chatExtractionEnabled(const Domain::Uid &userId)
{
    const auto value = m_repo.preference(userId, "chat_state_extraction");
    if (!value)
        return Result<bool, ApplicationError>::success(false);   // 默认关闭
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromStdString(*value));
    return Result<bool, ApplicationError>::success(
        document.object().value(QStringLiteral("enabled")).toBool(false));
}

Result<Domain::StateEvent, ApplicationError> StateUseCases::captureFromConversation(
    const Domain::Uid &userId, const ConversationInput &input)
{
    // 授权门禁：未开启一律拒绝（敏感内容最小收集的第一道闸）
    const auto enabled = chatExtractionEnabled(userId);
    if (!enabled || !enabled.value())
        return Result<Domain::StateEvent, ApplicationError>::failure(
            {ErrorCode::Permission, "chat state extraction is disabled by user", {}, false});

    if (input.confidence >= 1.0)
        return Result<Domain::StateEvent, ApplicationError>::failure(
            {ErrorCode::Validation, "ai inference confidence must be < 1.0", {}, false});
    if (input.conversationRef.empty())
        return Result<Domain::StateEvent, ApplicationError>::failure(
            {ErrorCode::Validation, "conversation context reference required", {}, false});

    const auto definition = m_repo.findDefinitionByCode(input.definitionCode);
    if (!definition)
        return Result<Domain::StateEvent, ApplicationError>::failure(
            {ErrorCode::NotFound, "state definition missing: " + input.definitionCode, {},
             false});

    // 敏感定义：逐项用户确认后才写入（最小收集第二道闸）
    const bool sensitive = definition->sensitivity == "sensitive";
    if (sensitive && !input.confirmedByUser)
        return Result<Domain::StateEvent, ApplicationError>::failure(
            {ErrorCode::Permission, "sensitive state requires explicit user confirmation",
             {}, false});

    Domain::StateEvent event;
    event.uid = m_uids.next();
    event.userId = userId;
    event.definitionId = definition->uid;
    event.source = Domain::StateSource::Conversation;
    event.confidence = input.confidence;
    event.observedAt = m_clock.utcIso();
    event.validUntil = m_clock.utcIsoPlusMinutes(definition->defaultTtlMin);
    event.consentScope = sensitive ? "explicit_confirmation" : "chat_extraction";
    event.conversationRef =
        input.conversationRef.size() > 500 ? input.conversationRef.substr(0, 500)
                                           : input.conversationRef;   // 最小收集
    event.idempotencyKey = "chat:" + m_uids.next().value();
    event.valueJson = input.valueJson;

    const auto saved = m_repo.appendEvent(event);
    if (!saved.ok)
        return Result<Domain::StateEvent, ApplicationError>::failure(saved.error);
    Audit::record({"ai", {}, "state.captured_from_conversation", "state_event",
                   event.uid.value(), "{\"definition\":\"" + input.definitionCode
                                           + "\",\"sensitive\":"
                                           + std::string(sensitive ? "true" : "false") + "}"});
    return Result<Domain::StateEvent, ApplicationError>::success(std::move(event));
}

} // namespace PersonOS::Application
