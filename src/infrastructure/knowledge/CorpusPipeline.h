#pragma once

#include <QSqlDatabase>

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/EmbeddingPort.h"
#include "application/ports/EmbeddingVectorStore.h"
#include "application/ports/UuidPort.h"
#include "domain/foundation/Clock.h"
#include "domain/foundation/Uid.h"

// 永久单文献处理核心的批量入口（DR-009；scope-v1 §5 开发者脚手架必须复用
// 与产品相同的单文献处理核心，不维护第二套分类/提取/版本/可信度规则）。
// 每个文献：获取 → 内容指纹/去重 → 文本提取（PDF/文本/网页）→ 分类标签 →
// 论文导入（KnowledgeUseCases 正式结构）→ FTS 索引。
namespace PersonOS::Infrastructure {

class CorpusPipeline
{
public:
    struct DownloadAttempt
    {
        std::string source;
        int attempt = 1;
        int httpStatus = 0;
        bool success = false;
        bool retryable = false;
        std::string error;
    };

    struct Item
    {
        std::string source;          // url(http/https) 或本地文件路径
        std::vector<std::string> sourceCandidates; // 多下载地址，按顺序回退
        std::string title;           // 标题（可空：从文件名推断）
        std::string kind = "paper";  // paper / text
        std::string domainCode = "unclassified";
        std::string paperNumber;
        std::string publishedYear;
        std::string studyType;
        std::string indexSourceFile;
        int indexStartLine = 0;
        int indexEndLine = 0;
        std::vector<std::string> researchFocus;
    };

    struct Outcome
    {
        bool ok = false;
        std::string status;          // imported / duplicate / failed
        std::string message;         // 失败原因（成功时为空）
        std::string itemUid;         // 导入后的知识条目 uid
        std::string sha256;
        std::string resolvedSource;
        std::string sourceUid;
        std::string assetUid;
        std::string assetRelativePath;
        std::string extractedText;
        std::string extractionTool;
        std::vector<DownloadAttempt> downloadAttempts;
    };

    CorpusPipeline(QSqlDatabase database, Application::UuidPort &uids,
                   const Domain::Clock &clock, Application::EmbeddingPort *embeddings = nullptr,
                   Application::EmbeddingVectorStore *vectorStore = nullptr);

    // 处理一个条目：真实下载/读取 + 提取 + 去重 + 导入；任何失败都有明确原因
    Outcome process(const Item &item);

    // 文件内容指纹（测试与断点续作用）
    static std::string sha256Of(const std::string &content);

private:
    QSqlDatabase m_database;
    Application::UuidPort &m_uids;
    const Domain::Clock &m_clock;
    Application::EmbeddingPort *m_embeddings = nullptr;
    Application::EmbeddingVectorStore *m_vectorStore = nullptr;
};

} // namespace PersonOS::Infrastructure
