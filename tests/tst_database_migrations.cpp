#include <QtTest>

#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>

#include "database/DatabaseManager.h"
#include "database/Migrations.h"
#include "infrastructure/persistence/MigrationRunner.h"

class TstDatabaseMigrations : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_path = QDir::temp().filePath(QStringLiteral("personos_migration_v2_to_v6.db"));
        QFile::remove(m_path);
        QFile::remove(m_path + QStringLiteral("-wal"));
        QFile::remove(m_path + QStringLiteral("-shm"));

        QSqlDatabase seed = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                       QStringLiteral("migration_seed"));
        seed.setDatabaseName(m_path);
        QVERIFY2(seed.open(), qPrintable(seed.lastError().text()));
        QSqlQuery pragma(seed);
        QVERIFY(pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON")));

        int applied = 0;
        for (const auto &step : Migrations::kSteps) {
            if (step.version > 2)
                break;
            QVERIFY(seed.transaction());
            for (const char *statement : step.statements) {
                QSqlQuery query(seed);
                QVERIFY2(query.exec(QString::fromUtf8(statement)), qPrintable(query.lastError().text()));
            }
            QSqlQuery version(seed);
            QVERIFY(version.exec(QStringLiteral(
                "INSERT INTO app_meta(key,value) VALUES('schema_version','%1') "
                "ON CONFLICT(key) DO UPDATE SET value='%1'").arg(step.version)));
            QVERIFY(seed.commit());
            applied = step.version;
        }
        QCOMPARE(applied, 2);

        QSqlQuery oldRow(seed);
        QVERIFY(oldRow.exec(QStringLiteral(
            "INSERT INTO goals(level,title,status,priority) VALUES('weekly','legacy goal','active',50)")));
        seed.close();
        seed = QSqlDatabase();
        QSqlDatabase::removeDatabase(QStringLiteral("migration_seed"));

        qputenv("PERSONOS_DB_PATH", m_path.toUtf8());
        QVERIFY2(DatabaseManager::instance().open(),
                 qPrintable(DatabaseManager::instance().lastError()));
    }

    void upgradesWithoutChangingLegacyData()
    {
        QCOMPARE(DatabaseManager::instance().schemaVersion(), Migrations::kLatestVersion);
        QSqlQuery query(DatabaseManager::instance().database());
        QVERIFY(query.exec(QStringLiteral("SELECT title FROM goals WHERE title='legacy goal'")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("legacy goal"));
    }

    void createsCriticalTablesAndIndexes()
    {
        const QStringList tables{QStringLiteral("user_profiles_v3"),
                                 QStringLiteral("goals_v3"),
                                 QStringLiteral("mels_v4"),
                                 QStringLiteral("state_events_v4"),
                                 QStringLiteral("knowledge_items_v5"),
                                 QStringLiteral("knowledge_versions_v5"),
                                 QStringLiteral("ai_jobs_v6"),
                                 QStringLiteral("outbox_events_v6"),
                                 QStringLiteral("knowledge_fts_v6")};
        for (const auto &table : tables) {
            QSqlQuery query(DatabaseManager::instance().database());
            query.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE name=?"));
            query.addBindValue(table);
            QVERIFY(query.exec());
            QVERIFY2(query.next(), qPrintable(QStringLiteral("missing table: %1").arg(table)));
        }
    }

    void foreignKeysAreClean()
    {
        QSqlQuery query(DatabaseManager::instance().database());
        QVERIFY(query.exec(QStringLiteral("PRAGMA foreign_key_check")));
        QVERIFY(!query.next());
    }

    void strictConstraintsRejectInvalidRows()
    {
        QSqlQuery query(DatabaseManager::instance().database());
        QVERIFY(!query.exec(QStringLiteral(
            "INSERT INTO user_profiles_v3(uid,timezone_id,locale,onboarding_status,profile_json,created_at,updated_at) "
            "VALUES('u','Asia/Shanghai','zh-CN','invalid','{}','2026-01-01T00:00:00Z','2026-01-01T00:00:00Z')")));
    }

    void failedStepRollsBackAndCanResume()
    {
        const QString path = QDir::temp().filePath(QStringLiteral("personos_migration_rollback.db"));
        QFile::remove(path);
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                     QStringLiteral("migration_rollback"));
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery pragma(db);
        QVERIFY(pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON")));

        PersonOS::Infrastructure::MigrationRunner runner;
        const auto failed = runner.migrate(db, [](int version, std::size_t index) {
            return !(version == 1 && index == 2);
        });
        QVERIFY(!failed);
        QCOMPARE(PersonOS::Infrastructure::MigrationRunner::currentVersion(db), 0);

        QSqlQuery table(db);
        QVERIFY(table.exec(QStringLiteral(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='core_values'")));
        QVERIFY(!table.next());

        const auto resumed = runner.migrate(db);
        QVERIFY2(resumed, resumed ? "" : resumed.error().detail.c_str());
        QCOMPARE(resumed.value(), Migrations::kLatestVersion);
        const auto repeated = runner.migrate(db);
        QVERIFY(repeated);
        QCOMPARE(repeated.value(), Migrations::kLatestVersion);

        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(QStringLiteral("migration_rollback"));
        QFile::remove(path);
    }

private:
    QString m_path;
};

QTEST_GUILESS_MAIN(TstDatabaseMigrations)
#include "tst_database_migrations.moc"
