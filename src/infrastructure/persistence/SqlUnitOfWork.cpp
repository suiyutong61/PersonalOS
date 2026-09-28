#include "infrastructure/persistence/SqlUnitOfWork.h"

#include <QSqlError>

namespace PersonOS::Infrastructure {

SqlUnitOfWork::SqlUnitOfWork(QSqlDatabase database) : m_database(std::move(database)) {}

SqlUnitOfWork::~SqlUnitOfWork()
{
    rollback();
}

Application::ApplicationError SqlUnitOfWork::storageError(const QString &operation) const
{
    return {Application::ErrorCode::Storage,
            operation.toStdString(),
            m_database.lastError().text().toStdString(),
            true};
}

Application::Result<void, Application::ApplicationError> SqlUnitOfWork::begin()
{
    if (m_active)
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Conflict, "transaction already active", {}, false});
    if (!m_database.transaction())
        return Application::Result<void, Application::ApplicationError>::failure(
            storageError(QStringLiteral("begin transaction failed")));
    m_active = true;
    return Application::Result<void, Application::ApplicationError>::success();
}

Application::Result<void, Application::ApplicationError> SqlUnitOfWork::commit()
{
    if (!m_active)
        return Application::Result<void, Application::ApplicationError>::failure(
            {Application::ErrorCode::Conflict, "no active transaction", {}, false});
    if (!m_database.commit())
        return Application::Result<void, Application::ApplicationError>::failure(
            storageError(QStringLiteral("commit transaction failed")));
    m_active = false;
    return Application::Result<void, Application::ApplicationError>::success();
}

void SqlUnitOfWork::rollback() noexcept
{
    if (m_active) {
        m_database.rollback();
        m_active = false;
    }
}

} // namespace PersonOS::Infrastructure
