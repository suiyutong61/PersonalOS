# 1. 实现（Implementation）

 明确的设计开始被真正实现，形成可运行的软件。

---

## 1.0 实现阶段总则

### 1.0.1 实现目标

第一阶段只实现 MVP 最小闭环（见 1.7.19）：

```text
Goal
 ↓
State
 ↓
Plan
 ↓
Task
 ↓
Execution
 ↓
Event
 ↓
Review
```

验证标准见 1.7.21：闭环能稳定运行，再谈扩展。

### 1.0.2 开发顺序

按照 1.7.22 确定的 13 步顺序执行：

```text
Step 1  建立 C++/Qt 项目骨架
Step 2  配置 CMake
Step 3  建立 SQLite 数据库
Step 4  实现核心数据模型
Step 5  实现 Core & Goal
Step 6  实现 State
Step 7  实现 Planning
Step 8  实现 Execution
Step 9  实现 Feedback
Step 10 实现 Review
Step 11 接入 AI Engine
Step 12 实现 Evolution 基础能力
Step 13 完成 MVP 测试
```

### 1.0.3 文档约定

本章作为实现阶段的开发日志：

- 每完成一个步骤，在本章记录：完成内容、关键代码逻辑、验证结果。
- 设计评审中确认的实质缺口在本章相应步骤中落地：
  - Proposal / ChangeRequest 实体补入数据模型（Step 4）
  - 循环触发时机（应用启动时补录未执行的日终复盘）在 Review 实现时明确
  - 手机使用时间等外部数据初期按手动指标处理
- 第 1、2 章（需求与设计）为冻结内容，实现阶段不修改。

### 1.0.4 开发环境（2026-09-20 实测）

```text
操作系统   Windows 11
Qt        Qt 6.11.2 MinGW 64-bit（<Qt 安装目录>\6.11.2\mingw_64）
编译器     MinGW 13.10（<Qt 安装目录>\Tools\mingw1310_64）
构建系统   CMake 4.4.3 + MinGW Makefiles
IDE       Qt Creator 20.0.1
版本控制   Git 2.55.0
```

> **环境问题记录**：本机 PATH 中存在另一套 MinGW（`D:\mingw64`，GCC 15.2.0），
> 命令行构建时若不加指定会混用编译器，导致链接失败（`undefined reference to __imp___argc`）。
> 解决方案：CMake 配置时显式指定 Qt kit 自带的编译器：
>
> ```text
> -DCMAKE_C_COMPILER=<Qt 安装目录>/Tools/mingw1310_64/bin/gcc.exe
> -DCMAKE_CXX_COMPILER=<Qt 安装目录>/Tools/mingw1310_64/bin/g++.exe
> -DCMAKE_MAKE_PROGRAM=<Qt 安装目录>/Tools/mingw1310_64/bin/mingw32-make.exe
> ```
>
> Qt Creator 内部已固定该工具链，不受影响。

---

## 1.1 Step 1 — 建立 C++/Qt 项目骨架

### 1.1.1 完成内容

1. 按 1.7.11 建立项目目录结构（`src/`、`qml/`、`resources/`、`tests/`、`docs/`）。
2. 迁移模板文件：`main.cpp` → `src/application/`，`Main.qml` → `qml/`。
3. 移除 Qt Creator 模板残留（`importedcontent/`）。
4. 重写 `CMakeLists.txt`：启用 C++20、更新源文件路径、添加 QML 模块版本号。
5. 创建 `.gitignore`（构建产物、IDE 用户配置、QML 缓存）。
6. 命令行验证：配置 + 编译通过（`appPersonOS.exe` 生成成功）。

### 1.1.2 目录结构

```text
PersonOS（仓库根目录）
│
├── CMakeLists.txt
├── .gitignore
│
├── src
│   ├── core
│   ├── goals
│   ├── state
│   ├── planning
│   ├── execution
│   ├── feedback
│   ├── review
│   ├── evolution
│   ├── ai
│   ├── database
│   ├── services
│   ├── models
│   └── application
│       └── main.cpp
│
├── qml
│   ├── Main.qml
│   ├── pages
│   ├── components
│   ├── layouts
│   └── dialogs
│
├── resources
├── tests
└── docs
```

空目录以 `.gitkeep` 占位，待后续步骤填充。

### 1.1.3 关键代码逻辑

#### CMakeLists.txt

- `set(CMAKE_CXX_STANDARD 20)`：README 2.7.4 要求 C++20。原模板只设置了
  `CMAKE_CXX_STANDARD_REQUIRED` 而未指定标准，实际会使用编译器默认值——这是
  设计评审中发现的小瑕疵，已修复。
- `qt_add_qml_module(appPersonOS URI PersonOS ...)`：QML 模块名为 `PersonOS`，
  `main.cpp` 通过 `engine.loadFromModule("PersonOS", "Main")` 加载入口页面。
  后续所有页面（`qml/pages/`）、组件（`qml/components/`）都注册在该模块下。
- 移除了 `importedcontent/` 子目录：模板中的 Figma 导入占位，从未被使用。

#### main.cpp

- 保持模板结构：`QGuiApplication` + `QQmlApplicationEngine`。
- 后续 Step 4+ 将在其中把 C++ 领域对象与列表 Model 注册到 QML 上下文。

#### .gitignore

- 忽略 `build*/`、`.qtcreator/`、`CMakeLists.txt.user*`、QML 编译缓存等本机生成文件。

### 1.1.4 验证结果

```text
cmake 配置成功 → 编译 100% → appPersonOS.exe 生成
```

### 1.1.5 收尾记录

- Git 基线已提交：`main` 分支，提交 `9ba30d9`「Step 1: 建立 C++/Qt 项目骨架（MVP 基线）」。
- 期间环境问题（均已解决并记录于 1.0.4）：
  - 本机存在两套 MinGW，命令行构建需显式指定 Qt kit 工具链；
  - Git 用户身份已配置。

---

## 1.2 Step 2 — 配置 CMake（已完成）

CMake 配置已在 Step 1 中一并完成并通过编译验证：

- `CMAKE_CXX_STANDARD 20`（README 2.7.4 要求）
- Qt 组件：`Qt6::Quick`；生成器 MinGW Makefiles（Qt Creator kit）
- 后续扩展点：Step 3 增加 `Qt6::Sql`（SQLite），Step 13 增加 `Qt6::Test`（单元测试）
- 命令行构建固定使用 1.0.4 记录的三组 `-DCMAKE_*` 参数锁定工具链

---

## 1.3 实现设计 — 数据结构与函数框架（Step 3~10 施工图）

本节把 1.3 概念数据模型与 1.4 模块职责落地为：数据库 schema、C++ 领域类、
仓库（Repository）接口、子系统服务接口（函数框架）与 QML 页面清单。
Step 3~10 的编码均以本节为准，设计变更须同步更新本节。

### 1.3.1 分层与依赖规则

```text
QML (UI)
   ↓
services/ApplicationService —— QML 唯一入口门面
   ↓
7 个子系统服务（core & goal / state / planning / execution / feedback / review / evolution）
   ↓
database/ 仓库层（Repository）—— 唯一允许访问 SQLite 的层
   ↓
SQLite
```

依赖规则：

- `src/models/`：纯数据类，只依赖 QtCore，不依赖任何上层
- `src/database/`：DatabaseManager（连接、迁移、schema 版本）+ 各 Repository，依赖 models + QtSql
- 子系统服务：依赖 models + 仓库；**子系统之间不直接互相调用**，跨子系统协作由 ApplicationService 编排
- 禁止 UI 直接操作数据库（1.7.8）
- 高层对象（Goal 等）的修改必须在服务层经过权限 / Proposal 检查（1.6），仓库层只提供读写原语

### 1.3.2 数据库 schema（MVP，schema v1）

MVP 建表范围 = 最小闭环 + 变更机制。以下 1.3 实体在 MVP 阶段**不建表**
（schema 版本化，Step 12 起按需扩展）：Project、Habit、Observation、Metric、
Decision、Experiment、Strategy、Policy、Resource、Constraint——理由见 1.7.20。

```sql
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;

-- 应用元数据（schema_version、Personal OS 版本等）
CREATE TABLE app_meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

-- 核心价值（修改频率最低，见 1.6.4）
CREATE TABLE core_values (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    description TEXT,
    sort_order  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);

-- 原则（修改须用户批准，见 1.6.5）
CREATE TABLE principles (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    text        TEXT NOT NULL,
    sort_order  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);

-- 目标（层级：vision/long_term/annual/quarterly/monthly/weekly，见 1.2.4）
CREATE TABLE goals (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    parent_id   INTEGER REFERENCES goals(id),
    level       TEXT NOT NULL CHECK (level IN ('vision','long_term','annual','quarterly','monthly','weekly')),
    title       TEXT NOT NULL,
    description TEXT,
    target_date TEXT,                                      -- YYYY-MM-DD，可空
    status      TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active','paused','achieved','abandoned')),
    priority    INTEGER NOT NULL DEFAULT 50,               -- 0~100，越大越优先（1.2.5）
    created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);
CREATE INDEX idx_goals_parent ON goals(parent_id);

-- 状态快照（每日一条，见 1.3.2-5）
CREATE TABLE state_snapshots (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    date        TEXT NOT NULL UNIQUE,                      -- YYYY-MM-DD
    sleep_hours REAL,
    energy      INTEGER CHECK (energy BETWEEN 1 AND 5),
    focus       INTEGER CHECK (focus BETWEEN 1 AND 5),
    mood        INTEGER CHECK (mood BETWEEN 1 AND 5),
    note        TEXT,
    created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    updated_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);

-- 计划（MVP 以日计划为主）
CREATE TABLE plans (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    period_type  TEXT NOT NULL CHECK (period_type IN ('daily','weekly','monthly')),
    period_start TEXT NOT NULL,                            -- YYYY-MM-DD
    period_end   TEXT,
    status       TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active','closed')),
    note         TEXT,
    created_at   TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    UNIQUE (period_type, period_start)
);

-- 任务（状态集见 1.4.5.4）
CREATE TABLE tasks (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    plan_id         INTEGER REFERENCES plans(id),
    goal_id         INTEGER REFERENCES goals(id),          -- 任务→目标关联（FR-010）
    title           TEXT NOT NULL,
    description     TEXT,
    planned_minutes INTEGER,
    due_date        TEXT NOT NULL,                         -- 所属日期 YYYY-MM-DD
    status          TEXT NOT NULL DEFAULT 'planned'
                    CHECK (status IN ('planned','started','completed','partial',
                                      'delayed','skipped','cancelled','interrupted')),
    actual_minutes  INTEGER,
    sort_order      INTEGER NOT NULL DEFAULT 0,
    created_at      TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    updated_at      TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    completed_at    TEXT
);
CREATE INDEX idx_tasks_due  ON tasks(due_date, status);
CREATE INDEX idx_tasks_plan ON tasks(plan_id);

-- 事件（append-only：仓库层只提供 INSERT/SELECT，修正走留痕流程，见 1.4.14）
CREATE TABLE events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    date        TEXT NOT NULL,                             -- YYYY-MM-DD
    occurred_at TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    type        TEXT NOT NULL,                             -- task_completed/task_skipped/interruption/review_done/custom...
    title       TEXT NOT NULL,
    description TEXT,
    task_id     INTEGER REFERENCES tasks(id),
    created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);
CREATE INDEX idx_events_date ON events(date);

-- 复盘（MVP：日复盘为主，FR-019）
CREATE TABLE reviews (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    review_type  TEXT NOT NULL CHECK (review_type IN ('daily','weekly','monthly')),
    period_start TEXT NOT NULL,                            -- YYYY-MM-DD
    period_end   TEXT,
    summary      TEXT NOT NULL,                            -- 计划 vs 实际 + 结论
    problems     TEXT,                                     -- 发现的问题
    causes       TEXT,                                     -- 原因分析
    next_actions TEXT,                                     -- 下一步调整
    created_at   TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    UNIQUE (review_type, period_start)
);

-- Proposal / ChangeRequest —— 设计评审补入的第 22 个实体（1.6.12/1.6.18）
CREATE TABLE proposals (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    target_type     TEXT NOT NULL,                         -- goal/strategy/plan...
    target_id       INTEGER,
    current_value   TEXT,
    proposed_value  TEXT NOT NULL,
    reason          TEXT NOT NULL,                         -- 为什么改（NFR-04 可解释）
    evidence        TEXT,                                  -- 依据数据（NFR-05 可追溯）
    expected_effect TEXT,
    risk            TEXT,
    status          TEXT NOT NULL DEFAULT 'draft'
                    CHECK (status IN ('draft','review','approved','rejected','applied')),
    created_at      TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    decided_at      TEXT
);

-- 变更记录（append-only，1.4.14 原则2）
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
);

-- 版本（Personal OS 自身版本，1.6.14）
CREATE TABLE versions (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    version_number TEXT NOT NULL UNIQUE,                   -- v0.1 / v1.0 ...
    parent_version TEXT,
    description    TEXT,
    status         TEXT NOT NULL DEFAULT 'stable' CHECK (status IN ('stable','experiment','archive')),
    created_at     TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);

-- 注意：schema_version 由迁移器在迁移成功后写入 app_meta，DDL 中不写死
```

写入纪律（对应 1.4.14 三原则）：

- `events`、`change_logs`：append-only，仓库层不提供无痕 UPDATE/DELETE；
  修正流程 = 写 change_logs（原始 → 修正 → 原因）后再更新，或追加修正事件
- `tasks` 等操作型数据：允许 UPDATE，但状态流转统一由 ExecutionService 控制
- 禁止为"提高完成率"回改历史计划（1.4.14 原则3）

### 1.3.3 领域类清单（src/models/）

每个概念实体一个纯数据类（纯数据成员 + 少量辅助方法，公开成员风格），共 11 个：

```text
CoreValue     corevalues
Principle     principles
Goal          goals          （含 level、priority、status）
StateSnapshot state_snapshots
Plan          plans
Task          tasks          （含全部 8 种状态）
Event         events
Review        reviews
Proposal      proposals
ChangeLog     change_logs
Version       versions
```

### 1.3.4 仓库接口（src/database/，函数框架）

```cpp
class GoalRepository {
public:
    std::optional<Goal> getById(int64_t id);
    std::vector<Goal> getAll();                      // 供目标树
    std::vector<Goal> getChildren(int64_t parentId);
    std::vector<Goal> getByLevel(const QString& level);
    int64_t create(const Goal& g);                   // 返回新 id
    bool update(const Goal& g);                      // 高层对象：调用前须经服务层权限检查
};

class StateRepository {
    std::optional<StateSnapshot> getByDate(const QString& date); // YYYY-MM-DD
    bool upsert(const StateSnapshot& s);             // 每天一条
};

class PlanRepository {
    std::optional<Plan> getDaily(const QString& date);
    int64_t ensureDaily(const QString& date);        // 不存在则创建，返回 id
};

class TaskRepository {
    std::vector<Task> getByDate(const QString& date);
    std::vector<Task> getByPlan(int64_t planId);
    int64_t create(const Task& t);
    bool update(const Task& t);
};

class EventRepository {
    int64_t append(const Event& e);                  // 唯一写入方式
    std::vector<Event> getByDate(const QString& date);
    std::vector<Event> getRange(const QString& from, const QString& to);
    bool correct(int64_t id, const Event& fix, const QString& reason);
    // ↑ 修正留痕：先写 change_logs（原始→修正+原因），再更新（1.4.14 原则1）
};

class ReviewRepository {
    std::optional<Review> getDaily(const QString& date);
    bool upsert(const Review& r);
};

class ProposalRepository {
    int64_t create(const Proposal& p);
    bool setStatus(int64_t id, const QString& status, const QDateTime& decidedAt);
    std::vector<Proposal> getByStatus(const QString& status);
};

class ChangeLogRepository { int64_t append(const ChangeLog& c); };   // 只追加
class VersionRepository   { int64_t create(const Version& v); std::optional<Version> getCurrent(); };
class CoreValueRepository { std::vector<CoreValue> getAll(); bool update(const CoreValue& v); };
class PrincipleRepository { std::vector<Principle> getAll(); bool update(const Principle& p); };
```

> 注：日期参数一律 `QString`（YYYY-MM-DD），与数据库 TEXT 列一致；本清单为施工图，实际签名以各步骤实现为准（服务层方法带 `QString *error` 出参，见 1.8）。

### 1.3.5 子系统服务接口（src/{goals,state,planning,...}/，函数框架）

```cpp
// Core & Goal（Step 5）
class CoreAndGoalService {
    QList<Goal> goalHierarchy() const;
    int  createGoal(const Goal& g);
    bool updateGoal(const Goal& g);      // 阶段目标可直接改；长期目标先经 Proposal（1.6.8）
};

// State（Step 6）
class StateService {
    std::optional<StateSnapshot> today() const;
    bool record(StateSnapshot s);
};

// Planning（Step 7）
class PlanningService {
    Plan ensureDailyPlan(const QDate& d);   // 应用启动 / 跨日时调用（1.0.3 触发时机）
    void addTaskToPlan(const Task& t);
    void reschedule(const QDate& d);        // 日间动态调整（1.5.5）：只改 Execution 层
};

// Execution（Step 8）
class ExecutionService {
    bool startTask(int64_t taskId);
    bool completeTask(int64_t taskId, int actualMinutes);
    bool skipTask(int64_t taskId, const QString& reason);
    // 每次状态流转同时向 EventRepository 追加事件（1.5.2）
};

// Feedback（Step 9）
class FeedbackService {
    int64_t recordEvent(const Event& e);
    QList<Event> eventsOfDay(const QDate& d);
};

// Review（Step 10）
class ReviewService {
    Review draftDailyReview(const QDate& d);
    // ↑ 读取当日 tasks + events + state，计算 planned vs actual，生成草案（1.5.6）
    bool saveDailyReview(const Review& r);
    void onAppStart();  // 启动检查：昨天未复盘则提示补录（1.0.3 循环触发时机）
};

// Evolution（Step 12，MVP 基础能力）
class EvolutionService {
    int64_t propose(const Proposal& p);              // AI/系统 发起变更请求
    bool decide(int64_t proposalId, bool approved);  // 用户批准/拒绝（1.6.19）
    bool applyApproved(int64_t proposalId);          // 生效：写 ChangeLog + 新 Version
};
```

### 1.3.6 QML 页面清单（MVP）

```text
qml/Main.qml                    导航壳（侧边栏：今日 / 目标 / 状态 / 复盘）
qml/pages/TodayPage.qml         今日任务列表：开始 / 完成(实际分钟) / 跳过(原因)
qml/pages/GoalsPage.qml         目标层级树（vision→weekly），含优先级
qml/pages/StatePage.qml         今日状态记录（睡眠 / 精力 / 专注 / 心情 1~5）
qml/pages/ReviewPage.qml        日终复盘：计划vs实际 + 总结 + 问题 + 下一步
qml/components/TaskItem.qml     任务条目组件
qml/dialogs/TaskEditDialog.qml  新建 / 编辑任务
```

C++ 对象经 `ApplicationService`（Q_PROPERTY + Q_INVOKABLE）暴露给 QML；
列表统一用 QAbstractListModel 子类（TaskListModel、EventListModel 等）。

### 1.3.7 一次典型数据流：日终复盘（验证 1.5.6 落地）

```text
用户打开"复盘"页（或应用启动时 onAppStart 检测到昨天未复盘 → 提示补录）
  ↓
ApplicationService::runDailyReview(date)
  ↓
ReviewService 读取 tasks(due_date) + events(date) + state_snapshot(date)
  ↓
计算 planned vs actual 偏差，生成 Review 草案（summary/problems/causes 预填）
  ↓
UI 呈现草案 → 用户编辑确认 → saveDailyReview
  ↓
若用户/系统发现问题 → EvolutionService::propose() 发起 Proposal（待批准）
  ↓
次日 PlanningService::ensureDailyPlan 参考昨日 next_actions 生成计划
```



---

## 1.4 Step 3 — 建立 SQLite 数据库层（已完成）

### 1.4.1 完成内容

1. 新建 `src/database/Migrations.h`：迁移注册表，内含 schema v1 全量 DDL（17 条语句）
2. 新建 `src/database/DatabaseManager.h/.cpp`：单例，负责打开/创建数据库、连接级 PRAGMA、schema 迁移
3. `main.cpp`：启动时设置应用标识（决定数据目录）并打开数据库；新增开发者工具参数 `--db-check`
4. CMake：`find_package` 增加 `Sql` 组件、链接 `Qt6::Sql`、`target_include_directories(src)`
5. 验证通过：建库、迁移、12 张表就位、重复运行幂等

### 1.4.2 关键代码逻辑

#### 数据库位置

- `QStandardPaths::AppLocalDataLocation` → `%LOCALAPPDATA%/PersonalOS/PersonalOS/personos.db`
- **踩坑记录**：Qt 的 `AppDataLocation` 在 Windows 指向 **Roaming** 而非 Local；
  本地大文件数据库用 `AppLocalDataLocation` 更合适（已修复并验证）

#### 迁移机制（Migrations.h + DatabaseManager::applyMigrations）

- 迁移注册表**只允许追加**，不得修改已发布版本的 SQL（历史数据不可篡改原则）
- 每个 Step 包在一个事务里，失败回滚；成功后写入 `app_meta.schema_version`
- 版本号由迁移器写入，DDL 中不写死

#### 连接级 PRAGMA

- `foreign_keys = ON`（外键约束生效）、`journal_mode = WAL`、`synchronous = NORMAL`

#### --db-check 开发者工具

- 无界面验证：路径、schema 版本、表清单写入当前目录 `dbcheck.log`，退出码 0/1
- **踩坑记录**：`WIN32_EXECUTABLE`（GUI 子系统）程序没有控制台，`qInfo`/`qCritical`
  输出在终端（含重定向）不可见，因此采用报告文件方案

### 1.4.3 验证结果

```text
数据库路径: C:/Users/<用户名>/AppData/Local/PersonalOS/PersonalOS/personos.db
schema 版本: 1
数据表: app_meta / change_logs / core_values / events / goals / plans /
        principles / proposals / reviews / state_snapshots / tasks / versions
RESULT: OK（重复运行幂等）
```

---

## 1.5 Step 4 — 实现核心数据模型（已完成）

### 1.5.1 完成内容

在 `src/models/` 下实现 11 个纯数据类（头文件定义，无 .cpp）：

```text
CoreValue.h     Principle.h     Goal.h           StateSnapshot.h
Plan.h          Task.h          Event.h          Review.h
Proposal.h      ChangeLog.h     Version.h
```

验证：全部头文件通过 g++ `-fsyntax-only` 编译检查。

### 1.5.2 关键代码逻辑

- **风格**：纯数据结构（公开成员 + 默认初始化），位于 `namespace PersonOS`，
  只依赖 QtCore（符合 1.3.1 依赖规则，仓库层/服务层在此基础上构建）
- **可选值语义**：`std::optional<double/int>` 表达"未填写"（如 `sleepHours`、
  `plannedMinutes`），与 SQLite 的 NULL 一一对应
- **默认值**：`status` 默认 `active/planned/draft/stable`，与 schema 的 DEFAULT 一致
- **少量辅助函数**：`Task::isDone()`（completed/cancelled/skipped）、
  `Goal::isRoot()`（parentId==0）、`Goal::isValid()`（title 非空）
- **日期类型**：一律 `QString`（YYYY-MM-DD），与数据库 TEXT 列直接对应，
  避免时区/精度问题；QDate 换算只在服务层需要时进行
- 每个类的头注释标注了对应的 README 设计出处与建表纪律（如 Event append-only、
  Goal 不得因计划失败而修改）

---

## 1.6 Step 5 — 实现 Core & Goal 子系统（已完成）

### 1.6.1 完成内容

1. 仓库层：`GoalRepository` / `CoreValueRepository` / `PrincipleRepository` + 公共工具 `RepoUtil.h`
2. 服务层：`CoreAndGoalService`（目标层级管理 + 核心价值/原则管理 + 修改校验）
3. CMake 重构为三目标：`personos_core`（静态库）+ `appPersonOS`（GUI）+ `smoke_goals`（控制台冒烟测试）
4. 冒烟测试 `tests/goals_smoke.cpp`：22 项检查全部通过

### 1.6.2 关键代码逻辑

#### CMake 目标结构（Step 5 起）

```text
personos_core（静态库：models + database + 各子系统服务，PUBLIC Qt6::Core/Qt6::Sql）
   ├── appPersonOS（GUI，链接 Quick + personos_core）
   └── smoke_goals（控制台，链接 Qt6::Core + personos_core）
```

后续各步骤新增的仓库/服务都进入 `personos_core`，两个可执行目标自动获得。

#### RepoUtil：外键"0 → NULL"映射

`foreign_keys=ON` 时，绑定 0 号外键会违反约束。所有关联 id 写入时
经 `RepoUtil::nullableId()` 把 0 转成 NULL（读取时 NULL 自然读回 0）。

#### CoreAndGoalService 校验（1.2.4 目标链）

- 层级合法性：vision / long_term / annual / quarterly / monthly / weekly
- 标题非空；父目标必须存在
- **环检测**：从新父目标沿父链上溯，遇到目标自身即拒绝（目标树禁止成环）
- 修改权限（1.6.8）：本服务接受"用户主导"的直接修改；
  AI 发起的修改在 Step 12 走 Proposal → 批准 → ChangeLog

#### 测试隔离

`PERSONOS_DB_PATH` 环境变量覆盖数据库路径（DatabaseManager），
冒烟测试使用 `%TEMP%/personos_smoke_goals.db`，不污染真实数据。

#### 踩坑记录：Qt 6 无控制台环境下的日志

Qt 6 在 Windows 上检测到进程**没有控制台**时，日志默认写入
`OutputDebugString`（终端不可见），而非 stderr。命令行验证时表现为
"程序退出码正确但毫无输出"。三种对策：

1. 环境变量 `QT_FORCE_STDERR_LOGGING=1`（官方开关）
2. 测试程序内 `qInstallMessageHandler` 安装自定义 handler 直写 stderr（已采用）
3. `--db-check` 类工具直接写报告文件（Step 3 已采用）

### 1.6.3 验证结果

```text
===== SMOKE PASS =====（22/22）
目标链创建（5 级）· 层级树读取 · 非法层级/空标题/父不存在拒绝
环检测 · 自身为父拒绝 · 更新标题/优先级 · 核心价值重名拒绝 · 原则增改
```

---

## 1.7 Step 6 — 实现 State 子系统（已完成）

### 1.7.1 完成内容

1. 仓库层：`StateRepository`（getByDate + upsert，每日一条）
2. 服务层：`StateService`（today() / getByDate() / record()，含范围校验）
3. 冒烟测试 `tests/state_smoke.cpp`：11 项检查全部通过
4. 测试公共工具提取：`tests/TestLogging.h`（自定义消息处理器，goals_smoke 同步复用）

### 1.7.2 关键代码逻辑

- **upsert 语义**：`INSERT ... ON CONFLICT(date) DO UPDATE`——同一天重复记录
  只更新不新增（对应 FR-004 补充/修正当天记录）
- **可选值纪律**：显式清空某字段 = 绑定 NULL（`RepoUtil::nullable*`），
  未填与填 0 严格区分；读取时 NULL → `std::optional` 空值
- **范围校验在服务层**：睡眠 0~24 小时、精力/专注/心情 1~5
  （数据库 CHECK 是最后防线，服务层负责给出友好错误）
- **State 只描述事实**：本子系统不做任何评价/打分解释（1.3.5 原则3），
  评价属于 Review（Step 10）
- `state_recorded` 事件在 Step 9 Feedback 落地后由应用层编排补录

### 1.7.3 验证结果

```text
===== SMOKE PASS =====（11/11）
完整快照读写 · 同日期 upsert 不新增 · 精力=6/睡眠=25/空日期拒绝
仅填 mood 时其余字段为空 · 多日期互不影响
（goals_smoke 重构复用 TestLogging 后 22/22 依然全绿）
```

---

## 1.8 Step 7 — 实现 Planning 子系统（已完成）

### 1.8.1 完成内容

1. 仓库层：`PlanRepository`（getById / getDaily / ensureDaily / setStatus）
2. 服务层：`PlanningService`（dailyPlan / ensureDailyPlan / closeDailyPlan，日期校验）
3. 冒烟测试 `tests/planning_smoke.cpp`：11 项检查全部通过
4. **服务层错误契约修复**：所有带 `QString *error` 出参的服务方法，进入时先清空
   error（error 只描述"本次调用"的结果，不残留上次调用的错误）

### 1.8.2 关键代码逻辑

- **ensureDailyPlan 幂等**：快路径查询 → `INSERT ... ON CONFLICT DO NOTHING` →
  再查询返回；`UNIQUE(period_type, period_start)` 兜底并发，应用启动/跨日可安全重复调用
- **计划生命周期**：ensureDailyPlan（active）→ 日终复盘后 closeDailyPlan（closed）；
  closed 计划再次 ensure 不新建（保留历史）
- **计划 ≠ 目标**：本子系统不提供任何修改 Goal 的入口（1.2.6）
- `addTaskToPlan` / `reschedule` 依赖 TaskRepository，随 Step 8 Execution 实现

### 1.8.3 验证结果

```text
===== SMOKE PASS =====（11/11）
创建日计划 · 同日幂等 · 多日期独立 · 非法/空日期拒绝
关闭计划 · 关闭不存在拒绝 · closed 不新建
（全量回归：goals 22/22 + state 11/11 同步通过）
```

---

## 1.9 Step 8 — 实现 Execution 子系统（已完成）

### 1.9.1 完成内容

1. 仓库层：`TaskRepository`（getById/getByDate/getByPlan/create/update）、
   `EventRepository`（append/getByDate/getRange；append-only）
2. 服务层：`ExecutionService`（start/complete/skip/cancel + updateTask 字段编辑）
3. `PlanningService::addTaskToPlan`（自动 ensure 计划并挂入任务，含目标关联 FR-010）
4. 冒烟测试 `tests/execution_smoke.cpp`：22 项检查全部通过
5. 全量回归：goals 22/22 + state 11/11 + planning 11/11 + execution 22/22

### 1.9.2 关键代码逻辑

#### 状态机（1.4.5.4 落地）

- 流转通道：`startTask`（→started）、`completeTask`（→completed，含实际时长与
  completedAt）、`skipTask`（→skipped，带原因）、`cancelTask`（→cancelled，带原因）
- **终态保护**：completed/cancelled/skipped 为终态，不可再流转
- **updateTask 只编辑字段**：标题/描述/时长/日期/顺序；试图改状态被拒绝
  （1.3.2 写入纪律：状态流转是唯一的状态变更通道）

#### 事件流（1.5.2 落地）

- 每次状态流转自动 `EventRepository::append` 一条事件（task_started/completed/
  skipped/cancelled），事件含 task_id 关联
- **事件日期语义**：event.date = 行动发生的日期（当天），任务计划日期在
  task.dueDate；复盘时经 task_id 关联（README 2.5.2 数据流）
- occurred_at 为空时由客户端填充当前时间戳（SQLite 的 DEFAULT 仅在列被省略时生效，显式绑定 NULL 会触发约束错误，见 3.10）

#### EventRepository 提前到本步实现

原计划 Event 属于 Step 9 Feedback，但 1.5.2 要求状态流转必须同时写事件，
故追加能力在本步落地；Step 9 Feedback 实现查询聚合与修正留痕（correct）。

#### 踩坑记录：Windows Defender 误报

新重链接的冒烟测试 exe 偶发被 Defender 按哈希拦截（bash 报 Permission denied，
复制同名文件同样被拒）。对策：`touch` 源文件触发重链接产生新哈希即可恢复。

### 1.9.3 验证结果

```text
===== SMOKE PASS =====（22/22）
addTaskToPlan 自动建计划 · 状态流转 start/complete/skip/cancel
终态重复流转拒绝 · 负数时长拒绝 · 每次流转追加事件（4 条验证）
updateTask 改状态拒绝/改名成功 · 任务→目标关联持久化
```

---

## 1.10 Step 9 — 实现 Feedback 子系统（已完成）

### 1.10.1 完成内容

1. `ChangeLogRepository`（append-only + getByTarget）——变更留痕基础设施
2. `EventRepository` 扩展：getById + **correct()（修正留痕流程）**
3. 服务层：`FeedbackService`（recordEvent / eventsOfDay / eventsOfRange /
   eventCountsByType / correctEvent）
4. 冒烟测试 `tests/feedback_smoke.cpp`：16 项检查全部通过
5. 全量回归：5 个冒烟测试（goals/state/planning/execution/feedback）全 PASS

### 1.10.2 关键代码逻辑

#### 修正留痕流程（1.4.14 原则1 落地）

```text
correctEvent(id, fix, reason)
  ↓
1. 读取原始事件（不存在 → 拒绝）
2. 原因必填（空原因 → 拒绝）
3. 先写 change_logs：原始摘要 → 修正摘要 + 原因
4. 再更新事件（仅允许修正 type/title/description，日期/时间戳/关联不可改）
```

即：历史事件永远可追溯"原来是什么、改成了什么、为什么改"。

#### 轻量聚合（1.4.6.2 数据流第一步）

`eventCountsByType(date)` 按类型统计当日事件数，供 Review（Step 10）起草复盘用；
完整的 Observation/Metric/Trend 分析留待闭环稳定后（1.7.20）扩展。

#### 踩坑记录：SQLite DEFAULT 与显式 NULL

`events.occurred_at TEXT NOT NULL DEFAULT (datetime(...))` 列上，**显式绑定 NULL
会触发 NOT NULL 约束错误**——SQLite 的 DEFAULT 只在列被"省略"时生效。
对策：append() 在客户端为空的 occurred_at 填充当前时间戳（与 ExecutionService 一致）。

### 1.10.3 验证结果

```text
===== SMOKE PASS =====（16/16）
事件记录与校验 · 按日/范围查询 · 按类型聚合 · correctEvent 留痕
（原始→修正+原因可查）· 无原因修正拒绝 · 不存在事件拒绝
```

---

## 1.11 Step 10 — 实现 Review 子系统（MVP 闭环闭合）

### 1.11.1 完成内容

1. 仓库层：`ReviewRepository`（getDaily + upsert，同日期修正 = FR-004）
2. 服务层：`ReviewService`（draftDailyReview / saveDailyReview / pendingReviewDates）
3. 冒烟测试 `tests/review_smoke.cpp`：17 项检查全部通过
4. 全量回归：6 个冒烟测试全部 PASS（共 99 项检查）

### 1.11.2 关键代码逻辑

#### 复盘草案（1.5.6 落地）

```text
draftDailyReview(date)
  ↓
读取 tasks(due_date=date) + state_snapshot(date)
  ↓
计划任务 N 项，计划时长 X 分钟
完成 M 项（实际 Y 分钟），跳过 S 项，取消 C 项，待处理 P 项
任务完成率: M/N%   时长偏差: Y-X 分钟
当日状态: 睡眠/精力/专注/心情（有则附）
  ↓
返回 Review 草案（id=0，不落库），用户编辑确认后 saveDailyReview
```

- **事实依据语义**：计划 vs 实际 = 任务状态（due_date 维度的执行真相）；
  事件流是审计轨迹（行动日期维度），不参与偏差计算（1.3.5 原则3：事实与评价分离）
- **补录机制**（1.0.3 循环触发时机落地）：`pendingReviewDates(N)` 返回近 N 天
  "有计划但未复盘"的日期，UI 启动时提示补录；补录后自动移出列表
- 复盘内容与评价（problems/causes/nextActions）由用户填写，系统只生成事实部分

### 1.11.3 验证结果

```text
===== SMOKE PASS =====（17/17）
草案统计（3/1/1/1）· 完成率 33% · 时长偏差 -100 · 状态摘要
无任务草案 · 保存/读回/upsert 修正 · 三类非法保存拒绝
补录列表：无计划不入列 / 有计划未复盘入列 / 补录后移出
```

### 1.11.4 里程碑：MVP 核心闭环闭合

1.7.19 定义的最小闭环至此全部实现并通过测试：

```text
Goal（Step 5）→ State（Step 6）→ Plan（Step 7）→ Task/Execution（Step 8）
→ Event/Feedback（Step 9）→ Review（Step 10）→ 下一轮计划（next_actions）
```

后续 Step 11~13 为：AI Engine、Evolution 基础、MVP 整体测试与 UI。

---

## 1.12 Step 11 — 接入 AI Engine（已完成）

### 1.12.1 完成内容

1. `src/ai/AiConfig`：配置加载（环境变量 → 本地文件 → 默认），key 不入库
2. `src/ai/ProviderAdapter`：可替换适配器接口（1.7.14）
3. `src/ai/OpenAiCompatibleAdapter`：OpenAI 兼容协议（QNetworkAccessManager，
   120s 超时，错误解析）；当前供应商 **DeepSeek**（用户提供 key）
4. `src/ai/AiEngine`：analyzeDailyReview（事实→复盘 JSON）+
   proposeChange（问题→提案 JSON，**status=draft 绝不自动生效**，1.7.17）
5. 冒烟测试 `tests/ai_smoke.cpp`：**真实 API 调用** 10 项检查全部通过

### 1.12.2 关键代码逻辑

#### 配置与安全

- key 存放在 `%LOCALAPPDATA%/PersonalOS/PersonalOS/ai_config.json`
  （已在 .gitignore，绝不入库）；环境变量 `PERSONOS_AI_KEY/BASE_URL/MODEL` 可覆盖
- 踩坑：使用 `QStandardPaths::AppLocalDataLocation` 前必须先设置
  OrganizationName/ApplicationName，否则落盘路径错误

#### 数据边界（1.7.16 落地）

- `AiEngine::buildDailyFacts(date)` 只提取**当日**任务/事件/状态摘要文本，
  绝不上传整个数据库或其他日期的数据
- 系统提示词强制约束：只基于事实、不编造、区分事实与推测、不替用户做决定

#### 权限边界（1.7.17 落地）

- AI 输出只生成 **Proposal 草案**（status=draft）与 Review 文本，
  不写任何正式数据；生效需经 Step 12 EvolutionService 的用户批准流程

### 1.12.3 验证结果（真实 DeepSeek 调用）

```text
===== SMOKE PASS =====（10/10）
AI 复盘分析：正确识别 90/120 分钟偏差、跳过原因、状态偏低
AI 变更提案：任务量下调 30% + 必做/选做分类（draft 状态）
无效 key → 错误信息正常返回
（本地 6 个冒烟全量回归通过）
```

---

## 1.13 Step 12 — 实现 Evolution 基础能力（已完成）

### 1.13.1 完成内容

1. 仓库层：`ProposalRepository`（create/getById/getByStatus/setStatus）、
   `VersionRepository`（create/getLatest/getAll）
2. 服务层：`EvolutionService`（propose / pendingProposals / decide /
   applyApproved / versions / currentVersion）
3. 冒烟测试 `tests/evolution_smoke.cpp`：21 项检查全部通过
4. 全量回归：7 个本地冒烟测试全部 PASS

### 1.13.2 关键代码逻辑

#### 变更生命周期（1.6.13 落地）

```text
propose()                    提案提交 → status=review（含三项必填校验）
  ↓
decide(id, approved)         用户决定 → approved / rejected（记录 decidedAt）
                             仅 review 状态可决定；已决定/已生效不可重复决定
  ↓
applyApproved(id)            仅 approved 可生效，三步原子流程：
                             1) 新建 Version（自动编号 v0.1 → v0.2 → …，父版本链）
                             2) 追加 ChangeLog（关联 proposal_id + version_id，
                                含修改前后 + 理由 → 1.6.21 可追溯）
                             3) 提案 status → applied
```

- **防作弊条款（1.6.23）落地**：系统不能自行批准提案——decide 只接受
  用户调用；AI 输出始终是 draft（Step 11），不经过本流程无法触碰正式数据
- **MVP 边界**：applyApproved 完成"记录生效"（版本+留痕），实际业务字段修改
  由未来调用方执行；"Applied → Observed → Accepted/Reverted"的观察与回滚
  机制（1.6.16/1.6.26）为后续扩展，版本表已预留 parent_version 链支持回滚

### 1.13.3 验证结果

```text
===== SMOKE PASS =====（21/21）
三类缺项提案拒绝 · 提交→review · 拒绝/批准 · 非待审不可重复决定
生效→v0.1（无父版本）· 留痕关联提案+版本+修改前后+理由
已生效/未批准不可生效 · 版本链 v0.1→v0.2 · 不存在提案拒绝
```

### 1.13.4 设计评审缺口收尾

Step 5 评审指出的缺口 ①"Proposal/ChangeRequest 实体缺失"至此完全落地：
实体、状态机、用户批准、变更留痕、版本链五个环节全部实现并测试通过。

---

## 1.14 Step 13 — MVP 整体测试与 QML UI（MVP 完成）

### 1.14.1 完成内容

1. `ApplicationService`（src/services）：QML 唯一入口门面，编排 7 个子系统 + AI Engine
   （1.3.1 依赖规则的最终落实：跨子系统协作只发生在这里）
2. QML 包装对象 `TaskItem` / `GoalItem`（属性驱动 UI 刷新）
3. QML 界面四页落地（1.3.6 清单全部实现）：
   - `Main.qml` 导航壳 + 补录提醒横幅（1.0.3 循环触发时机）
   - `TodayPage` 今日任务：添加/开始/完成（实际分钟）/跳过/取消
   - `GoalsPage` 目标层级树（缩进展示）+ 新建目标（层级/父目标下拉）
   - `StatePage` 状态记录（睡眠/精力/专注/心情）
   - `ReviewPage` 复盘：本地草案 / AI 分析（异步）/ 保存 /
     AI 提案区（生成 → 批准并生效 / 拒绝，走 EvolutionService 全流程）
4. `tst_mvp_loop`（Qt Test）：按 1.7.21 验收标准的闭环测试（4/4 通过）
5. `ctest` 一键回归：8 个测试全部注册，100% 通过
6. 开发者工具：`--ui-check`（QML 加载验证，1.5 秒自动退出）

### 1.14.2 关键代码逻辑

- **桥接模式**：QML 只认 `appService` 一个上下文对象；列表用
  `QList<QObject*>` + 属性包装对象（MVP 规模足够，QAbstractListModel 后续按需升级）
- **异步 AI 调用**：AI 分析/提案通过信号回 UI（`aiReviewReady`/`proposalChanged`），
  不阻塞界面；错误统一走 `lastError` 属性展示
- **补录闭环**：启动 `init()` → ensure 今日计划 + 刷新任务/目标 + 补录检查 →
  横幅提示 → 用户补录后 `refreshPending()` 自动移除
- **QML 踩坑**：页面内函数必须声明在根级作用域（子对象体内声明会导致
  ReferenceError）；ComboBox 初始 currentIndex 为 -1，`model.get()` 前必须判空

### 1.14.3 验证结果

```text
ctest 8/8 通过（smoke×7 + tst_mvp_loop）
tst_mvp_loop: 4/4（initTestCase + fullLoop + evolutionChain + cleanupTestCase）
--ui-check: QML 加载无警告无错误
```

### 1.14.4 MVP 里程碑总结

1.7.22 的 13 个开发步骤全部完成。MVP 具备：

```text
完整闭环：Goal → State → Plan → Task → Execution → Event → Review → 下一轮
权限机制：AI 只提案 → 用户批准 → ChangeLog + Version 链（防作弊条款落地）
AI 能力：复盘分析 + 变更提案（DeepSeek，数据最小化）
UI：今日/目标/状态/复盘 四页 + 补录提醒
测试：8 个测试 130+ 项检查全通过，ctest 一键回归
```

### 1.14.5 环境问题最终定论（重要）

今天全天出现的随机"Permission denied / BAD_COMMAND"（此前在 1.9、3.13 中
误判为 Windows Defender 哈希拦截）的**真正根因**：

> **Windows 11 智能应用控制（Smart App Control）处于开启状态**，
> 拦截无数字签名、缺乏信誉度的本地编译程序。cmd 直接运行报错：
> "已被组织由 Device Guard 策略阻止"。

- 影响：每次编译新 exe 都可能被随机拦截，开发不可持续
- 解决：用户已在 Windows 安全中心 → 应用和浏览器控制 → 智能应用控制 → **关闭**
  （单向开关，关闭后无法重新开启，除非重装系统）
- 验证：独立运行 appPersonOS.exe（仅 exe 目录 DLL，无 Qt PATH）exit 0；
  ctest 8/8 通过（0.72 秒，拦截消失后明显加速）
- **部署状态**：windeployqt 已把全部 Qt 依赖打入 build 目录，
  **双击 `build\Desktop_Qt_6_11_2_MinGW_64_bit_Debug\appPersonOS.exe` 即可使用**

开发者小tips：进入第 2 章（测试）之前，建议：使用真实数据试用 UI 一周，再按第 2 章
补充手工测试用例与缺陷修复记录。

---

# 2. 测试（Testing）

 验证：**实现出来的东西是否满足需求、是否正确可靠。**



# 3. 部署（Deployment）

 把经过验证的软件投入实际运行环境。



# 4. 运维与维护（Operation & Maintenance）

 系统上线后持续监控、修复问题、适应环境变化。



# 5. 迭代/演进（Evolution）

 出现新的需求或发现原设计不足后，进入下一轮：