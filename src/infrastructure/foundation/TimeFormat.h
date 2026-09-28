#pragma once

#include <string>

#include "domain/foundation/Clock.h"

// 时间文本格式（数据库设计 §1.3：UTC ISO-8601 带 Z；日期 YYYY-MM-DD）
namespace PersonOS::Infrastructure {

std::string formatUtcIso(const Domain::TimePoint &tp);
std::string formatUtcDate(const Domain::TimePoint &tp);

} // namespace PersonOS::Infrastructure
