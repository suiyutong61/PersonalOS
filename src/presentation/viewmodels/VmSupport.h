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

} // namespace PersonOS::Presentation
