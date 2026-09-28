#pragma once

#include "application/ports/OperationPorts.h"

// SQLite 一致性快照（VACUUM INTO，等价 Online Backup 的一致性保证；数据库设计 §7）
namespace PersonOS::Infrastructure {

class SqliteBackup final : public Application::BackupPort
{
public:
    Application::Result<void, Application::ApplicationError> createSnapshot(
        const std::string &sourceDbPath, const std::string &targetPath) override;
};

} // namespace PersonOS::Infrastructure
