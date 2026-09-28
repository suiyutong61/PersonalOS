#pragma once

#include <QSqlDatabase>
#include <QString>

namespace PersonOS::Infrastructure {

class DatabaseConnectionFactory final
{
public:
    explicit DatabaseConnectionFactory(QString databasePath);

    QSqlDatabase openForCurrentThread(const QString &purpose, QString *error = nullptr) const;
    static QString connectionName(const QString &purpose);
    static void closeCurrentThreadConnection(const QString &purpose);

private:
    QString m_databasePath;
};

} // namespace PersonOS::Infrastructure
