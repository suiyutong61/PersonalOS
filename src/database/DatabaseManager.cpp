#include "database/DatabaseManager.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

#include "infrastructure/persistence/MigrationRunner.h"

namespace {
const QString kConnectionName = QStringLiteral("personos_main");
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

void DatabaseManager::closeConnection()
{
    // 恢复切换前调用：移除具名连接；之后可再次 open()。
    // 调用方保证此刻没有存活的 QSqlDatabase 副本（恢复流程在受控点执行）。
    if (QSqlDatabase::contains(kConnectionName)) {
        {
            QSqlDatabase db = QSqlDatabase::database(kConnectionName);
            db.close();
        }
        QSqlDatabase::removeDatabase(kConnectionName);
    }
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
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        m_lastError = QStringLiteral("PRAGMA foreign_keys 失败: %1")
                          .arg(pragma.lastError().text());
        db.close();
        return false;
    }
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));
    // UI 导入与后台向量回填可能并发写，短锁等待避免 SQLITE_BUSY
    pragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));

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

    return PersonOS::Infrastructure::MigrationRunner::currentVersion(database());
}

QString DatabaseManager::lastError() const
{
    return m_lastError;
}

bool DatabaseManager::applyMigrations()
{
    const auto result = PersonOS::Infrastructure::MigrationRunner().migrate(database());
    if (result)
        return true;
    m_lastError = QString::fromStdString(result.error().message + ": " + result.error().detail);
    return false;
}
