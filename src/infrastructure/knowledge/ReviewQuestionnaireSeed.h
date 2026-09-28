#pragma once

#include <QSqlDatabase>

#include "application/foundation/ApplicationError.h"
#include "application/foundation/Result.h"
#include "domain/foundation/Clock.h"

// 复盘状态问卷种子（requirements R3.5：每轮复盘提供可跳过、可修改的拓展状态问卷）
// 覆盖基本状态 + 十类拓展状态（16 项状态定义）；问卷可跳过，跳过不算确认。
// 幂等：code='review_extended_states' 且 version_no=1 已存在时跳过。
namespace PersonOS::Infrastructure {

class ReviewQuestionnaireSeed
{
public:
    explicit ReviewQuestionnaireSeed(QSqlDatabase database, const Domain::Clock &clock);

    Application::Result<void, Application::ApplicationError> ensureSeeded();

private:
    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
