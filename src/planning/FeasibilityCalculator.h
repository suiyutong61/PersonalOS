#pragma once

#include <QString>

#include "database/PersonalModelRepository.h"
#include "database/TaskRepository.h"

namespace PersonOS {

// 计划可行性计算（design.md 1.4.1 Planning 扩展 / 1.9.3 / 1.10.1；FR-C-02）
//
// R = L_planned / C_available：
//   L_planned    = 当日任务的 planned_minutes 之和（>0 的计划）
//   C_available  = time_capacity（小时）× 精力节律系数（未校准 = 1.0）× 60
// 超载判定：R > workload_threshold。
//
// 工程模型声明（CHAT.md §12）：R 是系统设计工具，不是心理学常数；
// 阈值与容量先验为 population_default 占位（Q1 已定：人群先验起步 → 校准接管），
// 待领域②③研究文献到位后升级为 research_evidence（只改参数数据，不改本机制）。
class FeasibilityCalculator
{
public:
    struct Result
    {
        bool computable = false;        // time_capacity 参数缺失时为 false
        double ratio = 0.0;
        qint64 plannedMinutes = 0;
        qint64 capacityMinutes = 0;
        double threshold = 1.2;
        bool overload = false;
        QString explanation;            // 可解释输出（NFR-04/18；计划依据，FR-C-05）
    };

    // 计算指定日期的可行性；error 只描述本次调用（服务层错误契约）
    Result check(const QString &date, QString *error = nullptr);

private:
    TaskRepository m_tasks;
    PersonalModelRepository m_params;
};

} // namespace PersonOS
