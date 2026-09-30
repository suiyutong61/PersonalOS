#include "infrastructure/knowledge/EmbeddingBackfill.h"

#include <QSqlError>
#include <QSqlQuery>

#include <QtGlobal>

#include <string>
#include <utility>
#include <vector>

#include "application/audit/Audit.h"
#include "infrastructure/knowledge/SqlEmbeddingRepository.h"

namespace PersonOS::Infrastructure {

EmbeddingBackfill::EmbeddingBackfill(QSqlDatabase database,
                                     Application::EmbeddingPort &embeddings)
    : m_database(std::move(database)), m_embeddings(embeddings)
{}

EmbeddingBackfill::Progress EmbeddingBackfill::runOneBatch(int limitPerRun)
{
    Progress progress;
    const std::string modelId = m_embeddings.modelId();
    if (modelId.empty() || m_embeddings.dimension() <= 0)
        return progress;

    // 缺行集合：当前版本无同 owner+field+model+content_hash 向量行的条目
    // （形态与 SqlKnowledgeFtsIndex::rebuildAll 一致，只取三态可检索条目）
    struct Pending
    {
        std::string ownerUid;
        std::string ownerType;
        std::string title;
        std::string summary;
        std::string contentHash;
    };
    std::vector<Pending> pending;
    {
        QSqlQuery query(m_database);
        QString sql = QStringLiteral(
            "SELECT i.uid, i.library_type, i.title, v.summary, v.content_hash "
            "FROM knowledge_items_v5 i "
            "JOIN knowledge_versions_v5 v ON v.uid = i.current_version_uid "
            "WHERE i.status IN ('active','warned','candidate') "
            "AND NOT EXISTS (SELECT 1 FROM embedding_records_v6 e "
            "  WHERE e.owner_type = i.library_type AND e.owner_uid = i.uid "
            "    AND e.field_code = 'main' AND e.model_id = ? "
            "    AND e.content_hash = v.content_hash) "
            "ORDER BY i.id ");
        if (limitPerRun > 0)
            sql += QStringLiteral("LIMIT %1").arg(limitPerRun);
        query.prepare(sql);
        query.addBindValue(QString::fromStdString(modelId));
        if (!query.exec())
            return progress;
        while (query.next()) {
            Pending row;
            row.ownerUid = query.value(0).toString().toStdString();
            row.ownerType = query.value(1).toString().toStdString();
            row.title = query.value(2).toString().toStdString();
            row.summary = query.value(3).toString().toStdString();
            row.contentHash = query.value(4).toString().toStdString();
            pending.push_back(std::move(row));
        }
    }
    progress.missing = static_cast<int>(pending.size());

    SqlEmbeddingRepository store(m_database);
    for (const auto &row : pending) {
        const auto vector = m_embeddings.embedDocument(row.title + "\n" + row.summary);
        if (!vector) {
            qWarning("embedding backfill skipped %s: %s", row.ownerUid.c_str(),
                     vector.error().message.c_str());
            ++progress.failed;
            continue;
        }
        Application::EmbeddingRecord record;
        record.ownerType = row.ownerType;
        record.ownerUid = row.ownerUid;
        record.fieldCode = "main";
        record.modelId = modelId;
        record.dimension = static_cast<int>(vector.value().size());
        record.vector = vector.value();
        const auto saved = store.upsert(record, row.contentHash);
        if (!saved.hasValue()) {
            qWarning("embedding backfill upsert failed for %s: %s", row.ownerUid.c_str(),
                     saved.error().message.c_str());
            ++progress.failed;
        } else {
            ++progress.embedded;
        }
    }

    if (progress.embedded > 0 || progress.failed > 0)
        Application::Audit::record(
            {"system", {}, "knowledge.embedding_backfilled", "knowledge", {},
             "{\"embedded\":" + std::to_string(progress.embedded)
                 + ",\"failed\":" + std::to_string(progress.failed) + "}"});
    return progress;
}

} // namespace PersonOS::Infrastructure
