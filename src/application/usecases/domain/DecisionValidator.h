#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/GoalRepository.h"
#include "application/usecases/domain/DomainRegistry.h"

// 决策校验（DR-038；domain-configuration-design-v1.md §5 DecisionValidator）
// 对 AI 候选计划执行硬约束检查：契约结构、数值范围、引用完整性、
// 领域参数范围（来自领域清单，公共核心不写死）、知识支持标记一致性。
// 通过只表示格式与基本规则正确，不替代用户确认。
namespace PersonOS::Application {

class DecisionValidator
{
public:
    struct ValidationOutcome
    {
        bool ok = false;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };

    DecisionValidator(GoalRepository &goals);

    // contractType ∈ {mel_proposal_v1, route_proposal_v1}；domain 提供参数范围
    ValidationOutcome validate(const std::string &contractType, const std::string &jsonText,
                               const DomainConfig &domain, const Domain::Uid &userId);

private:
    GoalRepository &m_goals;
};

} // namespace PersonOS::Application
