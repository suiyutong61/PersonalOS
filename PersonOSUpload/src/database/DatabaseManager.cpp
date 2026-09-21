#include "database/DatabaseManager.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

#include "database/Migrations.h"

namespace {
const QString kConnectionName = QStringLiteral("personos_main");
const QString kSchemaVersionKey = QStringLiteral("schema_version");
} // namespace

DatabaseManager &DatabaseManager::instance()
{
    static DatabaseManager mgr;
    return mgr;
}

QString DatabaseManager::databasePath() const
{
    // 测试隔离：设置 PERSONOS_DB_PATH 环境变量后使用指定文件（冒烟测试用）
    const QString overridePath = qEnvironmentVariable("PERSONOS_DB_PATH");
    if (!overridePath.isEmpty())
        return overridePath;

    // AppLocalDataLocation：Windows 上为 %LOCALAPPDATA%/PersonalOS/PersonalOS/personos.db
    // （注意：AppDataLocation 默认指向 Roaming，本地大文件数据库用 Local 更合适）
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/personos.db");
}

bool DatabaseManager::open()
{
    if (QSqlDatabase::contains(kConnectionName))
        return true; // 已打开（例如 --db-check 重复调用）

    const QFileInfo fi(databasePath());
    if (!QDir().mkpath(fi.absolutePath())) {
        m_lastError = QStringLiteral("无法创建数据目录: %1").arg(fi.absolutePath());
        return false;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnectionName);
    db.setDatabaseName(databasePath());
    if (!db.open()) {
        m_lastError = db.lastError().text();
        return false;
    }

    // 连接级 PRAGMA（WAL 见 3.3.2；外键约束见 README 3.3.2 建表纪律）
    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));

    if (!applyMigrations()) {
        db.close();
        return false;
    }
    return true;
}

QSqlDatabase DatabaseManager::database() const
{
    return QSqlDatabase::database(kConnectionName);
}

int DatabaseManager::schemaVersion() const
{
    if (!QSqlDatabase::contains(kConnectionName))
        return 0;

    // app_meta 表由 v1 迁移创建；未迁移时不存在，返回 0
    QSqlQuery q(database());
    if (!q.exec(QStringLiteral("SELECT value FROM app_meta WHERE key='%1'").arg(kSchemaVersionKey)))
        return 0;
    if (!q.next())
        return 0;
    return q.value(0).toInt();
}

QString DatabaseManager::lastError() const
{
    return m_lastError;
}

bool DatabaseManager::applyMigrations()
{
    // 依次执行 version 大于当前 schema_version 的迁移；
    // 每个 Step 一个事务，成功后更新 schema_version（Migrations.h 规则 2/3）。
    const int current = schemaVersion();

    for (const auto &step : Migrations::kSteps) {
        if (step.version <= current)
            continue;

        QSqlDatabase db = database();
        if (!db.transaction()) {
            m_lastError = QStringLiteral("迁移 v%1 无法开启事务: %2")
                              .arg(step.version)
                              .arg(db.lastError().text());
            return false;
        }

        bool ok = true;
        for (const char *sql : step.statements) {
            QSqlQuery q(db);
            if (!q.exec(QString::fromUtf8(sql))) {
                m_lastError = QStringLiteral("迁移 v%1 失败: %2")
                                  .arg(step.version)
                                  .arg(q.lastError().text());
                ok = false;
                break;
            }
        }

        if (ok) {
            QSqlQuery q(db);
            ok = q.exec(QStringLiteral(
                            "INSERT INTO app_meta(key, value) VALUES('%1', '%2') "
                            "ON CONFLICT(key) DO UPDATE SET value='%2'")
                            .arg(kSchemaVersionKey)
                            .arg(step.version));
            if (!ok)
                m_lastError = QStringLiteral("迁移 v%1 写入版本号失败: %2")
                                  .arg(step.version)
                                  .arg(q.lastError().text());
        }

        if (ok)
            db.commit();
        else
            db.rollback();

        if (!ok)
            return false;
    }
    return true;
}
