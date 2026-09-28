#pragma once

#include <optional>
#include <string>

#include "application/foundation/Result.h"
#include "application/ports/GoalRepository.h"
#include "application/ports/UuidPort.h"
#include "domain/foundation/Clock.h"
#include "domain/goal/Goal.h"

// 目标用例（DD-001 §5.2 CreateGoal/ReviseGoal；requirements R1）
// 不变量：目标层级无环（写前校验）；更新走乐观并发 revision；
// 重大修改的用户确认由调用方（页面/协调层）承载，本用例执行写入。
namespace PersonOS::Application {

class GoalUseCases
{
public:
    struct CreateInput
    {
        Domain::Uid userId;
        Domain::Uid domainManifestId;
        std::optional<Domain::Uid> parentGoalId;
        std::string title;
        std::string description;
        std::string goalType;
        int priority = 50;
        std::optional<std::string> targetAt;
        std::string desiredLevelJson;
        bool userDefinedLevel = false;   // 用户是否已自定义掌握标准
        int sortOrder = 0;
    };

    struct CreateOutput
    {
        Domain::Goal goal;
    };

    GoalUseCases(GoalRepository &repo, UuidPort &uids, const Domain::Clock &clock);

    Result<CreateOutput, ApplicationError> createGoal(const CreateInput &input);

    // 修改目标字段；期望修订号不匹配返回 Conflict。parentGoalId 变更会先做环检测。
    Result<Domain::Goal, ApplicationError> reviseGoal(const Domain::Uid &goalUid,
                                                      const Domain::Goal &changes,
                                                      int expectedRevision);

private:
    bool wouldCreateCycle(const Domain::Uid &goalUid,
                          const std::optional<Domain::Uid> &parentGoalId);

    GoalRepository &m_repo;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
