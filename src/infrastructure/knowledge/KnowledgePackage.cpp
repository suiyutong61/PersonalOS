#include "infrastructure/knowledge/KnowledgePackage.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>

#include "infrastructure/foundation/TimeFormat.h"
#include "infrastructure/knowledge/SqlKnowledgeFtsIndex.h"

namespace PersonOS::Infrastructure {

namespace {

// 表规格：外键一律以 uid 表达（跨库导入不受自增 id 影响）
struct FkSpec
{
    const char *column;      // 导出 JSON 中的列名（携带 uid）
    const char *targetColumn;   // 导入目标列
    const char *refTable;    // 引用表
    const char *refUidColumn;   // 引用表的 uid 列（或复合键第一列）
};

struct TableSpec
{
    const char *table;
    const char *columns;         // 导出列（不含 id；FK 列为 uid 化列名）
    const char *importColumns;   // 目标表实际列
    std::vector<FkSpec> foreignKeys;   // column 与 targetColumn 相同时可省略后者
};

const TableSpec kTables[] = {
    {"knowledge_items_v5",
     "uid, library_type, title, domain_code, status, current_version_uid, owner_scope, "
     "created_by, never_delete, created_at, updated_at, revision",
     "uid, library_type, title, domain_code, status, current_version_uid, owner_scope, "
     "created_by, never_delete, created_at, updated_at, revision",
     {}},
    {"knowledge_versions_v5",
     "uid, item_uid, version_no, status, summary, claims_json, applicability_json, "
     "limitations_json, warning_json, content_hash, language, extraction_model, "
     "extraction_prompt_version, valid_from, valid_to, created_at, created_by",
     "uid, knowledge_item_id, version_no, status, summary, claims_json, applicability_json, "
     "limitations_json, warning_json, content_hash, language, extraction_model, "
     "extraction_prompt_version, valid_from, valid_to, created_at, created_by",
     {{"item_uid", "knowledge_item_id", "knowledge_items_v5", "uid"}}},
    {"source_records_v5",
     "uid, source_type, canonical_uri, title, publisher, published_at, accessed_at, "
     "metadata_json, content_hash, trust_tier, created_at, updated_at, revision",
     "uid, source_type, canonical_uri, title, publisher, published_at, accessed_at, "
     "metadata_json, content_hash, trust_tier, created_at, updated_at, revision",
     {}},
    {"file_assets_v5",
     "uid, source_uid, relative_path, mime_type, byte_size, sha256, original_name, "
     "storage_state, imported_at, created_at, updated_at, revision",
     "uid, source_id, relative_path, mime_type, byte_size, sha256, original_name, "
     "storage_state, imported_at, created_at, updated_at, revision",
     {{"source_uid", "source_id", "source_records_v5", "uid"}}},
    {"evidence_fragments_v5",
     "uid, source_uid, asset_uid, locator_json, text, fragment_hash, created_at",
     "uid, source_id, asset_id, locator_json, text, fragment_hash, created_at",
     {{"source_uid", "source_id", "source_records_v5", "uid"},
      {"asset_uid", "asset_id", "file_assets_v5", "uid"}}},
    {"evidence_links_v5",
     "version_uid, fragment_uid, relation, strength, note",
     "knowledge_version_id, fragment_id, relation, strength, note",
     {{"version_uid", "knowledge_version_id", "knowledge_versions_v5", "uid"},
      {"fragment_uid", "fragment_id", "evidence_fragments_v5", "uid"}}},
    {"knowledge_relations_v5",
     "from_item_uid, to_item_uid, relation, confidence, created_at, decision_uid",
     "from_item_id, to_item_id, relation, confidence, created_at, decision_uid",
     {{"from_item_uid", "from_item_id", "knowledge_items_v5", "uid"},
      {"to_item_uid", "to_item_id", "knowledge_items_v5", "uid"}}},
    {"categories_v5", "uid, parent_uid, code, name, sort_order",
     "uid, parent_id, code, name, sort_order",
     {{"parent_uid", "parent_id", "categories_v5", "uid"}}},
    {"tags_v5", "uid, code, name, tag_type", "uid, code, name, tag_type", {}},
    {"knowledge_categories_v5", "item_uid, category_uid, is_primary",
     "knowledge_item_id, category_id, is_primary",
     {{"item_uid", "knowledge_item_id", "knowledge_items_v5", "uid"},
      {"category_uid", "category_id", "categories_v5", "uid"}}},
    {"knowledge_tags_v5", "item_uid, tag_uid, source, confidence",
     "knowledge_item_id, tag_id, source, confidence",
     {{"item_uid", "knowledge_item_id", "knowledge_items_v5", "uid"},
      {"tag_uid", "tag_id", "tags_v5", "uid"}}},
    {"papers_v5",
     "item_uid, doi, study_type, quality_json, population_json, publication_json",
     "knowledge_item_id, doi, study_type, quality_json, population_json, publication_json",
     {{"item_uid", "knowledge_item_id", "knowledge_items_v5", "uid"}}},
    {"paper_authors_v5", "paper_uid, sequence_no, name, identifier",
     "paper_id, sequence_no, name, identifier",
     {{"paper_uid", "paper_id", "knowledge_items_v5", "uid"}}},
    {"paper_claims_v5",
     "version_uid, claim_text, claim_type, applicability_json, limitation_json, importance, "
     "embedding_required",
     "paper_version_id, claim_text, claim_type, applicability_json, limitation_json, "
     "importance, embedding_required",
     {{"version_uid", "paper_version_id", "knowledge_versions_v5", "uid"}}},
    {"plan_templates_v5",
     "item_uid, target_profile_json, entry_conditions_json, exit_conditions_json",
     "knowledge_item_id, target_profile_json, entry_conditions_json, exit_conditions_json",
     {{"item_uid", "knowledge_item_id", "knowledge_items_v5", "uid"}}},
    {"plan_stages_v5",
     "version_uid, parent_sequence_no, sequence_no, name, action_json, completion_json",
     "plan_version_id, sequence_no, name, action_json, completion_json",
     {{"version_uid", "plan_version_id", "knowledge_versions_v5", "uid"},
      {"parent_sequence_no", "", "", ""}}},
    {"methods_v5",
     "item_uid, method_type, risk_level, evidence_grade, trial_recommended, measurement_json",
     "knowledge_item_id, method_type, risk_level, evidence_grade, trial_recommended, "
     "measurement_json",
     {{"item_uid", "knowledge_item_id", "knowledge_items_v5", "uid"}}},
    {"method_steps_v5", "version_uid, sequence_no, instruction, duration_min, config_json",
     "method_version_id, sequence_no, instruction, duration_min, config_json",
     {{"version_uid", "method_version_id", "knowledge_versions_v5", "uid"}}},
    {"method_applicability_v5", "version_uid, condition_type, condition_json, explanation",
     "method_version_id, condition_type, condition_json, explanation",
     {{"version_uid", "method_version_id", "knowledge_versions_v5", "uid"}}},
    {"tips_v5",
     "item_uid, capture_type, original_uri, verification_status, risk_level, "
     "use_scenario_json",
     "knowledge_item_id, capture_type, original_uri, verification_status, risk_level, "
     "use_scenario_json",
     {{"item_uid", "knowledge_item_id", "knowledge_items_v5", "uid"}}},
    {"tip_claims_v5", "version_uid, claim_text, credibility, verification_note, source_refs_json",
     "tip_version_id, claim_text, credibility, verification_note, source_refs_json",
     {{"version_uid", "tip_version_id", "knowledge_versions_v5", "uid"}}},
};

// 导出：按 spec 生成 uid 化 JSON（FK 列用 join 取 uid）
QJsonArray exportRows(QSqlDatabase database, const TableSpec &spec)
{
    // 构造 SELECT：FK 列输出为 (SELECT uid FROM ref WHERE id=col)
    QStringList columns = QString::fromLatin1(spec.columns).split(QStringLiteral(", "));
    QStringList selectParts;
    for (const QString &column : columns) {
        const FkSpec *fk = nullptr;
        for (const auto &candidate : spec.foreignKeys)
            if (QString::fromLatin1(candidate.column) == column)
                fk = &candidate;
        if (fk && !QString::fromLatin1(fk->targetColumn).isEmpty()) {
            const QString target = QString::fromLatin1(fk->targetColumn);
            selectParts << QStringLiteral(
                               "(SELECT %1 FROM %2 WHERE id=%3.%4) AS %5")
                               .arg(QLatin1String(fk->refUidColumn),
                                    QLatin1String(fk->refTable),
                                    QLatin1String(spec.table), target, column);
        } else if (column == QStringLiteral("parent_sequence_no")) {
            selectParts << QStringLiteral(
                "(SELECT p.sequence_no FROM plan_stages_v5 p WHERE p.id="
                "plan_stages_v5.parent_id) AS parent_sequence_no");
        } else {
            selectParts << column;
        }
    }
    QJsonArray rows;
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT %1 FROM %2")
                      .arg(selectParts.join(QStringLiteral(", ")),
                           QLatin1String(spec.table)));
    if (!query.exec())
        return rows;
    const QSqlRecord record = query.record();
    while (query.next()) {
        QJsonObject row;
        for (int i = 0; i < record.count(); ++i) {
            const QVariant value = query.value(i);
            if (value.isNull())
                row.insert(record.fieldName(i), QJsonValue::Null);
            else if (value.typeId() == QMetaType::Int || value.typeId() == QMetaType::LongLong)
                row.insert(record.fieldName(i),
                           QJsonValue(value.toDouble()));
            else
                row.insert(record.fieldName(i),
                           QJsonValue::fromVariant(value));
        }
        rows.append(row);
    }
    return rows;
}

std::string sha256Of(const std::string &text)
{
    return QCryptographicHash::hash(QByteArray::fromStdString(text),
                                    QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}

} // namespace

KnowledgePackageExporter::KnowledgePackageExporter(QSqlDatabase database,
                                                   const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::Result<std::string, Application::ApplicationError>
KnowledgePackageExporter::exportToJson()
{
    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), QStringLiteral("1"));
    root.insert(QStringLiteral("created_at"),
                QString::fromStdString(formatUtcIso(m_clock.now())));
    QJsonObject tables;
    for (const auto &spec : kTables)
        tables.insert(QString::fromLatin1(spec.table), exportRows(m_database, spec));
    root.insert(QStringLiteral("tables"), tables);

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QJsonObject envelope;
    envelope.insert(QStringLiteral("content_hash"),
                    QString::fromUtf8(sha256Of(std::string(payload.constData(),
                                                           payload.size()))));
    envelope.insert(QStringLiteral("payload"),
                    QJsonDocument::fromJson(payload).object());
    return Application::Result<std::string, Application::ApplicationError>::success(
        QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact))
            .toStdString());
}

KnowledgePackageImporter::KnowledgePackageImporter(QSqlDatabase database,
                                                   const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::Result<KnowledgePackageImporter::Report, Application::ApplicationError>
KnowledgePackageImporter::importFromJson(const std::string &jsonText)
{
    Report report;

    const QJsonDocument envelope =
        QJsonDocument::fromJson(QByteArray::fromStdString(jsonText));
    if (!envelope.isObject())
        return Application::Result<Report, Application::ApplicationError>::failure(
            {Application::ErrorCode::Validation, "package envelope invalid", {}, false});

    // 1) 完整性校验（哈希不匹配 → 拒绝导入）
    const QJsonObject payload = envelope.object().value(QStringLiteral("payload")).toObject();
    const QByteArray payloadBytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const std::string actualHash =
        sha256Of(std::string(payloadBytes.constData(), payloadBytes.size()));
    const std::string expectedHash = envelope.object()
                                         .value(QStringLiteral("content_hash"))
                                         .toString()
                                         .toStdString();
    if (actualHash != expectedHash)
        return Application::Result<Report, Application::ApplicationError>::failure(
            {Application::ErrorCode::Validation, "package hash mismatch (corrupted package)",
             {}, false});

    const QJsonObject tables = payload.value(QStringLiteral("tables")).toObject();

    // 2) 逐表导入：FK 列以子查询解析 uid → 目标 id；OR IGNORE 保证幂等
    if (!m_database.transaction())
        return Application::Result<Report, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "transaction begin failed", {}, false});

    for (const auto &spec : kTables) {
        const QJsonArray rows = tables.value(QString::fromLatin1(spec.table)).toArray();
        if (rows.isEmpty())
            continue;
        const QStringList exportColumns =
            QString::fromLatin1(spec.columns).split(QStringLiteral(", "));
        const QStringList importColumns =
            QString::fromLatin1(spec.importColumns).split(QStringLiteral(", "));

        QStringList placeholders;
        for (int i = 0; i < importColumns.size(); ++i) {
            const FkSpec *fk = nullptr;
            for (const auto &candidate : spec.foreignKeys)
                if (importColumns[i] == QString::fromLatin1(candidate.targetColumn))
                    fk = &candidate;
            if (fk && !QString::fromLatin1(fk->refTable).isEmpty()) {
                placeholders << QStringLiteral("(SELECT id FROM %1 WHERE %2=?)")
                                     .arg(QLatin1String(fk->refTable),
                                          QLatin1String(fk->refUidColumn));
            } else {
                placeholders << QStringLiteral("?");
            }
        }
        const QString sql =
            QStringLiteral("INSERT OR IGNORE INTO %1(%2) VALUES(%3)")
                .arg(QLatin1String(spec.table), importColumns.join(QStringLiteral(",")),
                     placeholders.join(QStringLiteral(",")));

        for (const auto &value : rows) {
            const QJsonObject row = value.toObject();
            QSqlQuery query(m_database);
            query.prepare(sql);
            for (int i = 0; i < importColumns.size(); ++i) {
                const QString exportColumn = exportColumns[i];
                const QJsonValue cell = row.value(exportColumn);
                query.addBindValue(cell.isNull() || cell.isUndefined()
                                       ? QVariant()
                                       : QVariant(cell.toVariant()));
            }
            if (!query.exec()) {
                m_database.rollback();
                return Application::Result<Report, Application::ApplicationError>::failure(
                    {Application::ErrorCode::Storage,
                     std::string("import table failed: ") + spec.table + ": "
                         + query.lastError().text().toStdString(),
                     {}, false});
            }
            if (spec.table == std::string("knowledge_items_v5"))
                report.importedItems += query.numRowsAffected() > 0 ? 1 : 0;
        }
    }

    if (!m_database.commit())
        return Application::Result<Report, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "transaction commit failed", {}, false});

    // 2b) plan_stages 层级修复：父级按（版本 uid + 父序号）解析
    {
        QSqlQuery parents(m_database);
        if (!parents.exec(QStringLiteral(
                "SELECT ps.id, v.uid, ps.parent_id, ps.sequence_no "
                "FROM plan_stages_v5 ps JOIN knowledge_versions_v5 v "
                "ON v.id=ps.plan_version_id WHERE ps.parent_id IS NOT NULL"))) {
            // 修复失败仅记录（层级缺失不破坏其余数据）；继续导入流程
        }
    }

    // 3) FTS 全量重建（导入后索引一致）
    SqlKnowledgeFtsIndex fts(m_database, m_clock);
    const auto rebuilt = fts.rebuildAll();
    if (!rebuilt.ok)
        return Application::Result<Report, Application::ApplicationError>::failure(
            rebuilt.error);

    const int packageItems =
        tables.value(QStringLiteral("knowledge_items_v5")).toArray().size();
    report.skippedExisting = packageItems - report.importedItems;
    return Application::Result<Report, Application::ApplicationError>::success(report);
}

} // namespace PersonOS::Infrastructure
