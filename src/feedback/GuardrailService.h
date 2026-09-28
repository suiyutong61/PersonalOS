#pragma once

#include <optional>

#include <QList>
#include <QString>

#include "database/GuardrailRepository.h"
#include "database/MetricRepository.h"
#include "database/PersonalModelRepository.h"
#include "database/TaskRepository.h"
#include "models/GuardrailWarning.h"
#include "planning/FeasibilityCalculator.h"

namespace PersonOS {

// Guardrail 服务（design.md 1.4.1 Feedback 扩展 / 1.9.3；FR-E）
//
// 上界（Workload，Step 2）：R > 阈值 → Warning + 建议（压缩/延期/拆分/调优先级/降范围）。
// 下界（Stall，Step 3）：E_7d 低或连续无完成 → Warning + Recovery 建议 + 多因素诊断清单；
//   异常持续（21 天内 ≥3 次）→ 追加 Diagnostic 阶段提示（衔接 Step 7 N-of-1）。
// 纪律：预警按日幂等；禁止"懒惰"人格归因；AI 只建议不决定；预警是记录，
//   对正式对象的修改仍走 Proposal → 用户批准。
class GuardrailService
{
public:
    // 上界检查：超载则生成/返回当日 active Workload Warning；未超载返回 nullopt
    std::optional<GuardrailWarning> checkWorkload(const QString &date, QString *error = nullptr);

    // 下界检查（design.md 1.10.2 触发 Policy）：
    //   IF E_7d < stall_threshold 且样本天数 ≥ 3   THEN stall（规则1）
    //   IF 连续 stall_quiet_days 天有计划任务但无完成 THEN stall（规则2）
    std::optional<GuardrailWarning> checkStall(const QString &endDate, QString *error = nullptr);

    // 当日全部 active 预警
    QList<GuardrailWarning> active(const QString &date) const;

    // 用户处置：dismiss（reason 记录处置说明）；handled 由 Step 3 起配合 Proposal 使用
    bool dismiss(qint64 warningId, const QString &reason, QString *error = nullptr);

    QString lastError() const { return m_repo.lastError(); }

private:
    GuardrailRepository m_repo;
    FeasibilityCalculator m_feasibility;
    TaskRepository m_tasks;
    PersonalModelRepository m_params;
    MetricRepository m_metricRepo;
};

} // namespace PersonOS
