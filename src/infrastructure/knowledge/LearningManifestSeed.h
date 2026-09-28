#pragma once

#include <QSqlDatabase>

#include "application/foundation/ApplicationError.h"
#include "application/foundation/Result.h"
#include "domain/foundation/Clock.h"

// 学习领域清单种子（DR-038 / domain-configuration-design-v1.md §2）
// 学习是 v1 首发领域包。清单内容（工作流引用、参数规格、决策点）作为版本化
// JSON 入库；三天 MEL 周期以参数规格表达（默认 3，可调范围 1~14），
// 不在公共核心代码中写死。
namespace PersonOS::Infrastructure {

class LearningManifestSeed
{
public:
    explicit LearningManifestSeed(QSqlDatabase database, const Domain::Clock &clock);

    // 幂等：domain_code='learning' 已存在时直接返回成功。
    Application::Result<void, Application::ApplicationError> ensureSeeded();

private:
    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
