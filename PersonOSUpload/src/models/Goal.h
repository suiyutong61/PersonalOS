#pragma once

#include <QString>

// 目标（README 2.3.2-3 / 2.2.4）
// 对应表 goals。层级：vision → long_term → annual → quarterly → monthly → weekly。
// 目标 ≠ 计划：单次计划失败不得修改目标（2.2.6）。
namespace PersonOS {

struct Goal
{
    qint64 id = 0;
    qint64 parentId = 0;                             // 0 = 根目标
    QString level = QStringLiteral("weekly");        // vision/long_term/annual/quarterly/monthly/weekly
    QString title;
    QString description;
    QString targetDate;                              // YYYY-MM-DD 或空
    QString status = QStringLiteral("active");       // active/paused/achieved/abandoned
    int priority = 50;                               // 0~100，越大越优先（2.2.5）
    QString createdAt;
    QString updatedAt;

    bool isRoot() const { return parentId == 0; }
    bool isValid() const { return !title.trimmed().isEmpty(); }
};

} // namespace PersonOS
