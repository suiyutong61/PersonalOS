#include "infrastructure/ai/SqlAiRepository.h"

#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

SqlAiRepository::SqlAiRepository(QSqlDatabase database, const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::SaveResult SqlAiRepository::writeFailure(const char *operation,
                                                       const QSqlQuery &query) const
{
    return {false, false,
            {Application::ErrorCode::Storage, operation,
             query.lastError().text().toStdString(), false}};
}

std::optional<qint64> SqlAiRepository::resolvePk(const char *sql,
                                                 const std::string &uid) const
{
    QSqlQuery query(m_database);
    query.prepare(QString::fromLatin1(sql));
    query.addBindValue(QString::fromStdString(uid));
    if (!query.exec() || !query.next())
        return std::nullopt;
    return query.value(0).toLongLong();
}

std::optional<Domain::AiProviderConfig> SqlAiRepository::findConfig(const Domain::Uid &uid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, provider_code, display_name, endpoint, model, credential_ref, "
        "capabilities_json, enabled, revision, is_default FROM ai_provider_configs_v6 "
        "WHERE uid=?"));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto parsed = Domain::Uid::parse(query.value(0).toString().toStdString());
    if (!parsed)
        return std::nullopt;
    Domain::AiProviderConfig config;
    config.uid = *parsed;
    config.providerCode = query.value(1).toString().toStdString();
    config.displayName = query.value(2).toString().toStdString();
    config.endpoint = query.value(3).toString().toStdString();
    config.model = query.value(4).toString().toStdString();
    config.credentialRef = query.value(5).toString().toStdString();
    config.capabilitiesJson = query.value(6).toString().toStdString();
    config.enabled = query.value(7).toInt() != 0;
    config.revision = query.value(8).toInt();
    config.isDefault = query.value(9).toInt() != 0;
    return config;
}

std::optional<Domain::AiProviderConfig> SqlAiRepository::findEnabledConfig(
    const Domain::Uid &uid)
{
    auto config = findConfig(uid);
    if (config && !config->enabled)
        return std::nullopt;
    return config;
}

std::vector<Domain::AiProviderConfig> SqlAiRepository::listConfigs()
{
    std::vector<Domain::AiProviderConfig> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid FROM ai_provider_configs_v6 ORDER BY id"));
    if (!query.exec())
        return out;
    while (query.next()) {
        const auto uid = Domain::Uid::parse(query.value(0).toString().toStdString());
        if (!uid)
            continue;
        if (const auto config = findConfig(*uid))
            out.push_back(*config);
    }
    return out;
}

std::optional<Domain::AiProviderConfig> SqlAiRepository::findFirstEnabledConfig()
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid FROM ai_provider_configs_v6 WHERE enabled=1 "
        "ORDER BY is_default DESC, id LIMIT 1"));
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto uid = Domain::Uid::parse(query.value(0).toString().toStdString());
    if (!uid)
        return std::nullopt;
    return findConfig(*uid);
}

Application::SaveResult SqlAiRepository::insertConfig(const Domain::AiProviderConfig &config)
{
    if (!config.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "provider config invalid", {}, false}};
    // (provider_code, model) 有唯一约束；先查重返回稳定冲突错误，避免把
    // SQL 约束原文泄露给界面（重复保存是常见操作，不应显示数据库错误文本）。
    QSqlQuery existing(m_database);
    existing.prepare(QStringLiteral(
        "SELECT uid FROM ai_provider_configs_v6 WHERE provider_code=? AND model=?"));
    existing.addBindValue(QString::fromStdString(config.providerCode));
    existing.addBindValue(QString::fromStdString(config.model));
    if (!existing.exec())
        return writeFailure("provider config insert failed", existing);
    if (existing.next())
        return {false, true,
                {Application::ErrorCode::Conflict, "provider config already exists", {},
                 false}};
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO ai_provider_configs_v6(uid, provider_code, display_name, endpoint, "
        "model, credential_ref, capabilities_json, enabled, created_at, updated_at, revision) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(config.uid.value()));
    query.addBindValue(QString::fromStdString(config.providerCode));
    query.addBindValue(QString::fromStdString(config.displayName));
    query.addBindValue(QString::fromStdString(config.endpoint));
    query.addBindValue(QString::fromStdString(config.model));
    query.addBindValue(QString::fromStdString(config.credentialRef));
    query.addBindValue(QString::fromStdString(config.capabilitiesJson));
    query.addBindValue(config.enabled ? 1 : 0);
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("provider config insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlAiRepository::updateConfig(const Domain::AiProviderConfig &config,
                                                      int expectedRevision)
{
    // 修改模型标识时同样可能撞 (provider_code, model) 唯一约束；先查重。
    QSqlQuery clash(m_database);
    clash.prepare(QStringLiteral(
        "SELECT uid FROM ai_provider_configs_v6 WHERE provider_code=? AND model=? AND uid<>?"));
    clash.addBindValue(QString::fromStdString(config.providerCode));
    clash.addBindValue(QString::fromStdString(config.model));
    clash.addBindValue(QString::fromStdString(config.uid.value()));
    if (!clash.exec())
        return writeFailure("provider config update failed", clash);
    if (clash.next())
        return {false, true,
                {Application::ErrorCode::Conflict, "provider config already exists", {},
                 false}};
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE ai_provider_configs_v6 SET display_name=?, endpoint=?, model=?, "
        "capabilities_json=?, enabled=?, is_default=?, updated_at=?, revision=revision+1 "
        "WHERE uid=? AND revision=?"));
    query.addBindValue(QString::fromStdString(config.displayName));
    query.addBindValue(QString::fromStdString(config.endpoint));
    query.addBindValue(QString::fromStdString(config.model));
    query.addBindValue(QString::fromStdString(config.capabilitiesJson));
    query.addBindValue(config.enabled ? 1 : 0);
    query.addBindValue(config.isDefault ? 1 : 0);
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(config.uid.value()));
    query.addBindValue(expectedRevision);
    if (!query.exec())
        return writeFailure("provider config update failed", query);
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict, "provider config revision conflict", {},
                 false}};
    return {true, false, {}};
}

Application::SaveResult SqlAiRepository::setDefaultConfig(const Domain::Uid &uid)
{
    const auto target = findConfig(uid);
    if (!target)
        return {false, false,
                {Application::ErrorCode::NotFound, "provider config not found", {}, false}};
    // 单一默认:同一事务先清后设;部分唯一索引兜底约束
    if (!m_database.transaction())
        return {false, false,
                {Application::ErrorCode::Storage, "default config update failed", {}, false}};
    QSqlQuery clear(m_database);
    clear.prepare(QStringLiteral(
        "UPDATE ai_provider_configs_v6 SET is_default=0 WHERE is_default=1"));
    if (!clear.exec()) {
        m_database.rollback();
        return writeFailure("default config update failed", clear);
    }
    QSqlQuery set(m_database);
    set.prepare(QStringLiteral(
        "UPDATE ai_provider_configs_v6 SET is_default=1 WHERE uid=?"));
    set.addBindValue(QString::fromStdString(uid.value()));
    if (!set.exec() || set.numRowsAffected() == 0) {
        m_database.rollback();
        return writeFailure("default config update failed", set);
    }
    if (!m_database.commit())
        return {false, false,
                {Application::ErrorCode::Storage, "default config update failed", {}, false}};
    return {true, false, {}};
}

Application::SaveResult SqlAiRepository::deleteConfig(const Domain::Uid &uid)
{
    const auto configId = resolvePk(
        "SELECT id FROM ai_provider_configs_v6 WHERE uid=?", uid.value());
    if (!configId)
        return {false, false,
                {Application::ErrorCode::NotFound, "provider config not found", {}, false}};
    if (!m_database.transaction())
        return {false, false,
                {Application::ErrorCode::Storage, "provider config delete failed", {}, false}};
    QSqlQuery tests(m_database);
    tests.prepare(QStringLiteral(
        "DELETE FROM ai_connection_tests_v7 WHERE provider_config_id=?"));
    tests.addBindValue(*configId);
    if (!tests.exec()) {
        m_database.rollback();
        return writeFailure("connection test delete failed", tests);
    }
    QSqlQuery config(m_database);
    config.prepare(QStringLiteral("DELETE FROM ai_provider_configs_v6 WHERE id=?"));
    config.addBindValue(*configId);
    if (!config.exec() || config.numRowsAffected() == 0) {
        m_database.rollback();
        return {false, false,
                {Application::ErrorCode::NotFound, "provider config not found", {}, false}};
    }
    if (!m_database.commit())
        return {false, false,
                {Application::ErrorCode::Storage, "provider config delete failed", {}, false}};
    return {true, false, {}};
}

Application::SaveResult SqlAiRepository::insertConnectionTest(
    const Domain::AiConnectionTest &test)
{
    const auto configId = resolvePk("SELECT id FROM ai_provider_configs_v6 WHERE uid=?",
                                    test.providerConfigUid.value());
    if (!configId)
        return {false, false,
                {Application::ErrorCode::NotFound, "provider config not found", {}, false}};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO ai_connection_tests_v7(uid,provider_config_id,tested_at,provider_model,"
        "auth_ok,structured_ok,embedding_required,embedding_ok,overall_ok,capabilities_json,error_json) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(QString::fromStdString(test.uid.value()));
    query.addBindValue(*configId);
    query.addBindValue(QString::fromStdString(test.testedAt));
    query.addBindValue(QString::fromStdString(test.providerModel));
    query.addBindValue(test.authOk ? 1 : 0);
    query.addBindValue(test.structuredOk ? 1 : 0);
    query.addBindValue(test.embeddingRequired ? 1 : 0);
    query.addBindValue(test.embeddingOk ? 1 : 0);
    query.addBindValue(test.overallOk ? 1 : 0);
    query.addBindValue(QString::fromStdString(test.capabilitiesJson));
    query.addBindValue(QString::fromStdString(test.errorJson));
    if (!query.exec())
        return writeFailure("connection test insert failed", query);
    return {true, false, {}};
}

std::optional<Domain::AiConnectionTest> SqlAiRepository::latestConnectionTest(
    const Domain::Uid &configUid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT t.uid,t.tested_at,t.provider_model,t.auth_ok,t.structured_ok,"
        "t.embedding_required,t.embedding_ok,t.overall_ok,t.capabilities_json,t.error_json "
        "FROM ai_connection_tests_v7 t JOIN ai_provider_configs_v6 c ON c.id=t.provider_config_id "
        "WHERE c.uid=? ORDER BY t.tested_at DESC,t.id DESC LIMIT 1"));
    query.addBindValue(QString::fromStdString(configUid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    Domain::AiConnectionTest test;
    const auto uid = Domain::Uid::parse(query.value(0).toString().toStdString());
    if (!uid) return std::nullopt;
    test.uid = *uid;
    test.providerConfigUid = configUid;
    test.testedAt = query.value(1).toString().toStdString();
    test.providerModel = query.value(2).toString().toStdString();
    test.authOk = query.value(3).toInt() != 0;
    test.structuredOk = query.value(4).toInt() != 0;
    test.embeddingRequired = query.value(5).toInt() != 0;
    test.embeddingOk = query.value(6).toInt() != 0;
    test.overallOk = query.value(7).toInt() != 0;
    test.capabilitiesJson = query.value(8).toString().toStdString();
    test.errorJson = query.value(9).toString().toStdString();
    return test;
}

std::vector<Domain::AiJob> SqlAiRepository::listJobs(int limit)
{
    std::vector<Domain::AiJob> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT uid FROM ai_jobs_v6 ORDER BY id DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec())
        return out;
    while (query.next()) {
        const auto uid = Domain::Uid::parse(query.value(0).toString().toStdString());
        if (!uid)
            continue;
        if (const auto job = findJob(*uid))
            out.push_back(*job);
    }
    return out;
}

std::optional<Domain::AiJob> SqlAiRepository::findJob(const Domain::Uid &uid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, job_type, status, priority, request_json, result_json, schema_version, "
        "idempotency_key, attempt_count, max_attempts, next_attempt_at, error_json, revision "
        "FROM ai_jobs_v6 WHERE uid=?"));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto parsed = Domain::Uid::parse(query.value(0).toString().toStdString());
    if (!parsed)
        return std::nullopt;
    Domain::AiJob job;
    job.uid = *parsed;
    job.jobType = query.value(1).toString().toStdString();
    job.status = [&] {
        const auto text = query.value(2).toString().toStdString();
        if (text == "queued") return Domain::AiJobStatus::Queued;
        if (text == "running") return Domain::AiJobStatus::Running;
        if (text == "completed") return Domain::AiJobStatus::Completed;
        if (text == "failed_retryable") return Domain::AiJobStatus::FailedRetryable;
        if (text == "failed_terminal") return Domain::AiJobStatus::FailedTerminal;
        return Domain::AiJobStatus::Cancelled;
    }();
    job.priority = query.value(3).toInt();
    job.requestJson = query.value(4).toString().toStdString();
    if (!query.value(5).isNull())
        job.resultJson = query.value(5).toString().toStdString();
    job.schemaVersion = query.value(6).toString().toStdString();
    job.idempotencyKey = query.value(7).toString().toStdString();
    job.attemptCount = query.value(8).toInt();
    job.maxAttempts = query.value(9).toInt();
    if (!query.value(10).isNull())
        job.nextAttemptAt = query.value(10).toString().toStdString();
    if (!query.value(11).isNull())
        job.errorJson = query.value(11).toString().toStdString();
    job.revision = query.value(12).toInt();
    return job;
}

Application::SaveResult SqlAiRepository::insertJob(const Domain::AiJob &job)
{
    if (!job.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "ai job invalid", {}, false}};
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO ai_jobs_v6(uid, job_type, status, priority, request_json, result_json, "
        "schema_version, idempotency_key, attempt_count, max_attempts, next_attempt_at, "
        "error_json, created_at, updated_at, started_at, completed_at, revision) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(job.uid.value()));
    query.addBindValue(QString::fromStdString(job.jobType));
    query.addBindValue(QString::fromStdString(Domain::toString(job.status)));
    query.addBindValue(job.priority);
    query.addBindValue(QString::fromStdString(job.requestJson));
    query.addBindValue(job.resultJson
                           ? QVariant(QString::fromStdString(*job.resultJson))
                           : QVariant());
    query.addBindValue(QString::fromStdString(job.schemaVersion));
    query.addBindValue(QString::fromStdString(job.idempotencyKey));
    query.addBindValue(job.attemptCount);
    query.addBindValue(job.maxAttempts);
    query.addBindValue(job.nextAttemptAt
                           ? QVariant(QString::fromStdString(*job.nextAttemptAt))
                           : QVariant());
    query.addBindValue(job.errorJson
                           ? QVariant(QString::fromStdString(*job.errorJson))
                           : QVariant());
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QVariant());
    query.addBindValue(QVariant());
    if (!query.exec())
        return writeFailure("ai job insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlAiRepository::updateJob(const Domain::AiJob &job,
                                                   int expectedRevision)
{
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE ai_jobs_v6 SET status=?, result_json=?, attempt_count=?, next_attempt_at=?, "
        "error_json=?, started_at=?, completed_at=?, updated_at=?, revision=revision+1 "
        "WHERE uid=? AND revision=?"));
    query.addBindValue(QString::fromStdString(Domain::toString(job.status)));
    query.addBindValue(job.resultJson
                           ? QVariant(QString::fromStdString(*job.resultJson))
                           : QVariant());
    query.addBindValue(job.attemptCount);
    query.addBindValue(job.nextAttemptAt
                           ? QVariant(QString::fromStdString(*job.nextAttemptAt))
                           : QVariant());
    query.addBindValue(job.errorJson
                           ? QVariant(QString::fromStdString(*job.errorJson))
                           : QVariant());
    query.addBindValue(QVariant());
    query.addBindValue(job.status == Domain::AiJobStatus::Completed
                           ? QVariant(QString::fromStdString(now))
                           : QVariant());
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(job.uid.value()));
    query.addBindValue(expectedRevision);
    if (!query.exec())
        return writeFailure("ai job update failed", query);
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict, "ai job revision conflict", {}, false}};
    return {true, false, {}};
}

bool SqlAiRepository::existsJobKey(const std::string &idempotencyKey)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM ai_jobs_v6 WHERE idempotency_key=? LIMIT 1"));
    query.addBindValue(QString::fromStdString(idempotencyKey));
    return query.exec() && query.next();
}

std::optional<Domain::AiJob> SqlAiRepository::findJobByKey(const std::string &idempotencyKey)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid FROM ai_jobs_v6 WHERE idempotency_key=? ORDER BY id LIMIT 1"));
    query.addBindValue(QString::fromStdString(idempotencyKey));
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto uid = Domain::Uid::parse(query.value(0).toString().toStdString());
    if (!uid)
        return std::nullopt;
    return findJob(*uid);
}

Application::SaveResult SqlAiRepository::insertCall(const Domain::Uid &callUid,
                                                    const Domain::Uid &jobUid,
                                                    const Domain::Uid &configUid,
                                                    const std::string &requestHash,
                                                    const std::string &contextHash,
                                                    const std::string &responseHash,
                                                    qint64 latencyMs,
                                                    const std::string &status)
{
    const auto jobPk = resolvePk("SELECT id FROM ai_jobs_v6 WHERE uid=?", jobUid.value());
    const auto configPk =
        resolvePk("SELECT id FROM ai_provider_configs_v6 WHERE uid=?", configUid.value());
    if (!jobPk || !configPk)
        return {false, false,
                {Application::ErrorCode::NotFound, "job/config not found", {}, false}};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO ai_calls_v6(uid, job_id, provider_config_id, request_hash, context_hash, "
        "response_hash, usage_json, latency_ms, status, created_at) VALUES(?,?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(QString::fromStdString(callUid.value()));
    query.addBindValue(*jobPk);
    query.addBindValue(*configPk);
    query.addBindValue(QString::fromStdString(requestHash));
    query.addBindValue(QString::fromStdString(contextHash));
    query.addBindValue(QString::fromStdString(responseHash));
    query.addBindValue(QStringLiteral("{}"));
    query.addBindValue(latencyMs);
    query.addBindValue(QString::fromStdString(status));
    query.addBindValue(QString::fromStdString(formatUtcIso(m_clock.now())));
    if (!query.exec())
        return writeFailure("ai call insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlAiRepository::insertContextItem(const Domain::Uid &callUid,
                                                           const std::string &contextType,
                                                           const std::string &contextUid,
                                                           const std::string &versionUid,
                                                           int rank,
                                                           const std::string &reason)
{
    const auto callPk = resolvePk("SELECT id FROM ai_calls_v6 WHERE uid=?", callUid.value());
    if (!callPk)
        return {false, false,
                {Application::ErrorCode::NotFound, "call not found", {}, false}};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO ai_context_items_v6(call_id, context_type, context_uid, version_uid, "
        "rank, reason) VALUES(?,?,?,?,?,?)"));
    query.addBindValue(*callPk);
    query.addBindValue(QString::fromStdString(contextType));
    query.addBindValue(QString::fromStdString(contextUid));
    query.addBindValue(QString::fromStdString(versionUid));
    query.addBindValue(rank);
    query.addBindValue(QString::fromStdString(reason));
    if (!query.exec())
        return writeFailure("ai context item insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlAiRepository::insertDecision(const Domain::DecisionRecord &decision)
{
    if (!decision.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "decision invalid", {}, false}};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO decision_records_v6(uid, decision_type, aggregate_type, aggregate_uid, "
        "input_snapshot_json, candidate_json, selected_json, rationale, source_mode, "
        "warning_json, user_status, created_at, confirmed_at, job_uid) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(QString::fromStdString(decision.uid.value()));
    query.addBindValue(QString::fromStdString(decision.decisionType));
    query.addBindValue(QString::fromStdString(decision.aggregateType));
    query.addBindValue(QString::fromStdString(decision.aggregateUid));
    query.addBindValue(QString::fromStdString(decision.inputSnapshotJson));
    query.addBindValue(QString::fromStdString(decision.candidateJson));
    query.addBindValue(decision.selectedJson
                           ? QVariant(QString::fromStdString(*decision.selectedJson))
                           : QVariant());
    query.addBindValue(QString::fromStdString(decision.rationale));
    query.addBindValue(QString::fromStdString(Domain::toString(decision.sourceMode)));
    query.addBindValue(QString::fromStdString(decision.warningJson));
    query.addBindValue(QString::fromStdString(Domain::toString(decision.userStatus)));
    query.addBindValue(QString::fromStdString(decision.createdAt));
    query.addBindValue(decision.confirmedAt
                           ? QVariant(QString::fromStdString(*decision.confirmedAt))
                           : QVariant());
    query.addBindValue(decision.jobUid
                           ? QVariant(QString::fromStdString(*decision.jobUid))
                           : QVariant());
    if (!query.exec())
        return writeFailure("decision insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlAiRepository::deleteDecision(const Domain::Uid &uid)
{
    // 物理删除(2026-09-29 用户决策):连同关联 AI 任务/调用/上下文
    // (ai_context_items 经 ai_calls 级联);历史审计事件保留。
    const auto decision = findDecision(uid);
    if (!decision)
        return {false, false,
                {Application::ErrorCode::NotFound, "decision not found", {}, false}};
    if (!m_database.transaction())
        return {false, false,
                {Application::ErrorCode::Storage, "decision delete failed", {}, false}};
    if (decision->jobUid) {
        const auto jobId = resolvePk(
            "SELECT id FROM ai_jobs_v6 WHERE uid=?", *decision->jobUid);
        if (jobId) {
            QSqlQuery calls(m_database);
            calls.prepare(QStringLiteral("DELETE FROM ai_calls_v6 WHERE job_id=?"));
            calls.addBindValue(*jobId);
            if (!calls.exec()) {
                m_database.rollback();
                return writeFailure("ai calls delete failed", calls);
            }
            QSqlQuery job(m_database);
            job.prepare(QStringLiteral("DELETE FROM ai_jobs_v6 WHERE id=?"));
            job.addBindValue(*jobId);
            if (!job.exec()) {
                m_database.rollback();
                return writeFailure("ai job delete failed", job);
            }
        }
    }
    QSqlQuery record(m_database);
    record.prepare(QStringLiteral("DELETE FROM decision_records_v6 WHERE uid=?"));
    record.addBindValue(QString::fromStdString(uid.value()));
    if (!record.exec() || record.numRowsAffected() == 0) {
        m_database.rollback();
        return {false, false,
                {Application::ErrorCode::NotFound, "decision not found", {}, false}};
    }
    if (!m_database.commit())
        return {false, false,
                {Application::ErrorCode::Storage, "decision delete failed", {}, false}};
    return {true, false, {}};
}

std::optional<Domain::DecisionRecord> SqlAiRepository::findDecision(const Domain::Uid &uid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, decision_type, aggregate_type, aggregate_uid, input_snapshot_json, "
        "candidate_json, selected_json, rationale, source_mode, warning_json, user_status, "
        "created_at, confirmed_at, job_uid FROM decision_records_v6 WHERE uid=?"));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto parsed = Domain::Uid::parse(query.value(0).toString().toStdString());
    if (!parsed)
        return std::nullopt;
    Domain::DecisionRecord decision;
    decision.uid = *parsed;
    decision.decisionType = query.value(1).toString().toStdString();
    decision.aggregateType = query.value(2).toString().toStdString();
    decision.aggregateUid = query.value(3).toString().toStdString();
    decision.inputSnapshotJson = query.value(4).toString().toStdString();
    decision.candidateJson = query.value(5).toString().toStdString();
    if (!query.value(6).isNull())
        decision.selectedJson = query.value(6).toString().toStdString();
    decision.rationale = query.value(7).toString().toStdString();
    if (const auto mode = Domain::sourceModeFrom(query.value(8).toString().toStdString()))
        decision.sourceMode = *mode;
    decision.warningJson = query.value(9).toString().toStdString();
    const auto status = query.value(10).toString().toStdString();
    if (status == "not_required") decision.userStatus = Domain::DecisionUserStatus::NotRequired;
    else if (status == "accepted") decision.userStatus = Domain::DecisionUserStatus::Accepted;
    else if (status == "modified") decision.userStatus = Domain::DecisionUserStatus::Modified;
    else if (status == "rejected") decision.userStatus = Domain::DecisionUserStatus::Rejected;
    else decision.userStatus = Domain::DecisionUserStatus::Pending;
    decision.createdAt = query.value(11).toString().toStdString();
    if (!query.value(12).isNull())
        decision.confirmedAt = query.value(12).toString().toStdString();
    if (!query.value(13).isNull())
        decision.jobUid = query.value(13).toString().toStdString();
    return decision;
}

Application::SaveResult SqlAiRepository::updateDecisionStatus(
    const Domain::Uid &uid, const std::string &userStatus,
    const std::optional<std::string> &selectedJson)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE decision_records_v6 SET user_status=?, selected_json=?, confirmed_at=? "
        "WHERE uid=?"));
    query.addBindValue(QString::fromStdString(userStatus));
    query.addBindValue(selectedJson
                           ? QVariant(QString::fromStdString(*selectedJson))
                           : QVariant());
    query.addBindValue(QString::fromStdString(formatUtcIso(m_clock.now())));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec())
        return writeFailure("decision status update failed", query);
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict, "decision not found", {}, false}};
    return {true, false, {}};
}

std::vector<Domain::DecisionRecord> SqlAiRepository::listDecisions(
    const std::string &decisionType, int limit)
{
    std::vector<Domain::DecisionRecord> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid FROM decision_records_v6 WHERE decision_type=? ORDER BY id DESC LIMIT ?"));
    query.addBindValue(QString::fromStdString(decisionType));
    query.addBindValue(limit);
    if (!query.exec())
        return out;
    while (query.next()) {
        const auto uid = Domain::Uid::parse(query.value(0).toString().toStdString());
        if (!uid)
            continue;
        if (const auto decision = findDecision(*uid))
            out.push_back(*decision);
    }
    return out;
}

} // namespace PersonOS::Infrastructure
