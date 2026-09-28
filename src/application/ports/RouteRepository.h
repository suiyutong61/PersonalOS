#pragma once

#include <optional>
#include <vector>

#include "application/foundation/ApplicationError.h"
#include "application/ports/GoalRepository.h"
#include "domain/foundation/Uid.h"
#include "domain/route/Route.h"

// 路线仓储端口（DD-001 §5.3）
namespace PersonOS::Application {

class RouteRepository
{
public:
    virtual ~RouteRepository() = default;

    virtual std::optional<Domain::Route> findByUid(const Domain::Uid &uid) = 0;
    virtual std::vector<Domain::Route> findByGoal(const Domain::Uid &goalId) = 0;
    virtual SaveResult insert(const Domain::Route &route) = 0;
    virtual SaveResult update(const Domain::Route &route, int expectedRevision) = 0;

    // 版本与阶段：版本只追加；阶段随版本一次写入。成功返回生成的版本 UID。
    virtual Result<std::string, ApplicationError> insertVersion(
        const Domain::Uid &routeId, const Domain::RouteVersion &version) = 0;
    virtual std::vector<Domain::RouteVersion> versionsOf(const Domain::Uid &routeId) = 0;

    // 用户确认：记录版本确认时间（用户确认是不可覆盖的历史事实，只追加不撤销）
    virtual SaveResult markVersionConfirmed(const Domain::Uid &routeId, int versionNo,
                                            const std::string &confirmedAtIso) = 0;
};

} // namespace PersonOS::Application
