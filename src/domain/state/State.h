#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "domain/foundation/Uid.h"

// 状态时间线（DD-001 §3 StateTimeline；数据库设计 §4.2 state_definitions_v4/state_events_v4）
// 不变量：原始事件不可覆盖；过期状态不得被当作当前事实；未知不能被当作正常；
// 来源显式标注（explicit/questionnaire/conversation/system_derived/imported）。
namespace PersonOS::Domain {

enum class StateSource { Explicit, Questionnaire, Conversation, SystemDerived, Imported };

inline std::string toString(StateSource s)
{
    switch (s) {
    case StateSource::Explicit: return "explicit";
    case StateSource::Questionnaire: return "questionnaire";
    case StateSource::Conversation: return "conversation";
    case StateSource::SystemDerived: return "system_derived";
    case StateSource::Imported: return "imported";
    }
    return "explicit";
}

inline std::optional<StateSource> stateSourceFrom(std::string_view value)
{
    if (value == "explicit") return StateSource::Explicit;
    if (value == "questionnaire") return StateSource::Questionnaire;
    if (value == "conversation") return StateSource::Conversation;
    if (value == "system_derived") return StateSource::SystemDerived;
    if (value == "imported") return StateSource::Imported;
    return std::nullopt;
}

// 十类拓展状态 + basic（DR-036）
enum class StateCategory {
    Basic,
    Cognitive,
    EmotionPressure,
    MotivationRecreation,
    BodyHealth,
    Environment,
    ResourcesSocial,
    BehaviorMethod,
    SystemExperience,
    DomainSpecific,
};

inline std::string toString(StateCategory c)
{
    switch (c) {
    case StateCategory::Basic: return "basic";
    case StateCategory::Cognitive: return "cognitive";
    case StateCategory::EmotionPressure: return "emotion_pressure";
    case StateCategory::MotivationRecreation: return "motivation_recreation";
    case StateCategory::BodyHealth: return "body_health";
    case StateCategory::Environment: return "environment";
    case StateCategory::ResourcesSocial: return "resources_social";
    case StateCategory::BehaviorMethod: return "behavior_method";
    case StateCategory::SystemExperience: return "system_experience";
    case StateCategory::DomainSpecific: return "domain_specific";
    }
    return "basic";
}

struct StateDefinition
{
    Uid uid;
    std::string code;                 // UNIQUE
    StateCategory category = StateCategory::Basic;
    std::string valueType;            // integer / real / text / ordinal
    std::optional<double> scaleMin;
    std::optional<double> scaleMax;
    std::string unit;
    int defaultTtlMin = 1440;         // 过期前有效分钟数（>0）
    std::string sensitivity;          // normal / sensitive（S3 领域最小收集）
    std::string schemaJson = "{}";
    int revision = 1;

    bool isValid() const { return !uid.empty() && !code.empty() && defaultTtlMin > 0; }
};

struct StateEvent
{
    Uid uid;
    Uid userId;
    Uid definitionId;
    std::string valueJson;            // 量表原始值与上下限同时保存
    StateSource source = StateSource::Explicit;
    double confidence = 1.0;          // 0..1
    std::string observedAt;           // UTC ISO-8601
    std::string validUntil;           // 必须 >= observedAt（TTL 计算）
    std::string consentScope;         // 采集授权范围
    std::optional<std::string> conversationRef;
    std::optional<std::string> supersedesUid;
    std::string recordedAt;
    std::string idempotencyKey;       // UNIQUE

    bool isValid() const
    {
        return !uid.empty() && !userId.empty() && !definitionId.empty()
               && !observedAt.empty() && validUntil >= observedAt
               && confidence >= 0.0 && confidence <= 1.0;
    }
};

} // namespace PersonOS::Domain
