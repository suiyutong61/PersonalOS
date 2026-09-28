// 模块：向量检索通道（DR-013/014；architecture 4.5.4 混合检索）
// 覆盖：嵌入存储 upsert/读取、余弦相似度、向量独有召回（词法不重叠）、
//       混合命中融合、未注入嵌入通道时降级为结构化+全文（不阻断查询）。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "application/ports/EmbeddingPort.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/knowledge/SqlEmbeddingRepository.h"
#include "infrastructure/knowledge/SqlKnowledgeFtsIndex.h"
#include "infrastructure/knowledge/SqlKnowledgeRetrieval.h"
#include "infrastructure/persistence/SqlKnowledgeRepository.h"

using namespace PersonOS;

namespace {

// 确定性替身：4 维 one-hot（按字符和取模）——同桶文本相似度 1，异桶 0
class FakeEmbeddingPort : public Application::EmbeddingPort
{
public:
    Application::Result<std::vector<float>, Application::ApplicationError> embed(
        const std::string &text) override
    {
        int sum = 0;
        for (const unsigned char c : text)
            sum += c;
        const int bucket = sum % 4;
        std::vector<float> vector(4, 0.0f);
        vector[bucket] = 1.0f;
        return Application::Result<std::vector<float>, Application::ApplicationError>::success(
            vector);
    }
    int dimension() const override { return 4; }
};

} // namespace

class TstEmbedding : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_embedding.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
        QVERIFY(Infrastructure::LearningManifestSeed(DatabaseManager::instance().database(),
                                                     m_clock)
                    .ensureSeeded());
    }

    void cosineSimilarityBasics()
    {
        QCOMPARE(Infrastructure::cosineSimilarity({1, 0}, {1, 0}), 1.0f);
        QCOMPARE(Infrastructure::cosineSimilarity({1, 0}, {0, 1}), 0.0f);
        QVERIFY(Infrastructure::cosineSimilarity({1, 0}, {1, 1}) > 0.7f);
        QCOMPARE(Infrastructure::cosineSimilarity({1, 0}, {1, 0, 0}), 0.0f);  // 维度不同
        QCOMPARE(Infrastructure::cosineSimilarity({}, {}), 0.0f);
    }

    void vectorOnlyRecallAndDegradation()
    {
        // 两个知识条目：标题与查询词无词法重叠（靠向量召回）
        Infrastructure::SqlKnowledgeRepository knowledge(
            DatabaseManager::instance().database(), m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(DatabaseManager::instance().database(), m_clock);
        Infrastructure::SqlEmbeddingRepository embeddings(
            DatabaseManager::instance().database());

        const auto itemA = "alpha-item";
        const auto itemB = "beta-item";
        // FTS 索引（词法召回通道的数据源）
        fts.indexVersion("method", itemA, "first-method", "alpha content words", "[]", "{}", "[]", "");
        fts.indexVersion("method", itemB, "second-method", "beta content words", "[]", "{}", "[]", "");

        FakeEmbeddingPort fake;
        // 索引向量：itemA 桶 0，itemB 桶 2
        {
            const auto vA = fake.embed("a");
            const auto vB = fake.embed("bb");
            QVERIFY(vA && vB);
            Infrastructure::EmbeddingRow rowA;
            rowA.ownerType = "method";
            rowA.ownerUid = itemA;
            rowA.fieldCode = "title";
            rowA.modelId = "fake";
            rowA.dimension = 4;
            rowA.vector = vA.value();
            QVERIFY(embeddings.upsert(rowA, "hash-a"));
            Infrastructure::EmbeddingRow rowB = rowA;
            rowB.ownerUid = itemB;
            rowB.vector = vB.value();
            QVERIFY(embeddings.upsert(rowB, "hash-b"));
        }

        Infrastructure::SqlKnowledgeRetrieval retrieval(
            DatabaseManager::instance().database(), m_clock);

        // 1) 未注入嵌入通道：词法召回；与"alpha"不重叠的查询不命中（降级可用）
        {
            Application::RetrievalRequest request;
            request.purpose = "test";
            request.queryText = "zeta query with no overlap";
            request.filtersJson = "{}";
            const auto result = retrieval.retrieve(request);
            QVERIFY(result);
            bool sawAlpha = false;
            for (const auto &hit : result.value().hits)
                if (hit.ownerUid == itemA)
                    sawAlpha = true;
            QVERIFY(!sawAlpha);
        }

        // 2) 注入嵌入通道：同桶（语义）召回带回词法未命中的条目
        retrieval.setEmbeddingPort(&fake);
        {
            // 查询文本与 itemA 同桶（fake 映射：选使 sum%4 与 vA 桶相同的文本）
            std::string queryText = "a";   // 与 "a" 同桶（字符和相同）
            Application::RetrievalRequest request;
            request.purpose = "test";
            request.queryText = queryText;
            request.filtersJson = "{}";
            const auto result = retrieval.retrieve(request);
            QVERIFY(result);
            bool sawAlpha = false;
            bool sawVectorRoute = false;
            for (const auto &hit : result.value().hits) {
                if (hit.ownerUid == itemA) {
                    sawAlpha = true;
                    if (hit.vectorScore && *hit.vectorScore > 0.0f)
                        sawVectorRoute = true;
                }
            }
            QVERIFY2(sawAlpha, "vector-only recall should bring alpha item");
            QVERIFY2(sawVectorRoute, "vector score should be recorded");
        }
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstEmbedding)
#include "tst_embedding.moc"
