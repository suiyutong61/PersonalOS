// IMP-006a：AI 网关（DD-001 §9；DR-015/016/025；DB-06 幂等）
// 覆盖：连接配置（凭据引用不存密钥）、任务登记幂等、执行成功/验证失败重试/
// 终态、调用与上下文落库、决策记录（来源模式三档 + 用户决定）。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "database/DatabaseManager.h"
#include "application/foundation/ContractValidator.h"
#include "infrastructure/ai/AiGateway.h"
#include "infrastructure/ai/SqlAiRepository.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"

using namespace PersonOS;

namespace {
// 可脚本化 Fake Provider：按脚本依次返回响应
class FakeProvider final : public Application::AiProviderPort
{
public:
    struct ScriptedResponse
    {
        bool ok = false;
        std::string structuredJson;
        std::string errorMessage;
    };
    std::vector<ScriptedResponse> script;

    Application::ProviderResponse submit(const Domain::AiProviderConfig &,
                                         const Application::ProviderRequest &) override
    {
        ++calls;
        if (script.empty())
            return {false, {}, "provider unavailable", ""};
        const auto next = script.front();
        script.erase(script.begin());
        return {next.ok, "", next.structuredJson, next.errorMessage};
    }

    int calls = 0;
};
} // namespace

class TstAiGateway : public QObject
{
    Q_OBJECT

private slots:
    void paperAnalysisContractRequiresEvidenceAndUnknowns()
    {
        const std::string valid = R"JSON({"title":"Paper","summary":"Summary","study_type":"review","core_claims":[],"applicability":[],"limitations":[],"credibility":{"grade":"unknown","reason":"insufficient data"},"candidate_methods":[],"candidate_tips":[],"evidence_fragments":[],"unknowns":["population"]})JSON";
        QVERIFY(!Application::ContractValidator::validate("paper_analysis_v1", valid));
        const std::string missingEvidence = R"JSON({"title":"Paper","summary":"Summary","study_type":"review","core_claims":[],"applicability":[],"limitations":[],"credibility":{},"candidate_methods":[],"candidate_tips":[],"unknowns":[]})JSON";
        const auto error = Application::ContractValidator::validate(
            "paper_analysis_v1", missingEvidence);
        QVERIFY(error.has_value());
        QVERIFY(error->find("evidence_fragments") != std::string::npos);
    }
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_ai_gateway.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
    }

    void configStoresOnlyCredentialRef()
    {
        Infrastructure::SqlAiRepository repo(DatabaseManager::instance().database(), m_clock);
        Domain::AiProviderConfig config;
        config.uid = m_uids.next();
        config.providerCode = "deepseek";
        config.displayName = "DeepSeek 预设";
        config.endpoint = "https://api.deepseek.com";
        config.model = "deepseek-chat";
        config.credentialRef = "personos/ai/deepseek";
        config.capabilitiesJson = std::string("{\"text\":true,\"structured\":true}");
        const auto saved = repo.insertConfig(config);
        QVERIFY2(saved.ok,
                 qPrintable(QString::fromStdString(saved.error.message + ": "
                                                   + saved.error.detail)));
        m_configUid = config.uid;
        // 凭据引用存在但绝无密钥列
        QSqlQuery query(DatabaseManager::instance().database());
        QVERIFY(query.exec(QStringLiteral("PRAGMA table_info(ai_provider_configs_v6)")));
        while (query.next())
            QVERIFY(query.value(1).toString() != QStringLiteral("api_key"));
        // 停用连接不可执行
        config.enabled = false;
        QVERIFY(repo.updateConfig(config, 1).ok);
        QVERIFY(!repo.findEnabledConfig(m_configUid).has_value());
        config.enabled = true;
        QVERIFY(repo.updateConfig(config, 2).ok);
    }

    void submitIsIdempotent()
    {
        Infrastructure::SqlAiRepository repo(DatabaseManager::instance().database(), m_clock);
        FakeProvider provider;
        Infrastructure::AiGateway gateway(repo, provider, m_uids, m_clock);

        Application::AiGatewaySubmit input;
        input.providerConfigUid = m_configUid;
        input.jobType = "mel_proposal";
        input.contractType = "mel_proposal_v1";
        input.contractVersion = "1";
        input.requestJson = std::string("{\"goal\":\"操作系统\"}");
        input.idempotencyKey = "job:1:first";
        input.sourceMode = Domain::SourceMode::KnowledgeGrounded;
        input.contextItems = {{"knowledge", "k-uid-1", "v-uid-1", 0, "检索命中"}};

        const auto submitted = gateway.submit(input);
        if (!submitted)
            QFAIL(qPrintable(QString::fromStdString(submitted.error().message + ": "
                                                    + submitted.error().detail)));
        QVERIFY(submitted.value().status == Domain::AiJobStatus::Queued);
        // 幂等键重复 → 复用既有任务（真实使用中重复提交是常态，不报错）
        const auto duplicate = gateway.submit(input);
        if (!duplicate)
            QFAIL(qPrintable(QString::fromStdString(duplicate.error().message + ": "
                                                    + duplicate.error().detail)));
        QCOMPARE(duplicate.value().uid.value(), submitted.value().uid.value());
        QCOMPARE(duplicate.value().idempotencyKey, submitted.value().idempotencyKey);
    }

    void executeSuccessAndContextPersistence()
    {
        Infrastructure::SqlAiRepository repo(DatabaseManager::instance().database(), m_clock);
        FakeProvider provider;
        provider.script.push_back(
            {true,
             "{\"title\":\"第一轮\",\"tasks\":[{\"title\":\"绪论\"}],\"rationale\":\"r\","
             "\"capacity_min\":300,\"reserve_min\":30,\"period_days\":3}",
             ""});
        Infrastructure::AiGateway gateway(repo, provider, m_uids, m_clock);

        Application::AiGatewaySubmit input;
        input.providerConfigUid = m_configUid;
        input.jobType = "mel_proposal";
        input.contractType = "mel_proposal_v1";
        input.contractVersion = "1";
        input.requestJson = std::string("{\"goal\":\"操作系统\"}");
        input.idempotencyKey = "job:2:success";
        input.sourceMode = Domain::SourceMode::KnowledgeGrounded;
        input.contextItems = {{"knowledge", "k-uid-2", "v-uid-2", 0, "检索命中"}};

        const auto submitted = gateway.submit(input);
        QVERIFY(submitted);
        const auto executed = gateway.execute(submitted.value().uid);
        if (!executed)
            QFAIL(qPrintable(QString::fromStdString(executed.error().message + ": "
                                                    + executed.error().detail)));
        QVERIFY(executed.value().status == Domain::AiJobStatus::Completed);
        QVERIFY(executed.value().resultJson.has_value());

        // 调用与上下文落库
        QSqlQuery call(DatabaseManager::instance().database());
        call.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM ai_calls_v6 WHERE job_id="
            "(SELECT id FROM ai_jobs_v6 WHERE uid=?)"));
        call.addBindValue(QString::fromStdString(submitted.value().uid.value()));
        QVERIFY(call.exec() && call.next());
        QCOMPARE(call.value(0).toInt(), 1);

        QSqlQuery context(DatabaseManager::instance().database());
        context.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM ai_context_items_v6 WHERE call_id="
            "(SELECT id FROM ai_calls_v6 WHERE job_id="
            "(SELECT id FROM ai_jobs_v6 WHERE uid=?))"));
        context.addBindValue(QString::fromStdString(submitted.value().uid.value()));
        QVERIFY(context.exec() && context.next());
        QCOMPARE(context.value(0).toInt(), 1);
    }

    void validationFailureRetriesThenTerminal()
    {
        Infrastructure::SqlAiRepository repo(DatabaseManager::instance().database(), m_clock);
        FakeProvider provider;
        // 前两次返回非法结构（缺必填字段），第三次成功
        provider.script.push_back({true, "{\"title\":\"缺字段\"}", ""});
        provider.script.push_back({true, "{\"title\":\"缺字段\"}", ""});
        provider.script.push_back(
            {true,
             "{\"title\":\"第一轮\",\"tasks\":[],\"rationale\":\"r\",\"capacity_min\":300,"
             "\"reserve_min\":30,\"period_days\":3}",
             ""});
        Infrastructure::AiGateway gateway(repo, provider, m_uids, m_clock);

        Application::AiGatewaySubmit input;
        input.providerConfigUid = m_configUid;
        input.jobType = "mel_proposal";
        input.contractType = "mel_proposal_v1";
        input.contractVersion = "1";
        input.requestJson = std::string("{\"goal\":\"操作系统\"}");
        input.idempotencyKey = "job:3:retry";
        input.sourceMode = Domain::SourceMode::Ungrounded;
        input.maxAttempts = 3;

        const auto submitted = gateway.submit(input);
        QVERIFY(submitted);
        const auto uid = submitted.value().uid;

        // 第 1 次：验证失败 → failed_retryable + 退避时间
        const auto first = gateway.execute(uid);
        QVERIFY(first);
        QVERIFY(first.value().status == Domain::AiJobStatus::FailedRetryable);
        QVERIFY(first.value().nextAttemptAt.has_value());
        QCOMPARE(first.value().attemptCount, 1);

        // 第 2 次：仍失败 → 仍可重试
        const auto second = gateway.execute(uid);
        QVERIFY(second && second.value().status == Domain::AiJobStatus::FailedRetryable);
        QCOMPARE(second.value().attemptCount, 2);

        // 第 3 次：成功
        const auto third = gateway.execute(uid);
        QVERIFY(third && third.value().status == Domain::AiJobStatus::Completed);

        // 重试上限：另一任务 1 次机会，失败即终态
        FakeProvider failing;
        failing.script.push_back({true, "{\"broken\":true}", ""});
        Infrastructure::AiGateway failingGateway(repo, failing, m_uids, m_clock);
        Application::AiGatewaySubmit terminalInput = input;
        terminalInput.idempotencyKey = "job:4:terminal";
        terminalInput.maxAttempts = 1;
        const auto terminalJob = failingGateway.submit(terminalInput);
        QVERIFY(terminalJob);
        const auto terminalResult = failingGateway.execute(terminalJob.value().uid);
        QVERIFY(terminalResult
                && terminalResult.value().status == Domain::AiJobStatus::FailedTerminal);
    }

    void decisionRecordSourceModeAndUserDecision()
    {
        Infrastructure::SqlAiRepository repo(DatabaseManager::instance().database(), m_clock);

        Domain::DecisionRecord decision;
        decision.uid = m_uids.next();
        decision.decisionType = "mel_proposal";
        decision.aggregateType = "goal";
        decision.aggregateUid = "00000000-0000-0000-0000-0000000000cc";
        decision.inputSnapshotJson = std::string("{\"state\":\"fresh\"}");
        decision.candidateJson = std::string("{\"period_days\":3}");
        decision.rationale = QStringLiteral("依据检索到的间隔复习方法安排").toStdString();
        decision.sourceMode = Domain::SourceMode::KnowledgeGrounded;
        decision.userStatus = Domain::DecisionUserStatus::Pending;
        decision.createdAt = m_clock.utcIso();
        QVERIFY(repo.insertDecision(decision).ok);

        // 用户接受
        QVERIFY(repo.updateDecisionStatus(decision.uid, "accepted",
                                          std::string("{\"period_days\":3}"))
                    .ok);
        const auto after = repo.findDecision(decision.uid);
        QVERIFY(after.has_value());
        QVERIFY(after->userStatus == Domain::DecisionUserStatus::Accepted);
        QVERIFY(after->selectedJson.has_value());
        QVERIFY(after->confirmedAt.has_value());
        QVERIFY(after->sourceMode == Domain::SourceMode::KnowledgeGrounded);
    }

private:
    QString m_path;
    Domain::Uid m_configUid;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstAiGateway)
#include "tst_ai_gateway.moc"
