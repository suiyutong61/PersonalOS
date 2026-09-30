#include "infrastructure/persistence/AuditDatabaseSink.h"

#include <QSqlDatabase>

#include <utility>

#include "infrastructure/foundation/QtSystemClock.h"
#include "infrastructure/foundation/QtUidGenerator.h"
#include "infrastructure/persistence/DatabaseConnectionFactory.h"
#include "infrastructure/persistence/SqlAuditRepository.h"

namespace PersonOS::Infrastructure {

AuditDatabaseSink::AuditDatabaseSink(QString databasePath)
    : m_databasePath(std::move(databasePath))
{}

Application::Result<void, Application::ApplicationError> AuditDatabaseSink::append(
    const Application::AuditEvent &event)
{
    DatabaseConnectionFactory factory(m_databasePath);
    QString openError;
    const QSqlDatabase database =
        factory.openForCurrentThread(QStringLiteral("audit"), &openError);
    if (!database.isValid())
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Storage,
             "audit connection failed: " + openError.toStdString(), {}, false});
    // 连接按线程名复用，不在此关闭；时钟与 uid 生成器为调用栈瞬态
    QtSystemClock clock;
    QtUidGenerator uids;
    SqlAuditRepository repository(database, clock, uids);
    return repository.append(event);
}

} // namespace PersonOS::Infrastructure
