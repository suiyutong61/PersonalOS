// 模块：性能基准（requirements 10.9；architecture 6.1 性能基准）
// 覆盖：核心只读路径（领域清单加载、状态上下文、覆盖率、首页聚合、知识列表）
//       在代表性小数据规模下的响应时间测量；阈值采用宽松 CI 上限（<2s），
//       实测数值打印并作为回归参考（参考环境见输出）。
#include <QtTest>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSqlQuery>

#include "application/usecases/domain/DomainRegistry.h"
#include "application/usecases/domain/StateContextBuilder.h"
#include "application/usecases/goal/ContentMapUseCases.h"
#include "application/usecases/goal/GoalUseCases.h"
#include <functional>
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/knowledge/StateDefinitionsSeed.h"
#include "infrastructure/persistence/SqlContentMapRepository.h"
#include "infrastructure/persistence/SqlDomainManifestRepository.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlKnowledgeRepository.h"
#include "infrastructure/persistence/SqlMelRepository.h"
#include "infrastructure/persistence/SqlStateRepository.h"

using namespace PersonOS;

namespace {

// 宽松 CI 上限（真实产品门槛按 requirements 10.9 为 200ms/2s/5s；
// 共享 CI 机器上允许 10 倍裕度，记录实测供参考环境核对）
constexpr qint64 kCiUpperBoundMs = 2000;

} // namespace

class TstBenchmark : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_benchmark.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));
        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
        QVERIFY(Infrastructure::LearningManifestSeed(DatabaseManager::instance().database(),
                                                     m_clock)
                    .ensureSeeded());
        QVERIFY(Infrastructure::StateDefinitionsSeed(DatabaseManager::instance().database(),
                                                     m_clock)
                    .ensureSeeded());

        QSqlQuery user(DatabaseManager::instance().database());
        user.exec(QStringLiteral(
            "INSERT INTO user_profiles_v3(uid,timezone_id,locale,onboarding_status,profile_json,"
            "created_at,updated_at) VALUES('00000000-0000-0000-0000-0000000000aa',"
            "'Asia/Shanghai','zh-CN','complete','{}','2026-09-27T00:00:00Z','2026-09-27T00:00:00Z')"));

        // 代表性小数据规模：10 个目标 + 每个一张已确认地图（10 节点）
        QSqlQuery manifest(DatabaseManager::instance().database());
        manifest.exec(QStringLiteral(
            "SELECT uid FROM domain_manifests_v3 WHERE domain_code='learning'"));
        manifest.next();
        const QString manifestUid = manifest.value(0).toString();
        Infrastructure::SqlGoalRepository goals(DatabaseManager::instance().database(), m_clock);
        Infrastructure::SqlContentMapRepository maps(DatabaseManager::instance().database(),
                                                     m_clock);
        Application::GoalUseCases goalUseCases(goals, m_uids, m_clock);
        Application::ContentMapUseCases mapUseCases(maps, goals, m_uids, m_clock);
        for (int i = 0; i < 10; ++i) {
            Application::GoalUseCases::CreateInput input;
            input.userId = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");
            input.domainManifestId = *Domain::Uid::parse(manifestUid.toStdString());
            input.title = QStringLiteral("基准目标%1").arg(i).toStdString();
            input.desiredLevelJson = "{}";
            const auto goal = goalUseCases.createGoal(input);
            QVERIFY(goal);
            Application::ContentMapUseCases::CreateMapInput mapInput;
            mapInput.userId = input.userId;
            mapInput.goalId = goal.value().goal.uid;
            mapInput.title = QStringLiteral("教材%1").arg(i).toStdString();
            mapInput.sourceType = "user_document";
            const auto map = mapUseCases.createMap(mapInput);
            QVERIFY(map);
            Application::ContentMapUseCases::AddNodesInput nodesInput;
            for (int n = 0; n < 10; ++n) {
                Domain::ContentNode node;
                node.title = QStringLiteral("第%1章").arg(n).toStdString();
                node.nodeType = "chapter";
                node.sequenceNo = n;
                node.weight = 1.0;
                nodesInput.nodes.push_back(node);
            }
            QVERIFY(mapUseCases.addNodes(map.value().uid, nodesInput));
            QVERIFY(mapUseCases.confirmMap(map.value().uid, 1));
        }
    }

    void coreReadPaths()
    {
        auto measure = [](const char *label, const std::function<void()> &work) {
            QElapsedTimer timer;
            timer.start();
            work();
            const qint64 elapsed = timer.elapsed();
            qInfo().noquote() << QStringLiteral("benchmark %1: %2 ms").arg(label).arg(elapsed);
            return elapsed;
        };

        const auto userUid = *Domain::Uid::parse("00000000-0000-0000-0000-0000000000aa");
        const auto &db = DatabaseManager::instance().database();

        // 领域清单加载（含 JSON Schema 校验）
        const qint64 registryMs = measure("domain_registry_load", [&]() {
            Infrastructure::SqlDomainManifestRepository manifests(db);
            Application::DomainRegistry registry(manifests);
            QVERIFY(registry.load("learning"));
        });
        QVERIFY2(registryMs < kCiUpperBoundMs, "domain registry load too slow");

        // 状态上下文（22 定义三态时效）
        const qint64 contextMs = measure("state_context_build", [&]() {
            Infrastructure::SqlStateRepository states(db, m_clock);
            Application::StateContextBuilder builder(states, m_clock);
            QVERIFY(builder.items(userUid, m_clock.utcIso()));
        });
        QVERIFY2(contextMs < kCiUpperBoundMs, "state context build too slow");

        // 覆盖率（10 目标 × 10 节点）
        const qint64 coverageMs = measure("coverage_by_goal", [&]() {
            Infrastructure::SqlGoalRepository goals(db, m_clock);
            Infrastructure::SqlContentMapRepository maps(db, m_clock);
            Application::ContentMapUseCases useCases(maps, goals, m_uids, m_clock);
            for (const auto &goal : goals.findByUser(userUid))
                QVERIFY(useCases.coverageByGoal(goal.uid, userUid));
        });
        QVERIFY2(coverageMs < kCiUpperBoundMs, "coverage too slow");

        // 首页聚合（活跃 MEL + 提醒 + 知识列表）
        const qint64 dashboardMs = measure("dashboard_aggregate", [&]() {
            Infrastructure::SqlMelRepository mels(db, m_clock);
            mels.findActive(userUid, 1);
            Infrastructure::SqlKnowledgeRepository knowledge(db, m_clock);
            knowledge.listRecent(100);
            QSqlQuery pending(db);
            pending.exec(QStringLiteral("SELECT COUNT(*) FROM reminder_deliveries_v6 "
                                        "WHERE status='pending'"));
        });
        QVERIFY2(dashboardMs < kCiUpperBoundMs, "dashboard aggregate too slow");

        // 迁移与结构检查（启动路径参考）
        const qint64 startupMs = measure("startup_checks", [&]() {
            QSqlQuery check(db);
            check.exec(QStringLiteral("PRAGMA foreign_key_check"));
        });
        QVERIFY2(startupMs < kCiUpperBoundMs, "startup checks too slow");

        qInfo().noquote() << "reference environment: Windows 11 + Qt 6.11.2/MinGW "
                             "+ local SQLite, 10 goals / 100 content nodes";
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstBenchmark)
#include "tst_benchmark.moc"
