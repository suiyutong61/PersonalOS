#pragma once

#include <QByteArray>
#include <QMutex>
#include <QString>
#include <QStringList>
#include <QVector>

#include "application/ports/EmbeddingPort.h"

// 本地领域分类器(Research Base 01-43):
// 43 个领域描述(标题+子领域词表)本地向量化一次,
// 论文标题/摘要 → 余弦相似度 top-k 推荐,零 API 成本。
// 领域向量持久化到 embedding_records_v6(owner_type="domain",
// field_code="description"):内容指纹与当前 domains.json 一致时直接装载,
// 免每次启动重算;数据库不可用时回退纯内存计算(DR-013)。
namespace PersonOS::Infrastructure {

class DomainClassifier
{
public:
    struct Candidate
    {
        QString code;      // "01"
        QString slug;      // "goal-self-regulation"
        QString title;     // 中文标题
        double score = 0.0;
    };

    static DomainClassifier &instance();

    // 返回按相似度降序的 topK 领域;模型缺失时返回空
    QVector<Candidate> suggest(const QString &text, int topK = 3);

    bool available() const;

    // 数据库路径(由装配根启动时配置;未配置或打不开时回退纯内存向量)
    static void configureDatabasePath(const QString &databasePath);

    // 重建入口:确保领域向量已就绪后写入数据库(设置页"重建向量索引"
    // 清空向量后立即调用,避免重建→重启前领域行缺失的窗口)
    static bool persistDomainsToDatabase(const QString &databasePath);

    // 测试钩子(仅测试使用):替换嵌入提供者并复位内存缓存
    void setProviderForTesting(Application::EmbeddingPort *provider);
    void resetForTesting();

    // 缓存失效:备份恢复切换换库后调用(领域向量按 domains.json 指纹
    // 校验会自动重算/重种,但内存缓存必须清除才重新走装载路径)
    void invalidateCache();

private:
    DomainClassifier();
    void ensureSeeded();
    bool loadDomainsJson();
    QString domainsJsonPath() const;
    bool loadDomainVectorsFromDatabase(const QString &databasePath,
                                       const std::string &modelId);
    bool persistDomainVectorsToDatabase(const QString &databasePath,
                                        const std::string &modelId);

    mutable QMutex m_mutex;
    bool m_seeded = false;
    bool m_available = false;
    QByteArray m_domainsHash;   // domains.json 原始字节 SHA-256(十六进制文本)
    Application::EmbeddingPort *m_provider = nullptr;
    QVector<Candidate> m_domains;
    QStringList m_domainDescriptions;
    QVector<QVector<float>> m_domainVectors;

    static QString s_databasePath;
};

} // namespace PersonOS::Infrastructure
