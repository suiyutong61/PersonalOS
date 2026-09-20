#pragma once

#include <QString>

// 复盘（README 2.3.2-17 / 2.5.6）
// 对应表 reviews。复盘是"对状态的评价"，与 State 事实分离（2.3.5 原则3）。
// MVP：每日一条（review_type=daily）。
namespace PersonOS {

struct Review
{
    qint64 id = 0;
    QString reviewType = QStringLiteral("daily");    // daily/weekly/monthly
    QString periodStart;                             // YYYY-MM-DD
    QString periodEnd;
    QString summary;                                 // 计划 vs 实际 + 结论
    QString problems;                                // 发现的问题
    QString causes;                                  // 原因分析
    QString nextActions;                             // 下一步调整
    QString createdAt;
};

} // namespace PersonOS
