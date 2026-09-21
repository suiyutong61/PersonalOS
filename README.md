# Personal OS

一个动态、精准、高效的个人成长辅助系统 —— 通过「计划 → 行动 → 记录 → 复盘 → 调整」的闭环，辅助用户在长期维度上完成自我管理、能力培养与目标推进。

> **核心理念**：系统记录的不只是「用户今天完成了什么」，而是「用户这个人正在发生什么变化」。AI 只提供分析与建议，最终决定权永远属于用户。
环境
> 
<hr>

##  核心特性

### 1. 完整的个人管理闭环

```text
Goal → State → Plan → Task → Execution → Event → Review → 下一轮计划
```

覆盖目标管理、状态记录、计划制定、任务执行、事件追溯与日终复盘全流程。

### 2. 权限与变更机制（人类掌舵的自我改进系统）

- **分层权限**：越靠近 Core（核心价值观/原则），修改门槛越高；AI 永远只能 *读取 → 分析 → 提议*
- **Proposal 流程**：任何重要变更必须走 `AI 提案 → 用户批准 → 生效` 三阶段，系统不能自行批准自己的修改
- **完整可追溯**：每次生效自动写入 ChangeLog 并创建新版本（v0.1 → v0.2 → …），支持版本链回溯
- **防作弊条款**：系统不能为「提高指标」而降低目标——评价规则本身不可被无约束修改

### 3. AI 引擎（可替换 Provider）

- OpenAI 兼容协议适配器，当前支持 DeepSeek（可通过配置切换供应商）
- **日复盘分析**：基于当日最小必要事实生成问题/原因/建议
- **变更提案**：AI 输出永远只是 draft 草案，不直接写任何正式数据
- **数据最小化**：只发送任务所需的当日摘要，绝不上传整个数据库（隐私优先）

### 4. 本地优先与隐私保护

- 单机本地运行，SQLite 本地存储，无云端依赖
- 数据库位于用户本地应用数据目录，历史数据保留原始记录、修正必须留痕
- 外部 AI 仅接收最小必要上下文

### 5. 容错与自恢复

- 计划可以失败，系统不能因计划失败而失效
- 内置恢复模式（Recovery Mode）与诊断模式（Diagnostic Mode）
- 应用启动时自动检查「有计划但未复盘」的日期并提示补录

---

## 🏗️ 系统架构

7 个子系统 + 1 个 AI 引擎，上层约束下层：

```text
QML (UI)
   ↓
ApplicationService（QML 唯一入口门面）
   ↓
┌─────────────────────────────────────────────┐
│ Core & Goal → State → Planning → Execution   │
│        → Feedback → Review → Evolution       │
└─────────────────────────────────────────────┘
   ↓
Repository 仓库层（唯一允许访问 SQLite 的层）
   ↓
SQLite
```

- 子系统之间不直接互相调用，跨子系统协作由 ApplicationService 编排
- 事实与推论分离、目标与计划分离、状态与评价分离（核心数据纪律）

---

##  技术栈

| 层 | 技术 |
|---|---|
| 语言 | C++20 |
| UI | Qt 6 / Qt Quick / QML |
| 数据库 | SQLite（WAL 模式，schema 版本化迁移） |
| 构建 | CMake |
| AI | OpenAI 兼容协议（DeepSeek 等），Qt Network |
| 测试 | Qt Test + ctest 一键回归 |

---

##  目录结构

```text
PersonOS
├── CMakeLists.txt
├── src
│   ├── models/        # 领域数据模型（11 个核心实体）
│   ├── database/      # 仓库层（Repository）+ 数据库管理 + 迁移
│   ├── goals/         # Core & Goal 子系统
│   ├── state/         # 个人状态子系统
│   ├── planning/      # 计划子系统
│   ├── execution/     # 执行子系统（任务状态机 + 事件流）
│   ├── feedback/      # 反馈子系统（事件聚合 + 修正留痕）
│   ├── review/        # 复盘子系统
│   ├── evolution/     # 演化子系统（Proposal → ChangeLog → Version）
│   ├── ai/            # AI Engine + Provider 适配器
│   ├── services/      # ApplicationService（QML 门面）
│   └── application/   # main.cpp
├── qml
│   ├── Main.qml       # 导航壳 + 补录提醒
│   └── pages/         # 今日 / 目标 / 状态 / 复盘
└── tests/             # 冒烟测试 + MVP 闭环验收测试
```


##  构建与运行

### 环境要求

- Windows 11
- Qt 6.10+（MinGW 64-bit）
- CMake 3.16+

### 构建

```bash
cmake -S . -B build -G "MinGW Makefiles" \
  -DCMAKE_PREFIX_PATH=<Qt 安装目录>/6.11.2/mingw_64 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build -j
```

### 运行

```bash
build/appPersonOS.exe
```

> 提示：若 PATH 中存在多套 MinGW，请显式指定 Qt kit 自带的编译器（`-DCMAKE_CXX_COMPILER` / `-DCMAKE_MAKE_PROGRAM`），避免工具链混用。

### 配置 AI 能力（可选）

在应用数据目录（`%LOCALAPPDATA%/PersonalOS/PersonalOS/`）创建 `ai_config.json`：

```json
{
  "apiKey": "你的 API Key",
  "baseUrl": "https://api.deepseek.com",
  "model": "deepseek-chat"
}
```

未配置 AI 时系统核心功能完全可用，仅 AI 分析与 AI 提案不可用。

### 开发者工具

| 参数 | 用途 |
|---|---|
| `--db-check` | 无界面验证数据库（报告写入 dbcheck.log） |
| `--ui-check` | 验证 QML 加载与上下文注入，1.5 秒自动退出 |

---

##  测试

```bash
ctest --test-dir build
```

- 7 个冒烟测试（goals / state / planning / execution / feedback / review / evolution）
- MVP 闭环验收测试（Qt Test，按验收标准全链路验证）
- AI 冒烟测试需网络与 API key，手动运行 `smoke_ai`

---

##  文档

| 文档 | 内容 |
|---|---|
| [requirements.md](requirements.md) | 需求分析：25 条功能需求 + 11 条非功能需求 |
| [design.md](design.md) | 系统设计：架构、数据模型、模块职责、权限机制 |
| [worklog.md](worklog.md) | 实现工作日志：13 个开发步骤的完整记录 |

---

##  路线图

- [x] MVP：核心闭环 + 权限机制 + AI 引擎 + 四页 UI
- [ ] 周/月复盘与长期趋势分析
- [ ] 提醒与通知机制
- [ ] Observation / Metric 指标体系
- [ ] 实验系统（Experiment Loop）与策略自动优化
- [ ] 数据可视化看板
