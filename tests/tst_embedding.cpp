// 模块：向量检索通道（DR-013/014；architecture 4.5.4 混合检索）
// 覆盖：嵌入存储 upsert/读取、余弦相似度、向量独有召回（词法不重叠）、
//       混合命中融合与跨通道重排、模型空间隔离、领域行排除、状态过滤、
//       未注入嵌入通道时降级为结构化+全文（不阻断查询）。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "application/ports/EmbeddingPort.h"
#include "application/usecases/knowledge/KnowledgeUseCases.h"
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
    std::string modelId() const override { return "fake"; }
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

    // 导入一个"方法"条目并返回 uid（createdBy=generated 免来源）；失败返回空串
    std::string importMethod(Application::KnowledgeUseCases &useCases,
                             const std::string &title, const std::string &summary,
                             const std::string &hash)
    {
        Application::KnowledgeUseCases::ImportInput input;
        input.libraryType = Domain::LibraryType::Method;
        input.title = title;
        input.domainCode = "learning";
        input.summary = summary;
        input.contentHash = hash;
        input.createdBy = "generated";
        const auto imported = useCases.importKnowledge(input);
        if (!imported) {
            qWarning("importMethod failed: %s",
                     (imported.error().message + ": " + imported.error().detail).c_str());
            return {};
        }
        return imported.value().item.uid.value();
    }

    void vectorOnlyRecallAndDegradation()
    {
        Infrastructure::SqlKnowledgeRepository knowledge(
            DatabaseManager::instance().database(), m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(DatabaseManager::instance().database(), m_clock);
        Infrastructure::SqlEmbeddingRepository embeddings(
            DatabaseManager::instance().database());
        FakeEmbeddingPort fake;
        Application::KnowledgeUseCases useCases(knowledge, fts, m_uids, m_clock, &fake,
                                                &embeddings);
        // 桶设计：itemA 索引文本 "aa\na"（sum=301%4=1），itemB "bb\nbb"（402%4=2）
        const auto itemA = importMethod(useCases, "aa", "a", "hash-a");
        const auto itemB = importMethod(useCases, "bb", "bb", "hash-b");
        QVERIFY(!itemA.empty() && !itemB.empty());
        // 导入即写入向量行（field_code="main"、model_id="fake"）
        {
            QSqlQuery count(DatabaseManager::instance().database());
            QVERIFY(count.exec(QStringLiteral(
                "SELECT COUNT(*) FROM embedding_records_v6 WHERE owner_uid='%1'"
            ).arg(QString::fromStdString(itemA))));
            QVERIFY(count.next());
            QCOMPARE(count.value(0).toInt(), 1);
        }

        Infrastructure::SqlKnowledgeRetrieval retrieval(
            DatabaseManager::instance().database(), m_clock);

        // 1) 未注入嵌入通道：词法召回；无词法重叠的查询不命中（降级可用）
        {
            Application::RetrievalRequest request;
            request.purpose = "test";
            request.queryText = "zeta query with no overlap";
            request.filtersJson = "{}";
            const auto result = retrieval.retrieve(request);
            QVERIFY(result);
            QVERIFY(result.value().hits.empty());
        }

        // 2) 注入嵌入通道：同桶（语义）召回带回词法未命中的条目
        retrieval.setEmbeddingPort(&fake);
        {
            Application::RetrievalRequest request;
            request.purpose = "test";
            request.queryText = "a";   // 桶 1，与 itemA 同桶且无词法重叠
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

    void domainRowsExcludedFromRecall()
    {
        Infrastructure::SqlKnowledgeRepository knowledge(
            DatabaseManager::instance().database(), m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(DatabaseManager::instance().database(), m_clock);
        Infrastructure::SqlEmbeddingRepository embeddings(
            DatabaseManager::instance().database());
        FakeEmbeddingPort fake;
        Application::KnowledgeUseCases useCases(knowledge, fts, m_uids, m_clock, &fake,
                                                &embeddings);
        // "cc\nc"（sum=307%4=3）；查询 "c"（99%4=3）同桶
        const auto itemA = importMethod(useCases, "cc", "c", "hash-c");

        // 领域行与查询同桶：不得作为知识命中（'domain' 行仅领域分类专用）
        Infrastructure::EmbeddingRow domainRow;
        domainRow.ownerType = "domain";
        domainRow.ownerUid = "01";
        domainRow.fieldCode = "description";
        domainRow.modelId = "fake";
        domainRow.dimension = 4;
        domainRow.vector = fake.embed("c").value();
        QVERIFY(embeddings.upsert(domainRow, "domains-hash"));

        Infrastructure::SqlKnowledgeRetrieval retrieval(
            DatabaseManager::instance().database(), m_clock);
        retrieval.setEmbeddingPort(&fake);
        Application::RetrievalRequest request;
        request.purpose = "test";
        request.queryText = "c";
        request.filtersJson = "{}";
        const auto result = retrieval.retrieve(request);
        QVERIFY(result);
        bool sawDomain = false;
        bool sawAlpha = false;
        for (const auto &hit : result.value().hits) {
            if (hit.ownerType == "domain" || hit.ownerUid == "01")
                sawDomain = true;
            if (hit.ownerUid == itemA)
                sawAlpha = true;
        }
        QVERIFY2(!sawDomain, "domain rows must never enter retrieval hits");
        QVERIFY2(sawAlpha, "knowledge item of same bucket should still be recalled");
    }

    void modelIsolation()
    {
        Infrastructure::SqlKnowledgeRepository knowledge(
            DatabaseManager::instance().database(), m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(DatabaseManager::instance().database(), m_clock);
        Infrastructure::SqlEmbeddingRepository embeddings(
            DatabaseManager::instance().database());
        FakeEmbeddingPort fake;
        Application::KnowledgeUseCases useCases(knowledge, fts, m_uids, m_clock, &fake,
                                                &embeddings);
        // "dd\nd"（sum=310%4=2）；查询 "b"（98%4=2）同桶
        const auto itemA = importMethod(useCases, "dd", "d", "hash-d");
        // itemB 不带向量导入（无端口），随后手写一行"其他模型"向量：
        // 检索只与注入模型("fake")空间计算，其他模型行不得混入
        Application::KnowledgeUseCases plain(knowledge, fts, m_uids, m_clock);
        const auto itemB = importMethod(plain, "ee", "e", "hash-e");
        Infrastructure::EmbeddingRow otherRow;
        otherRow.ownerType = "method";
        otherRow.ownerUid = itemB;
        otherRow.fieldCode = "main";
        otherRow.modelId = "other-model";
        otherRow.dimension = 4;
        otherRow.vector = fake.embed("b").value();
        QVERIFY(embeddings.upsert(otherRow, "hash-e"));

        Infrastructure::SqlKnowledgeRetrieval retrieval(
            DatabaseManager::instance().database(), m_clock);
        retrieval.setEmbeddingPort(&fake);
        Application::RetrievalRequest request;
        request.purpose = "test";
        request.queryText = "b";
        request.filtersJson = "{}";
        const auto result = retrieval.retrieve(request);
        QVERIFY(result);
        bool sawA = false;
        bool sawB = false;
        for (const auto &hit : result.value().hits) {
            if (hit.ownerUid == itemA)
                sawA = true;
            if (hit.ownerUid == itemB)
                sawB = true;
        }
        QVERIFY2(sawA, "same-model vector row should be recalled");
        QVERIFY2(!sawB, "other-model vector row must not enter this model space");
    }

    void eligibleStatusFilter()
    {
        Infrastructure::SqlKnowledgeRepository knowledge(
            DatabaseManager::instance().database(), m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(DatabaseManager::instance().database(), m_clock);
        Infrastructure::SqlEmbeddingRepository embeddings(
            DatabaseManager::instance().database());
        FakeEmbeddingPort fake;
        Application::KnowledgeUseCases useCases(knowledge, fts, m_uids, m_clock, &fake,
                                                &embeddings);
        // "ff\nf"（sum=316%4=0）；查询 "d"（100%4=0）同桶
        const auto itemA = importMethod(useCases, "ff", "f", "hash-f");
        // 归档后向量行仍在，但 eligibility 过滤使归档条目不再浮现（与词法一致）
        const auto item = knowledge.findItem(*Domain::Uid::parse(itemA));
        QVERIFY(item);
        QVERIFY(useCases.deprecate(*Domain::Uid::parse(itemA), item->revision,
                                   Domain::KnowledgeStatus::Archived));

        Infrastructure::SqlKnowledgeRetrieval retrieval(
            DatabaseManager::instance().database(), m_clock);
        retrieval.setEmbeddingPort(&fake);
        Application::RetrievalRequest request;
        request.purpose = "test";
        request.queryText = "d";
        request.filtersJson = "{}";
        const auto result = retrieval.retrieve(request);
        QVERIFY(result);
        for (const auto &hit : result.value().hits)
            QVERIFY2(hit.ownerUid != itemA, "archived item must not surface via vectors");
    }

    void hybridRerankByFinalScore()
    {
        Infrastructure::SqlKnowledgeRepository knowledge(
            DatabaseManager::instance().database(), m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(DatabaseManager::instance().database(), m_clock);
        Infrastructure::SqlEmbeddingRepository embeddings(
            DatabaseManager::instance().database());
        FakeEmbeddingPort fake;
        Application::KnowledgeUseCases useCases(knowledge, fts, m_uids, m_clock, &fake,
                                                &embeddings);
        // 查询 "gg"：词法命中 itemA（bm25>0 ⇒ finalScore<1），向量桶 2 命中 itemB
        // （cosine=1.0）。itemA 桶 3（"gg\ng"=319%4=3）、itemB 桶 2（"hh\nh"=322%4=2）、
        // 填充条目桶 1/0 不参与。跨通道重排后 itemB 应排第一。
        const auto itemA = importMethod(useCases, "gg", "g", "hash-g");
        const auto itemB = importMethod(useCases, "hh", "h", "hash-h");
        importMethod(useCases, "ii", "i", "hash-i");
        importMethod(useCases, "jj", "j", "hash-j");

        Infrastructure::SqlKnowledgeRetrieval retrieval(
            DatabaseManager::instance().database(), m_clock);
        retrieval.setEmbeddingPort(&fake);
        Application::RetrievalRequest request;
        request.purpose = "test";
        request.queryText = "gg";
        request.filtersJson = "{}";
        const auto result = retrieval.retrieve(request);
        QVERIFY(result);
        // 断言限定在本用例条目上（历史用例条目可能同桶，不参与断言）
        const auto hitOf = [&](const std::string &uid) -> const Domain::RetrievalHit * {
            for (const auto &hit : result.value().hits)
                if (hit.ownerUid == uid)
                    return &hit;
            return nullptr;
        };
        const auto *hitB = hitOf(itemB);
        const auto *hitA = hitOf(itemA);
        QVERIFY2(hitB, "itemB should be recalled by vector");
        QVERIFY2(hitA, "itemA should be recalled by fts");
        QVERIFY(hitB->reasonJson.find("vector_only") != std::string::npos);
        QVERIFY(hitA->reasonJson.find("fts_lexical") != std::string::npos);
        QVERIFY2(hitB->finalScore > hitA->finalScore,
                 "vector-only hit must outrank pure lexical hit after rerank");
        // 全列表严格按 finalScore 降序（跨通道重排）
        for (size_t i = 1; i < result.value().hits.size(); ++i)
            QVERIFY(result.value().hits[i - 1].finalScore >= result.value().hits[i].finalScore);
        // rank 与顺序一致
        for (size_t i = 0; i < result.value().hits.size(); ++i)
            QCOMPARE(result.value().hits[i].rank, static_cast<int>(i + 1));
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstEmbedding)
#include "tst_embedding.moc"
