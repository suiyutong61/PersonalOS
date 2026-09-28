#pragma once

#include <QSqlDatabase>

#include <string>
#include <vector>

#include "application/foundation/Result.h"
#include "application/ports/GoalRepository.h"
#include "application/ports/MelRepository.h"
#include "application/ports/ReviewRepository.h"
#include "application/ports/StateRepository.h"
#include "application/ports/UuidPort.h"
#include "domain/foundation/Clock.h"

// 真实 v2 数据迁移（DD-001 §13.2 E2E-18；数据库设计 §7 数据搬迁）
// 适配规则（不把每日计划直接冒充正式 MEL）：
//   goals → goal_v3（层级/状态/优先级映射；父子关系转 goal_relation part_of）
//   state_snapshots → 状态事件（source=imported；原日期为 observed_at）
//   plans：仅 status='closed' 且有任务的计划映射为历史 MEL（state=closed，
//     任务→mel_tasks，实际耗时→progress_events）；active 计划不映射（报告记录）
//   reviews → reviews_v4 挂接映射出的 MEL（找不到对应 MEL 则报告记录）
// 幂等：legacy_id_map_v6 主键（source_table, source_id, target_table）保证重复
// 运行不产生重复对象；旧表不删除、不修改。
namespace PersonOS::Application {

class LegacyMigration
{
public:
    struct Report
    {
        int goalsMigrated = 0;
        int stateSnapshotsMigrated = 0;
        int plansMigrated = 0;
        int plansSkippedActive = 0;
        int tasksMigrated = 0;
        int eventsMigrated = 0;
        int reviewsMigrated = 0;
        int reviewsSkipped = 0;
    };

    LegacyMigration(QSqlDatabase database, GoalRepository &goals, MelRepository &mels,
                    ReviewRepository &reviews, StateRepository &states, UuidPort &uids,
                    const Domain::Clock &clock);

    // 执行一轮迁移（可续跑）；返回统计报告
    Result<Report, ApplicationError> run();

private:
    // 旧 id → 新 uid 映射（进程内缓存 + legacy_id_map_v6 持久化）
    std::string targetUidFor(const std::string &sourceTable, long long sourceId);

    QSqlDatabase m_database;
    GoalRepository &m_goals;
    MelRepository &m_mels;
    ReviewRepository &m_reviews;
    StateRepository &m_states;
    UuidPort &m_uids;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Application
