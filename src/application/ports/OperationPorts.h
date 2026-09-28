#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/ApplicationError.h"
#include "application/foundation/Result.h"
#include "application/ports/GoalRepository.h"   // SaveResult
#include "domain/foundation/Uid.h"
#include "domain/operations/Operations.h"

// 运维端口（DD-001 §5.4 NotificationPort/BackupPort；§12）
namespace PersonOS::Application {

// 通知端口：业务层只提交显示请求；失败不改变业务状态（DR-024）
class NotificationPort
{
public:
    virtual ~NotificationPort() = default;
    virtual bool deliver(const std::string &title, const std::string &body) = 0;
};

class ReminderRepositoryPort
{
public:
    virtual ~ReminderRepositoryPort() = default;

    virtual std::optional<Domain::ReminderRule> findRule(const Domain::Uid &uid) = 0;
    virtual SaveResult insertRule(const Domain::ReminderRule &rule) = 0;
    virtual SaveResult updateRule(const Domain::ReminderRule &rule, int expectedRevision) = 0;
    virtual std::vector<Domain::ReminderRule> enabledRules() = 0;

    virtual SaveResult insertDelivery(const Domain::ReminderDelivery &delivery) = 0;
    virtual bool existsDeliveryKey(const std::string &idempotencyKey) = 0;
    virtual std::vector<Domain::ReminderDelivery> pendingDeliveries(
        const std::string &nowIso) = 0;
    virtual SaveResult markDelivery(const Domain::Uid &uid, const std::string &status,
                                    const std::optional<std::string> &error) = 0;
};

class AchievementRepositoryPort
{
public:
    virtual ~AchievementRepositoryPort() = default;
    virtual SaveResult insert(const Domain::Achievement &achievement) = 0;
    virtual bool exists(const Domain::Uid &userId, const std::string &type,
                        const std::string &sourceType, const std::string &sourceUid) = 0;
    // 历史与成就页只读投影（真实事件驱动，不生成虚假成就）
    virtual std::vector<Domain::Achievement> listForUser(const Domain::Uid &userId,
                                                         int limit) = 0;
};

class BackupRepositoryPort
{
public:
    virtual ~BackupRepositoryPort() = default;
    virtual std::optional<Domain::BackupRecord> find(const Domain::Uid &uid) = 0;
    virtual SaveResult insert(const Domain::BackupRecord &record) = 0;
    virtual SaveResult update(const Domain::BackupRecord &record) = 0;
    // 设置页备份列表（只读投影）
    virtual std::vector<Domain::BackupRecord> list(int limit) = 0;
};

// 一致性快照生成（SQLite VACUUM INTO；实现位于基础设施）
class BackupPort
{
public:
    virtual ~BackupPort() = default;
    virtual Result<void, ApplicationError> createSnapshot(const std::string &sourceDbPath,
                                                          const std::string &targetPath) = 0;
};

// 数据库整体切换（恢复用；实现负责关闭旧连接→保护原文件→换入→重开→失败回退）
class DatabaseSwitchPort
{
public:
    virtual ~DatabaseSwitchPort() = default;
    virtual Result<void, ApplicationError> switchTo(const std::string &replacementDbPath,
                                                    std::string *preRestoreSnapshotPath) = 0;
};

} // namespace PersonOS::Application
