#include "infrastructure/persistence/SqlKnowledgeRepository.h"

#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "infrastructure/foundation/TimeFormat.h"

namespace PersonOS::Infrastructure {

SqlKnowledgeRepository::SqlKnowledgeRepository(QSqlDatabase database,
                                               const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::SaveResult SqlKnowledgeRepository::beginWrite()
{
    if (m_transactionDepth > 0) {
        ++m_transactionDepth;
        return {true, false, {}};
    }
    if (m_database.transaction()) {
        m_transactionDepth = 1;
        return {true, false, {}};
    }
    return {false, false,
            {Application::ErrorCode::Storage, "knowledge transaction begin failed",
             m_database.lastError().text().toStdString(), true}};
}

Application::SaveResult SqlKnowledgeRepository::commitWrite()
{
    if (m_transactionDepth <= 0)
        return {false, false,
                {Application::ErrorCode::Storage, "no knowledge transaction to commit", {},
                 false}};
    if (m_transactionDepth > 1) {
        --m_transactionDepth;
        return {true, false, {}};
    }
    if (m_database.commit()) {
        m_transactionDepth = 0;
        return {true, false, {}};
    }
    return {false, false,
            {Application::ErrorCode::Storage, "knowledge transaction commit failed",
             m_database.lastError().text().toStdString(), true}};
}

void SqlKnowledgeRepository::rollbackWrite()
{
    if (m_transactionDepth > 0)
        m_database.rollback();
    m_transactionDepth = 0;
}

Application::SaveResult SqlKnowledgeRepository::writeFailure(const char *operation,
                                                              const QSqlQuery &query) const
{
    return {false, false,
            {Application::ErrorCode::Storage, operation,
             query.lastError().text().toStdString(), false}};
}

std::optional<qint64> SqlKnowledgeRepository::resolvePk(const char *sql,
                                                        const std::string &uid) const
{
    QSqlQuery query(m_database);
    query.prepare(QString::fromLatin1(sql));
    query.addBindValue(QString::fromStdString(uid));
    if (!query.exec() || !query.next())
        return std::nullopt;
    return query.value(0).toLongLong();
}

std::optional<Domain::KnowledgeItem> SqlKnowledgeRepository::findItem(const Domain::Uid &uid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, library_type, title, domain_code, reference_code, status, "
        "current_version_uid, owner_scope, created_by, never_delete, revision "
        "FROM knowledge_items_v5 WHERE uid=?"));
    query.addBindValue(QString::fromStdString(uid.value()));
    if (!query.exec() || !query.next())
        return std::nullopt;
    const auto parsed = Domain::Uid::parse(query.value("uid").toString().toStdString());
    if (!parsed)
        return std::nullopt;
    Domain::KnowledgeItem item;
    item.uid = *parsed;
    if (const auto type =
            Domain::libraryTypeFrom(query.value("library_type").toString().toStdString()))
        item.libraryType = *type;
    item.title = query.value("title").toString().toStdString();
    item.domainCode = query.value("domain_code").toString().toStdString();
    item.referenceCode = query.value("reference_code").toString().toStdString();
    if (const auto status =
            Domain::knowledgeStatusFrom(query.value("status").toString().toStdString()))
        item.status = *status;
    if (!query.value("current_version_uid").isNull())
        item.currentVersionUid = query.value("current_version_uid").toString().toStdString();
    item.ownerScope = query.value("owner_scope").toString().toStdString();
    item.createdBy = query.value("created_by").toString().toStdString();
    item.neverDelete = query.value("never_delete").toInt() != 0;
    item.revision = query.value("revision").toInt();
    return item;
}

std::vector<Domain::KnowledgeItem> SqlKnowledgeRepository::findItemsByTitle(
    const std::string &title)
{
    std::vector<Domain::KnowledgeItem> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid FROM knowledge_items_v5 WHERE title LIKE ? AND status<>? "
        "ORDER BY id"));
    query.addBindValue(QStringLiteral("%%1%").arg(QString::fromStdString(title)));
    query.addBindValue(QStringLiteral("archived"));
    if (!query.exec())
        return out;
    while (query.next()) {
        const auto uid = Domain::Uid::parse(query.value(0).toString().toStdString());
        if (!uid)
            continue;
        if (const auto item = findItem(*uid))
            out.push_back(*item);
    }
    return out;
}

std::vector<Domain::KnowledgeItem> SqlKnowledgeRepository::listRecent(int limit)
{
    std::vector<Domain::KnowledgeItem> out;
    QSqlQuery query(m_database);
    // 归档/被替代条目不进入常规列表(审计保留,按需追溯)
    query.prepare(QStringLiteral(
        "SELECT uid FROM knowledge_items_v5 WHERE status NOT IN ('archived',"
        "'superseded') ORDER BY id DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec())
        return out;
    while (query.next()) {
        const auto uid = Domain::Uid::parse(query.value(0).toString().toStdString());
        if (!uid)
            continue;
        if (const auto item = findItem(*uid))
            out.push_back(*item);
    }
    return out;
}

Application::SaveResult SqlKnowledgeRepository::insertItem(const Domain::KnowledgeItem &item)
{
    if (!item.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "knowledge item invalid", {}, false}};
    if ((item.libraryType == Domain::LibraryType::Paper
         || item.libraryType == Domain::LibraryType::Method)
        && !item.neverDelete)
        return {false, false,
                {Application::ErrorCode::Validation,
                 "paper/method must be never_delete", {}, false}};

    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO knowledge_items_v5(uid, library_type, title, domain_code, "
        "reference_code, status, current_version_uid, owner_scope, created_by, "
        "never_delete, created_at, updated_at, revision) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(item.uid.value()));
    query.addBindValue(QString::fromStdString(Domain::toString(item.libraryType)));
    query.addBindValue(QString::fromStdString(item.title));
    query.addBindValue(QString::fromStdString(item.domainCode));
    query.addBindValue(QString::fromStdString(item.referenceCode));
    query.addBindValue(QString::fromStdString(Domain::toString(item.status)));
    query.addBindValue(item.currentVersionUid
                           ? QVariant(QString::fromStdString(*item.currentVersionUid))
                           : QVariant());
    query.addBindValue(QString::fromStdString(item.ownerScope));
    query.addBindValue(QString::fromStdString(item.createdBy));
    query.addBindValue(item.neverDelete ? 1 : 0);
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("knowledge item insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlKnowledgeRepository::updateItem(const Domain::KnowledgeItem &item,
                                                           int expectedRevision)
{
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "UPDATE knowledge_items_v5 SET status=?, current_version_uid=?, title=?, "
        "domain_code=?, reference_code=?, updated_at=?, revision=revision+1 "
        "WHERE uid=? AND revision=?"));
    query.addBindValue(QString::fromStdString(Domain::toString(item.status)));
    query.addBindValue(item.currentVersionUid
                           ? QVariant(QString::fromStdString(*item.currentVersionUid))
                           : QVariant());
    query.addBindValue(QString::fromStdString(item.title));
    query.addBindValue(QString::fromStdString(item.domainCode));
    query.addBindValue(QString::fromStdString(item.referenceCode));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(item.uid.value()));
    query.addBindValue(expectedRevision);
    if (!query.exec())
        return writeFailure("knowledge item update failed", query);
    if (query.numRowsAffected() == 0)
        return {false, true,
                {Application::ErrorCode::Conflict, "knowledge item revision conflict", {},
                 false}};
    return {true, false, {}};
}

Application::Result<std::string, Application::ApplicationError>
SqlKnowledgeRepository::insertVersion(const Domain::KnowledgeVersion &version)
{
    if (!version.isValid())
        return Application::Result<std::string, Application::ApplicationError>::failure(
            {Application::ErrorCode::Validation, "knowledge version invalid", {}, false});
    const auto itemPk =
        resolvePk("SELECT id FROM knowledge_items_v5 WHERE uid=?", version.itemUid.value());
    if (!itemPk)
        return Application::Result<std::string, Application::ApplicationError>::failure(
            {Application::ErrorCode::NotFound, "knowledge item not found", {}, false});

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO knowledge_versions_v5(uid, knowledge_item_id, version_no, status, "
        "summary, claims_json, applicability_json, limitations_json, warning_json, "
        "content_hash, language, extraction_model, extraction_prompt_version, valid_from, "
        "valid_to, created_at, created_by) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    query.addBindValue(QString::fromStdString(version.uid.value()));
    query.addBindValue(*itemPk);
    query.addBindValue(version.versionNo);
    query.addBindValue(QString::fromStdString(Domain::toString(version.status)));
    query.addBindValue(QString::fromStdString(version.summary));
    query.addBindValue(QString::fromStdString(version.claimsJson));
    query.addBindValue(QString::fromStdString(version.applicabilityJson));
    query.addBindValue(QString::fromStdString(version.limitationsJson));
    query.addBindValue(QString::fromStdString(version.warningJson));
    query.addBindValue(QString::fromStdString(version.contentHash));
    query.addBindValue(QString::fromStdString(version.language));
    query.addBindValue(version.extractionModel
                           ? QVariant(QString::fromStdString(*version.extractionModel))
                           : QVariant());
    query.addBindValue(version.extractionPromptVersion
                           ? QVariant(QString::fromStdString(*version.extractionPromptVersion))
                           : QVariant());
    query.addBindValue(QString::fromStdString(version.validFrom));
    query.addBindValue(version.validTo
                           ? QVariant(QString::fromStdString(*version.validTo))
                           : QVariant());
    query.addBindValue(QString::fromStdString(version.validFrom));
    query.addBindValue(QString::fromStdString(version.createdBy));
    if (!query.exec())
        return Application::Result<std::string, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "knowledge version insert failed",
             query.lastError().text().toStdString(), false});
    return Application::Result<std::string, Application::ApplicationError>::success(
        version.uid.value());
}

std::vector<Domain::KnowledgeVersion> SqlKnowledgeRepository::versionsOf(
    const Domain::Uid &itemUid)
{
    std::vector<Domain::KnowledgeVersion> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, version_no, status, summary, claims_json, applicability_json, "
        "limitations_json, warning_json, content_hash, language, extraction_model, "
        "extraction_prompt_version, valid_from, valid_to, created_by "
        "FROM knowledge_versions_v5 "
        "WHERE knowledge_item_id=(SELECT id FROM knowledge_items_v5 WHERE uid=?) "
        "ORDER BY version_no"));
    query.addBindValue(QString::fromStdString(itemUid.value()));
    if (!query.exec())
        return out;
    while (query.next()) {
        const auto uid = Domain::Uid::parse(query.value("uid").toString().toStdString());
        if (!uid)
            continue;
        Domain::KnowledgeVersion version;
        version.uid = *uid;
        version.itemUid = itemUid;
        version.versionNo = query.value("version_no").toInt();
        if (const auto status =
                Domain::knowledgeStatusFrom(query.value("status").toString().toStdString()))
            version.status = *status;
        version.summary = query.value("summary").toString().toStdString();
        version.claimsJson = query.value("claims_json").toString().toStdString();
        version.applicabilityJson = query.value("applicability_json").toString().toStdString();
        version.limitationsJson = query.value("limitations_json").toString().toStdString();
        version.warningJson = query.value("warning_json").toString().toStdString();
        version.contentHash = query.value("content_hash").toString().toStdString();
        version.language = query.value("language").toString().toStdString();
        if (!query.value("extraction_model").isNull())
            version.extractionModel =
                query.value("extraction_model").toString().toStdString();
        if (!query.value("extraction_prompt_version").isNull())
            version.extractionPromptVersion =
                query.value("extraction_prompt_version").toString().toStdString();
        version.validFrom = query.value("valid_from").toString().toStdString();
        if (!query.value("valid_to").isNull())
            version.validTo = query.value("valid_to").toString().toStdString();
        version.createdBy = query.value("created_by").toString().toStdString();
        out.push_back(std::move(version));
    }
    return out;
}

Application::SaveResult SqlKnowledgeRepository::insertSource(const Domain::SourceRecord &source)
{
    if (!source.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "source invalid", {}, false}};
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO source_records_v5(uid, source_type, canonical_uri, title, publisher, "
        "published_at, accessed_at, metadata_json, content_hash, trust_tier, created_at, "
        "updated_at, revision) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,1)"));
    query.addBindValue(QString::fromStdString(source.uid.value()));
    query.addBindValue(QString::fromStdString(source.sourceType));
    query.addBindValue(source.canonicalUri
                           ? QVariant(QString::fromStdString(*source.canonicalUri))
                           : QVariant());
    query.addBindValue(QString::fromStdString(source.title));
    query.addBindValue(QString::fromStdString(source.publisher));
    query.addBindValue(source.publishedAt
                           ? QVariant(QString::fromStdString(*source.publishedAt))
                           : QVariant());
    query.addBindValue(QString::fromStdString(source.accessedAt));
    query.addBindValue(QString::fromStdString(source.metadataJson));
    query.addBindValue(QString::fromStdString(source.contentHash));
    query.addBindValue(QString::fromStdString(source.trustTier));
    query.addBindValue(QString::fromStdString(now));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("source insert failed", query);
    return {true, false, {}};
}

std::optional<Domain::SourceRecord> SqlKnowledgeRepository::findSourceByContentHash(
    const std::string &contentHash)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT uid, source_type, canonical_uri, title, publisher, published_at, "
        "accessed_at, metadata_json, content_hash, trust_tier, revision "
        "FROM source_records_v5 WHERE content_hash=? ORDER BY id LIMIT 1"));
    query.addBindValue(QString::fromStdString(contentHash));
    if (!query.exec() || !query.next())
        return std::nullopt;
    Domain::SourceRecord source;
    if (const auto uid = Domain::Uid::parse(query.value(0).toString().toStdString()))
        source.uid = *uid;
    else
        return std::nullopt;
    source.sourceType = query.value(1).toString().toStdString();
    if (!query.value(2).isNull())
        source.canonicalUri = query.value(2).toString().toStdString();
    source.title = query.value(3).toString().toStdString();
    source.publisher = query.value(4).toString().toStdString();
    if (!query.value(5).isNull())
        source.publishedAt = query.value(5).toString().toStdString();
    source.accessedAt = query.value(6).toString().toStdString();
    source.metadataJson = query.value(7).toString().toStdString();
    source.contentHash = query.value(8).toString().toStdString();
    source.trustTier = query.value(9).toString().toStdString();
    source.revision = query.value(10).toInt();
    return source;
}

std::optional<Domain::MethodDetail> SqlKnowledgeRepository::methodDetailOf(
    const Domain::Uid &itemUid)
{
    const auto pk = resolvePk("SELECT id FROM knowledge_items_v5 WHERE uid=?",
                              itemUid.value());
    if (!pk)
        return std::nullopt;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT method_type, risk_level, evidence_grade, trial_recommended, "
        "measurement_json FROM methods_v5 WHERE knowledge_item_id=?"));
    query.addBindValue(*pk);
    if (!query.exec() || !query.next())
        return std::nullopt;
    Domain::MethodDetail detail;
    detail.itemUid = itemUid;
    detail.methodType = query.value(0).toString().toStdString();
    detail.riskLevel = query.value(1).toString().toStdString();
    detail.evidenceGrade = query.value(2).toString().toStdString();
    detail.trialRecommended = query.value(3).toInt() != 0;
    detail.measurementJson = query.value(4).toString().toStdString();
    return detail;
}

std::vector<Domain::MethodStep> SqlKnowledgeRepository::methodStepsOf(
    const Domain::Uid &versionUid)
{
    std::vector<Domain::MethodStep> out;
    const auto pk = resolvePk("SELECT id FROM knowledge_versions_v5 WHERE uid=?",
                              versionUid.value());
    if (!pk)
        return out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT sequence_no, instruction, duration_min, config_json FROM "
        "method_steps_v5 WHERE method_version_id=? ORDER BY sequence_no"));
    query.addBindValue(*pk);
    if (!query.exec())
        return out;
    while (query.next()) {
        Domain::MethodStep step;
        step.methodVersionUid = versionUid;
        step.sequenceNo = query.value(0).toInt();
        step.instruction = query.value(1).toString().toStdString();
        if (!query.value(2).isNull())
            step.durationMin = query.value(2).toInt();
        step.configJson = query.value(3).toString().toStdString();
        out.push_back(std::move(step));
    }
    return out;
}

std::vector<Domain::EvidenceFragment> SqlKnowledgeRepository::evidenceFragmentsOf(
    const Domain::Uid &versionUid)
{
    std::vector<Domain::EvidenceFragment> out;
    const auto pk = resolvePk("SELECT id FROM knowledge_versions_v5 WHERE uid=?",
                              versionUid.value());
    if (!pk)
        return out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT f.uid, s.uid, a.uid, f.locator_json, f.text, f.fragment_hash "
        "FROM evidence_fragments_v5 f "
        "JOIN evidence_links_v5 l ON l.fragment_id=f.id "
        "JOIN source_records_v5 s ON s.id=f.source_id "
        "LEFT JOIN file_assets_v5 a ON a.id=f.asset_id "
        "WHERE l.knowledge_version_id=? ORDER BY f.id"));
    query.addBindValue(*pk);
    if (!query.exec())
        return out;
    while (query.next()) {
        Domain::EvidenceFragment fragment;
        if (const auto uid = Domain::Uid::parse(query.value(0).toString().toStdString()))
            fragment.uid = *uid;
        else
            continue;
        if (const auto uid = Domain::Uid::parse(query.value(1).toString().toStdString()))
            fragment.sourceUid = *uid;
        else
            continue;
        if (!query.value(2).isNull())
            if (const auto uid = Domain::Uid::parse(query.value(2).toString().toStdString()))
                fragment.assetUid = uid->value();
        fragment.locatorJson = query.value(3).toString().toStdString();
        fragment.text = query.value(4).toString().toStdString();
        fragment.fragmentHash = query.value(5).toString().toStdString();
        out.push_back(std::move(fragment));
    }
    return out;
}

bool SqlKnowledgeRepository::hasContradictingEvidence(const Domain::Uid &versionUid)
{
    const auto pk = resolvePk("SELECT id FROM knowledge_versions_v5 WHERE uid=?",
                              versionUid.value());
    if (!pk)
        return false;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM evidence_links_v5 l "
        "WHERE l.knowledge_version_id=? AND l.relation='contradicts' LIMIT 1"));
    query.addBindValue(*pk);
    return query.exec() && query.next();
}

Application::SaveResult SqlKnowledgeRepository::deleteItemCascade(const Domain::Uid &uid)
{
    const auto itemPk =
        resolvePk("SELECT id FROM knowledge_items_v5 WHERE uid=?", uid.value());
    if (!itemPk)
        return {false, false,
                {Application::ErrorCode::NotFound, "knowledge item not found", {}, false}};
    // 使用仓储嵌套事务(外层用例可能已开启事务,原生 transaction() 会失败)
    const auto begun = beginWrite();
    if (!begun.ok)
        return begun;
    const auto fail = [this](const char *operation, const QSqlQuery &query) {
        rollbackWrite();
        return writeFailure(operation, query);
    };

    QVector<qint64> versionIds;
    {
        QSqlQuery versions(m_database);
        versions.prepare(QStringLiteral(
            "SELECT id FROM knowledge_versions_v5 WHERE knowledge_item_id=?"));
        versions.addBindValue(*itemPk);
        if (!versions.exec())
            return fail("item delete failed", versions);
        while (versions.next())
            versionIds.append(versions.value(0).toLongLong());
    }
    // 证据链接先摘除(候选通常无,防御性处理避免 FK 阻断)
    for (const qint64 versionId : versionIds) {
        QSqlQuery links(m_database);
        links.prepare(QStringLiteral(
            "DELETE FROM evidence_links_v5 WHERE knowledge_version_id=?"));
        links.addBindValue(versionId);
        if (!links.exec())
            return fail("evidence link delete failed", links);
    }
    // 类型详情(无级联,须先删)
    for (const char *table :
         {"papers_v5", "methods_v5", "tips_v5", "plan_templates_v5"}) {
        QSqlQuery detail(m_database);
        detail.prepare(QStringLiteral("DELETE FROM %1 WHERE knowledge_item_id=?")
                           .arg(QLatin1String(table)));
        detail.addBindValue(*itemPk);
        if (!detail.exec())
            return fail("type detail delete failed", detail);
    }
    // 知识关系(双向)
    {
        QSqlQuery relations(m_database);
        relations.prepare(QStringLiteral(
            "DELETE FROM knowledge_relations_v5 WHERE from_item_id=? OR to_item_id=?"));
        relations.addBindValue(*itemPk);
        relations.addBindValue(*itemPk);
        if (!relations.exec())
            return fail("relation delete failed", relations);
    }
    // 版本(方法步骤/适用性表带 ON DELETE CASCADE)
    {
        QSqlQuery versions(m_database);
        versions.prepare(QStringLiteral(
            "DELETE FROM knowledge_versions_v5 WHERE knowledge_item_id=?"));
        versions.addBindValue(*itemPk);
        if (!versions.exec())
            return fail("version delete failed", versions);
    }
    // 条目(分类/标签带 ON DELETE CASCADE)
    {
        QSqlQuery item(m_database);
        item.prepare(QStringLiteral("DELETE FROM knowledge_items_v5 WHERE id=?"));
        item.addBindValue(*itemPk);
        if (!item.exec() || item.numRowsAffected() == 0)
            return fail("item delete failed", item);
    }
    const auto committed = commitWrite();
    if (!committed.ok)
        return committed;
    return {true, false, {}};
}

int SqlKnowledgeRepository::countPapersInDomain(const std::string &domainCode)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM knowledge_items_v5 "
        "WHERE library_type='paper' AND domain_code=?"));
    query.addBindValue(QString::fromStdString(domainCode));
    if (!query.exec() || !query.next())
        return 0;
    return query.value(0).toInt();
}

int SqlKnowledgeRepository::derivedCandidateCount(const Domain::Uid &paperItemUid,
                                                  Domain::LibraryType type)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM knowledge_items_v5 i "
        "JOIN knowledge_relations_v5 r ON r.from_item_id=i.id "
        "WHERE i.library_type=? AND i.created_by='generated' "
        "AND i.status='candidate' AND r.relation='derived_from' "
        "AND r.to_item_id=(SELECT id FROM knowledge_items_v5 WHERE uid=?)"));
    query.addBindValue(QString::fromStdString(Domain::toString(type)));
    query.addBindValue(QString::fromStdString(paperItemUid.value()));
    if (!query.exec() || !query.next())
        return 0;
    return query.value(0).toInt();
}

Application::SaveResult SqlKnowledgeRepository::insertFragment(
    const Domain::EvidenceFragment &fragment)
{
    const auto sourcePk =
        resolvePk("SELECT id FROM source_records_v5 WHERE uid=?", fragment.sourceUid.value());
    if (!sourcePk)
        return {false, false,
                {Application::ErrorCode::NotFound, "source not found", {}, false}};
    std::optional<qint64> assetPk;
    if (fragment.assetUid) {
        assetPk = resolvePk("SELECT id FROM file_assets_v5 WHERE uid=?", *fragment.assetUid);
        if (!assetPk)
            return {false, false,
                    {Application::ErrorCode::NotFound, "file asset not found", {}, false}};
    }
    const std::string now = formatUtcIso(m_clock.now());
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO evidence_fragments_v5(uid, source_id, asset_id, locator_json, text, "
        "fragment_hash, created_at) VALUES(?,?,?,?,?,?,?)"));
    query.addBindValue(QString::fromStdString(fragment.uid.value()));
    query.addBindValue(*sourcePk);
    query.addBindValue(assetPk ? QVariant(*assetPk) : QVariant());
    query.addBindValue(QString::fromStdString(
        fragment.locatorJson.empty() ? std::string("{}") : fragment.locatorJson));
    query.addBindValue(QString::fromStdString(fragment.text));
    query.addBindValue(QString::fromStdString(fragment.fragmentHash));
    query.addBindValue(QString::fromStdString(now));
    if (!query.exec())
        return writeFailure("evidence fragment insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlKnowledgeRepository::insertEvidenceLink(
    const Domain::EvidenceLink &link)
{
    const auto versionPk = resolvePk(
        "SELECT id FROM knowledge_versions_v5 WHERE uid=?", link.knowledgeVersionUid);
    const auto fragmentPk =
        resolvePk("SELECT id FROM evidence_fragments_v5 WHERE uid=?", link.fragmentUid);
    if (!versionPk || !fragmentPk)
        return {false, false,
                {Application::ErrorCode::NotFound, "version/fragment not found", {}, false}};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO evidence_links_v5(knowledge_version_id, fragment_id, relation, "
        "strength, note) VALUES(?,?,?,?,?)"));
    query.addBindValue(*versionPk);
    query.addBindValue(*fragmentPk);
    query.addBindValue(QString::fromStdString(link.relation));
    query.addBindValue(link.strength);
    query.addBindValue(link.note.empty() ? QVariant()
                                         : QVariant(QString::fromStdString(link.note)));
    if (!query.exec())
        return writeFailure("evidence link insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlKnowledgeRepository::insertRelation(
    const Domain::KnowledgeRelation &relation)
{
    if (!relation.isValid())
        return {false, false,
                {Application::ErrorCode::Validation, "knowledge relation invalid", {}, false}};
    const auto fromPk =
        resolvePk("SELECT id FROM knowledge_items_v5 WHERE uid=?", relation.fromItemUid);
    const auto toPk =
        resolvePk("SELECT id FROM knowledge_items_v5 WHERE uid=?", relation.toItemUid);
    if (!fromPk || !toPk)
        return {false, false,
                {Application::ErrorCode::NotFound, "relation endpoint not found", {}, false}};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO knowledge_relations_v5(from_item_id, to_item_id, relation, confidence, "
        "created_at, decision_uid) VALUES(?,?,?,?,?,?)"));
    query.addBindValue(*fromPk);
    query.addBindValue(*toPk);
    query.addBindValue(QString::fromStdString(relation.relation));
    query.addBindValue(relation.confidence);
    query.addBindValue(QString::fromStdString(formatUtcIso(m_clock.now())));
    query.addBindValue(relation.decisionUid
                           ? QVariant(QString::fromStdString(*relation.decisionUid))
                           : QVariant());
    if (!query.exec())
        return writeFailure("knowledge relation insert failed", query);
    return {true, false, {}};
}

bool SqlKnowledgeRepository::relationExists(const Domain::KnowledgeRelation &relation)
{
    const auto fromPk =
        resolvePk("SELECT id FROM knowledge_items_v5 WHERE uid=?", relation.fromItemUid);
    const auto toPk =
        resolvePk("SELECT id FROM knowledge_items_v5 WHERE uid=?", relation.toItemUid);
    if (!fromPk || !toPk)
        return false;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT 1 FROM knowledge_relations_v5 WHERE from_item_id=? AND to_item_id=? "
        "AND relation=? LIMIT 1"));
    query.addBindValue(*fromPk);
    query.addBindValue(*toPk);
    query.addBindValue(QString::fromStdString(relation.relation));
    return query.exec() && query.next();
}

std::vector<Domain::KnowledgeRelation> SqlKnowledgeRepository::relationsOf(
    const Domain::Uid &itemUid)
{
    std::vector<Domain::KnowledgeRelation> out;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT a.uid AS from_uid, b.uid AS to_uid, r.relation, r.confidence, "
        "r.decision_uid FROM knowledge_relations_v5 r "
        "LEFT JOIN knowledge_items_v5 a ON a.id=r.from_item_id "
        "LEFT JOIN knowledge_items_v5 b ON b.id=r.to_item_id "
        "WHERE r.from_item_id=(SELECT id FROM knowledge_items_v5 WHERE uid=?) "
        "OR r.to_item_id=(SELECT id FROM knowledge_items_v5 WHERE uid=?)"));
    query.addBindValue(QString::fromStdString(itemUid.value()));
    query.addBindValue(QString::fromStdString(itemUid.value()));
    if (!query.exec())
        return out;
    while (query.next()) {
        Domain::KnowledgeRelation relation;
        relation.fromItemUid = query.value("from_uid").toString().toStdString();
        relation.toItemUid = query.value("to_uid").toString().toStdString();
        relation.relation = query.value("relation").toString().toStdString();
        relation.confidence = query.value("confidence").toDouble();
        if (!query.value("decision_uid").isNull())
            relation.decisionUid = query.value("decision_uid").toString().toStdString();
        out.push_back(std::move(relation));
    }
    return out;
}

Application::SaveResult SqlKnowledgeRepository::insertPaperDetail(
    const Domain::PaperDetail &detail)
{
    const auto itemPk =
        resolvePk("SELECT id FROM knowledge_items_v5 WHERE uid=?", detail.itemUid.value());
    if (!itemPk)
        return {false, false,
                {Application::ErrorCode::NotFound, "knowledge item not found", {}, false}};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO papers_v5(knowledge_item_id, doi, study_type, quality_json, "
        "population_json, publication_json) VALUES(?,?,?,?,?,?)"));
    query.addBindValue(*itemPk);
    query.addBindValue(detail.doi ? QVariant(QString::fromStdString(*detail.doi)) : QVariant());
    query.addBindValue(QString::fromStdString(detail.studyType));
    query.addBindValue(QString::fromStdString(
        detail.qualityJson.empty() ? std::string("{}") : detail.qualityJson));
    query.addBindValue(QString::fromStdString(
        detail.populationJson.empty() ? std::string("{}") : detail.populationJson));
    query.addBindValue(QString::fromStdString(
        detail.publicationJson.empty() ? std::string("{}") : detail.publicationJson));
    if (!query.exec())
        return writeFailure("paper detail insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlKnowledgeRepository::insertMethodDetail(
    const Domain::MethodDetail &detail)
{
    const auto itemPk =
        resolvePk("SELECT id FROM knowledge_items_v5 WHERE uid=?", detail.itemUid.value());
    if (!itemPk)
        return {false, false,
                {Application::ErrorCode::NotFound, "knowledge item not found", {}, false}};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO methods_v5(knowledge_item_id, method_type, risk_level, evidence_grade, "
        "trial_recommended, measurement_json) VALUES(?,?,?,?,?,?)"));
    query.addBindValue(*itemPk);
    query.addBindValue(QString::fromStdString(detail.methodType));
    query.addBindValue(QString::fromStdString(detail.riskLevel));
    query.addBindValue(QString::fromStdString(detail.evidenceGrade));
    query.addBindValue(detail.trialRecommended ? 1 : 0);
    query.addBindValue(QString::fromStdString(
        detail.measurementJson.empty() ? std::string("{}") : detail.measurementJson));
    if (!query.exec())
        return writeFailure("method detail insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlKnowledgeRepository::insertTipDetail(const Domain::TipDetail &detail)
{
    const auto itemPk =
        resolvePk("SELECT id FROM knowledge_items_v5 WHERE uid=?", detail.itemUid.value());
    if (!itemPk)
        return {false, false,
                {Application::ErrorCode::NotFound, "knowledge item not found", {}, false}};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO tips_v5(knowledge_item_id, capture_type, original_uri, "
        "verification_status, risk_level, use_scenario_json) VALUES(?,?,?,?,?,?)"));
    query.addBindValue(*itemPk);
    query.addBindValue(QString::fromStdString(detail.captureType));
    query.addBindValue(detail.originalUri
                           ? QVariant(QString::fromStdString(*detail.originalUri))
                           : QVariant());
    query.addBindValue(QString::fromStdString(detail.verificationStatus));
    query.addBindValue(QString::fromStdString(detail.riskLevel));
    query.addBindValue(QString::fromStdString(
        detail.useScenarioJson.empty() ? std::string("{}") : detail.useScenarioJson));
    if (!query.exec())
        return writeFailure("tip detail insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlKnowledgeRepository::insertPlanDetail(
    const Domain::PlanDetail &detail)
{
    const auto itemPk =
        resolvePk("SELECT id FROM knowledge_items_v5 WHERE uid=?", detail.itemUid.value());
    if (!itemPk)
        return {false, false,
                {Application::ErrorCode::NotFound, "knowledge item not found", {}, false}};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO plan_templates_v5(knowledge_item_id, target_profile_json, "
        "entry_conditions_json, exit_conditions_json) VALUES(?,?,?,?)"));
    query.addBindValue(*itemPk);
    query.addBindValue(QString::fromStdString(detail.targetProfileJson));
    query.addBindValue(QString::fromStdString(
        detail.entryConditionsJson.empty() ? std::string("[]")
                                           : detail.entryConditionsJson));
    query.addBindValue(QString::fromStdString(
        detail.exitConditionsJson.empty() ? std::string("[]") : detail.exitConditionsJson));
    if (!query.exec())
        return writeFailure("plan detail insert failed", query);
    return {true, false, {}};
}

Application::SaveResult SqlKnowledgeRepository::insertMethodStep(const Domain::MethodStep &step)
{
    const auto versionPk = resolvePk(
        "SELECT id FROM knowledge_versions_v5 WHERE uid=?", step.methodVersionUid.value());
    if (!versionPk)
        return {false, false,
                {Application::ErrorCode::NotFound, "method version not found", {}, false}};
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO method_steps_v5(method_version_id, sequence_no, instruction, "
        "duration_min, config_json) VALUES(?,?,?,?,?)"));
    query.addBindValue(*versionPk);
    query.addBindValue(step.sequenceNo);
    query.addBindValue(QString::fromStdString(step.instruction));
    query.addBindValue(step.durationMin ? QVariant(*step.durationMin) : QVariant());
    query.addBindValue(QString::fromStdString(
        step.configJson.empty() ? std::string("{}") : step.configJson));
    if (!query.exec())
        return writeFailure("method step insert failed", query);
    return {true, false, {}};
}

} // namespace PersonOS::Infrastructure
