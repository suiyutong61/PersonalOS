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

inline constexpr int kLatestVersion = 8;

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
    // v2 = v1.0 量化底座 + 证据/演化实体（design.md 1.8.2，纯增量）：
    //      tasks/proposals ALTER 增列 + personal_model_params / calibration_records /
    //      metrics / guardrail_warnings / experiments / decisions + 证据库四表。
    //      注：FR-A-02~05 延期 v1.1，goal_commitments / goal_links / goals.goal_type 不进本迁移。
    {2,
     {
         R"SQL(
ALTER TABLE tasks ADD COLUMN trigger_when TEXT
)SQL",
         R"SQL(
ALTER TABLE tasks ADD COLUMN trigger_where TEXT
)SQL",
         R"SQL(
ALTER TABLE tasks ADD COLUMN trigger_how TEXT
)SQL",
         R"SQL(
ALTER TABLE tasks ADD COLUMN task_type TEXT CHECK (task_type IN ('routine','learning'))
)SQL",
         R"SQL(
ALTER TABLE proposals ADD COLUMN basis_types TEXT
)SQL",
         R"SQL(
CREATE TABLE personal_model_params (
    key          TEXT PRIMARY KEY,
    value        REAL,
    value_text   TEXT,
    prior_source TEXT NOT NULL DEFAULT 'population_default'
                 CHECK (prior_source IN ('research_evidence','population_default','user_input')),
    evidence_ref TEXT,
    updated_at   TEXT NOT NULL DEFAULT (datetime('now','localtime'))
)
)SQL",
         R"SQL(
CREATE TABLE calibration_records (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    param_key   TEXT NOT NULL,
    old_value   TEXT,
    new_value   TEXT,
    data_window TEXT NOT NULL,
    rule        TEXT NOT NULL,
    trigger     TEXT NOT NULL CHECK (trigger IN ('periodic','experiment','manual')),
    created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
)
)SQL",
         R"SQL(
CREATE TABLE metrics (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    date       TEXT NOT NULL,
    key        TEXT NOT NULL,
    value      REAL NOT NULL,
    window     TEXT,
    note       TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    UNIQUE (date, key, window)
)
)SQL",
         R"SQL(
CREATE TABLE guardrail_warnings (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    date          TEXT NOT NULL,
    type          TEXT NOT NULL CHECK (type IN ('workload','stall')),
    rule          TEXT NOT NULL,
    data_snapshot TEXT NOT NULL,
    threshold     TEXT NOT NULL,
    suggestion    TEXT,
    evidence_ref  TEXT,
    status        TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active','handled','dismissed')),
    handled_by    TEXT,
    created_at    TEXT NOT NULL DEFAULT (datetime('now','localtime'))
)
)SQL",
         R"SQL(
CREATE TABLE experiments (
    id                 INTEGER PRIMARY KEY AUTOINCREMENT,
    hypothesis         TEXT NOT NULL,
    design             TEXT NOT NULL,
    baseline_start     TEXT,
    baseline_end       TEXT,
    intervention_start TEXT,
    intervention_end   TEXT,
    metric_keys        TEXT,
    status             TEXT NOT NULL DEFAULT 'draft'
                       CHECK (status IN ('draft','running','done','discarded')),
    conclusion         TEXT,
    created_at         TEXT NOT NULL DEFAULT (datetime('now','localtime'))
)
)SQL",
         R"SQL(
CREATE TABLE decisions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    title       TEXT NOT NULL,
    background  TEXT,
    basis_types TEXT NOT NULL,
    expected    TEXT,
    result      TEXT,
    created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
)
)SQL",
         R"SQL(
CREATE TABLE research_sources (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    title       TEXT NOT NULL,
    authors     TEXT,
    year        INTEGER,
    doi         TEXT,
    source_type TEXT,
    access      TEXT NOT NULL DEFAULT 'unknown' CHECK (access IN ('fulltext','abstract','missing')),
    created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
)
)SQL",
         R"SQL(
CREATE TABLE evidence (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    source_id     INTEGER NOT NULL REFERENCES research_sources(id),
    domain        INTEGER NOT NULL,
    population    TEXT,
    intervention  TEXT,
    comparator    TEXT,
    outcome       TEXT,
    finding       TEXT NOT NULL,
    limitations   TEXT,
    confidence    TEXT,
    grade         TEXT NOT NULL CHECK (grade IN ('A','B','C','D','E')),
    applicability TEXT,
    created_at    TEXT NOT NULL DEFAULT (datetime('now','localtime'))
)
)SQL",
         R"SQL(
CREATE INDEX idx_evidence_domain ON evidence(domain, grade)
)SQL",
         R"SQL(
CREATE TABLE claims (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    evidence_id INTEGER REFERENCES evidence(id),
    statement   TEXT NOT NULL,
    scope       TEXT,
    uncertainty TEXT
)
)SQL",
         R"SQL(
CREATE TABLE evidence_links (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    claim_id    INTEGER REFERENCES claims(id),
    target_type TEXT NOT NULL,
    target_id   INTEGER NOT NULL,
    created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
)
)SQL",
     }},
    // v3 = 用户、协议、领域配置、目标/路线与内容地图。
    {3,
     {
         R"SQL(
CREATE TABLE user_profiles_v3 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, display_name TEXT,
    timezone_id TEXT NOT NULL, locale TEXT NOT NULL,
    onboarding_status TEXT NOT NULL CHECK(onboarding_status IN ('new','in_progress','complete')),
    profile_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(profile_json)),
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE user_preferences_v3 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE,
    user_id INTEGER NOT NULL REFERENCES user_profiles_v3(id), key TEXT NOT NULL,
    value_json TEXT NOT NULL CHECK(json_valid(value_json)),
    source TEXT NOT NULL CHECK(source IN ('explicit','inferred','default')),
    confidence REAL NOT NULL DEFAULT 1 CHECK(confidence BETWEEN 0 AND 1),
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),
    UNIQUE(user_id,key)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE user_constraints_v3 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE,
    user_id INTEGER NOT NULL REFERENCES user_profiles_v3(id), constraint_type TEXT NOT NULL,
    value_json TEXT NOT NULL CHECK(json_valid(value_json)), starts_at TEXT, ends_at TEXT,
    active INTEGER NOT NULL DEFAULT 1 CHECK(active IN (0,1)), source TEXT NOT NULL,
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),
    CHECK(ends_at IS NULL OR starts_at IS NULL OR ends_at>starts_at)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE protocols_v3 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, code TEXT NOT NULL UNIQUE, name TEXT NOT NULL,
    current_version_uid TEXT, status TEXT NOT NULL CHECK(status IN ('draft','active','retired')),
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE protocol_versions_v3 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE,
    protocol_id INTEGER NOT NULL REFERENCES protocols_v3(id), version_no INTEGER NOT NULL CHECK(version_no>0),
    status TEXT NOT NULL CHECK(status IN ('draft','active','superseded','retired')),
    content_json TEXT NOT NULL CHECK(json_valid(content_json)), schema_version TEXT NOT NULL,
    change_summary TEXT, valid_from TEXT NOT NULL, valid_to TEXT, created_at TEXT NOT NULL,
    created_by TEXT NOT NULL, UNIQUE(protocol_id,version_no)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE protocol_acceptances_v3 (
    id INTEGER PRIMARY KEY, user_id INTEGER NOT NULL REFERENCES user_profiles_v3(id),
    protocol_version_id INTEGER NOT NULL REFERENCES protocol_versions_v3(id),
    accepted_at TEXT NOT NULL, revoked_at TEXT, UNIQUE(user_id,protocol_version_id)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE domain_manifests_v3 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, domain_code TEXT NOT NULL UNIQUE, name TEXT NOT NULL,
    current_version_uid TEXT, status TEXT NOT NULL CHECK(status IN ('draft','active','retired')),
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE domain_manifest_versions_v3 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE,
    manifest_id INTEGER NOT NULL REFERENCES domain_manifests_v3(id), version_no INTEGER NOT NULL CHECK(version_no>0),
    status TEXT NOT NULL CHECK(status IN ('draft','active','superseded','retired')),
    schema_version TEXT NOT NULL, manifest_json TEXT NOT NULL CHECK(json_valid(manifest_json)),
    content_hash TEXT NOT NULL, valid_from TEXT NOT NULL, valid_to TEXT,
    created_at TEXT NOT NULL, created_by TEXT NOT NULL, UNIQUE(manifest_id,version_no),
    UNIQUE(manifest_id,content_hash)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE domain_capabilities_v3 (
    id INTEGER PRIMARY KEY, manifest_version_id INTEGER NOT NULL REFERENCES domain_manifest_versions_v3(id) ON DELETE CASCADE,
    capability_code TEXT NOT NULL, required INTEGER NOT NULL CHECK(required IN (0,1)),
    config_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(config_json)),
    UNIQUE(manifest_version_id,capability_code)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE domain_state_bindings_v3 (
    id INTEGER PRIMARY KEY, manifest_version_id INTEGER NOT NULL REFERENCES domain_manifest_versions_v3(id) ON DELETE CASCADE,
    state_definition_uid TEXT NOT NULL, role TEXT NOT NULL CHECK(role IN ('input','output','guardrail','context')),
    weight REAL, config_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(config_json)),
    UNIQUE(manifest_version_id,state_definition_uid,role)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE domain_decision_points_v3 (
    id INTEGER PRIMARY KEY, manifest_version_id INTEGER NOT NULL REFERENCES domain_manifest_versions_v3(id) ON DELETE CASCADE,
    decision_code TEXT NOT NULL, sequence_no INTEGER NOT NULL CHECK(sequence_no>=0),
    input_schema TEXT NOT NULL CHECK(json_valid(input_schema)), output_schema TEXT NOT NULL CHECK(json_valid(output_schema)),
    policy_ref TEXT, UNIQUE(manifest_version_id,decision_code)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE goals_v3 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, user_id INTEGER NOT NULL REFERENCES user_profiles_v3(id),
    parent_goal_id INTEGER REFERENCES goals_v3(id), domain_manifest_id INTEGER NOT NULL REFERENCES domain_manifests_v3(id),
    title TEXT NOT NULL, description TEXT, goal_type TEXT NOT NULL,
    status TEXT NOT NULL CHECK(status IN ('draft','active','paused','achieved','abandoned','archived')),
    priority INTEGER NOT NULL DEFAULT 50 CHECK(priority BETWEEN 0 AND 100), target_at TEXT,
    desired_level_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(desired_level_json)),
    user_defined_level INTEGER NOT NULL DEFAULT 0 CHECK(user_defined_level IN (0,1)), sort_order INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),
    CHECK(parent_goal_id IS NULL OR parent_goal_id<>id)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE goal_versions_v3 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, goal_id INTEGER NOT NULL REFERENCES goals_v3(id),
    version_no INTEGER NOT NULL CHECK(version_no>0), status TEXT NOT NULL,
    snapshot_json TEXT NOT NULL CHECK(json_valid(snapshot_json)), reason TEXT,
    valid_from TEXT NOT NULL, valid_to TEXT, created_at TEXT NOT NULL, created_by TEXT NOT NULL,
    UNIQUE(goal_id,version_no)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE goal_relations_v3 (
    id INTEGER PRIMARY KEY, from_goal_id INTEGER NOT NULL REFERENCES goals_v3(id),
    to_goal_id INTEGER NOT NULL REFERENCES goals_v3(id),
    relation_type TEXT NOT NULL CHECK(relation_type IN ('depends_on','supports','conflicts_with','part_of')),
    strength REAL NOT NULL DEFAULT 1 CHECK(strength BETWEEN 0 AND 1),
    UNIQUE(from_goal_id,to_goal_id,relation_type), CHECK(from_goal_id<>to_goal_id)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE routes_v3 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, goal_id INTEGER NOT NULL REFERENCES goals_v3(id),
    status TEXT NOT NULL CHECK(status IN ('draft','proposed','confirmed','active','superseded','completed')),
    current_version_uid TEXT, created_by TEXT NOT NULL CHECK(created_by IN ('user','ai','system')),
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE route_versions_v3 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, route_id INTEGER NOT NULL REFERENCES routes_v3(id),
    version_no INTEGER NOT NULL CHECK(version_no>0), status TEXT NOT NULL,
    rationale TEXT NOT NULL, evidence_summary TEXT,
    assumptions_json TEXT NOT NULL DEFAULT '[]' CHECK(json_valid(assumptions_json)),
    user_confirmed_at TEXT, valid_from TEXT NOT NULL, valid_to TEXT, created_at TEXT NOT NULL, created_by TEXT NOT NULL,
    UNIQUE(route_id,version_no)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE route_stages_v3 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE,
    route_version_id INTEGER NOT NULL REFERENCES route_versions_v3(id) ON DELETE CASCADE,
    parent_stage_id INTEGER REFERENCES route_stages_v3(id), title TEXT NOT NULL, description TEXT,
    sequence_no INTEGER NOT NULL CHECK(sequence_no>=0),
    completion_rule_json TEXT NOT NULL CHECK(json_valid(completion_rule_json)),
    estimated_effort_min INTEGER CHECK(estimated_effort_min IS NULL OR estimated_effort_min>=0), status TEXT NOT NULL,
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),
    UNIQUE(route_version_id,parent_stage_id,sequence_no)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE route_stage_relations_v3 (
    id INTEGER PRIMARY KEY, from_stage_id INTEGER NOT NULL REFERENCES route_stages_v3(id) ON DELETE CASCADE,
    to_stage_id INTEGER NOT NULL REFERENCES route_stages_v3(id) ON DELETE CASCADE,
    relation_type TEXT NOT NULL CHECK(relation_type IN ('precedes','blocks','supports')),
    UNIQUE(from_stage_id,to_stage_id,relation_type), CHECK(from_stage_id<>to_stage_id)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE content_maps_v3 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, goal_id INTEGER NOT NULL REFERENCES goals_v3(id),
    title TEXT NOT NULL, source_type TEXT NOT NULL CHECK(source_type IN ('user_document','web','generated','manual')),
    source_ref TEXT, status TEXT NOT NULL,
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE content_nodes_v3 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE,
    content_map_id INTEGER NOT NULL REFERENCES content_maps_v3(id) ON DELETE CASCADE,
    parent_node_id INTEGER REFERENCES content_nodes_v3(id), title TEXT NOT NULL, node_type TEXT NOT NULL,
    sequence_no INTEGER NOT NULL CHECK(sequence_no>=0), weight REAL NOT NULL DEFAULT 1 CHECK(weight>0),
    locator_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(locator_json)),
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),
    UNIQUE(content_map_id,parent_node_id,sequence_no)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE content_progress_v3 (
    id INTEGER PRIMARY KEY, content_node_id INTEGER NOT NULL REFERENCES content_nodes_v3(id),
    user_id INTEGER NOT NULL REFERENCES user_profiles_v3(id),
    state TEXT NOT NULL CHECK(state IN ('not_started','in_progress','completed','skipped')),
    progress REAL NOT NULL DEFAULT 0 CHECK(progress BETWEEN 0 AND 1), updated_at TEXT NOT NULL,
    source_event_uid TEXT, UNIQUE(content_node_id,user_id)
) STRICT
)SQL",
         R"SQL(CREATE INDEX idx_goals_v3_user_status ON goals_v3(user_id,status,priority))SQL",
         R"SQL(CREATE INDEX idx_goals_v3_parent ON goals_v3(parent_goal_id))SQL",
         R"SQL(CREATE INDEX idx_routes_v3_goal_status ON routes_v3(goal_id,status))SQL",
         R"SQL(CREATE INDEX idx_route_stages_v3_parent ON route_stages_v3(parent_stage_id))SQL",
         R"SQL(CREATE INDEX idx_content_nodes_v3_parent ON content_nodes_v3(parent_node_id))SQL",
     }},
    // v4 = MEL、不可变进度/状态、复盘、能力验收与个人校准。
    {4,
     {
         R"SQL(
CREATE TABLE mels_v4 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, user_id INTEGER NOT NULL REFERENCES user_profiles_v3(id),
    goal_id INTEGER NOT NULL REFERENCES goals_v3(id), route_version_id INTEGER REFERENCES route_versions_v3(id),
    manifest_version_id INTEGER NOT NULL REFERENCES domain_manifest_versions_v3(id), title TEXT NOT NULL,
    state TEXT NOT NULL CHECK(state IN ('draft','awaiting_confirmation','active','paused','execution_complete','overdue','settling','awaiting_assessment','reviewing','closed','cancelled')),
    planned_start_at TEXT NOT NULL, planned_end_at TEXT NOT NULL, timezone_id TEXT NOT NULL,
    settlement_mode TEXT NOT NULL CHECK(settlement_mode IN ('deadline','startup','manual')),
    capacity_min INTEGER NOT NULL CHECK(capacity_min>=0), reserve_min INTEGER NOT NULL DEFAULT 0 CHECK(reserve_min>=0),
    rationale TEXT NOT NULL, knowledge_snapshot_uid TEXT, confirmed_at TEXT, activated_at TEXT, closed_at TEXT,
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),
    CHECK(planned_end_at>planned_start_at), CHECK(reserve_min<=capacity_min)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE mel_tasks_v4 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, mel_id INTEGER NOT NULL REFERENCES mels_v4(id) ON DELETE CASCADE,
    parent_task_id INTEGER REFERENCES mel_tasks_v4(id), title TEXT NOT NULL, description TEXT,
    sequence_no INTEGER NOT NULL CHECK(sequence_no>=0), required INTEGER NOT NULL DEFAULT 1 CHECK(required IN (0,1)),
    planned_effort_min INTEGER NOT NULL DEFAULT 0 CHECK(planned_effort_min>=0),
    completion_rule_json TEXT NOT NULL CHECK(json_valid(completion_rule_json)),
    state TEXT NOT NULL CHECK(state IN ('pending','active','completed','skipped','cancelled')),
    progress REAL NOT NULL DEFAULT 0 CHECK(progress BETWEEN 0 AND 1), completed_at TEXT,
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),
    UNIQUE(mel_id,parent_task_id,sequence_no)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE mel_transitions_v4 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, mel_id INTEGER NOT NULL REFERENCES mels_v4(id),
    from_state TEXT NOT NULL, to_state TEXT NOT NULL, trigger TEXT NOT NULL,
    actor_type TEXT NOT NULL, actor_ref TEXT, reason TEXT, idempotency_key TEXT NOT NULL UNIQUE,
    occurred_at TEXT NOT NULL, mel_revision_after INTEGER NOT NULL CHECK(mel_revision_after>0)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE mel_predictions_v4 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, mel_id INTEGER NOT NULL REFERENCES mels_v4(id),
    predicted_completion REAL NOT NULL CHECK(predicted_completion BETWEEN 0 AND 1),
    predicted_effort_min INTEGER NOT NULL CHECK(predicted_effort_min>=0), risk_level TEXT NOT NULL,
    basis_json TEXT NOT NULL CHECK(json_valid(basis_json)), created_at TEXT NOT NULL, superseded_at TEXT
) STRICT
)SQL",
         R"SQL(
CREATE TABLE progress_events_v4 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, user_id INTEGER NOT NULL REFERENCES user_profiles_v3(id),
    mel_id INTEGER REFERENCES mels_v4(id), goal_id INTEGER REFERENCES goals_v3(id), task_id INTEGER REFERENCES mel_tasks_v4(id),
    event_type TEXT NOT NULL CHECK(event_type IN ('started','incremented','completed','corrected','shared')),
    amount REAL, unit TEXT, note TEXT, evidence_asset_uid TEXT, occurred_at TEXT NOT NULL, recorded_at TEXT NOT NULL,
    actor_type TEXT NOT NULL, idempotency_key TEXT NOT NULL UNIQUE, supersedes_uid TEXT,
    CHECK(mel_id IS NOT NULL OR goal_id IS NOT NULL OR task_id IS NOT NULL)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE state_definitions_v4 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, code TEXT NOT NULL UNIQUE,
    category TEXT NOT NULL CHECK(category IN ('basic','cognitive','emotion_pressure','motivation_recreation','body_health','environment','resources_social','behavior_method','system_experience','domain_specific')),
    value_type TEXT NOT NULL, scale_min REAL, scale_max REAL, unit TEXT,
    default_ttl_min INTEGER NOT NULL CHECK(default_ttl_min>0), sensitivity TEXT NOT NULL,
    schema_json TEXT NOT NULL CHECK(json_valid(schema_json)),
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),
    CHECK(scale_min IS NULL OR scale_max IS NULL OR scale_max>scale_min)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE state_events_v4 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, user_id INTEGER NOT NULL REFERENCES user_profiles_v3(id),
    definition_id INTEGER NOT NULL REFERENCES state_definitions_v4(id), value_json TEXT NOT NULL CHECK(json_valid(value_json)),
    source TEXT NOT NULL CHECK(source IN ('explicit','questionnaire','conversation','system_derived','imported')),
    confidence REAL NOT NULL CHECK(confidence BETWEEN 0 AND 1), observed_at TEXT NOT NULL, valid_until TEXT NOT NULL,
    consent_scope TEXT NOT NULL, conversation_ref TEXT, supersedes_uid TEXT, recorded_at TEXT NOT NULL,
    idempotency_key TEXT NOT NULL UNIQUE, CHECK(valid_until>=observed_at)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE state_snapshots_v4 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, user_id INTEGER NOT NULL REFERENCES user_profiles_v3(id),
    as_of TEXT NOT NULL, basic_json TEXT NOT NULL CHECK(json_valid(basic_json)),
    extended_json TEXT NOT NULL CHECK(json_valid(extended_json)), source_event_max_id INTEGER,
    builder_version TEXT NOT NULL, content_hash TEXT NOT NULL, created_at TEXT NOT NULL,
    UNIQUE(user_id,as_of,content_hash)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE reviews_v4 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, mel_id INTEGER NOT NULL UNIQUE REFERENCES mels_v4(id),
    status TEXT NOT NULL CHECK(status IN ('draft','collecting','assessing','decision','confirmed','closed')),
    started_at TEXT NOT NULL, completed_at TEXT, summary TEXT, user_comment TEXT, next_action TEXT,
    knowledge_snapshot_uid TEXT, created_at TEXT NOT NULL, updated_at TEXT NOT NULL,
    revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE questionnaires_v4 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, code TEXT NOT NULL, version_no INTEGER NOT NULL CHECK(version_no>0),
    name TEXT NOT NULL, schema_json TEXT NOT NULL CHECK(json_valid(schema_json)), status TEXT NOT NULL,
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),
    UNIQUE(code,version_no)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE questionnaire_responses_v4 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, review_id INTEGER NOT NULL REFERENCES reviews_v4(id),
    questionnaire_id INTEGER NOT NULL REFERENCES questionnaires_v4(id),
    responses_json TEXT NOT NULL CHECK(json_valid(responses_json)), started_at TEXT NOT NULL, submitted_at TEXT,
    completion_ratio REAL NOT NULL DEFAULT 0 CHECK(completion_ratio BETWEEN 0 AND 1),
    UNIQUE(review_id,questionnaire_id)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE assessments_v4 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, user_id INTEGER NOT NULL REFERENCES user_profiles_v3(id),
    mel_id INTEGER REFERENCES mels_v4(id), goal_id INTEGER NOT NULL REFERENCES goals_v3(id),
    assessment_type TEXT NOT NULL CHECK(assessment_type IN ('recall','explanation','mind_map','exercise','project','operation','self_report','external')),
    status TEXT NOT NULL CHECK(status IN ('draft','ready','in_progress','scored','cancelled')),
    scope_json TEXT NOT NULL CHECK(json_valid(scope_json)), rubric_json TEXT NOT NULL CHECK(json_valid(rubric_json)),
    generated_by TEXT NOT NULL, knowledge_snapshot_uid TEXT,
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE assessment_items_v4 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE,
    assessment_id INTEGER NOT NULL REFERENCES assessments_v4(id) ON DELETE CASCADE,
    item_type TEXT NOT NULL, prompt TEXT NOT NULL, reference_answer TEXT,
    rubric_json TEXT NOT NULL CHECK(json_valid(rubric_json)), source_refs_json TEXT NOT NULL DEFAULT '[]' CHECK(json_valid(source_refs_json)),
    sequence_no INTEGER NOT NULL CHECK(sequence_no>=0), difficulty REAL CHECK(difficulty IS NULL OR difficulty BETWEEN 0 AND 1),
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),
    UNIQUE(assessment_id,sequence_no)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE assessment_attempts_v4 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, assessment_id INTEGER NOT NULL REFERENCES assessments_v4(id),
    started_at TEXT NOT NULL, submitted_at TEXT, answer_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(answer_json)),
    evidence_asset_uid TEXT, self_rating REAL, idempotency_key TEXT NOT NULL UNIQUE
) STRICT
)SQL",
         R"SQL(
CREATE TABLE assessment_results_v4 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, attempt_id INTEGER NOT NULL REFERENCES assessment_attempts_v4(id),
    item_id INTEGER REFERENCES assessment_items_v4(id),
    mastery TEXT NOT NULL CHECK(mastery IN ('fluent','prompted','not_recalled','not_applicable')),
    score REAL, feedback TEXT, scorer TEXT NOT NULL CHECK(scorer IN ('user','ai','rule','external')),
    confidence REAL NOT NULL CHECK(confidence BETWEEN 0 AND 1), created_at TEXT NOT NULL,
    confirmed_by_user INTEGER NOT NULL DEFAULT 0 CHECK(confirmed_by_user IN (0,1)), UNIQUE(attempt_id,item_id,scorer)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE retention_schedules_v4 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, user_id INTEGER NOT NULL REFERENCES user_profiles_v3(id),
    content_node_id INTEGER REFERENCES content_nodes_v3(id), assessment_item_id INTEGER REFERENCES assessment_items_v4(id),
    next_due_at TEXT NOT NULL, interval_min INTEGER NOT NULL CHECK(interval_min>0), stability REAL, difficulty REAL,
    algorithm_code TEXT NOT NULL, algorithm_version TEXT NOT NULL, last_result_uid TEXT,
    active INTEGER NOT NULL DEFAULT 1 CHECK(active IN (0,1)),
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),
    CHECK((content_node_id IS NOT NULL) <> (assessment_item_id IS NOT NULL))
) STRICT
)SQL",
         R"SQL(
CREATE TABLE calibration_records_v4 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, user_id INTEGER NOT NULL REFERENCES user_profiles_v3(id),
    domain_manifest_id INTEGER REFERENCES domain_manifests_v3(id), parameter_code TEXT NOT NULL,
    old_value_json TEXT CHECK(old_value_json IS NULL OR json_valid(old_value_json)),
    new_value_json TEXT NOT NULL CHECK(json_valid(new_value_json)), evidence_json TEXT NOT NULL CHECK(json_valid(evidence_json)),
    effective_from TEXT NOT NULL, created_at TEXT NOT NULL, decision_uid TEXT
) STRICT
)SQL",
         R"SQL(CREATE INDEX idx_mels_v4_due ON mels_v4(user_id,state,planned_end_at))SQL",
         R"SQL(CREATE INDEX idx_mel_tasks_v4_state ON mel_tasks_v4(mel_id,state))SQL",
         R"SQL(CREATE INDEX idx_progress_events_v4_mel_time ON progress_events_v4(mel_id,occurred_at))SQL",
         R"SQL(CREATE INDEX idx_state_events_v4_latest ON state_events_v4(user_id,definition_id,observed_at DESC))SQL",
         R"SQL(CREATE INDEX idx_retention_v4_due ON retention_schedules_v4(user_id,active,next_due_at))SQL",
     }},
    // v5 = 五库共享骨架、不可变知识版本、来源证据与类型专用结构。
    {5,
     {
         R"SQL(
CREATE TABLE knowledge_items_v5 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE,
    library_type TEXT NOT NULL CHECK(library_type IN ('paper','plan','state','method','tip')),
    title TEXT NOT NULL, domain_code TEXT NOT NULL,
    status TEXT NOT NULL CHECK(status IN ('candidate','active','warned','superseded','archived')),
    current_version_uid TEXT, owner_scope TEXT NOT NULL CHECK(owner_scope IN ('system','user')),
    created_by TEXT NOT NULL, never_delete INTEGER NOT NULL DEFAULT 0 CHECK(never_delete IN (0,1)),
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),
    CHECK(library_type<>'paper' OR never_delete=1)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE knowledge_versions_v5 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE,
    knowledge_item_id INTEGER NOT NULL REFERENCES knowledge_items_v5(id), version_no INTEGER NOT NULL CHECK(version_no>0),
    status TEXT NOT NULL CHECK(status IN ('draft','active','warned','superseded','archived')),
    summary TEXT NOT NULL, claims_json TEXT NOT NULL DEFAULT '[]' CHECK(json_valid(claims_json)),
    applicability_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(applicability_json)),
    limitations_json TEXT NOT NULL DEFAULT '[]' CHECK(json_valid(limitations_json)),
    warning_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(warning_json)), content_hash TEXT NOT NULL,
    language TEXT NOT NULL, extraction_model TEXT, extraction_prompt_version TEXT,
    valid_from TEXT NOT NULL, valid_to TEXT, created_at TEXT NOT NULL, created_by TEXT NOT NULL,
    UNIQUE(knowledge_item_id,version_no), UNIQUE(knowledge_item_id,content_hash)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE source_records_v5 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE,
    source_type TEXT NOT NULL CHECK(source_type IN ('file','url','doi','manual','conversation','generated')),
    canonical_uri TEXT, title TEXT NOT NULL, publisher TEXT, published_at TEXT, accessed_at TEXT NOT NULL,
    metadata_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(metadata_json)), content_hash TEXT NOT NULL,
    trust_tier TEXT NOT NULL,
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),
    UNIQUE(source_type,canonical_uri,content_hash)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE file_assets_v5 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, source_id INTEGER REFERENCES source_records_v5(id),
    relative_path TEXT NOT NULL UNIQUE, mime_type TEXT NOT NULL, byte_size INTEGER NOT NULL CHECK(byte_size>=0),
    sha256 TEXT NOT NULL UNIQUE, original_name TEXT NOT NULL,
    storage_state TEXT NOT NULL CHECK(storage_state IN ('quarantine','managed','missing','corrupt')),
    imported_at TEXT NOT NULL, created_at TEXT NOT NULL, updated_at TEXT NOT NULL,
    revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE evidence_fragments_v5 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, source_id INTEGER NOT NULL REFERENCES source_records_v5(id),
    asset_id INTEGER REFERENCES file_assets_v5(id), locator_json TEXT NOT NULL CHECK(json_valid(locator_json)),
    text TEXT NOT NULL, fragment_hash TEXT NOT NULL, created_at TEXT NOT NULL,
    UNIQUE(source_id,fragment_hash)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE evidence_links_v5 (
    id INTEGER PRIMARY KEY, knowledge_version_id INTEGER NOT NULL REFERENCES knowledge_versions_v5(id),
    fragment_id INTEGER NOT NULL REFERENCES evidence_fragments_v5(id),
    relation TEXT NOT NULL CHECK(relation IN ('supports','qualifies','contradicts','derived_from')),
    strength REAL NOT NULL CHECK(strength BETWEEN 0 AND 1), note TEXT,
    UNIQUE(knowledge_version_id,fragment_id,relation)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE knowledge_relations_v5 (
    id INTEGER PRIMARY KEY, from_item_id INTEGER NOT NULL REFERENCES knowledge_items_v5(id),
    to_item_id INTEGER NOT NULL REFERENCES knowledge_items_v5(id),
    relation TEXT NOT NULL CHECK(relation IN ('derived_from','supports','qualifies','contradicts','similar_to','supersedes','combined_with')),
    confidence REAL NOT NULL CHECK(confidence BETWEEN 0 AND 1), created_at TEXT NOT NULL, decision_uid TEXT,
    UNIQUE(from_item_id,to_item_id,relation), CHECK(from_item_id<>to_item_id)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE categories_v5 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, parent_id INTEGER REFERENCES categories_v5(id),
    code TEXT NOT NULL UNIQUE, name TEXT NOT NULL, sort_order INTEGER NOT NULL DEFAULT 0,
    CHECK(parent_id IS NULL OR parent_id<>id)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE tags_v5 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, code TEXT NOT NULL UNIQUE,
    name TEXT NOT NULL, tag_type TEXT NOT NULL
) STRICT
)SQL",
         R"SQL(
CREATE TABLE knowledge_categories_v5 (
    knowledge_item_id INTEGER NOT NULL REFERENCES knowledge_items_v5(id) ON DELETE CASCADE,
    category_id INTEGER NOT NULL REFERENCES categories_v5(id), is_primary INTEGER NOT NULL DEFAULT 0 CHECK(is_primary IN (0,1)),
    PRIMARY KEY(knowledge_item_id,category_id)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE knowledge_tags_v5 (
    knowledge_item_id INTEGER NOT NULL REFERENCES knowledge_items_v5(id) ON DELETE CASCADE,
    tag_id INTEGER NOT NULL REFERENCES tags_v5(id), source TEXT NOT NULL,
    confidence REAL NOT NULL CHECK(confidence BETWEEN 0 AND 1), PRIMARY KEY(knowledge_item_id,tag_id)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE papers_v5 (
    knowledge_item_id INTEGER PRIMARY KEY REFERENCES knowledge_items_v5(id), doi TEXT,
    study_type TEXT, quality_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(quality_json)),
    population_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(population_json)),
    publication_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(publication_json))
) STRICT
)SQL",
         R"SQL(
CREATE TABLE paper_authors_v5 (
    paper_id INTEGER NOT NULL REFERENCES papers_v5(knowledge_item_id) ON DELETE CASCADE,
    sequence_no INTEGER NOT NULL CHECK(sequence_no>=0), name TEXT NOT NULL, identifier TEXT,
    PRIMARY KEY(paper_id,sequence_no)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE paper_claims_v5 (
    id INTEGER PRIMARY KEY, paper_version_id INTEGER NOT NULL REFERENCES knowledge_versions_v5(id) ON DELETE CASCADE,
    claim_text TEXT NOT NULL, claim_type TEXT NOT NULL,
    applicability_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(applicability_json)),
    limitation_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(limitation_json)),
    importance REAL NOT NULL CHECK(importance BETWEEN 0 AND 1),
    embedding_required INTEGER NOT NULL DEFAULT 1 CHECK(embedding_required IN (0,1)),
    UNIQUE(paper_version_id,claim_text)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE plan_templates_v5 (
    knowledge_item_id INTEGER PRIMARY KEY REFERENCES knowledge_items_v5(id),
    target_profile_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(target_profile_json)),
    entry_conditions_json TEXT NOT NULL DEFAULT '[]' CHECK(json_valid(entry_conditions_json)),
    exit_conditions_json TEXT NOT NULL DEFAULT '[]' CHECK(json_valid(exit_conditions_json))
) STRICT
)SQL",
         R"SQL(
CREATE TABLE plan_stages_v5 (
    id INTEGER PRIMARY KEY, plan_version_id INTEGER NOT NULL REFERENCES knowledge_versions_v5(id) ON DELETE CASCADE,
    parent_id INTEGER REFERENCES plan_stages_v5(id), sequence_no INTEGER NOT NULL CHECK(sequence_no>=0),
    name TEXT NOT NULL, action_json TEXT NOT NULL CHECK(json_valid(action_json)),
    completion_json TEXT NOT NULL CHECK(json_valid(completion_json)), UNIQUE(plan_version_id,parent_id,sequence_no)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE methods_v5 (
    knowledge_item_id INTEGER PRIMARY KEY REFERENCES knowledge_items_v5(id), method_type TEXT NOT NULL,
    risk_level TEXT NOT NULL, evidence_grade TEXT NOT NULL,
    trial_recommended INTEGER NOT NULL DEFAULT 0 CHECK(trial_recommended IN (0,1)),
    measurement_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(measurement_json))
) STRICT
)SQL",
         R"SQL(
CREATE TABLE method_steps_v5 (
    id INTEGER PRIMARY KEY, method_version_id INTEGER NOT NULL REFERENCES knowledge_versions_v5(id) ON DELETE CASCADE,
    sequence_no INTEGER NOT NULL CHECK(sequence_no>=0), instruction TEXT NOT NULL,
    duration_min INTEGER CHECK(duration_min IS NULL OR duration_min>=0),
    config_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(config_json)), UNIQUE(method_version_id,sequence_no)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE method_applicability_v5 (
    id INTEGER PRIMARY KEY, method_version_id INTEGER NOT NULL REFERENCES knowledge_versions_v5(id) ON DELETE CASCADE,
    condition_type TEXT NOT NULL CHECK(condition_type IN ('applies','does_not_apply','caution')),
    condition_json TEXT NOT NULL CHECK(json_valid(condition_json)), explanation TEXT NOT NULL
) STRICT
)SQL",
         R"SQL(
CREATE TABLE tips_v5 (
    knowledge_item_id INTEGER PRIMARY KEY REFERENCES knowledge_items_v5(id), capture_type TEXT NOT NULL,
    original_uri TEXT, verification_status TEXT NOT NULL, risk_level TEXT NOT NULL,
    use_scenario_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(use_scenario_json))
) STRICT
)SQL",
         R"SQL(
CREATE TABLE tip_claims_v5 (
    id INTEGER PRIMARY KEY, tip_version_id INTEGER NOT NULL REFERENCES knowledge_versions_v5(id) ON DELETE CASCADE,
    claim_text TEXT NOT NULL, credibility REAL NOT NULL CHECK(credibility BETWEEN 0 AND 1),
    verification_note TEXT, source_refs_json TEXT NOT NULL DEFAULT '[]' CHECK(json_valid(source_refs_json))
) STRICT
)SQL",
         R"SQL(
CREATE TABLE mel_task_methods_v5 (
    id INTEGER PRIMARY KEY, mel_task_id INTEGER NOT NULL REFERENCES mel_tasks_v4(id) ON DELETE CASCADE,
    method_version_id INTEGER NOT NULL REFERENCES knowledge_versions_v5(id), rank INTEGER NOT NULL CHECK(rank>=0),
    reason TEXT NOT NULL, applicability_json TEXT NOT NULL CHECK(json_valid(applicability_json)),
    user_choice TEXT NOT NULL CHECK(user_choice IN ('pending','accepted','rejected','tried')),
    UNIQUE(mel_task_id,method_version_id)
) STRICT
)SQL",
         R"SQL(CREATE INDEX idx_knowledge_v5_library_status ON knowledge_items_v5(library_type,status,domain_code))SQL",
         R"SQL(CREATE INDEX idx_knowledge_versions_v5_item ON knowledge_versions_v5(knowledge_item_id,version_no DESC))SQL",
         R"SQL(CREATE INDEX idx_evidence_links_v5_fragment ON evidence_links_v5(fragment_id))SQL",
         R"SQL(CREATE INDEX idx_knowledge_relations_v5_to ON knowledge_relations_v5(to_item_id,relation))SQL",
     }},
    // v6 = 混合检索、内容摄入、AI作业、决策、提醒、审计与恢复。
    {6,
     {
         R"SQL(
CREATE TABLE embedding_records_v6 (
    id INTEGER PRIMARY KEY, owner_type TEXT NOT NULL, owner_uid TEXT NOT NULL, field_code TEXT NOT NULL,
    model_id TEXT NOT NULL, dimension INTEGER NOT NULL CHECK(dimension>0), vector_blob BLOB NOT NULL,
    content_hash TEXT NOT NULL, created_at TEXT NOT NULL,
    UNIQUE(owner_type,owner_uid,field_code,model_id,content_hash)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE retrieval_runs_v6 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, purpose TEXT NOT NULL, query_text TEXT NOT NULL,
    filters_json TEXT NOT NULL CHECK(json_valid(filters_json)), strategy_version TEXT NOT NULL,
    knowledge_snapshot_uid TEXT, started_at TEXT NOT NULL, completed_at TEXT,
    status TEXT NOT NULL CHECK(status IN ('running','completed','failed','cancelled'))
) STRICT
)SQL",
         R"SQL(
CREATE TABLE retrieval_hits_v6 (
    run_id INTEGER NOT NULL REFERENCES retrieval_runs_v6(id) ON DELETE CASCADE,
    owner_type TEXT NOT NULL, owner_uid TEXT NOT NULL, lexical_score REAL, vector_score REAL,
    final_score REAL NOT NULL, rank INTEGER NOT NULL CHECK(rank>0),
    reason_json TEXT NOT NULL CHECK(json_valid(reason_json)), PRIMARY KEY(run_id,owner_type,owner_uid)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE knowledge_snapshots_v6 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, created_at TEXT NOT NULL, purpose TEXT NOT NULL,
    manifest_versions_json TEXT NOT NULL CHECK(json_valid(manifest_versions_json)),
    knowledge_versions_json TEXT NOT NULL CHECK(json_valid(knowledge_versions_json)),
    state_snapshot_uid TEXT, retrieval_run_uid TEXT, content_hash TEXT NOT NULL UNIQUE
) STRICT
)SQL",
         R"SQL(
CREATE TABLE content_import_jobs_v6 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, source_type TEXT NOT NULL, source_uri TEXT,
    status TEXT NOT NULL CHECK(status IN ('queued','running','awaiting_confirmation','committed','failed_retryable','failed_terminal','cancelled')),
    stage TEXT NOT NULL, error_code TEXT, error_detail TEXT, idempotency_key TEXT NOT NULL UNIQUE,
    requested_by TEXT NOT NULL, started_at TEXT, completed_at TEXT,
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE content_import_artifacts_v6 (
    id INTEGER PRIMARY KEY, job_id INTEGER NOT NULL REFERENCES content_import_jobs_v6(id) ON DELETE CASCADE,
    stage TEXT NOT NULL, asset_uid TEXT, content_hash TEXT NOT NULL,
    metadata_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(metadata_json)), created_at TEXT NOT NULL,
    UNIQUE(job_id,stage,content_hash)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE ai_provider_configs_v6 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, provider_code TEXT NOT NULL, display_name TEXT NOT NULL,
    endpoint TEXT NOT NULL, model TEXT NOT NULL, credential_ref TEXT NOT NULL,
    capabilities_json TEXT NOT NULL CHECK(json_valid(capabilities_json)), enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1)),
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),
    UNIQUE(provider_code,model)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE ai_jobs_v6 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, job_type TEXT NOT NULL,
    status TEXT NOT NULL CHECK(status IN ('queued','running','completed','failed_retryable','failed_terminal','cancelled')),
    priority INTEGER NOT NULL DEFAULT 50, request_json TEXT NOT NULL CHECK(json_valid(request_json)),
    result_json TEXT CHECK(result_json IS NULL OR json_valid(result_json)), schema_version TEXT NOT NULL,
    idempotency_key TEXT NOT NULL UNIQUE, attempt_count INTEGER NOT NULL DEFAULT 0 CHECK(attempt_count>=0),
    max_attempts INTEGER NOT NULL CHECK(max_attempts>0), next_attempt_at TEXT,
    error_json TEXT CHECK(error_json IS NULL OR json_valid(error_json)),
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, started_at TEXT, completed_at TEXT,
    revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE ai_calls_v6 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, job_id INTEGER NOT NULL REFERENCES ai_jobs_v6(id),
    provider_config_id INTEGER NOT NULL REFERENCES ai_provider_configs_v6(id),
    request_hash TEXT NOT NULL, context_hash TEXT NOT NULL, response_hash TEXT,
    usage_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(usage_json)), latency_ms INTEGER CHECK(latency_ms IS NULL OR latency_ms>=0),
    status TEXT NOT NULL, created_at TEXT NOT NULL
) STRICT
)SQL",
         R"SQL(
CREATE TABLE ai_context_items_v6 (
    call_id INTEGER NOT NULL REFERENCES ai_calls_v6(id) ON DELETE CASCADE,
    context_type TEXT NOT NULL, context_uid TEXT NOT NULL, version_uid TEXT NOT NULL,
    rank INTEGER NOT NULL CHECK(rank>=0), reason TEXT NOT NULL,
    PRIMARY KEY(call_id,context_type,context_uid,version_uid)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE decision_records_v6 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, decision_type TEXT NOT NULL,
    aggregate_type TEXT NOT NULL, aggregate_uid TEXT NOT NULL,
    input_snapshot_json TEXT NOT NULL CHECK(json_valid(input_snapshot_json)),
    candidate_json TEXT NOT NULL CHECK(json_valid(candidate_json)),
    selected_json TEXT CHECK(selected_json IS NULL OR json_valid(selected_json)), rationale TEXT NOT NULL,
    source_mode TEXT NOT NULL CHECK(source_mode IN ('knowledge_grounded','partially_grounded','ungrounded')),
    warning_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(warning_json)),
    user_status TEXT NOT NULL CHECK(user_status IN ('not_required','pending','accepted','modified','rejected')),
    created_at TEXT NOT NULL, confirmed_at TEXT
) STRICT
)SQL",
         R"SQL(
CREATE TABLE calendar_events_v6 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, owner_type TEXT NOT NULL, owner_uid TEXT NOT NULL,
    starts_at TEXT NOT NULL, ends_at TEXT NOT NULL, timezone_id TEXT NOT NULL,
    status TEXT NOT NULL CHECK(status IN ('scheduled','completed','cancelled')),
    external_ref TEXT, created_at TEXT NOT NULL, updated_at TEXT NOT NULL,
    revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0), CHECK(ends_at>starts_at)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE reminder_rules_v6 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, event_id INTEGER REFERENCES calendar_events_v6(id),
    owner_type TEXT NOT NULL, owner_uid TEXT NOT NULL, offset_min INTEGER NOT NULL,
    channel TEXT NOT NULL, enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN (0,1)),
    quiet_hours_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(quiet_hours_json)),
    snooze_policy_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(snooze_policy_json)),
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE reminder_deliveries_v6 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, rule_id INTEGER NOT NULL REFERENCES reminder_rules_v6(id),
    scheduled_at TEXT NOT NULL, delivered_at TEXT,
    status TEXT NOT NULL CHECK(status IN ('pending','delivered','failed','suppressed','cancelled')),
    error TEXT, idempotency_key TEXT NOT NULL UNIQUE
) STRICT
)SQL",
         R"SQL(
CREATE TABLE achievements_v6 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, user_id INTEGER NOT NULL REFERENCES user_profiles_v3(id),
    achievement_type TEXT NOT NULL, title TEXT NOT NULL, description TEXT NOT NULL, earned_at TEXT NOT NULL,
    source_type TEXT NOT NULL, source_uid TEXT NOT NULL,
    evidence_json TEXT NOT NULL CHECK(json_valid(evidence_json)),
    created_at TEXT NOT NULL, updated_at TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1 CHECK(revision>0),
    UNIQUE(user_id,achievement_type,source_type,source_uid)
) STRICT
)SQL",
         R"SQL(
CREATE TABLE audit_events_v6 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, actor_type TEXT NOT NULL, actor_ref TEXT,
    action TEXT NOT NULL, aggregate_type TEXT NOT NULL, aggregate_uid TEXT NOT NULL,
    before_hash TEXT, after_hash TEXT, detail_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(detail_json)),
    occurred_at TEXT NOT NULL, correlation_uid TEXT
) STRICT
)SQL",
         R"SQL(
CREATE TABLE outbox_events_v6 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, event_type TEXT NOT NULL,
    aggregate_type TEXT NOT NULL, aggregate_uid TEXT NOT NULL,
    payload_json TEXT NOT NULL CHECK(json_valid(payload_json)), occurred_at TEXT NOT NULL,
    published_at TEXT, attempts INTEGER NOT NULL DEFAULT 0 CHECK(attempts>=0), last_error TEXT
) STRICT
)SQL",
         R"SQL(
CREATE TABLE backup_records_v6 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, started_at TEXT NOT NULL, completed_at TEXT,
    status TEXT NOT NULL CHECK(status IN ('running','verified','failed')),
    relative_path TEXT NOT NULL, sha256 TEXT, db_schema_version INTEGER NOT NULL,
    asset_manifest_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(asset_manifest_json)),
    error_json TEXT CHECK(error_json IS NULL OR json_valid(error_json))
) STRICT
)SQL",
         R"SQL(
CREATE TABLE legacy_migration_runs_v6 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE, source_schema INTEGER NOT NULL, target_schema INTEGER NOT NULL,
    status TEXT NOT NULL CHECK(status IN ('queued','running','completed','failed','cancelled')),
    cursor_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(cursor_json)),
    counts_json TEXT NOT NULL DEFAULT '{}' CHECK(json_valid(counts_json)),
    started_at TEXT, completed_at TEXT, error_json TEXT CHECK(error_json IS NULL OR json_valid(error_json))
) STRICT
)SQL",
         R"SQL(
CREATE TABLE legacy_id_maps_v6 (
    source_table TEXT NOT NULL, source_id INTEGER NOT NULL, target_table TEXT NOT NULL,
    target_uid TEXT NOT NULL, run_id INTEGER NOT NULL REFERENCES legacy_migration_runs_v6(id),
    PRIMARY KEY(source_table,source_id,target_table)
) STRICT
)SQL",
         R"SQL(
CREATE VIRTUAL TABLE knowledge_fts_v6 USING fts5(
    owner_type UNINDEXED, owner_uid UNINDEXED, title, summary, claims, applicability, limitations, steps,
    tokenize='unicode61 remove_diacritics 2'
)
)SQL",
         R"SQL(CREATE INDEX idx_embeddings_v6_owner ON embedding_records_v6(owner_type,owner_uid,model_id))SQL",
         R"SQL(CREATE INDEX idx_ai_jobs_v6_ready ON ai_jobs_v6(status,next_attempt_at,priority))SQL",
         R"SQL(CREATE INDEX idx_outbox_v6_pending ON outbox_events_v6(published_at,id))SQL",
         R"SQL(CREATE INDEX idx_reminders_v6_due ON reminder_deliveries_v6(status,scheduled_at))SQL",
         R"SQL(CREATE INDEX idx_audit_v6_aggregate ON audit_events_v6(aggregate_type,aggregate_uid,occurred_at))SQL",
     }},
    {7,
     {
         R"SQL(
CREATE TABLE ai_connection_tests_v7 (
    id INTEGER PRIMARY KEY, uid TEXT NOT NULL UNIQUE,
    provider_config_id INTEGER NOT NULL REFERENCES ai_provider_configs_v6(id),
    tested_at TEXT NOT NULL, provider_model TEXT NOT NULL,
    auth_ok INTEGER NOT NULL CHECK(auth_ok IN (0,1)),
    structured_ok INTEGER NOT NULL CHECK(structured_ok IN (0,1)),
    embedding_required INTEGER NOT NULL CHECK(embedding_required IN (0,1)),
    embedding_ok INTEGER NOT NULL CHECK(embedding_ok IN (0,1)),
    overall_ok INTEGER NOT NULL CHECK(overall_ok IN (0,1)),
    capabilities_json TEXT NOT NULL CHECK(json_valid(capabilities_json)),
    error_json TEXT NOT NULL CHECK(json_valid(error_json))
) STRICT
)SQL",
         R"SQL(CREATE INDEX idx_ai_connection_tests_v7_config ON ai_connection_tests_v7(provider_config_id,tested_at DESC,id DESC))SQL",
     }},
    {8,
     {
         R"SQL(ALTER TABLE ai_provider_configs_v6 ADD COLUMN is_default INTEGER NOT NULL DEFAULT 0)SQL",
         R"SQL(CREATE UNIQUE INDEX idx_ai_provider_configs_v6_default
              ON ai_provider_configs_v6(is_default) WHERE is_default=1)SQL",
     }},
};

} // namespace Migrations
