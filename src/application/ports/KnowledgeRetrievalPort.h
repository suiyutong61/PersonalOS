#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/ApplicationError.h"
#include "application/foundation/Result.h"
#include "domain/knowledge/Retrieval.h"

// 知识检索端口（DD-001 §7.2；IMP-005 实现结构化过滤+FTS 词法召回+排序去重；
// 向量召回接口由 IMP-006 的 EmbeddingPort 接入后扩展）
namespace PersonOS::Application {

struct RetrievalRequest
{
    std::string purpose;              // 检索意图（路线规划/MEL 下达/复盘/验收/咨询）
    std::string queryText;
    std::string filtersJson = "{}";   // {"library_type":"method","domain_code":"learning",...}
    std::string strategyVersion = "1";
};

struct RetrievalOutput
{
    Domain::RetrievalRun run;
    std::vector<Domain::RetrievalHit> hits;
};

class KnowledgeRetrievalPort
{
public:
    virtual ~KnowledgeRetrievalPort() = default;

    // 执行一次检索（结构化过滤 + FTS5 词法召回 + 排序/去重 + 落库）
    virtual Result<RetrievalOutput, ApplicationError> retrieve(
        const RetrievalRequest &request) = 0;

    // 知识快照（content_hash 唯一：同内容幂等返回已有快照）
    virtual Result<Domain::KnowledgeSnapshot, ApplicationError> snapshot(
        const std::string &purpose, const std::string &manifestVersionsJson,
        const std::string &knowledgeVersionsJson,
        const std::optional<std::string> &retrievalRunUid) = 0;
};

} // namespace PersonOS::Application
