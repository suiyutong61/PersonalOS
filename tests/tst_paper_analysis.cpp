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
        job.resultJson = result;
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
        m_path = QDir::temp().filePath(QStringLiteral("personos_paper_analysis.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
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
