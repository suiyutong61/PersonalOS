// IMP-006b：内容摄入状态机与论文转方法（DD-001 §8.1；DR-004/010/011/012/023）
// 覆盖：摄入状态机全链、失败可重试/终态、幂等、获取失败不猜测、
//       论文转方法（derived_from 关系、同标题合并、版本追加不覆盖）。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "application/usecases/knowledge/ContentIngestUseCases.h"
#include "application/usecases/knowledge/KnowledgeUseCases.h"
#include "application/usecases/knowledge/MethodDerivationUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/SqlContentIngestRepository.h"
#include "infrastructure/knowledge/SqlKnowledgeFtsIndex.h"
#include "infrastructure/persistence/SqlKnowledgeRepository.h"

using namespace PersonOS;

namespace {
// 可脚本化内容源
class FakeContentSource final : public Application::ContentSourcePort
{
public:
    bool succeed = true;
    Application::FetchedContent fetch(const std::string &uri) override
    {
        ++calls;
        if (!succeed)
            return {false, {}, {}, std::nullopt, {}, {}, "fetch denied"};
        return {true,
                QStringLiteral("测试视频标题").toStdString(),
                QStringLiteral("作者").toStdString(),
                std::string("2026-09-27"),
                QStringLiteral("正文：25 分钟专注学习法，休息 5 分钟。").toStdString(),
                "transcript",
                ""};
    }
    int calls = 0;
};
} // namespace

class TstContentIngest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_content_ingest.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
    }

    void pipelineStageRules()
    {
        // 管道阶段推进顺序（Running 状态内）
        QVERIFY(std::string(Domain::IngestStage::nextStage("fetching"))
                == "quarantined");
        QVERIFY(std::string(Domain::IngestStage::nextStage("deriving")) == "validating");
        QVERIFY(Domain::IngestStage::nextStage("validating") == nullptr);
        // 状态枚举与数据库 CHECK 一致
        QVERIFY(Domain::toString(Domain::IngestStatus::Queued) == "queued");
        QVERIFY(Domain::toString(Domain::IngestStatus::Running) == "running");
        QVERIFY(Domain::toString(Domain::IngestStatus::AwaitingConfirmation)
                == "awaiting_confirmation");
        QVERIFY(Domain::toString(Domain::IngestStatus::FailedRetryable)
                == "failed_retryable");
    }

    void fullPipelineAndIdempotency()
    {
        Infrastructure::SqlContentIngestRepository repo(
            DatabaseManager::instance().database(), m_clock);
        FakeContentSource source;
        Application::ContentIngestUseCases useCases(repo, source, m_uids, m_clock);

        Application::ContentIngestUseCases::SubmitInput input;
        input.sourceType = "url";
        input.sourceUri = "https://example.com/video/1";
        input.idempotencyKey = "ingest:1:first";
        const auto submitted = useCases.submit(input);
        if (!submitted)
            QFAIL(qPrintable(QString::fromStdString(submitted.error().message + ": "
                                                    + submitted.error().detail)));
        const auto uid = submitted.value().uid;
        QVERIFY(submitted.value().status == Domain::IngestStatus::Queued);

        // 幂等键重复
        const auto duplicate = useCases.submit(input);
        QVERIFY(!duplicate);
        QVERIFY(duplicate.error().code == Application::ErrorCode::Conflict);

        // 推进全链：queued→running(fetching，实际获取成功)→quarantined→…→
        // validating→awaiting_confirmation→commit
        int revision = 1;
        Domain::IngestStatus status = Domain::IngestStatus::Queued;
        int guard = 0;
        while (status != Domain::IngestStatus::AwaitingConfirmation && guard++ < 12) {
            const auto advanced = useCases.advance(uid, revision);
            if (!advanced)
                QFAIL(qPrintable(QString::fromStdString(advanced.error().message + ": "
                                                        + advanced.error().detail)));
            revision = advanced.value().revision;
            status = advanced.value().status;
        }
        QVERIFY(status == Domain::IngestStatus::AwaitingConfirmation);
        QCOMPARE(source.calls, 1);
        const auto committed = useCases.commit(uid, revision);
        if (!committed)
            QFAIL(qPrintable(QString::fromStdString(committed.error().message + ": "
                                                    + committed.error().detail)));
        QVERIFY(committed.value().status == Domain::IngestStatus::Committed);
        // 终态不可再推进
        QVERIFY(!useCases.advance(uid, committed.value().revision));
    }

    void fetchFailureRetryThenSucceed()
    {
        Infrastructure::SqlContentIngestRepository repo(
            DatabaseManager::instance().database(), m_clock);
        FakeContentSource source;
        source.succeed = false;
        Application::ContentIngestUseCases useCases(repo, source, m_uids, m_clock);

        Application::ContentIngestUseCases::SubmitInput input;
        input.sourceType = "url";
        input.sourceUri = "https://example.com/video/2";
        input.idempotencyKey = "ingest:2:retry";
        const auto submitted = useCases.submit(input);
        QVERIFY(submitted);
        const auto uid = submitted.value().uid;

        // 获取失败：advance 先落地 Running(stage=fetching) 再获取，
        // 获取失败返回 ExternalUnavailable（不根据标题猜测内容），
        // 由调用方以 fail() 落地失败状态
        const auto fetching = useCases.advance(uid, 1);
        QVERIFY(!fetching);
        QVERIFY(fetching.error().code == Application::ErrorCode::ExternalUnavailable);
        const auto failed = useCases.fail(uid, 2, false, "FETCH_DENIED", "无法合法获取");
        QVERIFY(failed
                && failed.value().status == Domain::IngestStatus::FailedRetryable);

        // 恢复后重试成功
        source.succeed = true;
        const auto retried = useCases.advance(uid, failed.value().revision);
        QVERIFY(retried);
        QVERIFY(retried.value().status == Domain::IngestStatus::Running);
        QVERIFY(retried.value().stage == "fetching");
        QCOMPARE(source.calls, 2);

        // 终态失败：内容源永久拒绝
        Infrastructure::SqlContentIngestRepository repo2(
            DatabaseManager::instance().database(), m_clock);
        FakeContentSource blocked;
        blocked.succeed = false;
        Application::ContentIngestUseCases useCases2(repo2, blocked, m_uids, m_clock);
        Application::ContentIngestUseCases::SubmitInput terminalInput;
        terminalInput.sourceType = "url";
        terminalInput.sourceUri = "https://example.com/video/3";
        terminalInput.idempotencyKey = "ingest:3:terminal";
        const auto terminalJob = useCases2.submit(terminalInput);
        QVERIFY(terminalJob);
        const auto terminalFetch = useCases2.advance(terminalJob.value().uid, 1);
        QVERIFY(!terminalFetch);
        const auto terminal = useCases2.fail(terminalJob.value().uid, 2, true,
                                             "COPYRIGHT", "版权限制");
        QVERIFY(terminal
                && terminal.value().status == Domain::IngestStatus::FailedTerminal);
    }

    void deriveMethodsFromPaperWithMergeAndSupplement()
    {
        Infrastructure::SqlKnowledgeRepository repo(DatabaseManager::instance().database(),
                                                    m_clock);
        Infrastructure::SqlKnowledgeFtsIndex fts(DatabaseManager::instance().database(),
                                                 m_clock);
        Application::KnowledgeUseCases knowledge(repo, fts, m_uids, m_clock);

        // 论文
        Application::KnowledgeUseCases::ImportInput paperInput;
        paperInput.libraryType = Domain::LibraryType::Paper;
        paperInput.title = QStringLiteral("检索练习论文").toStdString();
        paperInput.domainCode = "learning";
        paperInput.summary = QStringLiteral("检索练习提升长期保持").toStdString();
        paperInput.contentHash = "paper-1";
        paperInput.createdBy = "developer";
        Domain::SourceRecord source;
        source.sourceType = "doi";
        source.canonicalUri = "10.1/test";
        source.title = paperInput.title;
        source.contentHash = "paper-1";
        source.trustTier = "vetted";
        paperInput.source = source;
        Domain::PaperDetail paperDetail;
        paperDetail.studyType = "meta";
        paperInput.paper = paperDetail;
        const auto paperImported = knowledge.importKnowledge(paperInput);
        if (!paperImported)
            QFAIL(qPrintable(QString::fromStdString(paperImported.error().message + ": "
                                                    + paperImported.error().detail)));
        const auto paperUid = paperImported.value().item.uid;

        Application::MethodDerivationUseCases derive(repo, fts, m_uids, m_clock);

        // 推导两个方法（其中第二个与第一个同标题 → 合并）
        Application::MethodDerivationUseCases::AdmitInput admit;
        admit.paperItemUid = paperUid;
        Domain::MethodStep step1;
        step1.instruction = QStringLiteral("合上书回忆").toStdString();
        step1.sequenceNo = 0;
        Application::MethodDerivationUseCases::MethodCandidate candidate;
        candidate.title = QStringLiteral("检索练习法").toStdString();
        candidate.summary = QStringLiteral("主动回忆优于重读").toStdString();
        candidate.steps = {step1};
        candidate.evidenceGrade = "A";
        admit.candidates = {candidate};
        const auto admitted = derive.admit(admit);
        if (!admitted)
            QFAIL(qPrintable(QString::fromStdString(admitted.error().message + ": "
                                                    + admitted.error().detail)));
        QCOMPARE(admitted.value().admitted.size(), 1);
        QVERIFY(admitted.value().mergedIntoTitles.empty());
        const auto methodUid = admitted.value().admitted.front().uid;

        // derived_from 关系成立
        const auto relations = repo.relationsOf(methodUid);
        bool derived = false;
        for (const auto &relation : relations)
            if (relation.relation == "derived_from"
                && relation.toItemUid == paperUid.value())
                derived = true;
        QVERIFY(derived);

        // 同标题候选 → 合并（不新建；来源关系追加）
        const auto merged = derive.admit(admit);
        if (!merged)
            QFAIL(qPrintable(QString::fromStdString(merged.error().message + ": "
                                                    + merged.error().detail)));
        QVERIFY(merged.value().admitted.empty());
        QCOMPARE(merged.value().mergedIntoTitles.size(), 1);
        // 方法仍只有一个（未重复建条目）
        const auto byTitle = repo.findItemsByTitle(candidate.title);
        int methodCount = 0;
        for (const auto &item : byTitle)
            if (item.libraryType == Domain::LibraryType::Method)
                ++methodCount;
        QCOMPARE(methodCount, 1);

        // 新证据补充 → 追加新版本（不覆盖旧版本）
        Application::MethodDerivationUseCases::VersionInput supplement;
        supplement.summary = QStringLiteral("补充：间隔安排细节").toStdString();
        Domain::MethodStep step2;
        step2.instruction = QStringLiteral("按遗忘曲线复习").toStdString();
        supplement.steps = {step2};
        const auto item = repo.findItem(methodUid);
        const auto supplemented = derive.supplement(methodUid, item->revision, supplement);
        if (!supplemented)
            QFAIL(qPrintable(QString::fromStdString(supplemented.error().message + ": "
                                                    + supplemented.error().detail)));
        QCOMPARE(supplemented.value().versionNo, 2);
        const auto versions = repo.versionsOf(methodUid);
        QCOMPARE(versions.size(), 2);
        QVERIFY(versions[0].summary != versions[1].summary);
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstContentIngest)
#include "tst_content_ingest.moc"
