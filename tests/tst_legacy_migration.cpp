// 模块：真实 v2 数据迁移（E2E-18；数据库设计 §7 数据搬迁）
// 覆盖：目标（含父子层级）→ goal_v3、状态快照→导入态状态事件、
//       closed 计划→历史 MEL（任务+实际耗时事件）、active 计划不冒充 MEL、
//       复盘挂接、重复运行幂等（legacy_id_map）、旧表不删除。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "application/usecases/migration/LegacyMigration.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/knowledge/StateDefinitionsSeed.h"
#include "infrastructure/persistence/SqlGoalRepository.h"
#include "infrastructure/persistence/SqlMelRepository.h"
#include "infrastructure/persistence/SqlReviewRepository.h"
#include "infrastructure/persistence/SqlStateRepository.h"

using namespace PersonOS;

namespace {
const char *kUserUid = "00000000-0000-0000-0000-0000000000aa";
} // namespace

class TstLegacyMigration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_legacy_migration.db"));
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
        const bool userOk = user.exec(QStringLiteral(
            "INSERT INTO user_profiles_v3(uid,timezone_id,locale,onboarding_status,profile_json,"
            "created_at,updated_at) VALUES('00000000-0000-0000-0000-0000000000aa',"
            "'Asia/Shanghai','zh-CN','complete','{}','2026-09-27T00:00:00Z','2026-09-27T00:00:00Z')"));
        if (!userOk)
            QFAIL(qPrintable(QStringLiteral("user insert: %1").arg(user.lastError().text())));

        // 旧表样例（v1/v2 结构）：目标（含父子）、状态快照、closed+active 计划、
        // 任务、事件、复盘
        QSqlQuery data(DatabaseManager::instance().database());
        const QStringList statements = {
            QStringLiteral("INSERT INTO goals(id,parent_id,level,title,description,status,"
                           "priority) VALUES(1,NULL,'long_term','考研目标','计算机考研','active',80),"
                           "(2,1,'quarterly','操作系统','','active',60),"
                           "(3,1,'quarterly','数据库','','achieved',50)"),
            QStringLiteral("INSERT INTO state_snapshots(id,date,energy,focus,mood) "
                           "VALUES(1,'2026-09-20',4,3,4),(2,'2026-09-21',3,2,3)"),
            QStringLiteral("INSERT INTO plans(id,period_type,period_start,period_end,status)"
                           "VALUES(1,'daily','2026-09-20','2026-09-21','closed'),"
                           "(2,'daily','2026-09-21','2026-09-22','active')"),
            QStringLiteral("INSERT INTO tasks(id,plan_id,goal_id,title,planned_minutes,due_date,"
                           "status,actual_minutes,sort_order) "
                           "VALUES(1,1,2,'复习进程模型',60,'2026-09-20','completed',55,0),"
                           "(2,1,2,'复习调度',60,'2026-09-21','partial',70,1),"
                           "(3,2,3,'数据库事务',60,'2026-09-21','started',NULL,0)"),
            QStringLiteral("INSERT INTO events(id,date,type,title,task_id) "
                           "VALUES(1,'2026-09-20','execution','完成进程模型复习',1)"),
            QStringLiteral("INSERT INTO reviews(id,review_type,period_start,summary,problems,"
                           "next_actions) VALUES(1,'daily','2026-09-20','完成度 83%','耗时偏高',"
                           "'降低任务量')"),
        };
        for (const QString &statement : statements) {
            QSqlQuery query(DatabaseManager::instance().database());
            if (!query.exec(statement))
                QFAIL(qPrintable(QStringLiteral("legacy insert: %1").arg(
                    query.lastError().text())));
        }
    }

    void migrationMapsAndIsIdempotent()
    {
        Infrastructure::SqlGoalRepository goals(DatabaseManager::instance().database(), m_clock);
        Infrastructure::SqlMelRepository mels(DatabaseManager::instance().database(), m_clock);
        Infrastructure::SqlReviewRepository reviews(DatabaseManager::instance().database(),
                                                    m_clock);
        Infrastructure::SqlStateRepository states(DatabaseManager::instance().database(), m_clock);
        Application::LegacyMigration migration(DatabaseManager::instance().database(), goals,
                                               mels, reviews, states, m_uids, m_clock);

        const auto report = migration.run();
        if (!report)
            QFAIL(qPrintable(QString::fromStdString(report.error().message + ": "
                                                    + report.error().detail)));
        QCOMPARE(report.value().goalsMigrated, 3);
        QCOMPARE(report.value().stateSnapshotsMigrated, 2);
        QCOMPARE(report.value().plansMigrated, 1);
        QCOMPARE(report.value().plansSkippedActive, 1);   // active 计划不冒充 MEL
        QCOMPARE(report.value().tasksMigrated, 2);
        QCOMPARE(report.value().reviewsMigrated, 1);

        // goal_v3 落库 + 父子关系
        QSqlQuery goalCount(DatabaseManager::instance().database());
        QVERIFY(goalCount.exec(QStringLiteral(
            "SELECT COUNT(*) FROM goals_v3 WHERE title IN ('考研目标','操作系统','数据库')")));
        QVERIFY(goalCount.next());
        QCOMPARE(goalCount.value(0).toInt(), 3);
        QSqlQuery relations(DatabaseManager::instance().database());
        QVERIFY(relations.exec(QStringLiteral("SELECT COUNT(*) FROM goal_relations_v3")));
        QVERIFY(relations.next());
        QCOMPARE(relations.value(0).toInt(), 2);

        // 历史 MEL（closed）+ 任务 + 实际耗时事件
        QSqlQuery melCount(DatabaseManager::instance().database());
        QVERIFY(melCount.exec(QStringLiteral(
            "SELECT COUNT(*) FROM mels_v4 WHERE state='closed'")));
        QVERIFY(melCount.next());
        QCOMPARE(melCount.value(0).toInt(), 1);
        QSqlQuery taskCount(DatabaseManager::instance().database());
        QVERIFY(taskCount.exec(QStringLiteral("SELECT COUNT(*) FROM mel_tasks_v4")));
        QVERIFY(taskCount.next());
        QCOMPARE(taskCount.value(0).toInt(), 2);
        QSqlQuery eventCount(DatabaseManager::instance().database());
        QVERIFY(eventCount.exec(QStringLiteral(
            "SELECT COUNT(*) FROM progress_events_v4 WHERE idempotency_key LIKE 'mig:task:%'")));
        QVERIFY(eventCount.next());
        QCOMPARE(eventCount.value(0).toInt(), 2);   // 两个任务均有实际耗时

        // 导入态状态事件
        QSqlQuery stateCount(DatabaseManager::instance().database());
        QVERIFY(stateCount.exec(QStringLiteral(
            "SELECT COUNT(*) FROM state_events_v4 WHERE source='imported'")));
        QVERIFY(stateCount.next());
        QCOMPARE(stateCount.value(0).toInt(), 6);   // 2 快照 × 3 指标

        // 复盘挂接
        QSqlQuery reviewCount(DatabaseManager::instance().database());
        QVERIFY(reviewCount.exec(QStringLiteral("SELECT COUNT(*) FROM reviews_v4")));
        QVERIFY(reviewCount.next());
        QCOMPARE(reviewCount.value(0).toInt(), 1);

        // 幂等：重复运行不产生新对象
        const auto again = migration.run();
        QVERIFY(again);
        QCOMPARE(again.value().goalsMigrated, 0);
        QCOMPARE(again.value().plansMigrated, 0);
        QCOMPARE(again.value().tasksMigrated, 0);

        // 旧表未删除（数据保留）
        QSqlQuery legacy(DatabaseManager::instance().database());
        QVERIFY(legacy.exec(QStringLiteral("SELECT COUNT(*) FROM goals")));
        QVERIFY(legacy.next());
        QCOMPARE(legacy.value(0).toInt(), 3);
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstLegacyMigration)
#include "tst_legacy_migration.moc"
