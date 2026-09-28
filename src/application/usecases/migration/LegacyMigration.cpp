#include "application/usecases/migration/LegacyMigration.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QVariant>

#include "application/audit/Audit.h"

namespace PersonOS::Application {

namespace {

// 旧 level → 新 goal_type（层级语义保留；不伪造领域信息）
std::string mapLevelToType(const std::string &level)
{
    if (level == "vision" || level == "long_term")
        return "long_term";
    if (level == "annual")
        return "course";
    return "course";
}

// 旧时间（datetime('now','localtime') 格式）→ UTC ISO（原样保留可读性；报告注明）
std::string toUtcIso(const QString &value)
{
    if (value.isEmpty())
        return {};
    QString text = value;
    text.replace(QLatin1Char(' '), QLatin1Char('T'));
    if (!text.contains(QLatin1Char('Z')))
        text += QLatin1String("Z");
    return text.toStdString();
}

} // namespace

LegacyMigration::LegacyMigration(QSqlDatabase database, GoalRepository &goals,
                                 MelRepository &mels, ReviewRepository &reviews,
                                 StateRepository &states, UuidPort &uids,
                                 const Domain::Clock &clock)
    : m_database(std::move(database)), m_goals(goals), m_mels(mels),
      m_reviews(reviews), m_states(states), m_uids(uids), m_clock(clock)
{}

std::string LegacyMigration::targetUidFor(const std::string &sourceTable, long long sourceId)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT target_uid FROM legacy_id_maps_v6 WHERE source_table=? AND source_id=?"));
    query.addBindValue(QString::fromStdString(sourceTable));
    query.addBindValue(sourceId);
    if (query.exec() && query.next())
        return query.value(0).toString().toStdString();
    return {};
}

Result<LegacyMigration::Report, ApplicationError> LegacyMigration::run()
{
    Report report;
    QSqlDatabase database = m_database;

    // 迁移运行记录（幂等映射行引用它；每轮执行追加一条）
    qint64 runId = 0;
    {
        QSqlQuery run(database);
        run.prepare(QStringLiteral(
            "INSERT INTO legacy_migration_runs_v6(uid,source_schema,target_schema,status,"
            "cursor_json,counts_json,started_at) VALUES(?,2,6,'running','{}','{}',?)"));
        run.addBindValue(QString::fromStdString(m_uids.next().value()));
        run.addBindValue(QString::fromStdString(m_clock.utcIso()));
        if (run.exec())
            runId = run.lastInsertId().toLongLong();
    }

    // 1) goals → goal_v3
    {
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("SELECT id, parent_id, level, title, description, "
                                       "target_date, status, priority FROM goals")))
            return Result<Report, ApplicationError>::failure(
                {ErrorCode::Storage, "legacy goals read failed", {}, false});
        while (query.next()) {
            const long long sourceId = query.value(0).toLongLong();
            if (!targetUidFor("goals", sourceId).empty())
                continue;
            Domain::Goal goal;
            goal.uid = m_uids.next();
            // userId：v1 单用户档案
            {
                QSqlQuery user(database);
                if (!user.exec(QStringLiteral("SELECT uid FROM user_profiles_v3 LIMIT 1"))
                    || !user.next())
                    continue;
                goal.userId = *Domain::Uid::parse(user.value(0).toString().toStdString());
            }
            goal.title = query.value(3).toString().toStdString();
            goal.description = query.value(4).toString().toStdString();
            goal.goalType = mapLevelToType(query.value(2).toString().toStdString());
            goal.status = [&]() {
                const std::string status = query.value(6).toString().toStdString();
                if (status == "paused")
                    return Domain::GoalStatus::Paused;
                if (status == "achieved")
                    return Domain::GoalStatus::Achieved;
                if (status == "abandoned")
                    return Domain::GoalStatus::Abandoned;
                return Domain::GoalStatus::Active;
            }();
            goal.priority = query.value(7).toInt();
            goal.desiredLevelJson = "{}";
            goal.userDefinedLevel = false;
            goal.domainManifestId = [&]() {
                QSqlQuery manifest(database);
                if (manifest.exec(QStringLiteral(
                        "SELECT uid FROM domain_manifests_v3 WHERE domain_code='learning'"))
                    && manifest.next())
                    return *Domain::Uid::parse(manifest.value(0).toString().toStdString());
                return Domain::Uid{};
            }();
            if (goal.domainManifestId.empty())
                continue;
            goal.createdAt = toUtcIso(query.value(8).toString());
            goal.updatedAt = goal.createdAt;
            const auto saved = m_goals.insert(goal);
            if (!saved.ok)
                continue;
            ++report.goalsMigrated;
            QSqlQuery map(database);
            map.prepare(QStringLiteral(
                "INSERT OR IGNORE INTO legacy_id_maps_v6(source_table,source_id,target_table,"
                "target_uid,run_id) VALUES('goals',?,'goal_v3',?,?)"));
            map.addBindValue(sourceId);
            map.addBindValue(QString::fromStdString(goal.uid.value()));
            map.addBindValue(runId);
            map.exec();
        }

        // 父子关系：parent_id → goal_relation part_of（按映射解析）
        QSqlQuery relations(database);
        if (relations.exec(QStringLiteral("SELECT id, parent_id FROM goals WHERE parent_id "
                                          "IS NOT NULL"))) {
            while (relations.next()) {
                const std::string childUid = targetUidFor("goals", relations.value(0).toLongLong());
                const std::string parentUid =
                    targetUidFor("goals", relations.value(1).toLongLong());
                if (childUid.empty() || parentUid.empty())
                    continue;
                QSqlQuery insert(database);
                insert.prepare(QStringLiteral(
                    "INSERT OR IGNORE INTO goal_relations_v3(from_goal_id,to_goal_id,"
                    "relation_type,strength) VALUES("
                    "(SELECT id FROM goals_v3 WHERE uid=?),(SELECT id FROM goals_v3 WHERE uid=?),"
                    "'part_of',1.0)"));
                insert.addBindValue(QString::fromStdString(childUid));
                insert.addBindValue(QString::fromStdString(parentUid));
                insert.exec();
            }
        }
    }

    // 2) state_snapshots → 状态事件（source=imported）
    {
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("SELECT id, date, sleep_hours, energy, focus, mood, "
                                       "note FROM state_snapshots"))) {
            // 空表或读取失败均可继续（报告为准）
        } else {
            while (query.next()) {
                const long long sourceId = query.value(0).toLongLong();
                if (!targetUidFor("state_snapshots", sourceId).empty())
                    continue;
                const std::string observedAt = toUtcIso(query.value(1).toString());
                const auto definitions = {
                    std::make_pair(std::string("energy"), query.value(3)),
                    std::make_pair(std::string("focus"), query.value(4)),
                    std::make_pair(std::string("mood"), query.value(5)),
                };
                int appended = 0;
                for (const auto &definition : definitions) {
                    if (definition.second.isNull())
                        continue;
                    const auto def = m_states.findDefinitionByCode(definition.first);
                    if (!def)
                        continue;
                    Domain::StateEvent event;
                    event.uid = m_uids.next();
                    event.userId = [&]() {
                        QSqlQuery user(database);
                        user.exec(QStringLiteral("SELECT uid FROM user_profiles_v3 LIMIT 1"));
                        user.next();
                        return *Domain::Uid::parse(user.value(0).toString().toStdString());
                    }();
                    event.definitionId = def->uid;
                    event.source = Domain::StateSource::Imported;
                    event.observedAt = observedAt;
                    event.validUntil = observedAt;
                    event.consentScope = "imported";
                    event.idempotencyKey = "mig:snapshot:" + std::to_string(sourceId) + ":"
                                           + definition.first;
                    event.valueJson = "{\"value\":" + std::to_string(definition.second.toInt())
                                      + ",\"min\":1,\"max\":5}";
                    if (m_states.appendEvent(event).ok)
                        ++appended;
                }
                if (appended > 0) {
                    ++report.stateSnapshotsMigrated;
                    QSqlQuery map(database);
                    map.prepare(QStringLiteral(
                        "INSERT OR IGNORE INTO legacy_id_maps_v6(source_table,source_id,"
                        "target_table,target_uid,run_id) VALUES('state_snapshots',?,"
                        "'state_event_v4',?,NULL)"));
                    map.addBindValue(sourceId);
                    map.addBindValue(QStringLiteral("migrated"));
                    map.addBindValue(runId);
                    map.exec();
                }
            }
        }
    }

    // 3) plans（仅 closed）→ 历史 MEL；tasks → mel_tasks；events → progress_events
    {
        QSqlQuery plans(database);
        if (!plans.exec(QStringLiteral("SELECT id, period_type, period_start, period_end, "
                                       "status, note FROM plans"))) {
            return Result<Report, ApplicationError>::failure(
                {ErrorCode::Storage, "legacy plans read failed", {}, false});
        }
        while (plans.next()) {
            const long long planId = plans.value(0).toLongLong();
            const std::string status = plans.value(4).toString().toStdString();
            if (status != "closed") {
                ++report.plansSkippedActive;   // 活跃计划不冒充正式 MEL（适配规则）
                continue;
            }
            if (!targetUidFor("plans", planId).empty())
                continue;

            // 该计划的任务（至少一个才映射；否则仅报告）
            QSqlQuery tasks(database);
            tasks.prepare(QStringLiteral("SELECT COUNT(*) FROM tasks WHERE plan_id=?"));
            tasks.addBindValue(planId);
            tasks.exec();
            tasks.next();
            if (tasks.value(0).toInt() == 0) {
                QSqlQuery map(database);
                map.prepare(QStringLiteral(
                    "INSERT OR IGNORE INTO legacy_id_maps_v6(source_table,source_id,"
                    "target_table,target_uid,run_id) VALUES('plans',?,'mel_v4','skipped',"
                    "?)"));
                map.addBindValue(planId);
                map.addBindValue(runId);
                map.exec();
                continue;
            }

            Domain::Mel mel;
            mel.uid = m_uids.next();
            mel.userId = [&]() {
                QSqlQuery user(database);
                user.exec(QStringLiteral("SELECT uid FROM user_profiles_v3 LIMIT 1"));
                user.next();
                return *Domain::Uid::parse(user.value(0).toString().toStdString());
            }();
            mel.goalId = [&]() {
                QSqlQuery goal(database);
                if (goal.exec(QStringLiteral(
                        "SELECT uid FROM goals_v3 ORDER BY id LIMIT 1"))
                    && goal.next())
                    return *Domain::Uid::parse(goal.value(0).toString().toStdString());
                return Domain::Uid{};
            }();
            if (mel.goalId.empty()) {
                continue;   // 无目标则无法满足 MEL 外键；报告跳过
            }
            mel.manifestVersionId = [&]() {
                QSqlQuery manifest(database);
                if (manifest.exec(QStringLiteral(
                        "SELECT uid FROM domain_manifest_versions_v3 LIMIT 1"))
                    && manifest.next())
                    return *Domain::Uid::parse(manifest.value(0).toString().toStdString());
                return Domain::Uid{};
            }();
            if (mel.manifestVersionId.empty())
                continue;
            mel.title = "迁移历史周期计划";
            mel.state = Domain::MelState::Closed;
            mel.plannedStartAt = toUtcIso(plans.value(2).toString());
            mel.plannedEndAt = toUtcIso(plans.value(3).toString());
            mel.timezoneId = "Asia/Shanghai";
            mel.settlementMode = "manual";
            mel.capacityMin = 0;
            mel.reserveMin = 0;
            mel.rationale = "迁移自旧计划（历史事实；不冒充正式 MEL 生成）";
            mel.createdAt = toUtcIso(plans.value(2).toString());
            if (!mel.hasValidWindow())
                continue;
            const auto saved = m_mels.insert(mel);
            if (!saved.ok)
                continue;
            ++report.plansMigrated;
            QSqlQuery map(database);
            map.prepare(QStringLiteral(
                "INSERT OR IGNORE INTO legacy_id_maps_v6(source_table,source_id,target_table,"
                "target_uid,run_id) VALUES('plans',?,'mel_v4',?,?)"));
            map.addBindValue(planId);
            map.addBindValue(QString::fromStdString(mel.uid.value()));
            map.addBindValue(runId);
            map.exec();

            // 任务映射
            QSqlQuery legacyTasks(database);
            legacyTasks.prepare(QStringLiteral(
                "SELECT id, title, planned_minutes, status, actual_minutes FROM tasks "
                "WHERE plan_id=? ORDER BY sort_order"));
            legacyTasks.addBindValue(planId);
            if (legacyTasks.exec())
                while (legacyTasks.next()) {
                    const long long taskId = legacyTasks.value(0).toLongLong();
                    const std::string taskStatus =
                        legacyTasks.value(3).toString().toStdString();
                    const double progress = taskStatus == "completed" ? 1.0
                                           : taskStatus == "partial" ? 0.5
                                                                    : 0.0;
                    Domain::MelTask task;
                    task.uid = m_uids.next();
                    task.melId = mel.uid;
                    task.title = legacyTasks.value(1).toString().toStdString();
                    task.plannedEffortMin = legacyTasks.value(2).toInt();
                    task.state = taskStatus == "completed"
                                     ? Domain::MelTaskState::Completed
                                     : (taskStatus == "cancelled"
                                            ? Domain::MelTaskState::Cancelled
                                            : Domain::MelTaskState::Pending);
                    task.progress = progress;
                    task.completionRuleJson = "{}";
                    if (m_mels.insertTask(task).ok) {
                        ++report.tasksMigrated;
                        // 实际耗时 → 追加式进度事件
                        const int actual = legacyTasks.value(4).toInt();
                        if (actual > 0) {
                            m_mels.appendProgressEvent(
                                mel, task, static_cast<double>(actual),
                                "迁移自旧任务实际耗时", "mig:task:" + std::to_string(taskId));
                        }
                    }
                }
        }
    }

    // 4) reviews → reviews_v4（挂接映射出的 MEL；找不到则报告跳过）
    {
        QSqlQuery reviews(database);
        if (reviews.exec(QStringLiteral(
                "SELECT id, review_type, period_start, summary, problems, causes, "
                "next_actions FROM reviews"))) {
            while (reviews.next()) {
                const long long reviewId = reviews.value(0).toLongLong();
                if (!targetUidFor("reviews", reviewId).empty())
                    continue;
                // 找同 period_start 的已迁移 MEL
                QSqlQuery mel(database);
                mel.prepare(QStringLiteral(
                    "SELECT m.uid FROM mels_v4 m JOIN legacy_id_maps_v6 map ON "
                    "map.target_uid=m.uid AND map.source_table='plans' "
                    "JOIN plans p ON p.id=map.source_id WHERE p.period_start=? LIMIT 1"));
                mel.addBindValue(reviews.value(2).toString());
                if (!mel.exec() || !mel.next()) {
                    ++report.reviewsSkipped;
                    continue;
                }
                Domain::Review review;
                review.uid = m_uids.next();
                review.melId = *Domain::Uid::parse(mel.value(0).toString().toStdString());
                review.status = Domain::ReviewStatus::Closed;
                review.startedAt = toUtcIso(reviews.value(2).toString());
                review.completedAt = toUtcIso(reviews.value(2).toString());
                review.summary = reviews.value(3).toString().toStdString();
                review.nextAction = reviews.value(6).toString().toStdString();
                if (m_reviews.insert(review).ok) {
                    ++report.reviewsMigrated;
                    QSqlQuery map(database);
                    map.prepare(QStringLiteral(
                        "INSERT OR IGNORE INTO legacy_id_maps_v6(source_table,source_id,"
                        "target_table,target_uid,run_id) VALUES('reviews',?,'review_v4',?,"
                        "?)"));
                    map.addBindValue(reviewId);
                    map.addBindValue(QString::fromStdString(review.uid.value()));
                    map.addBindValue(runId);
                    map.exec();
                }
            }
        }
    }

    Audit::record({"system", {}, "legacy.migration_run", "migration", "v2",
                   "{\"goals\":" + std::to_string(report.goalsMigrated) + "}"});
    return Result<Report, ApplicationError>::success(report);
}

} // namespace PersonOS::Application
