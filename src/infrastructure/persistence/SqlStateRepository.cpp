#include "infrastructure/persistence/SqlStateRepository.h"

#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

SqlStateRepository::SqlStateRepository(QSqlDatabase database, const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::SaveResult SqlStateRepository::writeFailure(const char *operation,
                                                          const QSqlQuery &query) const
{
    return {false, false,
            {Application::ErrorCode::Storage, operation,
             query.lastError().text().toStdString(), false}};
}

std::optional<Domain::StateDefinition> SqlStateRepository::findDefinitionByCode(
    const std::string &code)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, code, category, value_type, scale_min, scale_max, unit, "
        "default_ttl_min, sensitivity, schema_json, revision "
        "FROM state_definitions_v4 WHERE code=?"));
    query.addBindValue(QString::fromStdString(code));
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
    if (!uid)
        return std::nullopt;
    Domain::StateDefinition def;
    def.uid = *uid;
    def.code = query.value("code").toString().toStdString();
    if (const auto category =
            [&](const QString &value) -> std::optional<Domain::StateCategory> {
                const auto text = value.toStdString();
                if (text == "basic") return Domain::StateCategory::Basic;
                if (text == "cognitive") return Domain::StateCategory::Cognitive;
                if (text == "emotion_pressure") return Domain::StateCategory::EmotionPressure;
                if (text == "motivation_recreation")
                    return Domain::StateCategory::MotivationRecreation;
                if (text == "body_health") return Domain::StateCategory::BodyHealth;
                if (text == "environment") return Domain::StateCategory::Environment;
                if (text == "resources_social") return Domain::StateCategory::ResourcesSocial;
                if (text == "behavior_method") return Domain::StateCategory::BehaviorMethod;
                if (text == "system_experience") return Domain::StateCategory::SystemExperience;
                if (text == "domain_specific") return Domain::StateCategory::DomainSpecific;
                return std::nullopt;
            }(query.value("category").toString()))
        def.category = *category;
    def.valueType = query.value("value_type").toString().toStdString();
    if (!query.value("scale_min").isNull())
        def.scaleMin = query.value("scale_min").toDouble();
    if (!query.value("scale_max").isNull())
        def.scaleMax = query.value("scale_max").toDouble();
    def.unit = query.value("unit").toString().toStdString();
    def.defaultTtlMin = query.value("default_ttl_min").toInt();
    def.sensitivity = query.value("sensitivity").toString().toStdString();
    def.schemaJson = query.value("schema_json").toString().toStdString();
    def.revision = query.value("revision").toInt();
    return def;
}

Application::SaveResult SqlStateRepository::insertDefinition(
    const Domain::StateDefinition &definition)
{
    if (!definition.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "definition invalid", {}, false}};
    const std::string now = formatUtcIso(m_clock.now());
    const std::string schema =
        definition.schemaJson.empty() ? std::string("{}") : definition.schemaJson;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO state_definitions_v4(uid, code, category, value_type, scale_min, "
        "scale_max, unit, default_ttl_min, sensitivity, schema_json, created_at, "
        "updated_at, revision) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(definition.uid.value()));
    query.addBindValue(QString::fromStdString(definition.code));
    query.addBindValue(QString::fromStdString(Domain::toString(definition.category)));
    query.addBindValue(QString::fromStdString(definition.valueType));
    query.addBindValue(definition.scaleMin ? QVariant(*definition.scaleMin) : QVariant());
    query.addBindValue(definition.scaleMax ? QVariant(*definition.scaleMax) : QVariant());
    query.addBindValue(QString::fromStdString(definition.unit));
    query.addBindValue(definition.defaultTtlMin);
    query.addBindValue(QString::fromStdString(definition.sensitivity));
    query.addBindValue(QString::fromStdString(schema));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("state definition insert failed", query);
    return {true, false, {}};
}

std::vector<Domain::StateDefinition> SqlStateRepository::allDefinitions()
{
    std::vector<Domain::StateDefinition> out;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT code FROM state_definitions_v4 ORDER BY code")))
        return out;
    while (query.next())
        if (const auto def = findDefinitionByCode(query.value(0).toString().toStdString()))
            out.push_back(*def);
    return out;
}

Application::SaveResult SqlStateRepository::appendEvent(const Domain::StateEvent &event)
{
    if (!event.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "state event invalid", {}, false}};
    // 外键解析
    QSqlQuery userQuery(m_database);
    userQuery.prepare(QStringLiteral("SELECT id FROM user_profiles_v3 WHERE uid=?"));
    userQuery.addBindValue(QString::fromStdString(event.userId.value()));
    if (!userQuery.exec() || !userQuery.next())
        return {false, false,
                {Application::ErrorCode::NotFound, "user not found", {}, false}};
    QSqlQuery defQuery(m_database);
    defQuery.prepare(QStringLiteral("SELECT id FROM state_definitions_v4 WHERE uid=?"));
    defQuery.addBindValue(QString::fromStdString(event.definitionId.value()));
    if (!defQuery.exec() || !defQuery.next())
        return {false, false,
                {Application::ErrorCode::NotFound, "definition not found", {}, false}};

    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO state_events_v4(uid, user_id, definition_id, value_json, source, "
        "confidence, observed_at, valid_until, consent_scope, conversation_ref, "
        "supersedes_uid, recorded_at, idempotency_key) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(
        QString::fromStdString(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()));
    query.addBindValue(userQuery.value(0).toLongLong());
    query.addBindValue(defQuery.value(0).toLongLong());
    query.addBindValue(QString::fromStdString(event.valueJson));
    query.addBindValue(QString::fromStdString(Domain::toString(event.source)));
    query.addBindValue(event.confidence);
    query.addBindValue(QString::fromStdString(event.observedAt));
    query.addBindValue(QString::fromStdString(event.validUntil));
    query.addBindValue(QString::fromStdString(event.consentScope));
    query.addBindValue(event.conversationRef
                           ? QVariant(QString::fromStdString(*event.conversationRef))
                           : QVariant());
    query.addBindValue(event.supersedesUid
                           ? QVariant(QString::fromStdString(*event.supersedesUid))
                           : QVariant());
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(event.idempotencyKey));
    if (!query.exec())
        return writeFailure("state event append failed", query);
    return {true, false, {}};
}

bool SqlStateRepository::existsIdempotencyKey(const std::string &key)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM state_events_v4 WHERE idempotency_key=? LIMIT 1"));
    query.addBindValue(QString::fromStdString(key));
    return query.exec() && query.next();
}

std::optional<Domain::StateEvent> SqlStateRepository::latestValid(
    const Domain::Uid &userId, const std::string &definitionCode, const std::string &nowIso)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, value_json, source, confidence, observed_at, valid_until, "
        "consent_scope, recorded_at, idempotency_key "
        "FROM state_events_v4 "
        "WHERE user_id=(SELECT id FROM user_profiles_v3 WHERE uid=?) "
        "AND definition_id=(SELECT id FROM state_definitions_v4 WHERE code=?) "
        "AND valid_until>=? ORDER BY observed_at DESC LIMIT 1"));
    query.addBindValue(QString::fromStdString(userId.value()));
    query.addBindValue(QString::fromStdString(definitionCode));
    query.addBindValue(QString::fromStdString(nowIso));
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
    if (!uid)
        return std::nullopt;
    Domain::StateEvent event;
    event.uid = *uid;
    event.userId = userId;
    event.valueJson = query.value("value_json").toString().toStdString();
    if (const auto source =
            Domain::stateSourceFrom(query.value("source").toString().toStdString()))
        event.source = *source;
    event.confidence = query.value("confidence").toDouble();
    event.observedAt = query.value("observed_at").toString().toStdString();
    event.validUntil = query.value("valid_until").toString().toStdString();
    event.consentScope = query.value("consent_scope").toString().toStdString();
    event.recordedAt = query.value("recorded_at").toString().toStdString();
    event.idempotencyKey = query.value("idempotency_key").toString().toStdString();
    return event;
}

std::vector<Application::RecentStateEvent> SqlStateRepository::recentEvents(
    const Domain::Uid &userId, int limit)
{
    std::vector<Application::RecentStateEvent> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT e.uid, d.code AS definition_code, e.value_json, "
        "e.observed_at, e.valid_until, e.idempotency_key "
        "FROM state_events_v4 e JOIN state_definitions_v4 d ON d.id=e.definition_id "
        "WHERE e.user_id=(SELECT id FROM user_profiles_v3 WHERE uid=?) "
        "ORDER BY e.observed_at DESC, e.id DESC LIMIT ?"));
    query.addBindValue(QString::fromStdString(userId.value()));
    query.addBindValue(limit);
    if (!query.exec())
        return out;
    while (query.next()) {
        const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
        if (!uid)
            continue;
        Application::RecentStateEvent event;
        event.uid = *uid;
        event.definitionCode = query.value("definition_code").toString().toStdString();
        event.valueJson = query.value("value_json").toString().toStdString();
        event.observedAt = query.value("observed_at").toString().toStdString();
        event.validUntil = query.value("valid_until").toString().toStdString();
        event.idempotencyKey = query.value("idempotency_key").toString().toStdString();
        out.push_back(std::move(event));
    }
    return out;
}


std::optional<std::string> SqlStateRepository::preference(const Domain::Uid &userId,
                                                          const std::string &key)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT value_json FROM user_preferences_v3 WHERE "
        "user_id=(SELECT id FROM user_profiles_v3 WHERE uid=?) AND key=?"));
    query.addBindValue(QString::fromStdString(userId.value()));
    query.addBindValue(QString::fromStdString(key));
    if (!query.exec() || !query.next())
        return std::nullopt;
    return query.value(0).toString().toStdString();
}

Application::SaveResult SqlStateRepository::setPreference(const Domain::Uid &userId,
                                                          const std::string &key,
                                                          const std::string &valueJson)
{
    if (key.empty())
        return {false, false,
                {Application::ErrorCode::Validation, "preference key required", {}, false}};
    QSqlQuery user(m_database);
    user.prepare(QStringLiteral("SELECT id FROM user_profiles_v3 WHERE uid=?"));
    user.addBindValue(QString::fromStdString(userId.value()));
    if (!user.exec() || !user.next())
        return {false, false,
                {Application::ErrorCode::NotFound, "user not found", {}, false}};

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO user_preferences_v3(uid, user_id, key, value_json, source, confidence, "
        "created_at, updated_at, revision) VALUES(?,?,?,?,'explicit',1.0,"
        "strftime('%Y-%m-%dT%H:%M:%SZ','now'),strftime('%Y-%m-%dT%H:%M:%SZ','now'),1) "
        "ON CONFLICT(user_id,key) DO UPDATE SET value_json=excluded.value_json, "
        "updated_at=excluded.updated_at, revision=revision+1"));
    query.addBindValue(
        QString::fromStdString(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()));
    query.addBindValue(user.value(0).toLongLong());
    query.addBindValue(QString::fromStdString(key));
    query.addBindValue(QString::fromStdString(valueJson));
    if (!query.exec())
        return {false, false,
                {Application::ErrorCode::Storage, "preference upsert failed: "
                                                      + query.lastError().text().toStdString(),
                 {}, false}};
    return {true, false, {}};
}

} // namespace PersonOS::Infrastructure
