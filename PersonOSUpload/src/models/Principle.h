#pragma once

#include <QString>

// 原则（README 2.3.2-2 / 2.6.5）
// 对应表 principles。修改须用户批准（AI Proposal → 用户审查）。
namespace PersonOS {

struct Principle
{
    qint64 id = 0;
    QString text;
    int sortOrder = 0;
    QString createdAt;
};

} // namespace PersonOS
