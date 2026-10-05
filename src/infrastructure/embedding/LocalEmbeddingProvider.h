#pragma once

#include <QString>
#include <QMutex>

#include <memory>
#include <string>
#include <vector>

#include "application/ports/EmbeddingPort.h"

// 本地嵌入模型(multilingual-e5-small,ONNX Runtime):
// 零 API 成本的向量化。模型文件缺失时 embed 返回明确错误,
// 调用方按"结构化筛选+全文召回"降级,不阻断知识检索。
namespace PersonOS::Infrastructure {

class SentencePieceUnigram;

class LocalEmbeddingProvider final : public Application::EmbeddingPort
{
public:
    explicit LocalEmbeddingProvider(QString modelPath = defaultModelPath(),
                                    QString tokenizerPath = defaultTokenizerPath());
    ~LocalEmbeddingProvider() override;

    Application::Result<std::vector<float>, Application::ApplicationError> embed(
        const std::string &text) override;
    // 索引/文档侧:e5 系列需要 passage 前缀(与查询侧 query 前缀区分)
    Application::Result<std::vector<float>, Application::ApplicationError> embedDocument(
        const std::string &text) override;
    int dimension() const override { return 384; }
    // 模型标识(含量化版本 + 模型文件 SHA-256 前缀):向量行按模型隔离,
    // 不同模型空间不混算余弦;模型文件更换后哈希前缀变化 → 旧向量行在
    // 新模型下不再命中,启动回填自动为新模型重嵌入(旧行保留不删除)
    std::string modelId() const override;

    // 模型文件 SHA-256 前 8 位(首次计算后缓存;文件缺失返回 "missing")
    std::string modelFileHashPrefix() const;

    // e5 系列要求任务前缀;分类/检索分别使用两侧
    Application::Result<std::vector<float>, Application::ApplicationError> embedQuery(
        const std::string &text);
    Application::Result<std::vector<float>, Application::ApplicationError> embedPassage(
        const std::string &text);

    // 触发懒加载并返回是否就绪(首次调用约 1-2 秒,调用方置于后台线程)
    bool isReady();
    // 仅检查模型/分词器文件是否存在(不加载,启动期可用)
    static bool filesPresent();
    QString lastError() const { return m_lastError; }

    // 默认模型位置:应用可执行文件同目录 embedding/
    static QString defaultModelPath();
    static QString defaultTokenizerPath();

private:
    Application::Result<std::vector<float>, Application::ApplicationError> embedWithPrefix(
        const std::string &text, const QString &prefix);
    bool ensureLoaded();

    QString m_modelPath;
    QString m_tokenizerPath;
    mutable QMutex m_mutex;
    bool m_ready = false;
    QString m_lastError;
    mutable std::string m_modelHashPrefix;   // 首次计算后缓存
    std::unique_ptr<SentencePieceUnigram> m_tokenizer;
    void *m_env = nullptr;        // OrtEnv*
    void *m_session = nullptr;    // OrtSession*
    void *m_allocator = nullptr;  // OrtAllocator*
};

// 进程内共享实例(函数局部静态):领域分类、导入索引、检索注入、回填
// 共用同一 ONNX 会话,避免 130MB 模型重复加载;推理由内部互斥串行化。
LocalEmbeddingProvider &sharedLocalEmbedding();

} // namespace PersonOS::Infrastructure
