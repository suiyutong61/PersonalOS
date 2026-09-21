#pragma once

#include <QString>

// 计划（README 2.3.2-10 / 2.4.4）
// 对应表 plans。MVP 以日计划为主；计划可动态调整，目标保持稳定（2.2.6）。
namespace PersonOS {

struct Plan
{
    qint64 id = 0;
    QString periodType = QStringLiteral("daily");    // daily/weekly/monthly
    QString periodStart;                             // YYYY-MM-DD
    QString periodEnd;
    QString status = QStringLiteral("active");       // active/closed
    QString note;
    QString createdAt;
};

} // namespace PersonOS
