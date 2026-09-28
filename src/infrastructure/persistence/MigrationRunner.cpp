#include "infrastructure/persistence/MigrationRunner.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "database/Migrations.h"

namespace PersonOS::Infrastructure {
namespace {
constexpr auto kVersionKey = "schema_version";

Application::ApplicationError failure(const QString &message, const QString &detail)
{
    return {Application::ErrorCode::Storage,
            message.toStdString(),
            detail.toStdString(),
            false};
}
} // namespace

int MigrationRunner::currentVersion(const QSqlDatabase &database)
{
    if (!database.isOpen())
        return 0;
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT value FROM app_meta WHERE key=?"));
    query.addBindValue(QString::fromLatin1(kVersionKey));
    if (!query.exec() || !query.next())
        return 0;
    return query.value(0).toInt();
}

Application::Result<int, Application::ApplicationError> MigrationRunner::migrate(
    QSqlDatabase database, BeforeStatement beforeStatement) const
{
    if (!database.isOpen())
        return Application::Result<int, Application::ApplicationError>::failure(
            failure(QStringLiteral("数据库未打开"), database.lastError().text()));

    int current = currentVersion(database);
    for (const auto &step : Migrations::kSteps) {
        if (step.version <= current)
            continue;
        if (!database.transaction())
            return Application::Result<int, Application::ApplicationError>::failure(
                failure(QStringLiteral("迁移 v%1 无法开启事务").arg(step.version),
                        database.lastError().text()));

        std::size_t index = 0;
        bool ok = true;
        QString detail;
        for (const char *sql : step.statements) {
            if (beforeStatement && !beforeStatement(step.version, index)) {
                ok = false;
                detail = QStringLiteral("测试/调用方在语句 %1 前终止").arg(index);
                break;
            }
            QSqlQuery query(database);
            if (!query.exec(QString::fromUtf8(sql))) {
                ok = false;
                detail = query.lastError().text();
                break;
            }
            ++index;
        }

        if (ok) {
            QSqlQuery version(database);
            version.prepare(QStringLiteral(
                "INSERT INTO app_meta(key,value) VALUES(?,?) "
                "ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
            version.addBindValue(QString::fromLatin1(kVersionKey));
            version.addBindValue(QString::number(step.version));
            ok = version.exec();
            if (!ok)
                detail = version.lastError().text();
        }

        if (ok)
            ok = database.commit();
        else
            database.rollback();

        if (!ok) {
            if (detail.isEmpty())
                detail = database.lastError().text();
            return Application::Result<int, Application::ApplicationError>::failure(
                failure(QStringLiteral("迁移 v%1 失败").arg(step.version), detail));
        }
        current = step.version;
    }
    return Application::Result<int, Application::ApplicationError>::success(current);
}

} // namespace PersonOS::Infrastructure
