#pragma once

#include <string>
#include <vector>

#include "application/foundation/ApplicationError.h"
#include "application/foundation/Result.h"

// 嵌入向量存储端口（architecture 4.5.4；DR-013/014）
// 向量属于可重建派生数据：按 (owner_type, owner_uid, field_code, model_id)
// 关联被索引对象，同一对象在同一模型下只保留当前内容指纹一行。
// 实现：SqlEmbeddingRepository（embedding_records_v6）。
namespace PersonOS::Application {

struct EmbeddingRecord
{
    std::string ownerType;   // 知识库类型(paper/plan/state/method/tip)或 "domain"
    std::string ownerUid;
    std::string fieldCode;   // "main"(知识条目索引) / "description"(领域描述)
    std::string modelId;
    int dimension = 0;
    std::vector<float> vector;
    std::string contentHash; // 读取时填充(写入时经 upsert 参数传入)
};

class EmbeddingVectorStore
{
public:
    virtual ~EmbeddingVectorStore() = default;

    // 覆盖式 upsert(同 owner+field+model+content_hash 更新;同 owner+field+model
    // 的旧 content_hash 行由实现清理,保证当前版本只保留一行)
    virtual Result<void, ApplicationError> upsert(const EmbeddingRecord &record,
                                                  const std::string &contentHash) = 0;

    // 某模型的全部向量行(检索侧内存计算余弦;v1 规模可接受)
    virtual std::vector<EmbeddingRecord> allForModel(const std::string &modelId) = 0;

    // 某模型下某 owner_type 的全部向量行(领域行装载用)
    virtual std::vector<EmbeddingRecord> allForOwnerType(const std::string &ownerType,
                                                         const std::string &modelId) = 0;

    // 清理某 owner 的全部向量(条目删除/索引重建用)
    virtual Result<void, ApplicationError> removeForOwner(const std::string &ownerType,
                                                          const std::string &ownerUid) = 0;

    // 清理某模型的全部向量(向量索引重建用)
    virtual Result<void, ApplicationError> removeForModel(const std::string &modelId) = 0;
};

} // namespace PersonOS::Application
