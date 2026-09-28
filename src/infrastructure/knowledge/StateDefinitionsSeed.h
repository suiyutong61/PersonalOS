#pragma once

#include <QSqlDatabase>

#include "application/foundation/ApplicationError.h"
#include "application/foundation/Result.h"
#include "domain/foundation/Clock.h"
#include "domain/foundation/Uid.h"

// 状态定义种子（DR-036：基本状态 + 十类拓展状态）
// 敏感类别（身体/情绪等）标 sensitivity=sensitive（S3 最小收集）。
// 幂等：code 已存在则跳过。
namespace PersonOS::Infrastructure {

class StateDefinitionsSeed
{
public:
    explicit StateDefinitionsSeed(QSqlDatabase database, const Domain::Clock &clock);

    Application::Result<void, Application::ApplicationError> ensureSeeded();

private:
    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
