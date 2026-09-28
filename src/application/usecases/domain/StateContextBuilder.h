#pragma once

#include <string>

#include "application/foundation/Result.h"
#include "application/ports/StateRepository.h"
#include "domain/foundation/Clock.h"

// 状态上下文构建（DR-036；domain-configuration-design-v1.md §5）
// 计划生成前读取基本状态与十类拓展状态，逐项标注时效：
// current（有效期内）/ last_known（有过记录但已过期）/ unknown（从未记录）。
// 未知不得当作正常；过期不得当作当前事实。
namespace PersonOS::Application {

class StateContextBuilder
{
public:
    struct Item
    {
        std::string code;
        std::string freshness;        // current / last_known / unknown
        std::string valueJson;
        std::string observedAt;
        std::string validUntil;
    };

    StateContextBuilder(StateRepository &states, const Domain::Clock &clock);

    // 构建全部已注册状态定义的上下文；输出 JSON：
    // {"items":[{"code","freshness","value","observed_at","valid_until"}], "as_of":...}
    Result<std::string, ApplicationError> build(const Domain::Uid &userId,
                                                const std::string &asOfIso);

    // 结构化视图（供决策校验直接使用）
    Result<std::vector<Item>, ApplicationError> items(const Domain::Uid &userId,
                                                      const std::string &asOfIso);

private:
    StateRepository &m_states;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
