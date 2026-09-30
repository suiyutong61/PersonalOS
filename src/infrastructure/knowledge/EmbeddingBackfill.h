#pragma once

#include <QSqlDatabase>

#include "application/ports/EmbeddingPort.h"

// 知识条目向量回填（可重建派生数据维护，架构 4.5.4）：
// 对"当前版本存在但该模型无向量行"的条目逐条 embedDocument(title+summary)
// 并 upsert。缺行即断点续作（无需额外状态），限量分批，
// 失败条目本次跳过（下次运行自然补上，不重试风暴）。
namespace PersonOS::Infrastructure {

class EmbeddingBackfill
{
public:
    struct Progress
    {
        int embedded = 0;   // 本批成功写入
        int missing = 0;    // 运行前缺行数（尚未覆盖的条目）
        int failed = 0;     // 本批嵌入/写入失败
    };

    EmbeddingBackfill(QSqlDatabase database, Application::EmbeddingPort &embeddings);

    // 回填一批；limitPerRun<=0 表示不限量（重建用）。返回进度。
    Progress runOneBatch(int limitPerRun = 20);

private:
    QSqlDatabase m_database;
    Application::EmbeddingPort &m_embeddings;
};

} // namespace PersonOS::Infrastructure
