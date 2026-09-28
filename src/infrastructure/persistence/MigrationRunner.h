#pragma once

#include <QSqlDatabase>

#include <cstddef>
#include <functional>

#include "application/foundation/ApplicationError.h"
#include "application/foundation/Result.h"

namespace PersonOS::Infrastructure {

class MigrationRunner final
{
public:
    using BeforeStatement = std::function<bool(int version, std::size_t statementIndex)>;

    static int currentVersion(const QSqlDatabase &database);
    Application::Result<int, Application::ApplicationError> migrate(
        QSqlDatabase database, BeforeStatement beforeStatement = {}) const;
};

} // namespace PersonOS::Infrastructure
