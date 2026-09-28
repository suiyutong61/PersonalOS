#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/AssessmentRepository.h"
#include "application/ports/MelRepository.h"
#include "application/ports/StateRepository.h"
#include "application/ports/UuidPort.h"
#include "domain/foundation/Clock.h"

// R2 子问题动态检测（requirements R2.1–R2.3；架构 4.3.2）
// 纪律：检测结果是"待确认判断"——同一种现象给出多种可能原因，不贴"不自律"
// 标签；阈值与观察周期为参数（携带依据来源，公共核心不写死）；
// 检测结果以状态事件追加保存（source=system_derived，可被用户更正）。
namespace PersonOS::Application {

class DetectionUseCases
{
public:
    struct DetectionConfig
    {
        double progressLagTolerance = 0.0;   // 允许落后于计划节奏的比例（如 0.3）
        int inactivityMinutes = 0;           // 无执行记录超过该时长才提示
        double effortRatioThreshold = 0.0;   // 实际耗时/预计耗时 超过该值才提示
        int weakAssessmentWindowMinutes = 0; // 该窗口内的"提示后仍不能"结果触发提示
        std::string basisSource;             // 必填：阈值依据（领域配置/论文/方法）
        bool isValid() const
        {
            return progressLagTolerance >= 0.0 && progressLagTolerance <= 1.0
                   && inactivityMinutes > 0 && effortRatioThreshold > 1.0
                   && weakAssessmentWindowMinutes > 0 && !basisSource.empty();
        }
    };

    struct Detection
    {
        std::string code;                 // 状态定义 code（subproblem_*）
        std::string title;                // 中性表述（不贴标签）
        std::vector<std::string> causes;  // 多原因假设（R2.2）
        std::vector<std::string> interventions;   // 干预选项（R2.3）
        std::string basis;                // 检测依据（事实 + 参数来源）
    };

    // 已保存检测的只读投影（valueJson 交由表现层解析展示）
    struct DetectionEvent
    {
        std::string definitionCode;
        std::string valueJson;
        std::string observedAt;
    };

    DetectionUseCases(StateRepository &states, MelRepository &mels,
                      AssessmentRepository &assessments, UuidPort &uids,
                      const Domain::Clock &clock);

    // 运行一轮检测：为每个触发规则生成待确认判断，追加为状态事件（幂等：
    // 同一定义在同一有效期内只落一条）。返回本次新增的检测数。
    Result<int, ApplicationError> detect(const Domain::Uid &userId,
                                         const std::string &nowIso,
                                         const DetectionConfig &config);

    // 最近检测事件（供界面与 R5 建议读取；解析由表现层完成）
    Result<std::vector<DetectionEvent>, ApplicationError> recentDetections(
        const Domain::Uid &userId, int limit);

private:
    // 返回是否真正新增（false = 已存在有效事件/幂等键，本轮未落库）
    Result<bool, ApplicationError> appendDetection(const Domain::Uid &userId,
                                                   const std::string &definitionCode,
                                                   const Detection &detection,
                                                   const std::string &nowIso,
                                                   const std::string &idempotencyKey);

    StateRepository &m_states;
    MelRepository &m_mels;
    AssessmentRepository &m_assessments;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

// ISO 文本 → 自纪元分钟数（规范 UTC 格式 YYYY-MM-DDTHH:MM:SSZ；解析失败返回 nullopt）
std::optional<long long> isoToEpochMinutes(const std::string &iso);

} // namespace PersonOS::Application
