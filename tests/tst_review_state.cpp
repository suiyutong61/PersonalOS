// IMP-003b：复盘与状态（DD-001 §3/§5；DR-022/036）
// 覆盖：结算开启复盘（每 MEL 至多一个）、提交/关闭与 revision 冲突、
//       状态定义种子（基本+十类）、状态事件追加/幂等/时效（过期=未知）。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "application/usecases/goal/GoalUseCases.h"
#include "application/usecases/review/ReviewUseCases.h"
#include "application/usecases/state/StateUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/knowledge/StateDefinitionsSeed.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlReviewRepository.h"
#include "infrastructure/persistence/SqlStateRepository.h"

using namespace PersonOS;

namespace {
const char *kUserUid = "00000000-0000-0000-0000-0000000000aa";
const char *kMelUid = "00000000-0000-0000-0000-0000000000bb";
} // namespace

class TstReviewState : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_review_state.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
        QVERIFY(Infrastructure::LearningManifestSeed(DatabaseManager::instance().database(),
                                                     m_clock)
                    .ensureSeeded());
        QSqlQuery user(DatabaseManager::instance().database());
        const bool ok = user.exec(QStringLiteral(
            "INSERT INTO user_profiles_v3(uid,timezone_id,locale,onboarding_status,profile_json,"
            "created_at,updated_at) VALUES('00000000-0000-0000-0000-0000000000aa',"
            "'Asia/Shanghai','zh-CN','complete','{}','2026-09-27T00:00:00Z','2026-09-27T00:00:00Z')"));
        if (!ok)
            QFAIL(qPrintable(QStringLiteral("user insert: %1").arg(user.lastError().text())));

        // 目标（MEL 外键需要）
        QSqlQuery manifest(DatabaseManager::instance().database());
        QVERIFY(manifest.exec(QStringLiteral(
            "SELECT uid FROM domain_manifests_v3 WHERE domain_code='learning'")));
        QVERIFY(manifest.next());
        const QString manifestUid = manifest.value(0).toString();
        Infrastructure::SqlGoalRepository goalsRepo(DatabaseManager::instance().database(),
                                                    m_clock);
        Application::GoalUseCases goalUseCases(goalsRepo, m_uids, m_clock);
        Application::GoalUseCases::CreateInput goalInput;
        goalInput.userId = *Domain::Uid::parse(kUserUid);
        goalInput.domainManifestId = *Domain::Uid::parse(manifestUid.toStdString());
        goalInput.title = QStringLiteral("测试目标").toStdString();
        goalInput.desiredLevelJson = std::string("{}");
        goalInput.userDefinedLevel = true;
        QVERIFY(goalUseCases.createGoal(goalInput));

        // MEL 行（复盘用例的 mel_id 引用）
        QSqlQuery mel(DatabaseManager::instance().database());
        const bool melOk = mel.exec(QStringLiteral(
            "INSERT INTO mels_v4(uid,user_id,goal_id,manifest_version_id,title,state,"
            "planned_start_at,planned_end_at,timezone_id,settlement_mode,capacity_min,"
            "reserve_min,rationale,created_at,updated_at) "
            "SELECT '%1',u.id,g.id,mv.id,'复盘测试','draft','2026-10-01T00:00:00Z',"
            "'2026-10-04T00:00:00Z','Asia/Shanghai','deadline',300,30,'测试',"
            "'2026-09-27T00:00:00Z','2026-09-27T00:00:00Z' "
            "FROM user_profiles_v3 u, goals_v3 g, domain_manifest_versions_v3 mv "
            "WHERE u.uid='00000000-0000-0000-0000-0000000000aa' AND g.title='测试目标' "
            "AND mv.version_no=1")
                                   .arg(QString::fromLatin1(kMelUid)));
        if (!melOk)
            QFAIL(qPrintable(QStringLiteral("mel insert: %1").arg(mel.lastError().text())));
    }

    void stateDefinitionsSeedAndEvents()
    {
        Infrastructure::StateDefinitionsSeed seed(DatabaseManager::instance().database(),
                                                  m_clock);
        QVERIFY(seed.ensureSeeded());
        QVERIFY(seed.ensureSeeded());   // 幂等

        Infrastructure::SqlStateRepository repo(DatabaseManager::instance().database(),
                                                m_clock);
        QVERIFY(repo.findDefinitionByCode("energy").has_value());
        QVERIFY(repo.findDefinitionByCode("stress").has_value());
        QCOMPARE(repo.allDefinitions().size(), 22);   // 基本+十类 16 项 + R2 子问题 6 项

        Application::StateUseCases useCases(repo, m_uids, m_clock);
        const auto userUid = *Domain::Uid::parse(kUserUid);

        Application::StateUseCases::ObserveInput input;
        input.userId = userUid;
        input.definitionCode = "energy";
        input.valueJson = QStringLiteral("{\"value\":4,\"min\":1,\"max\":5}").toStdString();
        input.source = Domain::StateSource::Explicit;
        input.idempotencyKey = "state:1:first";
        const auto observed = useCases.observe(input);
        if (!observed)
            QFAIL(qPrintable(QString::fromStdString(observed.error().message + ": "
                                                    + observed.error().detail)));
        QVERIFY(observed.value().validUntil > observed.value().observedAt);

        // 当前有效状态可查询
        const auto current = useCases.current(userUid, "energy");
        QVERIFY(current && current.value().has_value());
        QCOMPARE(current.value().value().idempotencyKey, std::string("state:1:first"));

        // 幂等：重复键拒绝
        const auto dup = useCases.observe(input);
        QVERIFY(!dup);
        QVERIFY(dup.error().code == Application::ErrorCode::Conflict);

        // 未知定义拒绝
        Application::StateUseCases::ObserveInput bad = input;
        bad.definitionCode = "not_exist";
        bad.idempotencyKey = "state:bad";
        QVERIFY(!useCases.observe(bad));

        // 过期状态 = 未知：2099 年查询时 valid_until 早已过期 → 无有效事件
        const auto none =
            repo.latestValid(userUid, "energy", "2099-01-01T00:00:00Z");
        QVERIFY(!none.has_value());
    }

    void reviewLifecycle()
    {
        Infrastructure::SqlReviewRepository repo(DatabaseManager::instance().database(),
                                                 m_clock);
        Application::ReviewUseCases useCases(repo, m_uids, m_clock);
        const auto melUid = *Domain::Uid::parse(kMelUid);

        // 开启复盘（每 MEL 至多一个）
        const auto opened = useCases.openReview(melUid);
        if (!opened)
            QFAIL(qPrintable(QString::fromStdString(opened.error().message + ": "
                                                    + opened.error().detail)));
        QVERIFY(opened.value().status == Domain::ReviewStatus::Collecting);
        const auto duplicate = useCases.openReview(melUid);
        QVERIFY(!duplicate);
        QVERIFY(duplicate.error().code == Application::ErrorCode::Conflict);

        // 提交复盘内容（revision 1 → 2）
        Application::ReviewUseCases::SubmitInput input;
        input.summary = QStringLiteral("完成率 100%，耗时偏差 -30 分钟，验收待提交")
                            .toStdString();
        input.userComment = QStringLiteral("节奏比上轮好").toStdString();
        input.nextAction = QStringLiteral("生成下一轮 MEL").toStdString();
        const auto submitted = useCases.submitReview(melUid, 1, input);
        if (!submitted)
            QFAIL(qPrintable(QString::fromStdString(submitted.error().message + ": "
                                                    + submitted.error().detail)));
        QVERIFY(submitted.value().status == Domain::ReviewStatus::Confirmed);
        QVERIFY(submitted.value().nextAction == input.nextAction);

        // 陈旧 revision 冲突
        const auto stale = useCases.submitReview(melUid, 1, input);
        QVERIFY(!stale);
        QVERIFY(stale.error().code == Application::ErrorCode::Conflict);

        // 关闭复盘
        const auto closed = useCases.closeReview(melUid, 2);
        if (!closed)
            QFAIL(qPrintable(QString::fromStdString(closed.error().message + ": "
                                                    + closed.error().detail)));
        QVERIFY(closed.value().status == Domain::ReviewStatus::Closed);
        QVERIFY(closed.value().completedAt.has_value());
        // 已关闭不可再提交
        QVERIFY(!useCases.submitReview(melUid, 3, input));
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstReviewState)
#include "tst_review_state.moc"
