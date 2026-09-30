#pragma once

#include <QSqlDatabase>

// 首次启动引导（requirements §8 用户档案；DR-038 领域清单；本地单用户）：
// 用户档案缺失时创建默认本地档案（时区/语言取系统，onboarding_status 置
// complete——应用目前没有引导页，创建后即可用；未来实现引导页时再改）；
// 学习领域清单/状态定义/复盘问卷种子幂等种入。
// 纯本地写入，无任何外发。启动与备份恢复切换后都要执行。
namespace PersonOS::Infrastructure {

// 档案已存在时返回 true 且不写入；写入成功返回 true，失败返回 false。
bool ensureDefaultUserProfile(QSqlDatabase database);

// 完整启动引导(档案 + 三种子,各自幂等):启动时与恢复切换后调用。
// 任何一步失败不阻断(调用方记录警告),返回是否全部成功。
bool ensureStartupSeeded(QSqlDatabase database);

} // namespace PersonOS::Infrastructure
