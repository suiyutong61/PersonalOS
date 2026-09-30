// 模块：领域向量持久化（DomainClassifier ↔ embedding_records_v6）
// 覆盖：库中 43 行指纹一致时直接装载（免重算）、空库首次分类落库、
//       指纹不匹配时强制重算并重写；全程确定性 fake provider，离线运行。
#include <QtTest>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlQuery>

#include "application/ports/EmbeddingPort.h"
#include "database/DatabaseManager.h"
#include "infrastructure/embedding/DomainClassifier.h"
#include "infrastructure/knowledge/SqlEmbeddingRepository.h"

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
        std::vector<float> vector(4, 0.0f);
        vector[sum % 4] = 1.0f;
        return Application::Result<std::vector<float>, Application::ApplicationError>::success(
            vector);
    }
    int dimension() const override { return 4; }
    std::string modelId() const override { return "fake"; }
};

} // namespace

class TstDomainSeed : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_domain_seed.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));

        const QByteArray raw = readDomainsJson();
        QVERIFY2(!raw.isEmpty(), "domains.json not deployed (POST_BUILD copy)");
        m_domainsHash =
            QCryptographicHash::hash(raw, QCryptographicHash::Sha256).toHex();
        QCOMPARE(domainCodes(raw).size(), 43);

        Infrastructure::DomainClassifier::configureDatabasePath(
            DatabaseManager::instance().databasePath());
        Infrastructure::DomainClassifier::instance().setProviderForTesting(&m_fake);
    }

    void seededWrongVectorsDriveSuggestions()
    {
        // 预种 43 行（正确指纹、刻意构造的向量：仅 06 与查询同桶）——
        // suggest 若装载自库则 top1 必为 06；若按描述重算则结果不同
        Infrastructure::SqlEmbeddingRepository store(
            DatabaseManager::instance().database());
        QVERIFY(store.removeForModel("fake"));
        const QByteArray raw = readDomainsJson();
        const QStringList codes = domainCodes(raw);
        const auto queryVector = m_fake.embed("x").value();   // sum("x")=120%4=0
        const auto otherVector = m_fake.embed("y").value();   // sum("y")=121%4=1
        for (const QString &code : codes) {
            Infrastructure::EmbeddingRow row;
            row.ownerType = "domain";
            row.ownerUid = code.toStdString();
            row.fieldCode = "description";
            row.modelId = "fake";
            row.dimension = 4;
            row.vector = code == QStringLiteral("06") ? queryVector : otherVector;
            QVERIFY(store.upsert(row, m_domainsHash.toStdString()));
        }

        Infrastructure::DomainClassifier::instance().resetForTesting();
        const auto suggestions =
            Infrastructure::DomainClassifier::instance().suggest(QStringLiteral("x"), 3);
        QVERIFY(!suggestions.isEmpty());
        QCOMPARE(suggestions.first().code, QStringLiteral("06"));
        QVERIFY(suggestions.first().score > 0.99);
    }

    void suggestPersistsDomainRowsWhenEmpty()
    {
        // 空库：首次分类计算 43 个领域向量并落库（后续启动直接装载）
        Infrastructure::SqlEmbeddingRepository store(
            DatabaseManager::instance().database());
        QVERIFY(store.removeForModel("fake"));

        Infrastructure::DomainClassifier::instance().resetForTesting();
        const auto suggestions =
            Infrastructure::DomainClassifier::instance().suggest(QStringLiteral("x"), 3);
        QVERIFY(!suggestions.isEmpty());

        const auto rows = store.allForOwnerType("domain", "fake");
        QCOMPARE(rows.size(), size_t(43));
        for (const auto &row : rows) {
            QCOMPARE(row.contentHash, m_domainsHash.toStdString());
            QCOMPARE(row.dimension, 4);
        }
    }

    void mismatchedHashForcesRecompute()
    {
        // 指纹不匹配：必须重算并重写为当前 domains.json 的正确指纹
        QSqlQuery update(DatabaseManager::instance().database());
        QVERIFY(update.exec(QStringLiteral(
            "UPDATE embedding_records_v6 SET content_hash='stale' WHERE owner_type='domain'")));

        Infrastructure::DomainClassifier::instance().resetForTesting();
        const auto suggestions =
            Infrastructure::DomainClassifier::instance().suggest(QStringLiteral("x"), 3);
        QVERIFY(!suggestions.isEmpty());

        Infrastructure::SqlEmbeddingRepository store(
            DatabaseManager::instance().database());
        const auto rows = store.allForOwnerType("domain", "fake");
        QCOMPARE(rows.size(), size_t(43));
        for (const auto &row : rows)
            QCOMPARE(row.contentHash, m_domainsHash.toStdString());
    }

private:
    QByteArray readDomainsJson() const
    {
        QFile file(QCoreApplication::applicationDirPath()
                   + QStringLiteral("/embedding/domains.json"));
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }

    QStringList domainCodes(const QByteArray &raw) const
    {
        QStringList codes;
        const QJsonDocument document = QJsonDocument::fromJson(raw);
        for (const auto &value : document.array())
            codes.append(value.toObject().value(QStringLiteral("code")).toString());
        return codes;
    }

    QString m_path;
    QByteArray m_domainsHash;
    FakeEmbeddingPort m_fake;
};

QTEST_GUILESS_MAIN(TstDomainSeed)
#include "tst_domain_seed.moc"
