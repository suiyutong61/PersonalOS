#include "infrastructure/persistence/DatabaseConnectionFactory.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QThread>

namespace PersonOS::Infrastructure {

DatabaseConnectionFactory::DatabaseConnectionFactory(QString databasePath)
    : m_databasePath(std::move(databasePath))
{}

QString DatabaseConnectionFactory::connectionName(const QString &purpose)
{
    return QStringLiteral("personos_%1_%2")
        .arg(purpose)
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);
}

QSqlDatabase DatabaseConnectionFactory::openForCurrentThread(const QString &purpose,
                                                              QString *error) const
{
    const QString name = connectionName(purpose);
    QSqlDatabase db = QSqlDatabase::contains(name)
                          ? QSqlDatabase::database(name, false)
                          : QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(m_databasePath);
    if (!db.isOpen() && !db.open()) {
        if (error)
            *error = db.lastError().text();
        return db;
    }

    QSqlQuery pragma(db);
    const QStringList statements{QStringLiteral("PRAGMA foreign_keys=ON"),
                                 QStringLiteral("PRAGMA journal_mode=WAL"),
                                 QStringLiteral("PRAGMA synchronous=NORMAL"),
                                 QStringLiteral("PRAGMA busy_timeout=5000")};
    for (const auto &statement : statements) {
        if (!pragma.exec(statement)) {
            if (error)
                *error = pragma.lastError().text();
            db.close();
            return db;
        }
    }
    return db;
}

void DatabaseConnectionFactory::closeCurrentThreadConnection(const QString &purpose)
{
    const QString name = connectionName(purpose);
    if (!QSqlDatabase::contains(name))
        return;
    {
        QSqlDatabase db = QSqlDatabase::database(name, false);
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
}

} // namespace PersonOS::Infrastructure
