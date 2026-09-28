#pragma once

#include <optional>
#include <string>

#include "application/foundation/Result.h"
#include "application/ports/StateRepository.h"
#include "application/ports/UuidPort.h"
#include "domain/foundation/Clock.h"
#include "domain/state/State.h"

// 状态用例（DD-001 §5.2 CaptureConversationState/SubmitReviewState；requirements R4.3）
// 纪律：原始事件只追加；过期状态显示"未知/需确认"，不自动补成正常；
// 敏感状态最小收集；AI 推断不得冒充显式事实。
namespace PersonOS::Application {

class StateUseCases
{
public:
    struct ObserveInput
    {
        Domain::Uid userId;
        std::string definitionCode;
        std::string valueJson;        // 量表原始值与上下限
        Domain::StateSource source = Domain::StateSource::Explicit;
        double confidence = 1.0;
        std::string consentScope = "explicit";
        std::optional<int> ttlMin;    // 未提供时用定义默认 TTL
        std::string idempotencyKey;   // 必填，唯一
    };

    StateUseCases(StateRepository &repo, UuidPort &uids, const Domain::Clock &clock);

    Result<Domain::StateEvent, ApplicationError> observe(const ObserveInput &input);

    // 聊天提取授权开关（R4.3.3：默认关闭；开启后才允许 AI 从对话生成候选状态）
    Result<void, ApplicationError> setChatExtractionEnabled(const Domain::Uid &userId,
                                                            bool enabled);
    Result<bool, ApplicationError> chatExtractionEnabled(const Domain::Uid &userId);

    // 从对话捕获候选状态（source=conversation）：必须开启授权；敏感定义
    // 需要逐项用户确认；AI 推断身份与原始语境片段随事件保存（最小收集，
    // 不保存整段对话）；置信度不得为 1.0（推断不得冒充确定事实）
    struct ConversationInput
    {
        std::string definitionCode;
        std::string valueJson;
        double confidence = 0.8;        // AI 推断：必须 < 1.0
        std::string conversationRef;    // 原始语境片段（限长，见实现）
        bool confirmedByUser = false;   // 敏感定义必填 true
    };
    Result<Domain::StateEvent, ApplicationError> captureFromConversation(
        const Domain::Uid &userId, const ConversationInput &input);

    // 当前有效状态（valid_until >= now 的最新事件）；无有效事件 = 未知（不伪造）
    Result<std::optional<Domain::StateEvent>, ApplicationError> current(
        const Domain::Uid &userId, const std::string &definitionCode);

private:
    StateRepository &m_repo;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
