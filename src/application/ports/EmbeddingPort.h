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
    virtual Result<std::vector<float>, ApplicationError> embed(const std::string &text) = 0;

    // 该实现声明的向量维度（0 = 未知/不可用）
    virtual int dimension() const = 0;
};

} // namespace PersonOS::Application
