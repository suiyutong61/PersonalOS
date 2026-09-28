#pragma once

#include <QString>

#include "application/ports/OperationPorts.h"

// 数据库整体切换实现（DR-026；数据库设计 §7 安全恢复流程）
// 顺序：关闭当前连接 → 原文件改名保护（恢复前安全快照）→ 换入备份文件 →
// 重开并迁移 → 任一步失败回退原文件并重开。
namespace PersonOS::Infrastructure {

class DatabaseManagerSwitch final : public Application::DatabaseSwitchPort
{
public:
    // 切换完成后 DatabaseManager::open() 会重新打开并迁移
    Application::Result<void, Application::ApplicationError> switchTo(
        const std::string &replacementDbPath,
        std::string *preRestoreSnapshotPath) override;

    // 当前活动数据库路径（供快照命名与回退）
    static QString activeDatabasePath();
};

} // namespace PersonOS::Infrastructure
