#pragma once

#include <QString>

// Proposal / ChangeRequest（README 2.6.12 / 2.6.18；设计评审补入的第 22 个实体）
// 对应表 proposals。AI/系统对重要对象的修改必须先经 Proposal → 用户批准 → 生效。
// 禁止无 Proposal 直接覆盖高层对象（2.6.11）。
namespace PersonOS {

struct Proposal
{
    qint64 id = 0;
    QString targetType;                               // goal/strategy/plan...
    qint64 targetId = 0;
    QString currentValue;
    QString proposedValue;
    QString reason;                                   // 为什么改（NFR-04 可解释）
    QString evidence;                                 // 依据数据（NFR-05 可追溯）
    QString expectedEffect;
    QString risk;
    QString status = QStringLiteral("draft");         // draft/review/approved/rejected/applied
    QString createdAt;
    QString decidedAt;
};

} // namespace PersonOS
