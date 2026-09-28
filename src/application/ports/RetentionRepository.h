#pragma once

#include <optional>
#include <vector>

#include "application/ports/GoalRepository.h"   // SaveResult
#include "domain/assessment/Retention.h"
#include "domain/foundation/Uid.h"

// 保持抽查仓储端口（DD-001 §5.3；数据库设计 §4.3）
namespace PersonOS::Application {

// 到期候选：附带最近结果与节点权重，供用例层排序与上层（AI）再排序。
struct RetentionCandidate
{
    Domain::RetentionSchedule schedule;
    std::optional<Domain::Mastery> lastMastery;
    std::optional<double> nodeWeight;
};

class RetentionRepository
{
public:
    virtual ~RetentionRepository() = default;

    virtual std::optional<Domain::RetentionSchedule> findByUid(const Domain::Uid &uid) = 0;
    virtual SaveResult insert(const Domain::RetentionSchedule &schedule) = 0;
    virtual SaveResult update(const Domain::RetentionSchedule &schedule,
                              int expectedRevision) = 0;

    // 到期候选（active=1 且 next_due_at <= nowIso），按 next_due_at 升序
    virtual std::vector<RetentionCandidate> dueCandidates(const Domain::Uid &userId,
                                                          const std::string &nowIso,
                                                          int limit) = 0;
    virtual std::vector<Domain::RetentionSchedule> listForUser(const Domain::Uid &userId) = 0;
};

} // namespace PersonOS::Application
