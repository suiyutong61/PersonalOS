#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlQuery>

#include <algorithm>

#include "application/usecases/knowledge/KnowledgeUseCases.h"
#include "application/usecases/knowledge/PaperAnalysisUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/SqlKnowledgeFtsIndex.h"
#include "infrastructure/persistence/SqlKnowledgeRepository.h"

using namespace PersonOS;

namespace {
class FakeGateway final : public Application::AiGatewayPort
{
public:
    std::string result;
    QStringList results;   // 非空时按块序取用(复现真实多块响应)
    bool correctContract = false;
    int submitCount = 0;
    int largestPaperText = 0;
    Application::Result<Domain::AiJob, Application::ApplicationError> submit(
        const Application::AiGatewaySubmit &input) override
    {
        ++submitCount;
        correctContract = input.contractType == "paper_analysis_v1";
        const auto request = QJsonDocument::fromJson(
            QByteArray::fromStdString(input.requestJson)).object();
        largestPaperText = std::max(largestPaperText,
                                    static_cast<int>(request.value(
                                        QStringLiteral("paper_text")).toString().size()));
        Domain::AiJob job;
        job.uid = *Domain::Uid::parse("11111111-1111-4111-8111-111111111111");
        job.jobType = input.jobType;
        job.schemaVersion = input.contractVersion;
        job.idempotencyKey = input.idempotencyKey;
        return Application::Result<Domain::AiJob, Application::ApplicationError>::success(job);
    }
    Application::Result<Domain::AiJob, Application::ApplicationError> execute(
        const Domain::Uid &uid) override
    {
        Domain::AiJob job;
        job.uid = uid;
        job.jobType = "paper_analysis";
        job.status = Domain::AiJobStatus::Completed;
        job.schemaVersion = "1";
        job.idempotencyKey = "analysis:test";
        job.resultJson = results.isEmpty()
                             ? result
                             : results.at((submitCount - 1) % results.size())
                                   .toStdString();
        return Application::Result<Domain::AiJob, Application::ApplicationError>::success(job);
    }
};
} // namespace

class TstPaperAnalysis : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // 诊断支持:PERSONOS_TEST_DB_PATH 指定外部库(产品库副本复现用)
        const QString overridePath = qEnvironmentVariable("PERSONOS_TEST_DB_PATH");
        if (overridePath.isEmpty()) {
            m_path = QDir::temp().filePath(QStringLiteral("personos_paper_analysis.db"));
            QFile::remove(m_path);
            QFile::remove(m_path + QStringLiteral("-wal"));
            QFile::remove(m_path + QStringLiteral("-shm"));
        } else {
            m_path = overridePath;
        }
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
    }

    void analysisCreatesDraftEvidenceAndCandidates()
    {
        auto db = DatabaseManager::instance().database();
        Infrastructure::SqlKnowledgeRepository repo(db, m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(db, m_clock);
        Application::KnowledgeUseCases knowledge(repo, fts, m_uids, m_clock);

        Domain::SourceRecord source;
        source.uid = m_uids.next();
        source.sourceType = "url";
        source.canonicalUri = "https://example.org/paper.pdf";
        source.title = "Source paper";
        source.accessedAt = m_clock.utcIso();
        source.contentHash = "source-hash";
        source.trustTier = "unverified";
        QVERIFY(repo.insertSource(source).ok);

        Application::KnowledgeUseCases::ImportInput paperInput;
        paperInput.libraryType = Domain::LibraryType::Paper;
        paperInput.title = "Source paper";
        paperInput.domainCode = "research.04.learning-science";
        paperInput.summary = "Raw extracted paper text";
        paperInput.contentHash = "paper-hash";
        paperInput.createdBy = "generated";
        Domain::PaperDetail paperDetail;
        paperDetail.studyType = "experiment";
        paperInput.paper = paperDetail;
        const auto imported = knowledge.importKnowledge(paperInput);
        QVERIFY2(imported.hasValue(), imported.hasValue() ? "" : imported.error().message.c_str());

        FakeGateway gateway;
        gateway.result = R"JSON({
          "title":"Source paper","summary":"Retrieval practice improves retention.",
          "study_type":"experiment",
          "core_claims":[{"text":"Retrieval improves delayed retention","evidence_refs":["e1"]}],
          "applicability":[{"population":"students"}],
          "limitations":[{"text":"Single course context"}],
          "credibility":{"grade":"B","reason":"controlled experiment"},
          "candidate_methods":[{"title":"Retrieval practice","summary":"Recall before review","steps":["Attempt recall","Check answer"],"risk_level":"low","evidence_grade":"B"}],
          "candidate_tips":[{"title":"Recall first","claim":"Try recalling before rereading","risk_level":"low","use_scenario":{"task":"study"}}],
          "evidence_fragments":[{"text":"students retained more after retrieval practice","locator":{"page":3},"hash":"fragment-1","relation":"supports","strength":0.8}],
          "unknowns":["long-term generalization"]
        })JSON";
        Application::PaperAnalysisUseCases useCases(gateway, repo, knowledge, m_uids, m_clock);
        Application::PaperAnalysisUseCases::AnalyzeInput input;
        input.paperItemUid = imported.value().item.uid;
        input.sourceUid = source.uid;
        input.providerConfigUid = m_uids.next();
        input.title = "Source paper";
        input.extractedText = "full paper text";
        input.analysisContentHash = "analysis-hash";
        input.modelVersion = "test-model-v1";
        input.idempotencyKey = "analysis:test";
        const auto output = useCases.analyze(input);
        QVERIFY2(output.hasValue(), output.hasValue() ? "" : output.error().message.c_str());
        QVERIFY(gateway.correctContract);
        QVERIFY(output.value().paperVersion.status == Domain::KnowledgeStatus::Draft);
        QCOMPARE(output.value().evidenceFragmentCount, 1);
        QCOMPARE(output.value().analyzedChunkCount, 1);
        QCOMPARE(output.value().candidateMethods.size(), size_t(1));
        QCOMPARE(output.value().candidateTips.size(), size_t(1));
        QVERIFY(output.value().candidateMethods.front().status
                == Domain::KnowledgeStatus::Candidate);
        QVERIFY(output.value().candidateTips.front().status
                == Domain::KnowledgeStatus::Candidate);

        QSqlQuery count(db);
        QVERIFY(count.exec(QStringLiteral("SELECT COUNT(*) FROM evidence_links_v5")));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), 1);
        QVERIFY(count.exec(QStringLiteral(
            "SELECT COUNT(*) FROM knowledge_relations_v5 WHERE relation='derived_from'")));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), 2);

        Application::PaperAnalysisUseCases::RetractInput retract;
        retract.paperItemUid = imported.value().item.uid;
        retract.analysisVersionUid = output.value().paperVersion.uid;
        for (const auto &candidate : output.value().candidateMethods)
            retract.candidateItemUids.push_back(candidate.uid);
        for (const auto &candidate : output.value().candidateTips)
            retract.candidateItemUids.push_back(candidate.uid);
        const auto retracted = useCases.retract(retract);
        QVERIFY(retracted.hasValue());
        QCOMPARE(retracted.value().archivedCandidateCount, 2);
        QCOMPARE(retracted.value().retainedAnalysisVersionUid,
                 output.value().paperVersion.uid);
        for (const auto &candidateUid : retract.candidateItemUids) {
            const auto archived = repo.findItem(candidateUid);
            QVERIFY(archived.has_value());
            QVERIFY(archived->status == Domain::KnowledgeStatus::Archived);
        }
        const auto retainedVersions = repo.versionsOf(imported.value().item.uid);
        QVERIFY(std::any_of(retainedVersions.begin(), retainedVersions.end(),
                            [&](const auto &value) {
            return value.uid == output.value().paperVersion.uid
                   && value.status == Domain::KnowledgeStatus::Draft;
        }));

        // 复合写入中途失败时，版本、证据和候选必须整体回滚。
        const size_t versionCountBeforeFailure = retainedVersions.size();
        QSqlQuery evidenceBefore(db);
        QVERIFY(evidenceBefore.exec(QStringLiteral("SELECT COUNT(*) FROM evidence_fragments_v5")));
        QVERIFY(evidenceBefore.next());
        const int evidenceCountBeforeFailure = evidenceBefore.value(0).toInt();
        gateway.result = R"JSON({
          "title":"Source paper","summary":"invalid candidate transaction",
          "study_type":"experiment","core_claims":[],"applicability":[],
          "limitations":[],"credibility":{"grade":"unknown","reason":"test"},
          "candidate_methods":[{"title":"","summary":"invalid"}],
          "candidate_tips":[],
          "evidence_fragments":[{"text":"temporary evidence must roll back","locator":{"page":4}}],
          "unknowns":[]
        })JSON";
        input.analysisContentHash = "analysis-hash-invalid";
        input.idempotencyKey = "analysis:test:invalid";
        const auto failedAnalysis = useCases.analyze(input);
        QVERIFY(!failedAnalysis.hasValue());
        QCOMPARE(repo.versionsOf(imported.value().item.uid).size(), versionCountBeforeFailure);
        QSqlQuery evidenceAfter(db);
        QVERIFY(evidenceAfter.exec(QStringLiteral("SELECT COUNT(*) FROM evidence_fragments_v5")));
        QVERIFY(evidenceAfter.next());
        QCOMPARE(evidenceAfter.value(0).toInt(), evidenceCountBeforeFailure);
    }

    void normalizesModelRelationAndStrength()
    {
        auto db = DatabaseManager::instance().database();
        Infrastructure::SqlKnowledgeRepository repo(db, m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(db, m_clock);
        Application::KnowledgeUseCases knowledge(repo, fts, m_uids, m_clock);

        Domain::SourceRecord source;
        source.uid = m_uids.next();
        source.sourceType = "url";
        source.canonicalUri = "https://example.org/paper2.pdf";
        source.title = "Normalization paper";
        source.accessedAt = m_clock.utcIso();
        source.contentHash = "source-hash-2";
        source.trustTier = "unverified";
        QVERIFY(repo.insertSource(source).ok);

        Application::KnowledgeUseCases::ImportInput paperInput;
        paperInput.libraryType = Domain::LibraryType::Paper;
        paperInput.title = "Normalization paper";
        paperInput.domainCode = "research.04.learning-science";
        paperInput.summary = "Raw text";
        paperInput.contentHash = "paper-hash-2";
        paperInput.createdBy = "generated";
        Domain::PaperDetail paperDetail;
        paperDetail.studyType = "experiment";
        paperInput.paper = paperDetail;
        const auto imported = knowledge.importKnowledge(paperInput);
        QVERIFY2(imported.hasValue(), imported.hasValue() ? "" : imported.error().message.c_str());

        // 真实模型曾返回数据库约束之外的关系名与越界强度
        // (真实批次 13/15 复现,evidence link insert failed);必须规范化
        FakeGateway gateway;
        gateway.result = R"JSON({
          "title":"Normalization paper","summary":"x","study_type":"experiment",
          "core_claims":[],"applicability":[],"limitations":[],
          "credibility":{"grade":"unknown","reason":"x"},
          "candidate_methods":[],"candidate_tips":[],
          "evidence_fragments":[{"text":"quoted evidence","locator":{},"relation":"limits","strength":5}],
          "unknowns":[]
        })JSON";
        Application::PaperAnalysisUseCases useCases(gateway, repo, knowledge, m_uids, m_clock);
        Application::PaperAnalysisUseCases::AnalyzeInput input;
        input.paperItemUid = imported.value().item.uid;
        input.sourceUid = source.uid;
        input.providerConfigUid = m_uids.next();
        input.title = "Normalization paper";
        input.extractedText = "full paper text";
        input.analysisContentHash = "analysis-hash-2";
        input.modelVersion = "test-model-v1";
        input.idempotencyKey = "analysis:normalize";
        const auto output = useCases.analyze(input);
        QVERIFY2(output.hasValue(), output.hasValue() ? "" : output.error().message.c_str());
        QCOMPARE(output.value().evidenceFragmentCount, 1);

        QSqlQuery check(db);
        QVERIFY(check.exec(QStringLiteral(
            "SELECT relation, strength FROM evidence_links_v5 ORDER BY id DESC LIMIT 1")));
        QVERIFY(check.next());
        QCOMPARE(check.value(0).toString(), QStringLiteral("qualifies"));
        QCOMPARE(check.value(1).toDouble(), 1.0);
    }

    void purgeRemovesGeneratedCandidatesCompletely()
    {
        auto db = DatabaseManager::instance().database();
        Infrastructure::SqlKnowledgeRepository repo(db, m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(db, m_clock);
        Application::KnowledgeUseCases knowledge(repo, fts, m_uids, m_clock);

        Domain::SourceRecord source;
        source.uid = m_uids.next();
        source.sourceType = "url";
        source.canonicalUri = "https://example.org/paper3.pdf";
        source.title = "Purge paper";
        source.accessedAt = m_clock.utcIso();
        source.contentHash = "source-hash-3";
        source.trustTier = "unverified";
        QVERIFY(repo.insertSource(source).ok);

        Application::KnowledgeUseCases::ImportInput paperInput;
        paperInput.libraryType = Domain::LibraryType::Paper;
        paperInput.title = "Purge paper";
        paperInput.domainCode = "research.04.learning-science";
        paperInput.summary = "Raw text";
        paperInput.contentHash = "paper-hash-3";
        paperInput.createdBy = "generated";
        Domain::PaperDetail paperDetail;
        paperDetail.studyType = "experiment";
        paperInput.paper = paperDetail;
        const auto imported = knowledge.importKnowledge(paperInput);
        QVERIFY2(imported.hasValue(), imported.hasValue() ? "" : imported.error().message.c_str());

        FakeGateway gateway;
        gateway.result = R"JSON({
          "title":"Purge paper","summary":"x","study_type":"experiment",
          "core_claims":[],"applicability":[],"limitations":[],
          "credibility":{"grade":"unknown","reason":"x"},
          "candidate_methods":[{"title":"Junk method","summary":"junk","steps":["step1"],"risk_level":"low","evidence_grade":"C"}],
          "candidate_tips":[{"title":"Junk tip","summary":"junk","risk_level":"low","use_scenario":{"task":"x"}}],
          "evidence_fragments":[{"text":"quoted evidence","locator":{},"relation":"supports","strength":0.5}],
          "unknowns":[]
        })JSON";
        Application::PaperAnalysisUseCases useCases(gateway, repo, knowledge, m_uids, m_clock);
        Application::PaperAnalysisUseCases::AnalyzeInput input;
        input.paperItemUid = imported.value().item.uid;
        input.sourceUid = source.uid;
        input.providerConfigUid = m_uids.next();
        input.title = "Purge paper";
        input.extractedText = "full paper text";
        input.analysisContentHash = "analysis-hash-3";
        input.modelVersion = "test-model-v1";
        input.idempotencyKey = "analysis:purge";
        const auto output = useCases.analyze(input);
        QVERIFY2(output.hasValue(), output.hasValue() ? "" : output.error().message.c_str());
        const auto methodUid = output.value().candidateMethods.front().uid;
        const auto tipUid = output.value().candidateTips.front().uid;

        // 物理删除:条目/版本/步骤/关系/FTS 全部清除,论文与草稿保留
        Application::PaperAnalysisUseCases::PurgeInput purge;
        purge.paperItemUid = imported.value().item.uid;
        purge.candidateItemUids = {methodUid, tipUid};
        const auto purged = useCases.purgeGeneratedCandidates(purge);
        QVERIFY2(purged.hasValue(), purged.hasValue() ? "" : purged.error().message.c_str());
        QCOMPARE(purged.value().purgedCount, 2);
        QVERIFY(repo.findItem(methodUid) == std::nullopt);
        QVERIFY(repo.findItem(tipUid) == std::nullopt);
        QVERIFY(repo.findItem(imported.value().item.uid).has_value());
        QSqlQuery check(db);
        QVERIFY(check.exec(QStringLiteral(
            "SELECT COUNT(*) FROM knowledge_fts_v6 WHERE owner_uid IN ('%1','%2')")
                               .arg(QString::fromStdString(methodUid.value()),
                                    QString::fromStdString(tipUid.value()))));
        QVERIFY(check.next());
        QCOMPARE(check.value(0).toInt(), 0);
        QVERIFY(check.exec(QStringLiteral(
            "SELECT COUNT(*) FROM knowledge_relations_v5 WHERE from_item_id IN "
            "(SELECT id FROM knowledge_items_v5 WHERE uid IN ('%1','%2'))")
                               .arg(QString::fromStdString(methodUid.value()),
                                    QString::fromStdString(tipUid.value()))));
        QVERIFY(check.next());
        QCOMPARE(check.value(0).toInt(), 0);

        // 资格守卫:非生成条目拒绝
        Domain::SourceRecord userSource;
        userSource.uid = m_uids.next();
        userSource.sourceType = "manual";
        userSource.title = "user source";
        userSource.accessedAt = m_clock.utcIso();
        userSource.contentHash = "user-source-hash";
        userSource.trustTier = "unverified";
        Application::KnowledgeUseCases::ImportInput userInput;
        userInput.libraryType = Domain::LibraryType::Method;
        userInput.title = "User method";
        userInput.domainCode = "learning";
        userInput.summary = "user summary";
        userInput.contentHash = std::string("manual:") + m_uids.next().value();
        userInput.createdBy = "user";
        userInput.source = userSource;
        const auto userItem = knowledge.importKnowledge(userInput);
        QVERIFY2(userItem.hasValue(),
                 userItem.hasValue() ? "" : userItem.error().message.c_str());
        Application::PaperAnalysisUseCases::PurgeInput badPurge;
        badPurge.paperItemUid = imported.value().item.uid;
        badPurge.candidateItemUids = {userItem.value().item.uid};
        const auto rejected = useCases.purgeGeneratedCandidates(badPurge);
        QVERIFY(!rejected.hasValue());
    }

    void numbersDerivedCandidatesFromPaperCode()
    {
        auto db = DatabaseManager::instance().database();
        Infrastructure::SqlKnowledgeRepository repo(db, m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(db, m_clock);
        Application::KnowledgeUseCases knowledge(repo, fts, m_uids, m_clock);

        Domain::SourceRecord source;
        source.uid = m_uids.next();
        source.sourceType = "url";
        source.canonicalUri = "https://example.org/paper4.pdf";
        source.title = "Numbered paper";
        source.accessedAt = m_clock.utcIso();
        source.contentHash = "source-hash-4";
        source.trustTier = "unverified";
        QVERIFY(repo.insertSource(source).ok);

        Application::KnowledgeUseCases::ImportInput paperInput;
        paperInput.libraryType = Domain::LibraryType::Paper;
        paperInput.title = "Numbered paper";
        paperInput.domainCode = "research.04.learning-science";
        paperInput.referenceCode = "4.1";
        paperInput.summary = "Raw text";
        paperInput.contentHash = "paper-hash-4";
        paperInput.createdBy = "generated";
        Domain::PaperDetail paperDetail;
        paperDetail.studyType = "experiment";
        paperInput.paper = paperDetail;
        const auto imported = knowledge.importKnowledge(paperInput);
        QVERIFY2(imported.hasValue(), imported.hasValue() ? "" : imported.error().message.c_str());

        FakeGateway gateway;
        gateway.result = R"JSON({
          "title":"Numbered paper","summary":"x","study_type":"experiment",
          "core_claims":[],"applicability":[],"limitations":[],
          "credibility":{"grade":"unknown","reason":"x"},
          "candidate_methods":[{"title":"Method A","summary":"a","steps":["s"],"risk_level":"low","evidence_grade":"C"}],
          "candidate_tips":[{"title":"Tip A","summary":"a","risk_level":"low","use_scenario":{"task":"x"}}],
          "evidence_fragments":[{"text":"quoted evidence","locator":{},"relation":"supports","strength":0.5}],
          "unknowns":[]
        })JSON";
        Application::PaperAnalysisUseCases useCases(gateway, repo, knowledge, m_uids, m_clock);
        Application::PaperAnalysisUseCases::AnalyzeInput input;
        input.paperItemUid = imported.value().item.uid;
        input.sourceUid = source.uid;
        input.providerConfigUid = m_uids.next();
        input.title = "Numbered paper";
        input.extractedText = "full paper text";
        input.analysisContentHash = "analysis-hash-4";
        input.modelVersion = "test-model-v1";
        input.idempotencyKey = "analysis:numbered";
        const auto output = useCases.analyze(input);
        QVERIFY2(output.hasValue(), output.hasValue() ? "" : output.error().message.c_str());
        QCOMPARE(QString::fromStdString(output.value().candidateMethods.front().referenceCode),
                 QStringLiteral("4.1-M1"));
        QCOMPARE(QString::fromStdString(output.value().candidateTips.front().referenceCode),
                 QStringLiteral("4.1-T1"));

        // 幂等重跑:同指纹不重复建版本/候选(真实产品曾因此整体回滚)
        const auto again = useCases.analyze(input);
        QVERIFY2(again.hasValue(), again.hasValue() ? "" : again.error().message.c_str());
        QVERIFY(again.value().reusedExisting);
        QCOMPARE(again.value().candidateMethods.size(), size_t(1));
        QCOMPARE(again.value().candidateTips.size(), size_t(1));
        QCOMPARE(repo.versionsOf(imported.value().item.uid).size(), size_t(2));
    }

    void duplicateCandidatesAcrossChunksDoNotRollBack()
    {
        auto db = DatabaseManager::instance().database();
        Infrastructure::SqlKnowledgeRepository repo(db, m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(db, m_clock);
        Application::KnowledgeUseCases knowledge(repo, fts, m_uids, m_clock);

        Domain::SourceRecord source;
        source.uid = m_uids.next();
        source.sourceType = "url";
        source.canonicalUri = "https://example.org/paper5.pdf";
        source.title = "Dedup paper";
        source.accessedAt = m_clock.utcIso();
        source.contentHash = "source-hash-5";
        source.trustTier = "unverified";
        QVERIFY(repo.insertSource(source).ok);

        Application::KnowledgeUseCases::ImportInput paperInput;
        paperInput.libraryType = Domain::LibraryType::Paper;
        paperInput.title = "Dedup paper";
        paperInput.domainCode = "research.04.learning-science";
        paperInput.summary = "Raw text";
        paperInput.contentHash = "paper-hash-5";
        paperInput.createdBy = "generated";
        Domain::PaperDetail paperDetail;
        paperDetail.studyType = "experiment";
        paperInput.paper = paperDetail;
        const auto imported = knowledge.importKnowledge(paperInput);
        QVERIFY2(imported.hasValue(), imported.hasValue() ? "" : imported.error().message.c_str());

        // 两块都返回同标题候选(真实跨块重复场景):不得因关系唯一约束整体回滚
        FakeGateway gateway;
        gateway.result = R"JSON({
          "title":"Dedup paper","summary":"x","study_type":"experiment",
          "core_claims":[],"applicability":[],"limitations":[],
          "credibility":{"grade":"unknown","reason":"x"},
          "candidate_methods":[{"title":"Same method","summary":"same","steps":["s"],"risk_level":"low","evidence_grade":"C"}],
          "candidate_tips":[],
          "evidence_fragments":[{"text":"quoted evidence","locator":{},"relation":"supports","strength":0.5}],
          "unknowns":[]
        })JSON";
        Application::PaperAnalysisUseCases useCases(gateway, repo, knowledge, m_uids, m_clock);
        Application::PaperAnalysisUseCases::AnalyzeInput input;
        input.paperItemUid = imported.value().item.uid;
        input.sourceUid = source.uid;
        input.providerConfigUid = m_uids.next();
        input.title = "Dedup paper";
        // 多块:提取文本远大于块下限(splitText 钳制最小 1000 字符/块)
        input.extractedText = std::string(1200, 'a') + " filler " + std::string(1200, 'b');
        input.analysisContentHash = "analysis-hash-5";
        input.modelVersion = "test-model-v1";
        input.idempotencyKey = "analysis:dedup";
        input.maxChunkCharacters = 1000;
        input.chunkOverlapCharacters = 10;
        const auto output = useCases.analyze(input);
        QVERIFY2(output.hasValue(), output.hasValue() ? "" : output.error().message.c_str());
        QVERIFY(output.value().analyzedChunkCount >= 2);
        // 跨块同标题候选各自成为独立条目(内容指纹不去重,属产品待优化),
        // 关键断言:分析不因关系/版本约束整体回滚
        QCOMPARE(output.value().candidateMethods.size(), size_t(3));
        QVERIFY(output.value().evidenceFragmentCount >= 2);

        QSqlQuery count(db);
        count.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM knowledge_relations_v5 r "
            "JOIN knowledge_items_v5 i ON i.id=r.to_item_id "
            "WHERE r.relation='derived_from' AND i.uid=?"));
        count.addBindValue(QString::fromStdString(imported.value().item.uid.value()));
        QVERIFY(count.exec());
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), 3);
    }

    void reproduceRealProductFailure()
    {
        // 诊断用:读取真实失败批次的模型响应,复现并打印失败步骤;
        // 环境变量 PERSONOS_REPRO_DIR 指向含 realchunk0..4.json 的目录
        const QString dir = qEnvironmentVariable("PERSONOS_REPRO_DIR");
        if (dir.isEmpty())
            QSKIP("PERSONOS_REPRO_DIR not set");
        auto db = DatabaseManager::instance().database();
        Infrastructure::SqlKnowledgeRepository repo(db, m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(db, m_clock);
        Application::KnowledgeUseCases knowledge(repo, fts, m_uids, m_clock);

        Domain::SourceRecord source;
        source.uid = m_uids.next();
        source.sourceType = "url";
        source.canonicalUri = "https://example.org/repro.pdf";
        source.title = "Repro paper";
        source.accessedAt = m_clock.utcIso();
        source.contentHash = "source-hash-repro";
        source.trustTier = "unverified";
        QVERIFY(repo.insertSource(source).ok);

        Application::KnowledgeUseCases::ImportInput paperInput;
        paperInput.libraryType = Domain::LibraryType::Paper;
        paperInput.title = "Repro paper";
        paperInput.domainCode = "research.01.goal-self-regulation";
        paperInput.summary = "Raw text";
        paperInput.contentHash = "paper-hash-repro";
        paperInput.createdBy = "generated";
        Domain::PaperDetail paperDetail;
        paperDetail.studyType = "meta-analysis";
        paperInput.paper = paperDetail;
        const auto imported = knowledge.importKnowledge(paperInput);
        QVERIFY(imported.hasValue());

        FakeGateway gateway;
        for (int i = 0; i < 5; ++i) {
            QFile file(dir + QStringLiteral("/realchunk%1.json").arg(i));
            if (!file.open(QIODevice::ReadOnly))
                QSKIP("realchunk files missing");
            gateway.results.append(QString::fromUtf8(file.readAll()));
        }
        Application::PaperAnalysisUseCases useCases(gateway, repo, knowledge, m_uids, m_clock);
        Application::PaperAnalysisUseCases::AnalyzeInput input;
        input.paperItemUid = imported.value().item.uid;
        input.sourceUid = source.uid;
        input.providerConfigUid = m_uids.next();
        input.title = "Repro paper";
        input.extractedText = std::string(200000, 'x');
        input.analysisContentHash = "analysis-hash-repro";
        input.modelVersion = "test-model-v1";
        input.idempotencyKey = "analysis:repro";
        const auto output = useCases.analyze(input);
        if (!output)
            qInfo() << "REPRO FAILURE:" << QString::fromStdString(output.error().message);
        QVERIFY2(output.hasValue(), output.hasValue() ? "" : output.error().message.c_str());

        // 第二例:真实 Klein 双块结果(字符串 locator + 候选方法/贴士)
        QFile kleinFile0(dir + QStringLiteral("/realchunk-klein0.json"));
        QFile kleinFile1(dir + QStringLiteral("/realchunk-klein.json"));
        if (kleinFile0.open(QIODevice::ReadOnly) && kleinFile1.open(QIODevice::ReadOnly)) {
            Application::PaperAnalysisUseCases::AnalyzeInput kleinInput;
            kleinInput.paperItemUid = imported.value().item.uid;
            kleinInput.sourceUid = source.uid;
            kleinInput.providerConfigUid = m_uids.next();
            kleinInput.title = "Klein repro";
            kleinInput.extractedText = std::string(130000, 'y');
            kleinInput.analysisContentHash = "analysis-hash-klein";
            kleinInput.modelVersion = "test-model-v1";
            kleinInput.idempotencyKey = "analysis:klein";
            FakeGateway kleinGateway;
            kleinGateway.results.append(QString::fromUtf8(kleinFile0.readAll()));
            kleinGateway.results.append(QString::fromUtf8(kleinFile1.readAll()));
            Application::PaperAnalysisUseCases kleinUseCases(kleinGateway, repo, knowledge,
                                                             m_uids, m_clock);
            const auto kleinOutput = kleinUseCases.analyze(kleinInput);
            if (!kleinOutput)
                qInfo() << "KLEIN REPRO FAILURE:"
                        << QString::fromStdString(kleinOutput.error().message);
            QVERIFY2(kleinOutput.hasValue(),
                     kleinOutput.hasValue() ? "" : kleinOutput.error().message.c_str());
        }
    }

    void reproduceRealProductClickA()
    {
        // 诊断:用产品库副本 + 真实条目/来源/资产,重放点击A的 analyze
        const QString dir = qEnvironmentVariable("PERSONOS_REPRO_DIR");
        if (dir.isEmpty())
            QSKIP("PERSONOS_REPRO_DIR not set");
        auto db = DatabaseManager::instance().database();
        Infrastructure::SqlKnowledgeRepository repo(db, m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(db, m_clock);
        Application::KnowledgeUseCases knowledge(repo, fts, m_uids, m_clock);

        // 真实条目(产品库副本中的论文)
        const auto paper = repo.findItem(
            *Domain::Uid::parse("245fd562-1c1e-488d-a7ca-eb3c70536829"));
        if (!paper)
            QSKIP("real paper not in this DB");
        const auto source = repo.findSourceByContentHash(
            "a883c0e661d5fe54d82d0708180fe25c70671a3b10f72a7539c9b44a9265f038");
        QVERIFY(source.has_value());

        FakeGateway gateway;
        for (const QString &name : {QStringLiteral("realchunk-klein0.json"),
                                    QStringLiteral("realchunk-klein.json")}) {
            QFile file(dir + QStringLiteral("/") + name);
            if (!file.open(QIODevice::ReadOnly))
                QSKIP("chunk files missing");
            gateway.results.append(QString::fromUtf8(file.readAll()));
        }
        Application::PaperAnalysisUseCases useCases(gateway, repo, knowledge, m_uids, m_clock);
        Application::PaperAnalysisUseCases::AnalyzeInput input;
        input.paperItemUid = paper->uid;
        input.sourceUid = source->uid;
        input.providerConfigUid = *Domain::Uid::parse(
            "00de1d98-1bdd-4e31-9601-16e1d7a387f7");
        input.title = paper->title;
        input.extractedText = std::string(80696, 'z');
        input.modelVersion = "openai_compatible:deepseek-v4-pro";
        input.idempotencyKey = "paper-analysis:" + paper->uid.value() + ":"
                               + "a883c0e661d5fe54d82d0708180fe25c70671a3b10f72a7539c9b44a9265f038"
                               + ":openai_compatible:deepseek-v4-pro:v2";
        input.analysisContentHash = QCryptographicHash::hash(
            QByteArray::fromStdString(paper->uid.value()
                                      + "a883c0e661d5fe54d82d0708180fe25c70671a3b10f72a7539c9b44a9265f038"
                                      + "openai_compatible:deepseek-v4-pro:v2"),
            QCryptographicHash::Sha256).toHex().toStdString();
        const auto output = useCases.analyze(input);
        if (!output)
            qInfo() << "CLICK-A REPRO FAILURE:"
                    << QString::fromStdString(output.error().message)
                    << "|" << QString::fromStdString(output.error().detail);
        QVERIFY2(output.hasValue(), output.hasValue() ? "" : output.error().message.c_str());
    }

    void longPaperIsSplitIntoTraceableChunks()
    {
        auto db = DatabaseManager::instance().database();
        Infrastructure::SqlKnowledgeRepository repo(db, m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(db, m_clock);
        Application::KnowledgeUseCases knowledge(repo, fts, m_uids, m_clock);
        Domain::SourceRecord source;
        source.uid = m_uids.next();
        source.sourceType = "url";
        source.canonicalUri = "https://example.org/long.pdf";
        source.title = "Long paper";
        source.accessedAt = m_clock.utcIso();
        source.contentHash = "long-source";
        source.trustTier = "unverified";
        QVERIFY(repo.insertSource(source).ok);
        Application::KnowledgeUseCases::ImportInput paperInput;
        paperInput.libraryType = Domain::LibraryType::Paper;
        paperInput.title = "Long paper";
        paperInput.domainCode = "research.01.test";
        paperInput.summary = "raw";
        paperInput.contentHash = "long-paper";
        paperInput.createdBy = "generated";
        paperInput.paper = Domain::PaperDetail{};
        const auto imported = knowledge.importKnowledge(paperInput);
        QVERIFY(imported.hasValue());
        FakeGateway gateway;
        gateway.result = R"JSON({"title":"Long","summary":"part","study_type":"unknown","core_claims":[],"applicability":[],"limitations":[],"credibility":{"grade":"unknown","reason":"chunk"},"candidate_methods":[],"candidate_tips":[],"evidence_fragments":[{"text":"quoted evidence","locator":{},"relation":"supports","strength":0.5}],"unknowns":[]})JSON";
        Application::PaperAnalysisUseCases useCases(gateway, repo, knowledge, m_uids, m_clock);
        Application::PaperAnalysisUseCases::AnalyzeInput input;
        input.paperItemUid = imported.value().item.uid;
        input.sourceUid = source.uid;
        input.providerConfigUid = m_uids.next();
        input.title = "Long paper";
        input.extractedText = std::string(3500, 'x');
        input.analysisContentHash = "long-analysis";
        input.modelVersion = "test-model";
        input.idempotencyKey = "analysis:long";
        input.maxChunkCharacters = 1200;
        input.chunkOverlapCharacters = 100;
        const auto output = useCases.analyze(input);
        QVERIFY(output.hasValue());
        QVERIFY(output.value().analyzedChunkCount >= 3);
        QCOMPARE(gateway.submitCount, output.value().analyzedChunkCount);
        QVERIFY(gateway.largestPaperText <= 1200);
        QCOMPARE(output.value().evidenceFragmentCount, output.value().analyzedChunkCount);
        const auto merged = QJsonDocument::fromJson(
            QByteArray::fromStdString(output.value().rawAnalysisJson)).object();
        QCOMPARE(merged.value(QStringLiteral("evidence_fragments")).toArray().first().toObject()
                     .value(QStringLiteral("locator")).toObject()
                     .value(QStringLiteral("chunk_index")).toInt(), 0);
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstPaperAnalysis)
#include "tst_paper_analysis.moc"
