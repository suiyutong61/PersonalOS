# 架构概览

## 分层

PersonOS 采用面向端口的分层结构：

```text
QML
 └─ Presentation / ViewModels
     └─ Application / Use Cases + Ports
         ├─ Domain / Entities + State Machines
         └─ Infrastructure / SQLite + AI + Retrieval + OS adapters
```

- `domain` 不依赖 Qt UI、网络或具体数据库。
- `application` 编排业务流程，定义仓储与外部能力端口。
- `infrastructure` 实现 SQLite、模型服务、知识检索、凭据和备份。
- `presentation` 将用例暴露给 QML，并负责异步任务和界面状态。

旧模块仍在渐进迁移中；新功能应进入上述分层，不再扩大旧跨模块服务的职责。

## 主要聚合

- **Goal**：用户希望获得的长期结果。
- **Route**：Goal 的版本化粗粒度阶段图；正式采用需用户确认。
- **Stage Detail**：阶段内部的结果、任务、项目、标准和资料建议。
- **MEL**：短周期最小可执行闭环，包含任务、时间窗、容量和状态机。
- **Knowledge**：资料、版本、证据、关系、检索命中和知识快照。
- **Decision / AI Job**：AI 候选、输入快照、来源模式和用户决定。

## MEL 进度更新

进度更新遵守候选—确认模型：

1. 用户用自然语言汇报已完成内容、结果、困难和耗时。
2. AI 返回结构化的全任务更新候选。
3. 应用校验任务集合、状态/进度一致性、总体进度和完成标志。
4. 页面显示当前值与候选值。
5. 用户确认后，在一个 Unit of Work 内更新任务、追加事件、转移 MEL 并接受决策。

正式数据不得在步骤 5 之前改变。确认时使用 revision 做乐观并发检查；过期候选必须重新生成。

## 数据持久化

- SQLite 是本地事实来源。
- 迁移按版本追加，已发布迁移不可重写。
- 进度、状态转移、预测和审计尽量采用追加式事实。
- 关键写入使用 revision 与幂等键防止重复或覆盖。
- 用户数据与应用发布文件分离，仓库不包含运行数据库。

## AI 与知识约束

- AI 输出必须通过 JSON 契约和领域硬约束。
- 路线、方法、资料等知识引用必须来自本次检索候选集。
- 来源支持不足时需要标注 `partially_grounded` 或 `ungrounded`。
- AI 负责提出候选，用户负责是否采用。
- 模型调用在后台线程运行；UI 更新必须回到 QObject 上下文。

## 关键不变量

- 未确认路线、阶段详情、MEL 或进度候选不得成为正式事实。
- 所有必需任务完成后才能进入 `execution_complete`。
- MEL 状态转移必须遵守领域状态机。
- 同一聚合只应存在一个有效的 Pending 候选。
- 事务中的任一步失败必须整体回滚。
- 凭据不能写入 SQLite 明文字段、日志或 Git。
