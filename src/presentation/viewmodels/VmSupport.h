#pragma once

#include <QSqlDatabase>
#include <QString>

#include <optional>
#include <string>

#include "domain/foundation/Uid.h"

// ViewModel 公共辅助（DD-001 §11：ViewModel 只做只读聚合与页面状态，
// 不保存正式业务事实、不直接操作数据库写路径之外的逻辑）
namespace PersonOS::Presentation {

// v1 单活动用户：读取 user_profiles_v3 中唯一的用户 uid；
// 无用户时返回 nullopt（页面显示 empty 状态，不伪造用户）
std::optional<Domain::Uid> activeUserUid(QSqlDatabase database);

// 错误信息脱敏拼接（detail 为数据库错误文本时只取首行，避免整段暴露）
QString friendlyError(const std::string &message, const std::string &detail = {});

// 0..1 → "NN%"（百分比展示统一口径）
QString percentText(double ratio);

// PDF/OCR 工具路径(QSettings 持久化 + 常见位置自动探测),
// 并把结果写入 PERSONOS_PDFTOTEXT_PATH/PERSONOS_PDFTOPPM_PATH/
// PERSONOS_TESSERACT_PATH 供提取器使用
void applyPdfToolPaths();

// 当前配置的 pdftotext 路径(设置页回显用;空 = 未配置)
QString configuredPdfToTextPath();
QString configuredTesseractPath();

// ---- 展示层标签映射(内部枚举串 → 中文标签;未知值原样返回) ----
QString knowledgeTypeLabel(const QString &type);        // paper/method/tip/...
QString knowledgeStatusLabel(const QString &status);    // active/candidate/...
QString knowledgeDomainLabel(const QString &domain);    // research.NN.slug/learning/...
QString melStateLabel(const QString &state);
QString melTaskStateLabel(const QString &state);
QString reviewStatusLabel(const QString &status);
QString assessmentStatusLabel(const QString &status);
QString assessmentTypeLabel(const QString &type);
QString routeStatusLabel(const QString &status);
QString stageMaterialChoiceLabel(const QString &choice);   // pending/accepted/rejected
QString reminderDeliveryStatusLabel(const QString &status);   // pending/delivered/failed/suppressed/cancelled
QString contentMapStatusLabel(const QString &status);
QString sourceTypeLabel(const QString &type);
QString sourceModeLabel(const QString &mode);           // 知识支持程度三档
QString questionnaireItemLabel(const QString &code);    // 16 项拓展状态码
QString achievementTypeLabel(const QString &type);
QString providerCodeLabel(const QString &code);
// UTC ISO-8601 → 本地可读 "yyyy-MM-dd HH:mm"(解析失败原样返回)
QString displayDateTime(const QString &isoUtc);

} // namespace PersonOS::Presentation
