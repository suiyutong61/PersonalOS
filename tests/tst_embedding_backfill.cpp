// 模块：知识条目向量回填（EmbeddingBackfill；架构 4.5.4 可重建派生数据）
// 覆盖：限量分批与幂等、归档条目跳过、版本变更重嵌入、单条失败容错继续。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include <set>

#include "application/ports/EmbeddingPort.h"
#include "application/usecases/knowledge/KnowledgeUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/EmbeddingBackfill.h"
#include "infrastructure/knowledge/SqlEmbeddingRepository.h"
#include "infrastructure/knowledge/SqlKnowledgeFtsIndex.h"
#include "infrastructure/persistence/SqlKnowledgeRepository.h"

using namespace PersonOS;

namespace {

// 可控替身：4 维 one-hot；指定文本可注入失败（容错用例用）
class FakeEmbeddingPort : public Application::EmbeddingPort
{
public:
    Application::Result<std::vector<float>, Application::ApplicationError> embed(
        const std::string &text) override
    {
        if (m_failingTexts.count(text))
            return Application::Result<std::vector<float>, Application::ApplicationError>::
                failure({Application::ErrorCode::ExternalUnavailable, "injected failure",
                         {}, true});
        int sum = 0;
        for (const unsigned char c : text)
            sum += c;
        std::vector<float> vector(4, 0.0f);
        vector[sum % 4] = 1.0f;
        return Application::Result<std::vector<float>, Application::ApplicationError>::success(
            vector);
    }
    int dimension() const override { return 4; }
    std::string modelId() const override { return "fake"; }

    std::set<std::string> m_failingTexts;
};

} // namespace

class TstEmbeddingBackfill : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_embedding_backfill.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
    }

    // 导入"方法"条目（不带端口 → 无向量行），失败返回空串
    std::string importMethod(const std::string &title, const std::string &summary,
                             const std::string &hash)
    {
        Infrastructure::SqlKnowledgeRepository repo(DatabaseManager::instance().database(),
                                                    m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(DatabaseManager::instance().database(), m_clock);
        Application::KnowledgeUseCases useCases(repo, fts, m_uids, m_clock);
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

    void backfillFillsMissingRowsWithLimit()
    {
        const auto itemA = importMethod("bf-a", "summary a", "hash-bf-a");
        const auto itemB = importMethod("bf-b", "summary b", "hash-bf-b");
        const auto itemC = importMethod("bf-c", "summary c", "hash-bf-c");
        QVERIFY(!itemA.empty() && !itemB.empty() && !itemC.empty());

        FakeEmbeddingPort fake;
        Infrastructure::EmbeddingBackfill backfill(DatabaseManager::instance().database(),
                                                   fake);
        // 限量 2：第一批只覆盖 2 条，缺行即断点续作
        const auto p1 = backfill.runOneBatch(2);
        QCOMPARE(p1.embedded, 2);
        QCOMPARE(p1.missing, 2);
        const auto p2 = backfill.runOneBatch(2);
        QCOMPARE(p2.embedded, 1);
        const auto p3 = backfill.runOneBatch(2);
        QCOMPARE(p3.embedded, 0);
        QCOMPARE(p3.missing, 0);

        Infrastructure::SqlEmbeddingRepository store(
            DatabaseManager::instance().database());
        const auto rows = store.allForModel("fake");
        QCOMPARE(rows.size(), size_t(3));
    }

    void backfillSkipsArchived()
    {
        const auto itemA = importMethod("bf-arch", "summary arch", "hash-bf-arch");
        QVERIFY(!itemA.empty());
        Infrastructure::SqlKnowledgeRepository repo(DatabaseManager::instance().database(),
                                                    m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(DatabaseManager::instance().database(), m_clock);
        Application::KnowledgeUseCases useCases(repo, fts, m_uids, m_clock);
        const auto item = repo.findItem(*Domain::Uid::parse(itemA));
        QVERIFY(item);
        QVERIFY(useCases.deprecate(*Domain::Uid::parse(itemA), item->revision,
                                   Domain::KnowledgeStatus::Archived));

        FakeEmbeddingPort fake;
        Infrastructure::EmbeddingBackfill backfill(DatabaseManager::instance().database(),
                                                   fake);
        const auto progress = backfill.runOneBatch(0);
        QCOMPARE(progress.embedded, 0);   // 归档条目不参与回填（与检索三态一致）
    }

    void backfillReembedsChangedVersion()
    {
        const auto itemA = importMethod("bf-ver", "summary v1", "hash-bf-ver-v1");
        QVERIFY(!itemA.empty());
        // 旧 hash 的行已存在（模拟此前写入）
        {
            Infrastructure::SqlEmbeddingRepository store(
                DatabaseManager::instance().database());
            Infrastructure::EmbeddingRow row;
            row.ownerType = "method";
            row.ownerUid = itemA;
            row.fieldCode = "main";
            row.modelId = "fake";
            row.dimension = 4;
            row.vector = {1.0f, 0.0f, 0.0f, 0.0f};
            QVERIFY(store.upsert(row, "hash-bf-ver-v1"));
        }
        // 追加激活版本（新内容指纹）→ 回填必须重嵌入并清理旧行
        Infrastructure::SqlKnowledgeRepository repo(DatabaseManager::instance().database(),
                                                    m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(DatabaseManager::instance().database(), m_clock);
        Application::KnowledgeUseCases useCases(repo, fts, m_uids, m_clock);
        Application::KnowledgeUseCases::VersionInput v2;
        v2.summary = "summary v2";
        v2.contentHash = "hash-bf-ver-v2";
        v2.isActive = true;
        const auto added = useCases.addVersion(*Domain::Uid::parse(itemA), 2, v2);
        if (!added)
            QFAIL(qPrintable(QString::fromStdString(added.error().message + ": "
                                                    + added.error().detail)));

        FakeEmbeddingPort fake;
        Infrastructure::EmbeddingBackfill backfill(DatabaseManager::instance().database(),
                                                   fake);
        const auto progress = backfill.runOneBatch(0);
        QCOMPARE(progress.embedded, 1);

        Infrastructure::SqlEmbeddingRepository store(
            DatabaseManager::instance().database());
        int ownRows = 0;
        for (const auto &row : store.allForOwnerType("method", "fake"))
            if (row.ownerUid == itemA) {
                ++ownRows;
                QCOMPARE(row.contentHash, std::string("hash-bf-ver-v2"));
            }
        QCOMPARE(ownRows, 1);   // 旧 hash 行已清理，只保留当前版本一行
    }

    void backfillToleratesEmbedFailure()
    {
        const auto itemA = importMethod("bf-fail-a", "summary fa", "hash-bf-fail-a");
        const auto itemB = importMethod("bf-fail-b", "summary fb", "hash-bf-fail-b");
        const auto itemC = importMethod("bf-fail-c", "summary fc", "hash-bf-fail-c");
        QVERIFY(!itemA.empty() && !itemB.empty() && !itemC.empty());

        FakeEmbeddingPort fake;
        fake.m_failingTexts.insert("bf-fail-b\nsummary fb");   // 嵌入文本 = title+"\n"+summary
        Infrastructure::EmbeddingBackfill backfill(DatabaseManager::instance().database(),
                                                   fake);
        const auto progress = backfill.runOneBatch(0);
        QCOMPARE(progress.embedded, 2);
        QCOMPARE(progress.failed, 1);

        Infrastructure::SqlEmbeddingRepository store(
            DatabaseManager::instance().database());
        bool sawA = false;
        bool sawB = false;
        bool sawC = false;
        for (const auto &row : store.allForOwnerType("method", "fake")) {
            if (row.ownerUid == itemA)
                sawA = true;
            if (row.ownerUid == itemB)
                sawB = true;
            if (row.ownerUid == itemC)
                sawC = true;
        }
        QVERIFY(sawA && sawC);
        QVERIFY(!sawB);   // 失败条目跳过，下次运行自然续作
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstEmbeddingBackfill)
#include "tst_embedding_backfill.moc"
