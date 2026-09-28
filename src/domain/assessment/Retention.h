#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#include "domain/assessment/Assessment.h"   // Mastery
#include "domain/foundation/Uid.h"

// 保持抽查/遗忘调度（架构 4.3.6、DR-021；数据库设计 §4.3 retention_schedules_v4）
// 纪律：调度算法与参数不得写死——参数必须来自论文库、方法库或领域配置，
// 且携带来源标识（basisSource）；代码只实现确定性、带版本的计算函数并执行
// 频率/状态规则。推迟抽查不判定为能力下降；抽查结果不修改已完成 MEL 进度。
namespace PersonOS::Domain {

// 调度参数（每次计算间隔时由调用方从知识库/领域配置解析后传入）
struct RetentionParameters
{
    int baseIntervalMin = 0;          // 首次间隔（分钟）
    double fluentMultiplier = 0.0;    // 三档结果倍率：无需提示
    double promptedMultiplier = 0.0;  // 提示后能够完成
    double notRecalledMultiplier = 0.0; // 提示后仍不能完成
    int minIntervalMin = 0;           // 程序执行的硬下界（频率约束）
    int maxIntervalMin = 0;           // 硬上界
    std::string basisSource;          // 必填：方法/论文/领域配置来源标识（可追溯）

    bool isValid() const
    {
        return baseIntervalMin > 0 && fluentMultiplier > 0.0 && promptedMultiplier > 0.0
               && notRecalledMultiplier > 0.0 && minIntervalMin > 0
               && maxIntervalMin >= minIntervalMin && !basisSource.empty();
    }
};

inline double multiplierFor(const RetentionParameters &params, Mastery mastery)
{
    switch (mastery) {
    case Mastery::Fluent: return params.fluentMultiplier;
    case Mastery::Prompted: return params.promptedMultiplier;
    case Mastery::NotRecalled: return params.notRecalledMultiplier;
    case Mastery::NotApplicable: return 1.0;   // 不适用时不改变节奏
    }
    return 1.0;
}

// 确定性、带版本的计算函数 "multiplier_schedule"：
//   next = clamp(round(current × multiplier(mastery)), min, max)
// 参数无效（缺依据/越界）时返回 nullopt——不猜测、不默认。
inline std::optional<int> nextRetentionInterval(const RetentionParameters &params,
                                                std::optional<int> currentIntervalMin,
                                                Mastery mastery)
{
    if (!params.isValid())
        return std::nullopt;
    const double current = currentIntervalMin ? static_cast<double>(*currentIntervalMin)
                                              : static_cast<double>(params.baseIntervalMin);
    if (current <= 0.0)
        return std::nullopt;
    const double next =
        std::clamp(current * multiplierFor(params, mastery),
                   static_cast<double>(params.minIntervalMin),
                   static_cast<double>(params.maxIntervalMin));
    return static_cast<int>(std::lround(next));
}

struct RetentionSchedule
{
    Uid uid;
    Uid userId;
    std::optional<Uid> contentNodeId;      // 与 assessmentItemId 互斥（CHECK 兜底）
    std::optional<Uid> assessmentItemId;
    std::string nextDueAt;                 // UTC ISO-8601
    int intervalMin = 0;                   // >0
    std::optional<double> stability;
    std::optional<double> difficulty;
    std::string algorithmCode;             // 调度方法标识（方法库引用或命名策略）
    std::string algorithmVersion;
    std::optional<std::string> lastResultUid;   // 最近一次抽查的三档结果
    bool active = true;
    int revision = 1;
    std::string createdAt;
    std::string updatedAt;

    bool isValid() const
    {
        return !uid.empty() && !userId.empty() && !nextDueAt.empty() && intervalMin > 0
               && !algorithmCode.empty() && !algorithmVersion.empty()
               && (contentNodeId.has_value() != assessmentItemId.has_value());
    }
};

} // namespace PersonOS::Domain
