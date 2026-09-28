#pragma once

#include <QSqlDatabase>

#include "application/ports/UnitOfWork.h"

namespace PersonOS::Infrastructure {

class SqlUnitOfWork final : public Application::UnitOfWork
{
public:
    explicit SqlUnitOfWork(QSqlDatabase database);
    ~SqlUnitOfWork() override;

    Application::Result<void, Application::ApplicationError> begin() override;
    Application::Result<void, Application::ApplicationError> commit() override;
    void rollback() noexcept override;
    bool active() const noexcept override { return m_active; }

private:
    Application::ApplicationError storageError(const QString &operation) const;
    QSqlDatabase m_database;
    bool m_active = false;
};

} // namespace PersonOS::Infrastructure
