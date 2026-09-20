#pragma once

#include <QSqlDatabase>
#include <QString>

// Personal OS 本地数据库管理（README 3.3.1 仓库层基础设施）
//
// 单例。负责：打开/创建数据库文件、连接级 PRAGMA、schema 迁移。
// 仓库层通过 database() 获取连接；业务代码不得直接持有连接做写操作。
class DatabaseManager
{
public:
    static DatabaseManager &instance();

    // 打开（或创建）数据库并完成迁移；失败时通过 lastError() 取原因
    bool open();

    // 每次调用取当前连接（不要缓存 QSqlDatabase 成员，避免连接失效）
    QSqlDatabase database() const;

    // 数据库文件完整路径（Windows: %LOCALAPPDATA%/PersonalOS/PersonalOS/personos.db）
    // 基于 QStandardPaths::AppLocalDataLocation
    QString databasePath() const;

    // 当前 schema 版本（app_meta.schema_version，未迁移返回 0）
    int schemaVersion() const;

    QString lastError() const;

private:
    DatabaseManager() = default;
    Q_DISABLE_COPY(DatabaseManager)

    bool applyMigrations();

    QString m_lastError;
};
