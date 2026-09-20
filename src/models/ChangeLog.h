#pragma once

#include <QString>

// 变更记录（README 2.4.8.5 / 2.6.21）
// 对应表 change_logs。append-only：重要修改必须能回答
// "为什么改 / 改了什么 / 依据是什么"（2.4.14 原则2）。
namespace PersonOS {

struct ChangeLog
{
    qint64 id = 0;
    QString targetType;
    qint64 targetId = 0;
    QString beforeSummary;
    QString afterSummary;
    QString reason;
    qint64 proposalId = 0;
    qint64 versionId = 0;
    QString createdAt;
};

} // namespace PersonOS
