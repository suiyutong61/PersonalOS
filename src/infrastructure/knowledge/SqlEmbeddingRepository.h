#pragma once

#include <QSqlDatabase>

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/ApplicationError.h"
#include "application/foundation/Result.h"

// 嵌入向量仓储（embedding_record_v6；可重建派生数据，模型更换后可重新生成）
namespace PersonOS::Infrastructure {

struct EmbeddingRow
{
    std::string ownerType;
    std::string ownerUid;
    std::string fieldCode;
    std::string modelId;
    int dimension = 0;
    std::vector<float> vector;
};

class SqlEmbeddingRepository
{
public:
    explicit SqlEmbeddingRepository(QSqlDatabase database);

    // 覆盖式 upsert（同一 owner+field+model 只保留一份；content_hash 参与唯一键）
    Application::Result<void, Application::ApplicationError> upsert(const EmbeddingRow &row,
                                                                    const std::string &contentHash);

    // 全部向量（检索侧内存计算余弦；v1 规模可接受，量级增长后改批量 top-k 策略）
    std::vector<EmbeddingRow> all();

    // 清理某模型/某 owner 的向量（索引重建用）
    Application::Result<void, Application::ApplicationError> removeForOwner(
        const std::string &ownerType, const std::string &ownerUid);

private:
    QSqlDatabase m_database;
};

// 余弦相似度（两向量维度不一致返回 0；零向量返回 0）
float cosineSimilarity(const std::vector<float> &a, const std::vector<float> &b);

} // namespace PersonOS::Infrastructure
