#pragma once

#include <optional>

#include <QString>
#include <QStringList>

#include "database/PlanRepository.h"
#include "database/ReviewRepository.h"
#include "database/StateRepository.h"
#include "database/TaskRepository.h"
#include "models/Review.h"

namespace PersonOS {

// Review & Decision 子系统（README 2.4.7 / 3.3.5）
// 职责：日复盘的草案生成、保存，与补录检查。
//
// 事实依据语义（2.5.6 / 2.3.5 原则3）：
// - 计划 vs 实际 = 以任务的 due_date 维度统计（任务状态是执行真相）
// - 事件流是审计轨迹（行动日期维度），不参与偏差计算
class ReviewService
{
public:
    std::optional<Review> dailyReview(const QString &date) const; // YYYY-MM-DD

    // 生成复盘草案（不落库）：计划 vs 实际偏差 + 当日状态摘要（2.5.6）
    Review draftDailyReview(const QString &date) const;

    // 保存日复盘（upsert 语义，FR-004 可修正当天记录）
    bool saveDailyReview(const Review &r, QString *error = nullptr);

    // 启动检查（3.0.3 循环触发时机）：近 N 天内"有计划但未复盘"的日期，
    // 按时间从近到远排序，供 UI 提示补录
    QStringList pendingReviewDates(int maxDaysBack = 7) const;

private:
    ReviewRepository m_reviews;
    TaskRepository m_tasks;
    StateRepository m_states;
    PlanRepository m_plans;
};

} // namespace PersonOS
