#include "infrastructure/operations/SqliteBackup.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace PersonOS::Infrastructure {

Application::Result<void, Application::ApplicationError> SqliteBackup::createSnapshot(
    const std::string &sourceDbPath, const std::string &targetPath)
{
    if (!QFile::exists(QString::fromStdString(sourceDbPath)))
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::NotFound, "source database not found", {}, false});

    const QString connectionName = QStringLiteral("personos_backup_worker");
    {
        QSqlDatabase db = QSqlDatabase::contains(connectionName)
                              ? QSqlDatabase::database(connectionName, false)
                              : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                          connectionName);
        db.setDatabaseName(QString::fromStdString(sourceDbPath));
        if (!db.isOpen() && !db.open())
            return Application::Result<void, Application::ApplicationError>::failure(
                {Application::ErrorCode::Storage, "backup source open failed",
                 db.lastError().text().toStdString(), false});
        // 与主连接/factory 一致:后台回填与向量重建可能持写锁,
        // 默认 busy_timeout=0 会让 VACUUM INTO 立即 SQLITE_BUSY。
        // 注意 VACUUM 要求连接上无存活语句:PRAGMA 查询限定作用域。
        {
            QSqlQuery backupPragma(db);
            backupPragma.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
        }
        QSqlQuery vacuum(db);
        const QString escaped = QString::fromStdString(targetPath).replace("'", "''");
        if (!vacuum.exec(QStringLiteral("VACUUM INTO '%1'").arg(escaped)))
            return Application::Result<void, Application::ApplicationError>::failure(
                {Application::ErrorCode::Storage, "vacuum into failed",
                 vacuum.lastError().text().toStdString(), false});
        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return Application::Result<void, Application::ApplicationError>::success();
}

} // namespace PersonOS::Infrastructure
