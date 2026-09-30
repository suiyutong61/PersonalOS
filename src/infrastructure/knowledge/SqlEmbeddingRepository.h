#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QVariant>

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/ApplicationError.h"
#include "application/foundation/Result.h"
#include "application/ports/EmbeddingVectorStore.h"

// 嵌入向量仓储（embedding_records_v6；可重建派生数据，模型更换后可重新生成）
// 实现 EmbeddingVectorStore：知识条目索引行(field_code="main")与领域行
// (owner_type="domain", field_code="description")共用同一张表。
namespace PersonOS::Infrastructure {

using EmbeddingRow = Application::EmbeddingRecord;

class SqlEmbeddingRepository final : public Application::EmbeddingVectorStore
{
public:
    explicit SqlEmbeddingRepository(QSqlDatabase database);

    // 覆盖式 upsert（同一 owner+field+model+content_hash 更新向量；随后清理
    // 同 owner+field+model 的旧 content_hash 行——当前内容只保留一行）
    Application::Result<void, Application::ApplicationError> upsert(
        const EmbeddingRow &row, const std::string &contentHash) override;

    // 某模型的全部向量行（含领域行；调用方按需过滤）
    std::vector<EmbeddingRow> allForModel(const std::string &modelId) override;

    // 某模型下某 owner_type 的向量行（领域行装载用）
    std::vector<EmbeddingRow> allForOwnerType(const std::string &ownerType,
                                              const std::string &modelId) override;

    // 清理某模型/某 owner 的向量（条目删除用）
    Application::Result<void, Application::ApplicationError> removeForOwner(
        const std::string &ownerType, const std::string &ownerUid) override;

    // 清理某模型的全部向量（向量索引重建用）
    Application::Result<void, Application::ApplicationError> removeForModel(
        const std::string &modelId) override;

    // 全部向量（诊断用；检索侧已改走 allForModel）
    std::vector<EmbeddingRow> all();

private:
    std::vector<EmbeddingRow> load(const QString &where, const QList<QVariant> &binds);

    QSqlDatabase m_database;
};

// 余弦相似度（两向量维度不一致返回 0；零向量返回 0）
float cosineSimilarity(const std::vector<float> &a, const std::vector<float> &b);

} // namespace PersonOS::Infrastructure
