// 模块：备份恢复切换（DR-026；数据库设计 §7 安全恢复流程）
// 覆盖：验证过的备份整体恢复（数据回滚到备份状态、连接重开可用、恢复前
//       安全快照保留）、哈希篡改拒绝、损坏文件拒绝、未验证记录拒绝、
//       不存在的记录 NotFound、失败路径保持原数据可用。
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "application/usecases/operations/OperationUseCases.h"
#include "database/DatabaseManager.h"
#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/knowledge/LearningManifestSeed.h"
#include "infrastructure/operations/DatabaseManagerSwitch.h"
#include "infrastructure/operations/SqlOperationsRepository.h"
#include "infrastructure/operations/SqliteBackup.h"

using namespace PersonOS;

namespace {
const char *kUserUid = "00000000-0000-0000-0000-0000000000aa";
} // namespace

class TstRestore : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_restore.db"));
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
        const bool userOk = user.exec(QStringLiteral(
            "INSERT INTO user_profiles_v3(uid,timezone_id,locale,onboarding_status,profile_json,"
            "created_at,updated_at) VALUES('00000000-0000-0000-0000-0000000000aa',"
            "'Asia/Shanghai','zh-CN','complete','{}','2026-09-27T00:00:00Z','2026-09-27T00:00:00Z')"));
        if (!userOk)
            QFAIL(qPrintable(QStringLiteral("user insert: %1").arg(user.lastError().text())));

        // 备份前数据
        QSqlQuery goal(DatabaseManager::instance().database());
        const bool goalOk = goal.exec(QStringLiteral(
            "INSERT INTO goals_v3(uid,user_id,domain_manifest_id,title,description,goal_type,"
            "status,priority,desired_level_json,user_defined_level,sort_order,created_at,"
            "updated_at) SELECT '00000000-0000-0000-0000-0000000000ca',u.id,dm.id,'恢复前目标',"
            "'','course','active',1,'{}',1,0,'2026-09-27T00:00:00Z','2026-09-27T00:00:00Z' "
            "FROM user_profiles_v3 u, domain_manifests_v3 dm WHERE "
            "u.uid='00000000-0000-0000-0000-0000000000aa' AND dm.domain_code='learning'"));
        if (!goalOk)
            QFAIL(qPrintable(QStringLiteral("goal insert: %1").arg(goal.lastError().text())));
    }

    // 创建一份已验证备份（QTEST 宏只能在 void 槽中使用，此处返回 optional）
    std::optional<Domain::BackupRecord> makeBackup(const QString &targetPath)
    {
        Infrastructure::SqlOperationsRepository repo(DatabaseManager::instance().database(),
                                                     m_clock);
        Infrastructure::SqliteBackup snapshots;
        Application::BackupService backups(repo, snapshots, m_uids, m_clock);
        Application::BackupService::CreateInput input;
        input.sourceDbPath = m_path.toStdString();
        input.targetPath = targetPath.toStdString();
        input.dbSchemaVersion = 6;
        const auto backup = backups.createBackup(input);
        if (!backup)
            return std::nullopt;
        return backup.value();
    }

    void restoreRollsBackToBackupState()
    {
        const QString backupFile =
            QDir::temp().filePath(QStringLiteral("personos_restore_backup.db"));
        QFile::remove(backupFile);
        const auto backupRecord = makeBackup(backupFile);
        QVERIFY2(backupRecord.has_value(), "makeBackup failed");
        const Domain::BackupRecord record = *backupRecord;

        // 备份之后写入新数据（应被恢复回滚）
        QSqlQuery postBackup(DatabaseManager::instance().database());
        QVERIFY(postBackup.exec(QStringLiteral(
            "INSERT INTO goals_v3(uid,user_id,domain_manifest_id,title,description,goal_type,"
            "status,priority,desired_level_json,user_defined_level,sort_order,created_at,"
            "updated_at) SELECT '00000000-0000-0000-0000-0000000000cb',u.id,dm.id,'恢复后新增',"
            "'','course','active',1,'{}',1,0,'2026-09-28T00:00:00Z','2026-09-28T00:00:00Z' "
            "FROM user_profiles_v3 u, domain_manifests_v3 dm WHERE "
            "u.uid='00000000-0000-0000-0000-0000000000aa' AND dm.domain_code='learning'")));

        // 恢复（受控点：此作用域结束前不持有数据库副本）
        Application::RestoreService::RestoreReport report;
        {
            Infrastructure::SqlOperationsRepository repo(DatabaseManager::instance().database(),
                                                         m_clock);
            Infrastructure::DatabaseManagerSwitch switcher;
            Application::RestoreService restores(repo, switcher, m_uids, m_clock);
            const auto restored = restores.restore(record.uid, DatabaseManager::instance().schemaVersion());
            if (!restored)
                QFAIL(qPrintable(QString::fromStdString(restored.error().message + ": "
                                                        + restored.error().detail)));
            report = restored.value();
        }
        QVERIFY(report.switched);
        QVERIFY(!report.preRestoreSnapshotPath.empty());
        QVERIFY(QFile::exists(QString::fromStdString(report.preRestoreSnapshotPath)));

        // 恢复后：连接已重开可用；数据回到备份状态
        QSqlQuery check(DatabaseManager::instance().database());
        QVERIFY(check.exec(QStringLiteral(
            "SELECT COUNT(*) FROM goals_v3 WHERE title='恢复前目标'")));
        QVERIFY(check.next());
        QCOMPARE(check.value(0).toInt(), 1);
        QVERIFY(check.exec(QStringLiteral(
            "SELECT COUNT(*) FROM goals_v3 WHERE title='恢复后新增'")));
        QVERIFY(check.next());
        QCOMPARE(check.value(0).toInt(), 0);

        // 恢复后：用新连接追加备份状态与审计（切换后旧连接失效）
        {
            Infrastructure::SqlOperationsRepository repo(DatabaseManager::instance().database(),
                                                         m_clock);
            const auto current = repo.find(record.uid);
            QVERIFY(current);
            auto updated = *current;
            // 备份文件快照早于"verified"状态写入，恢复后记录回到快照态；
            // 此处用新连接补记完成（状态枚举固定 running/verified/failed）
            updated.status = "verified";
            updated.completedAt = m_clock.utcIso();
            const auto updatedSaved = repo.update(updated);
            QVERIFY2(updatedSaved.ok,
                     qPrintable(QString::fromStdString(updatedSaved.error.message + ": "
                                                       + updatedSaved.error.detail)));
        }
        QSqlQuery audit(DatabaseManager::instance().database());
        QVERIFY(audit.exec(QStringLiteral(
            "SELECT COUNT(*) FROM backup_records_v6 WHERE status='verified'")));
        QVERIFY(audit.next());
        QCOMPARE(audit.value(0).toInt(), 1);
    }

    void failurePathsKeepOriginalData()
    {
        // 不存在 → NotFound
        {
            Infrastructure::SqlOperationsRepository repo(DatabaseManager::instance().database(),
                                                         m_clock);
            Infrastructure::DatabaseManagerSwitch switcher;
            Application::RestoreService restores(repo, switcher, m_uids, m_clock);
            const auto missing = restores.restore(
                *Domain::Uid::parse("00000000-0000-0000-0000-0000000000ff"),
                DatabaseManager::instance().schemaVersion());
            QVERIFY(!missing);
            QVERIFY(missing.error().code == Application::ErrorCode::NotFound);
        }

        // 篡改备份文件 → 哈希拒绝；原数据不动
        const QString tamperedFile =
            QDir::temp().filePath(QStringLiteral("personos_restore_tampered.db"));
        QFile::remove(tamperedFile);
        const auto backupRecord = makeBackup(tamperedFile);
        QVERIFY2(backupRecord.has_value(), "makeBackup failed");
        const Domain::BackupRecord record = *backupRecord;
        {
            QFile file(tamperedFile);
            QVERIFY(file.open(QIODevice::Append));
            file.write("tamper");
            file.close();
        }
        {
            Infrastructure::SqlOperationsRepository repo(DatabaseManager::instance().database(),
                                                         m_clock);
            Infrastructure::DatabaseManagerSwitch switcher;
            Application::RestoreService restores(repo, switcher, m_uids, m_clock);
            const auto tampered = restores.restore(record.uid, DatabaseManager::instance().schemaVersion());
            QVERIFY(!tampered);
            QVERIFY(tampered.error().code == Application::ErrorCode::Conflict);
        }

        // 未验证记录 → Conflict
        Domain::BackupRecord unverified;
        unverified.uid = m_uids.next();
        unverified.startedAt = m_clock.utcIso();
        unverified.relativePath = tamperedFile.toStdString();
        unverified.status = "running";
        unverified.dbSchemaVersion = 6;
        {
            Infrastructure::SqlOperationsRepository repo(DatabaseManager::instance().database(),
                                                         m_clock);
            QVERIFY(repo.insert(unverified).ok);
            Infrastructure::DatabaseManagerSwitch switcher;
            Application::RestoreService restores(repo, switcher, m_uids, m_clock);
            const auto rejected = restores.restore(unverified.uid, DatabaseManager::instance().schemaVersion());
            QVERIFY(!rejected);
            QVERIFY(rejected.error().code == Application::ErrorCode::Conflict);
        }

        // 原数据完好
        QSqlQuery check(DatabaseManager::instance().database());
        QVERIFY(check.exec(QStringLiteral(
            "SELECT COUNT(*) FROM goals_v3 WHERE title='恢复前目标'")));
        QVERIFY(check.next());
        QCOMPARE(check.value(0).toInt(), 1);
    }

private:
    QString m_path;
    Infrastructure::QtSystemClock m_clock;
    Infrastructure::QtUidGenerator m_uids;
};

QTEST_GUILESS_MAIN(TstRestore)
#include "tst_restore.moc"
