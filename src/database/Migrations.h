#pragma once

#include <initializer_list>

// Personal OS 数据库迁移注册表（README 3.3.2 schema v1）
//
// 规则：
// 1. 只允许追加新版本，不得修改已发布版本的 SQL —— 历史数据不可篡改原则（2.4.14）。
// 2. 每个 Step 的 statements 按顺序执行，整个 Step 包在一个事务里。
// 3. schema_version 由迁移器在迁移成功后写入 app_meta，SQL 中不写死。
namespace Migrations {

struct Step {
    int version;
    std::initializer_list<const char *> statements;
};

// v1 = MVP 最小闭环 + 变更机制（goals/state_snapshots/plans/tasks/events/
//      reviews/proposals/change_logs/versions + core_values/principles/app_meta）
inline const std::initializer_list<Step> kSteps = {
    {1,
     {
         R"SQL(
CREATE TABLE app_meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
)
)SQL",
         R"SQL(
CREATE TABLE core_values (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    description TEXT,
    sort_order  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
)
)SQL",
         R"SQL(
CREATE TABLE principles (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    text        TEXT NOT NULL,
    sort_order  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
)
)SQL",
         R"SQL(
CREATE TABLE goals (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    parent_id   INTEGER REFERENCES goals(id),
    level       TEXT NOT NULL CHECK (level IN ('vision','long_term','annual','quarterly','monthly','weekly')),
    title       TEXT NOT NULL,
    description TEXT,
    target_date TEXT,
    status      TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active','paused','achieved','abandoned')),
    priority    INTEGER NOT NULL DEFAULT 50,
    created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
)
)SQL",
         R"SQL(
CREATE INDEX idx_goals_parent ON goals(parent_id)
)SQL",
         R"SQL(
CREATE TABLE state_snapshots (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    date        TEXT NOT NULL UNIQUE,
    sleep_hours REAL,
    energy      INTEGER CHECK (energy BETWEEN 1 AND 5),
    focus       INTEGER CHECK (focus BETWEEN 1 AND 5),
    mood        INTEGER CHECK (mood BETWEEN 1 AND 5),
    note        TEXT,
    created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
)
)SQL",
         R"SQL(
CREATE TABLE plans (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    period_type  TEXT NOT NULL CHECK (period_type IN ('daily','weekly','monthly')),
    period_start TEXT NOT NULL,
    period_end   TEXT,
    status       TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active','closed')),
    note         TEXT,
    created_at   TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    UNIQUE (period_type, period_start)
)
)SQL",
         R"SQL(
CREATE TABLE tasks (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    plan_id         INTEGER REFERENCES plans(id),
    goal_id         INTEGER REFERENCES goals(id),
    title           TEXT NOT NULL,
    description     TEXT,
    planned_minutes INTEGER,
    due_date        TEXT NOT NULL,
    status          TEXT NOT NULL DEFAULT 'planned'
                    CHECK (status IN ('planned','started','completed','partial',
                                      'delayed','skipped','cancelled','interrupted')),
    actual_minutes  INTEGER,
    sort_order      INTEGER NOT NULL DEFAULT 0,
    created_at      TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    updated_at      TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    completed_at    TEXT
)
)SQL",
         R"SQL(
CREATE INDEX idx_tasks_due ON tasks(due_date, status)
)SQL",
         R"SQL(
CREATE INDEX idx_tasks_plan ON tasks(plan_id)
)SQL",
         R"SQL(
CREATE TABLE events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    date        TEXT NOT NULL,
    occurred_at TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    type        TEXT NOT NULL,
    title       TEXT NOT NULL,
    description TEXT,
    task_id     INTEGER REFERENCES tasks(id),
    created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
)
)SQL",
         R"SQL(
CREATE INDEX idx_events_date ON events(date)
)SQL",
         R"SQL(
CREATE TABLE reviews (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    review_type  TEXT NOT NULL CHECK (review_type IN ('daily','weekly','monthly')),
    period_start TEXT NOT NULL,
    period_end   TEXT,
    summary      TEXT NOT NULL,
    problems     TEXT,
    causes       TEXT,
    next_actions TEXT,
    created_at   TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    UNIQUE (review_type, period_start)
)
)SQL",
         R"SQL(
CREATE TABLE proposals (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    target_type     TEXT NOT NULL,
    target_id       INTEGER,
    current_value   TEXT,
    proposed_value  TEXT NOT NULL,
    reason          TEXT NOT NULL,
    evidence        TEXT,
    expected_effect TEXT,
    risk            TEXT,
    status          TEXT NOT NULL DEFAULT 'draft'
                    CHECK (status IN ('draft','review','approved','rejected','applied')),
    created_at      TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    decided_at      TEXT
)
)SQL",
         R"SQL(
CREATE TABLE change_logs (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    target_type    TEXT NOT NULL,
    target_id      INTEGER NOT NULL,
    before_summary TEXT,
    after_summary  TEXT,
    reason         TEXT NOT NULL,
    proposal_id    INTEGER REFERENCES proposals(id),
    version_id     INTEGER REFERENCES versions(id),
    created_at     TEXT NOT NULL DEFAULT (datetime('now','localtime'))
)
)SQL",
         R"SQL(
CREATE TABLE versions (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    version_number TEXT NOT NULL UNIQUE,
    parent_version TEXT,
    description    TEXT,
    status         TEXT NOT NULL DEFAULT 'stable' CHECK (status IN ('stable','experiment','archive')),
    created_at     TEXT NOT NULL DEFAULT (datetime('now','localtime'))
)
)SQL",
     }},
};

} // namespace Migrations
