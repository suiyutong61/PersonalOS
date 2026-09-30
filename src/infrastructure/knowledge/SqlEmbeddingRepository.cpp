#include "infrastructure/knowledge/SqlEmbeddingRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include <cmath>
#include <cstring>

namespace PersonOS::Infrastructure {

namespace {

QByteArray toBlob(const std::vector<float> &vector)
{
    QByteArray bytes;
    bytes.resize(static_cast<int>(vector.size() * sizeof(float)));
    if (!vector.empty())
        std::memcpy(bytes.data(), vector.data(), bytes.size());
    return bytes;
}

std::vector<float> fromBlob(const QByteArray &bytes)
{
    std::vector<float> vector;
    if (bytes.size() % static_cast<int>(sizeof(float)) != 0)
        return vector;
    vector.resize(bytes.size() / sizeof(float));
    if (!vector.empty())
        std::memcpy(vector.data(), bytes.constData(), bytes.size());
    return vector;
}

} // namespace

SqlEmbeddingRepository::SqlEmbeddingRepository(QSqlDatabase database)
    : m_database(std::move(database))
{}

Application::Result<void, Application::ApplicationError> SqlEmbeddingRepository::upsert(
    const EmbeddingRow &row, const std::string &contentHash)
{
    if (row.ownerType.empty() || row.ownerUid.empty() || row.fieldCode.empty()
        || row.modelId.empty() || row.vector.empty() || contentHash.empty())
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Validation, "embedding row invalid", {}, false});

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO embedding_records_v6(owner_type, owner_uid, field_code, model_id, "
        "dimension, vector_blob, content_hash, created_at) VALUES(?,?,?,?,?,?,?,"
        "strftime('%Y-%m-%dT%H:%M:%SZ','now')) "
        "ON CONFLICT(owner_type,owner_uid,field_code,model_id,content_hash) DO UPDATE SET "
        "dimension=excluded.dimension, vector_blob=excluded.vector_blob"));
    query.addBindValue(QString::fromStdString(row.ownerType));
    query.addBindValue(QString::fromStdString(row.ownerUid));
    query.addBindValue(QString::fromStdString(row.fieldCode));
    query.addBindValue(QString::fromStdString(row.modelId));
    query.addBindValue(static_cast<int>(row.vector.size()));
    query.addBindValue(toBlob(row.vector));
    query.addBindValue(QString::fromStdString(contentHash));
    if (!query.exec())
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "embedding upsert failed: "
                                                 + query.lastError().text().toStdString(),
             {}, false});
    // 同 owner+field+model 只保留当前内容指纹一行(版本切换/内容更新后
    // 旧 hash 行不残留,向量空间内同一对象至多一条)
    QSqlQuery cleanup(m_database);
    cleanup.prepare(QStringLiteral(
        "DELETE FROM embedding_records_v6 WHERE owner_type=? AND owner_uid=? "
        "AND field_code=? AND model_id=? AND content_hash<>?"));
    cleanup.addBindValue(QString::fromStdString(row.ownerType));
    cleanup.addBindValue(QString::fromStdString(row.ownerUid));
    cleanup.addBindValue(QString::fromStdString(row.fieldCode));
    cleanup.addBindValue(QString::fromStdString(row.modelId));
    cleanup.addBindValue(QString::fromStdString(contentHash));
    if (!cleanup.exec())
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "embedding stale row cleanup failed: "
                                                 + cleanup.lastError().text().toStdString(),
             {}, false});
    return Application::Result<void, Application::ApplicationError>::success();
}

std::vector<EmbeddingRow> SqlEmbeddingRepository::allForModel(const std::string &modelId)
{
    return load(QStringLiteral("WHERE model_id=?"),
                {QString::fromStdString(modelId)});
}

std::vector<EmbeddingRow> SqlEmbeddingRepository::allForOwnerType(const std::string &ownerType,
                                                                  const std::string &modelId)
{
    return load(QStringLiteral("WHERE owner_type=? AND model_id=?"),
                {QString::fromStdString(ownerType), QString::fromStdString(modelId)});
}

std::vector<EmbeddingRow> SqlEmbeddingRepository::all()
{
    return load(QString(), {});
}

std::vector<EmbeddingRow> SqlEmbeddingRepository::load(const QString &where,
                                                       const QList<QVariant> &binds)
{
    std::vector<EmbeddingRow> out;
    QSqlQuery query(m_database);
    QString sql = QStringLiteral(
        "SELECT owner_type, owner_uid, field_code, model_id, dimension, vector_blob, "
        "content_hash "
        "FROM embedding_records_v6 ");
    sql += where;
    query.prepare(sql);
    for (const QVariant &value : binds)
        query.addBindValue(value);
    if (!query.exec())
        return out;
    while (query.next()) {
        EmbeddingRow row;
        row.ownerType = query.value(0).toString().toStdString();
        row.ownerUid = query.value(1).toString().toStdString();
        row.fieldCode = query.value(2).toString().toStdString();
        row.modelId = query.value(3).toString().toStdString();
        row.dimension = query.value(4).toInt();
        row.vector = fromBlob(query.value(5).toByteArray());
        row.contentHash = query.value(6).toString().toStdString();
        if (!row.vector.empty())
            out.push_back(std::move(row));
    }
    return out;
}

Application::Result<void, Application::ApplicationError>
SqlEmbeddingRepository::removeForOwner(const std::string &ownerType,
                                       const std::string &ownerUid)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "DELETE FROM embedding_records_v6 WHERE owner_type=? AND owner_uid=?"));
    query.addBindValue(QString::fromStdString(ownerType));
    query.addBindValue(QString::fromStdString(ownerUid));
    if (!query.exec())
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "embedding remove failed", {}, false});
    return Application::Result<void, Application::ApplicationError>::success();
}

Application::Result<void, Application::ApplicationError>
SqlEmbeddingRepository::removeForModel(const std::string &modelId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM embedding_records_v6 WHERE model_id=?"));
    query.addBindValue(QString::fromStdString(modelId));
    if (!query.exec())
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage, "embedding remove for model failed", {}, false});
    return Application::Result<void, Application::ApplicationError>::success();
}

float cosineSimilarity(const std::vector<float> &a, const std::vector<float> &b)
{
    if (a.empty() || a.size() != b.size())
        return 0.0f;
    double dot = 0.0;
    double normA = 0.0;
    double normB = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += static_cast<double>(a[i]) * static_cast<double>(b[i]);
        normA += static_cast<double>(a[i]) * static_cast<double>(a[i]);
        normB += static_cast<double>(b[i]) * static_cast<double>(b[i]);
    }
    if (normA <= 0.0 || normB <= 0.0)
        return 0.0f;
    return static_cast<float>(dot / (std::sqrt(normA) * std::sqrt(normB)));
}

} // namespace PersonOS::Infrastructure
