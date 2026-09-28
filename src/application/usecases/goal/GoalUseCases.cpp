#include "application/usecases/goal/GoalUseCases.h"

#include "application/audit/Audit.h"

namespace PersonOS::Application {

GoalUseCases::GoalUseCases(GoalRepository &repo, UuidPort &uids, const Domain::Clock &clock)
    : m_repo(repo), m_uids(uids), m_clock(clock)
{}

bool GoalUseCases::wouldCreateCycle(const Domain::Uid &goalUid,
                                    const std::optional<Domain::Uid> &parentGoalId)
{
    if (!parentGoalId)
        return false;
    std::optional<Domain::Uid> cursor = parentGoalId;
    // 防御性上限：目标树深度超过 64 视为异常（正常目标链远小于此）
    for (int depth = 0; depth < 64 && cursor; ++depth) {
        if (*cursor == goalUid)
            return true;
        const auto parent = m_repo.findByUid(*cursor);
        if (!parent)
            return false; // 父链中断（父不存在由写入时的外键约束兜底）
        cursor = parent->parentGoalId;
    }
    return false;
}

Result<GoalUseCases::CreateOutput, ApplicationError> GoalUseCases::createGoal(
    const CreateInput &input)
{
    Domain::Goal goal;
    goal.uid = m_uids.next();
    goal.userId = input.userId;
    goal.domainManifestId = input.domainManifestId;
    goal.parentGoalId = input.parentGoalId;
    goal.title = input.title;
    goal.description = input.description;
    goal.goalType = input.goalType;
    goal.status = Domain::GoalStatus::Draft;
    goal.priority = input.priority;
    goal.targetAt = input.targetAt;
    goal.desiredLevelJson = input.desiredLevelJson;
    goal.userDefinedLevel = input.userDefinedLevel;
    goal.sortOrder = input.sortOrder;
    // created_at/updated_at 由仓储在写入时按 Clock 生成（DD-001 §2.2 依赖方向）

    if (!goal.isValid())
        return Result<CreateOutput, ApplicationError>::failure(
            {ErrorCode::Validation, "goal title/uid/manifest required", {}, false});
    if (input.parentGoalId && !m_repo.findByUid(*input.parentGoalId))
        return Result<CreateOutput, ApplicationError>::failure(
            {ErrorCode::Validation, "parent goal does not exist", {}, false});

    const auto saved = m_repo.insert(goal);
    if (!saved.ok) {
        ApplicationError error = saved.error;
        // 引用不存在 / 约束违反 → 对用户暴露为校验错误，不泄漏 SQL 细节
        if (error.detail.find("FOREIGN KEY") != std::string::npos
            || error.detail.find("NOT NULL") != std::string::npos)
            error.code = ErrorCode::Validation;
        return Result<CreateOutput, ApplicationError>::failure(std::move(error));
    }
    Audit::record({"user", {}, "goal.created", "goal", goal.uid.value(),
                   "{\"title\":\"" + goal.title + "\"}"});
    return Result<CreateOutput, ApplicationError>::success(CreateOutput{std::move(goal)});
}

Result<Domain::Goal, ApplicationError> GoalUseCases::reviseGoal(
    const Domain::Uid &goalUid, const Domain::Goal &changes, int expectedRevision)
{
    const auto current = m_repo.findByUid(goalUid);
    if (!current)
        return Result<Domain::Goal, ApplicationError>::failure(
            {ErrorCode::NotFound, "goal not found", {}, false});

    if (changes.parentGoalId && wouldCreateCycle(goalUid, changes.parentGoalId))
        return Result<Domain::Goal, ApplicationError>::failure(
            {ErrorCode::Validation, "parent change would create a goal cycle", {}, false});

    Domain::Goal updated = *current;
    if (!changes.title.empty())
        updated.title = changes.title;
    if (!changes.description.empty())
        updated.description = changes.description;
    if (!changes.goalType.empty())
        updated.goalType = changes.goalType;
    updated.status = changes.status;
    updated.priority = changes.priority;
    updated.targetAt = changes.targetAt;
    if (!changes.desiredLevelJson.empty())
        updated.desiredLevelJson = changes.desiredLevelJson;
    updated.userDefinedLevel = changes.userDefinedLevel;
    if (changes.parentGoalId)
        updated.parentGoalId = changes.parentGoalId;

    const auto saved = m_repo.update(updated, expectedRevision);
    if (!saved.ok)
        return Result<Domain::Goal, ApplicationError>::failure(saved.error);
    updated.revision = expectedRevision + 1;
    Audit::record({"user", {}, "goal.revised", "goal", updated.uid.value(), "{}"});
    return Result<Domain::Goal, ApplicationError>::success(std::move(updated));
}

} // namespace PersonOS::Application
