#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "domain/foundation/Uid.h"

// 运维域对象：日历/提醒/成就/备份（DD-001 §12；DR-024/026/031；数据库设计 §6）
namespace PersonOS::Domain {

struct CalendarEvent
{
    Uid uid;
    std::string ownerType;            // mel / goal / custom
    std::string ownerUid;
    std::string startsAt;             // UTC ISO-8601
    std::string endsAt;               // > starts_at（DB CHECK）
    std::string timezoneId;
    std::string status = "scheduled"; // scheduled / completed / cancelled
    std::optional<std::string> externalRef;
    int revision = 1;

    bool isValid() const
    {
        return !uid.empty() && !ownerType.empty() && !ownerUid.empty()
               && !startsAt.empty() && endsAt > startsAt;
    }
};

struct ReminderRule
{
    Uid uid;
    std::optional<std::string> eventUid;   // 可空：独立提醒
    std::string ownerType;
    std::string ownerUid;
    int offsetMin = 0;                     // 事件前 N 分钟触发（可负？v1 限 >=0）
    std::string channel = "app";           // app / native
    bool enabled = true;
    std::string quietHoursJson = "{}";
    std::string snoozePolicyJson = "{}";
    int revision = 1;

    bool isValid() const
    {
        return !uid.empty() && !ownerType.empty() && !ownerUid.empty() && offsetMin >= 0;
    }
};

struct ReminderDelivery
{
    Uid uid;
    Uid ruleUid;
    std::string scheduledAt;
    std::optional<std::string> deliveredAt;
    std::string status = "pending";       // pending/delivered/failed/suppressed/cancelled
    std::optional<std::string> error;
    std::string idempotencyKey;           // UNIQUE（补发不重复，DB-06）
    int attemptCount = 0;                 // 失败重试计数（达到上限后终态失败，不再重选）

    bool isValid() const
    {
        return !uid.empty() && !ruleUid.empty() && !scheduledAt.empty()
               && !idempotencyKey.empty();
    }
};

struct Achievement
{
    Uid uid;
    Uid userId;
    std::string achievementType;          // mel_completed / stage_reached / review_streak
    std::string title;
    std::string description;
    std::string earnedAt;
    std::string sourceType;               // mel / goal / review
    std::string sourceUid;
    std::string evidenceJson = "{}";
    int revision = 1;

    bool isValid() const
    {
        return !uid.empty() && !userId.empty() && !achievementType.empty()
               && !title.empty() && !sourceType.empty() && !sourceUid.empty();
    }
};

struct BackupRecord
{
    Uid uid;
    std::string startedAt;
    std::optional<std::string> completedAt;
    std::string status = "running";       // running / verified / failed
    std::string relativePath;
    std::optional<std::string> sha256;
    int dbSchemaVersion = 0;
    std::string assetManifestJson = "{}";
    std::optional<std::string> errorJson;

    bool isValid() const
    {
        return !uid.empty() && !startedAt.empty() && !relativePath.empty()
               && dbSchemaVersion > 0;
    }
};

} // namespace PersonOS::Domain
