#pragma once

#include <QString>

// 校准记录（design.md 1.3.1-23 / NFR-17）
// 对应表 calibration_records，append-only：每次个人参数更新必须留痕
// （窗口 / 规则 / 触发源），保证校准可追溯、可解释、可审计。
namespace PersonOS {

struct CalibrationRecord
{
    qint64 id = 0;
    QString paramKey;
    QString oldValue;
    QString newValue;
    QString dataWindow;                 // 如 '2026-09-01..2026-09-14'
    QString rule;                       // 更新规则描述（如"滚动均值 14d"）
    QString trigger = QStringLiteral("periodic");   // periodic / experiment / manual
    QString createdAt;
};

} // namespace PersonOS
