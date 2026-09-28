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
