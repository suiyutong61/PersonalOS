#pragma once

#include <optional>
#include <string>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/MelRepository.h"
#include "application/ports/OperationPorts.h"
#include "application/ports/UuidPort.h"
#include "application/usecases/mel/MelUseCases.h"
#include "domain/foundation/Clock.h"
#include "domain/operations/Operations.h"

// 运维用例（DD-001 §12；DR-024/026/031；requirements R3.3.1/10.6）
namespace PersonOS::Application {

class ReminderService
{
public:
    ReminderService(ReminderRepositoryPort &repo, MelRepository &mels,
                    NotificationPort &notifications, UuidPort &uids,
                    const Domain::Clock &clock);

    // 为 owner 建立提醒（相对事件时间 offset 分钟）
    struct RuleInput
    {
        std::optional<std::string> eventUid;
        std::string ownerType;
        std::string ownerUid;
        int offsetMin;
        std::string channel = "app";
    };
    Result<Domain::ReminderRule, ApplicationError> createRule(const RuleInput &input);

    // 关闭/暂停提醒（状态表达，不物理删除）
    Result<Domain::ReminderRule, ApplicationError> disableRule(const Domain::Uid &ruleUid,
                                                               int expectedRevision);

    // 重新启用提醒（规则管理 UI 开关）
    Result<Domain::ReminderRule, ApplicationError> enableRule(const Domain::Uid &ruleUid,
                                                              int expectedRevision);

    // 为无任何提醒规则的活跃 MEL 自动建立默认 Deadline 规则（app 渠道、
    // 提前 0 分钟）。停用规则视为"用户已决定"，不自动重建。返回新建数。
    Result<int, ApplicationError> ensureMelDeadlineReminders(const Domain::Uid &userId,
                                                             int limit);

    // 调度：v1 只调度绑定 MEL 的规则——到期时刻 = MEL Deadline − offsetMin；
    // 未到期不产生投递；幂等键 = rule uid + 到期时刻（重启补发不重复，DB-06）
    Result<int, ApplicationError> scheduleDue(const std::string &triggerAtIso);

    // 投递：发送全部待投递（静默时段记 suppressed；通知失败只记录 failed
    // 并计入重试，不改变业务状态，DR-024）
    Result<int, ApplicationError> dispatchPending();

private:
    ReminderRepositoryPort &m_repo;
    MelRepository &m_mels;
    NotificationPort &m_notifications;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

class AchievementService
{
public:
    AchievementService(AchievementRepositoryPort &repo, UuidPort &uids,
                       const Domain::Clock &clock);

    // 由真实业务事件触发（唯一约束防重复解锁；DR-031）
    Result<Domain::Achievement, ApplicationError> earn(
        const Domain::Uid &userId, const std::string &type, const std::string &title,
        const std::string &description, const std::string &sourceType,
        const std::string &sourceUid, const std::string &evidenceJson = "{}");

private:
    AchievementRepositoryPort &m_repo;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

class BackupService
{
public:
    BackupService(BackupRepositoryPort &repo, BackupPort &snapshots, UuidPort &uids,
                  const Domain::Clock &clock);

    // 创建一致性快照（SQLite VACUUM INTO）+ SHA-256 校验记录
    struct CreateInput
    {
        std::string sourceDbPath;     // 数据库文件绝对路径
        std::string targetPath;       // 备份文件路径
        int dbSchemaVersion;
    };
    Result<Domain::BackupRecord, ApplicationError> createBackup(const CreateInput &input);

    // 校验备份（存在 + 哈希匹配）
    Result<bool, ApplicationError> verifyBackup(const Domain::Uid &backupUid);

private:
    BackupRepositoryPort &m_repo;
    BackupPort &m_snapshots;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

// 安全恢复（DR-026；数据库设计 §7）：校验备份→临时区完整性检查→整体切换→失败回退
class RestoreService
{
public:
    RestoreService(BackupRepositoryPort &repo, DatabaseSwitchPort &switcher,
                    UuidPort &uids, const Domain::Clock &clock);

    struct RestoreReport
    {
        bool switched = false;      // 是否已整体切换
        std::string preRestoreSnapshotPath;   // 恢复前安全快照（成功时保留）
    };

    // 恢复指定备份：任何一步失败都保持原数据可用（调用方先自行备份）。
    // supportedSchemaVersion = 当前程序支持的数据库 schema 版本
    // （高于此版本的备份被拒绝——版本随应用演进，不得写死）
    Result<RestoreReport, ApplicationError> restore(const Domain::Uid &backupUid,
                                                    int supportedSchemaVersion);

private:
    BackupRepositoryPort &m_repo;
    DatabaseSwitchPort &m_switcher;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

// 启动恢复编排（DD-001 §12：迁移后恢复未完成作业 → 逾期 MEL 结算 → 提醒补发）
class StartupRecovery
{
public:
    StartupRecovery(MelRepository &mels, MelUseCases &melUseCases,
                    ReminderService &reminders, const Domain::Uid &userId,
                    const Domain::Clock &clock);

    struct Report
    {
        int dueMelsFound = 0;
        int settled = 0;
        int remindersCreated = 0;     // 自动建立的默认 Deadline 规则数
        int remindersScheduled = 0;   // 补发的投递行数
    };
    Result<Report, ApplicationError> run();

private:
    MelRepository &m_mels;
    MelUseCases &m_melUseCases;
    ReminderService &m_reminders;
    const Domain::Uid &m_userId;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
