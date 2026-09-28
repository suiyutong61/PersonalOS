#pragma once

#include <QSqlDatabase>

#include "application/ports/EmbeddingPort.h"
#include "application/ports/KnowledgeRetrievalPort.h"
#include "domain/foundation/Clock.h"

// SQLite 混合检索实现（结构化过滤 + FTS5 词法召回 + 排序去重 + 落库）
namespace PersonOS::Infrastructure {

class SqlKnowledgeRetrieval final : public Application::KnowledgeRetrievalPort
{
public:
    explicit SqlKnowledgeRetrieval(QSqlDatabase database, const Domain::Clock &clock);

    Application::Result<Application::RetrievalOutput, Application::ApplicationError> retrieve(
        const Application::RetrievalRequest &request) override;
    Application::Result<Domain::KnowledgeSnapshot, Application::ApplicationError> snapshot(
        const std::string &purpose, const std::string &manifestVersionsJson,
        const std::string &knowledgeVersionsJson,
        const std::optional<std::string> &retrievalRunUid) override;

    // 注入嵌入通道（可选）：注入后检索增加向量语义召回；nullptr 时降级为
    // 结构化筛选+全文召回（DR-013：向量不可用不得阻断基本知识查询）
    void setEmbeddingPort(Application::EmbeddingPort *port);

private:
    Application::EmbeddingPort *m_embeddings = nullptr;
    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
