#pragma once

#include <string>
#include <vector>

#include "application/foundation/ApplicationError.h"
#include "application/foundation/Result.h"

// 嵌入端口（DD-001 §5.4 EmbeddingPort；DR-013/014）
// 实现：本地嵌入模型或经 AI 网关调用具备 embedding 能力的模型连接；
// 不可用时检索降级为结构化筛选+全文召回（不得阻断基本知识查询）。
namespace PersonOS::Application {

class EmbeddingPort
{
public:
    virtual ~EmbeddingPort() = default;

    // 文本 → 向量（维度由实现声明；失败返回明确错误）
    // 语义：查询侧（检索/分类输入）
    virtual Result<std::vector<float>, ApplicationError> embed(const std::string &text) = 0;

    // 文本 → 向量（索引/文档侧；默认与 embed 相同，需要任务前缀的模型
    // 如 e5 系列可覆盖为 passage 前缀，保证两侧语义空间一致）
    virtual Result<std::vector<float>, ApplicationError> embedDocument(const std::string &text)
    {
        return embed(text);
    }

    // 该实现声明的向量维度（0 = 未知/不可用）
    virtual int dimension() const = 0;

    // 嵌入模型标识（含版本；不同模型空间不混算余弦）。
    // 空 = 未知 → 调用方跳过向量阶段（DR-013 降级）
    virtual std::string modelId() const { return {}; }
};

} // namespace PersonOS::Application
