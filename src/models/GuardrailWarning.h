#pragma once

#include <QString>

// Guardrail 预警（design.md 1.3.1-27 / FR-E-02/03）
// 对应表 guardrail_warnings。预警本身是记录，任何对正式对象的修改仍走
// Proposal → 用户批准（继承 v0.1 权限机制）；AI 只建议，最终决定权在用户。
namespace PersonOS {

struct GuardrailWarning
{
    qint64 id = 0;
    QString date;                       // 预警所属日期 YYYY-MM-DD
    QString type;                       // workload（上界）/ stall（下界）
    QString rule;                       // 触发规则（可解释，FR-E-04）
    QString dataSnapshot;               // 使用的数据（R/E 值、窗口）
    QString threshold;                  // 当前阈值
    QString suggestion;                 // 建议：压缩/延期/拆分/调优先级/降范围
    QString evidenceRef;                // 引用证据（可空；无证据显式标注，NFR-13）
    QString status = QStringLiteral("active");   // active/handled/dismissed
    QString handledBy;                  // 用户处理说明（dismiss 原因等）
    QString createdAt;
};

} // namespace PersonOS
