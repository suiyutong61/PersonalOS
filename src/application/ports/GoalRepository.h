#pragma once

#include <optional>
#include <vector>

#include "application/foundation/ApplicationError.h"
#include "application/foundation/Result.h"
#include "domain/foundation/Uid.h"
#include "domain/goal/Goal.h"

// 目标仓储端口（DD-001 §5.3）
// 端口不得泄漏 SQL；实现位于 infrastructure。所有更新使用乐观并发（revision）。
namespace PersonOS::Application {

struct SaveResult
{
    bool ok = false;
    bool conflict = false;   // revision 或唯一约束冲突
    ApplicationError error;
};

class GoalRepository
{
public:
    virtual ~GoalRepository() = default;

    virtual std::optional<Domain::Goal> findByUid(const Domain::Uid &uid) = 0;
    virtual std::vector<Domain::Goal> findByUser(const Domain::Uid &userId) = 0;
    virtual std::vector<Domain::Goal> findChildren(const Domain::Uid &parentGoalId) = 0;
    virtual SaveResult insert(const Domain::Goal &goal) = 0;
    virtual SaveResult update(const Domain::Goal &goal, int expectedRevision) = 0;
};

} // namespace PersonOS::Application
