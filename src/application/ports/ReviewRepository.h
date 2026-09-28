#pragma once

#include <optional>

#include "application/foundation/Result.h"
#include "application/ports/GoalRepository.h"   // SaveResult
#include "domain/foundation/Uid.h"
#include "domain/review/Review.h"

// 复盘仓储端口（DD-001 §5.3）
namespace PersonOS::Application {

class ReviewRepository
{
public:
    virtual ~ReviewRepository() = default;

    virtual std::optional<Domain::Review> findByMel(const Domain::Uid &melId) = 0;
    virtual std::optional<Domain::Review> findByUid(const Domain::Uid &uid) = 0;
    virtual SaveResult insert(const Domain::Review &review) = 0;
    virtual SaveResult update(const Domain::Review &review, int expectedRevision) = 0;
    // 历史时间线只读投影（最近复盘）
    virtual std::vector<Domain::Review> listRecent(int limit) = 0;
};

} // namespace PersonOS::Application
