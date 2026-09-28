#include <QtTest>

#include <QDir>
#include <QFile>

#include <algorithm>

#include "database/DatabaseManager.h"
#include "infrastructure/ai/ModelConnectionTestService.h"
#include "infrastructure/ai/SqlAiRepository.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"

using namespace PersonOS;

namespace {
class ProbeProvider final : public Application::AiProviderPort,
                            public Application::AiEmbeddingProviderPort
{
public:
    bool textOk = true;
    bool embeddingOk = true;
    Application::ProviderResponse submit(const Domain::AiProviderConfig &,
                                         const Application::ProviderRequest &) override
    {
        if (!textOk) return {false, {}, {}, "authentication failed"};
        return {true, "connection-ok", "{\"user_text\":\"connection-ok\"}", {}};
    }
    Application::Result<std::vector<float>, Application::ApplicationError> embed(
        const Domain::AiProviderConfig &, const std::string &) override
    {
        if (!embeddingOk)
            return Application::Result<std::vector<float>, Application::ApplicationError>::failure(
                {Application::ErrorCode::ExternalUnavailable, "embedding failed", {}, true});
        return Application::Result<std::vector<float>, Application::ApplicationError>::success(
            {0.1f, 0.2f});
    }
};
}

class TstModelConnection : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_model_gate.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
    }

    void recordsRequiredCapabilitiesAndLatestResult()
    {
        Infrastructure::SqlAiRepository repo(DatabaseManager::instance().database(), m_clock);
        Domain::AiProviderConfig config;
        config.uid = m_uids.next();
        config.providerCode = "openai_compatible";
        config.displayName = "Test";
        config.endpoint = "https://example.invalid/v1";
        config.model = "model-a";
        config.credentialRef = "credential-ref";
        config.capabilitiesJson = "{\"text\":true,\"embedding\":true}";
        config.enabled = false;
        QVERIFY(repo.insertConfig(config).ok);

        ProbeProvider provider;
        Infrastructure::ModelConnectionTestService service(repo, provider, &provider,
                                                            m_uids, m_clock);
        const auto passed = service.test(config.uid);
        QVERIFY(passed.hasValue());
        QVERIFY(passed.value().overallOk);
        QVERIFY(passed.value().structuredOk);
        QVERIFY(passed.value().embeddingOk);
        const auto latest = repo.latestConnectionTest(config.uid);
        QVERIFY(latest.has_value());
        QVERIFY(latest->overallOk);
        QCOMPARE(latest->providerModel, std::string("model-a"));

        provider.embeddingOk = false;
        const auto failed = service.test(config.uid);
        QVERIFY(failed.hasValue());
        QVERIFY(!failed.value().overallOk);
        QVERIFY(!repo.latestConnectionTest(config.uid)->overallOk);
    }

    void rejectsDuplicateProviderModelConfig()
    {
        Infrastructure::SqlAiRepository repo(DatabaseManager::instance().database(), m_clock);
        const auto makeConfig = [this](const QString &model) {
            Domain::AiProviderConfig config;
            config.uid = m_uids.next();
            config.providerCode = "openai_compatible";
            config.displayName = QStringLiteral("Test %1").arg(model).toStdString();
            config.endpoint = "https://example.invalid/v1";
            config.model = model.toStdString();
            config.credentialRef = "credential-ref";
            config.capabilitiesJson = "{\"text\":true}";
            config.enabled = false;
            return config;
        };
        QVERIFY(repo.insertConfig(makeConfig(QStringLiteral("model-dup"))).ok);
        QVERIFY(repo.insertConfig(makeConfig(QStringLiteral("model-other"))).ok);
        const auto duplicate = repo.insertConfig(makeConfig(QStringLiteral("model-dup")));
        QVERIFY(!duplicate.ok);
        QVERIFY(duplicate.conflict);
        QCOMPARE(duplicate.error.code, Application::ErrorCode::Conflict);
        QCOMPARE(duplicate.error.message, std::string("provider config already exists"));
        // 不允许把数据库约束原文带进错误详情
        QVERIFY(duplicate.error.detail.empty());

        // 改模型标识撞上其他连接时同样返回稳定冲突错误
        const auto all = repo.listConfigs();
        const auto other = std::find_if(all.begin(), all.end(), [](const auto &c) {
            return c.model == "model-other";
        });
        QVERIFY(other != all.end());
        auto edited = *other;
        edited.model = "model-dup";
        const auto clash = repo.updateConfig(edited, edited.revision);
        QVERIFY(!clash.ok);
        QVERIFY(clash.conflict);
        QCOMPARE(clash.error.message, std::string("provider config already exists"));
        QVERIFY(clash.error.detail.empty());
    }

    void deleteConfigRemovesConfigAndTestFacts()
    {
        Infrastructure::SqlAiRepository repo(DatabaseManager::instance().database(), m_clock);
        Domain::AiProviderConfig config;
        config.uid = m_uids.next();
        config.providerCode = "openai_compatible";
        config.displayName = "To be deleted";
        config.endpoint = "https://example.invalid/v1";
        config.model = "model-del";
        config.credentialRef = "credential-ref";
        config.capabilitiesJson = "{\"text\":true}";
        config.enabled = false;
        QVERIFY(repo.insertConfig(config).ok);
        QVERIFY(repo.latestConnectionTest(config.uid) == std::nullopt);

        // 写入一条测试事实后删除：事实随配置同一事务清除
        ProbeProvider provider;
        Infrastructure::ModelConnectionTestService service(repo, provider, &provider,
                                                            m_uids, m_clock);
        QVERIFY(service.test(config.uid).hasValue());
        QVERIFY(repo.latestConnectionTest(config.uid).has_value());

        const auto removed = repo.deleteConfig(config.uid);
        QVERIFY2(removed.ok, removed.error.message.c_str());
        QVERIFY(repo.findConfig(config.uid) == std::nullopt);
        QVERIFY(repo.latestConnectionTest(config.uid) == std::nullopt);

        // 再次删除返回 NotFound
        const auto again = repo.deleteConfig(config.uid);
        QVERIFY(!again.ok);
        QCOMPARE(again.error.code, Application::ErrorCode::NotFound);
    }

    void defaultConfigIsPreferredAmongEnabled()
    {
        Infrastructure::SqlAiRepository repo(DatabaseManager::instance().database(), m_clock);
        const auto makeConfig = [this](const QString &model) {
            Domain::AiProviderConfig config;
            config.uid = m_uids.next();
            config.providerCode = "openai_compatible";
            config.displayName = QStringLiteral("Test %1").arg(model).toStdString();
            config.endpoint = "https://example.invalid/v1";
            config.model = model.toStdString();
            config.credentialRef = "credential-ref";
            config.capabilitiesJson = "{\"text\":true}";
            config.enabled = true;
            return config;
        };
        const auto first = makeConfig(QStringLiteral("model-def-a"));
        const auto second = makeConfig(QStringLiteral("model-def-b"));
        QVERIFY(repo.insertConfig(first).ok);
        QVERIFY(repo.insertConfig(second).ok);

        QVERIFY(repo.findFirstEnabledConfig().has_value());
        QCOMPARE(repo.findFirstEnabledConfig()->uid.value(), first.uid.value());

        const auto setB = repo.setDefaultConfig(second.uid);
        QVERIFY2(setB.ok, setB.error.message.c_str());
        QVERIFY(repo.findConfig(second.uid)->isDefault);
        QVERIFY(!repo.findConfig(first.uid)->isDefault);
        QCOMPARE(repo.findFirstEnabledConfig()->uid.value(), second.uid.value());

        // 切换默认:旧的自动清除
        const auto setA = repo.setDefaultConfig(first.uid);
        QVERIFY2(setA.ok, setA.error.message.c_str());
        QVERIFY(repo.findConfig(first.uid)->isDefault);
        QVERIFY(!repo.findConfig(second.uid)->isDefault);

        // 不存在的连接返回 NotFound
        const auto missing = repo.setDefaultConfig(m_uids.next());
        QVERIFY(!missing.ok);
        QCOMPARE(missing.error.code, Application::ErrorCode::NotFound);
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstModelConnection)
#include "tst_model_connection.moc"
