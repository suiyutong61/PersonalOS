#pragma once

#include <optional>
#include <vector>

#include <QList>
#include <QString>

#include "database/CoreValueRepository.h"
#include "database/GoalRepository.h"
#include "database/PrincipleRepository.h"
#include "models/CoreValue.h"
#include "models/Goal.h"
#include "models/Principle.h"

namespace PersonOS {

// Core & Goal 子系统（README 2.4.2 / 3.3.5）
// 职责：目标层级管理 + 核心价值/原则管理。
// 修改权限（2.6.8）：本服务只接受"用户主导"的直接修改；
// AI 发起的修改在 Step 12 走 Proposal → 批准 → ChangeLog 流程。
class CoreAndGoalService
{
public:
    // ---- 目标 ----
    QList<Goal> goalHierarchy() const;                 // 全量目标，UI 按 parentId 组树
    std::vector<Goal> goalsByLevel(const QString &level) const;
    std::optional<Goal> goal(qint64 id) const;

    // 校验：level 合法 / 标题非空 / 父目标存在 / 不构成环（2.2.4 目标链）
    qint64 createGoal(const Goal &g, QString *error = nullptr);
    bool updateGoal(const Goal &g, QString *error = nullptr);

    // ---- 核心价值（仅用户主动修改）----
    std::vector<CoreValue> coreValues() const;
    qint64 createCoreValue(const CoreValue &v, QString *error = nullptr);
    bool updateCoreValue(const CoreValue &v, QString *error = nullptr);

    // ---- 原则（用户确认后修改）----
    std::vector<Principle> principles() const;
    qint64 createPrinciple(const Principle &p, QString *error = nullptr);
    bool updatePrinciple(const Principle &p, QString *error = nullptr);

private:
    bool validateLevel(const QString &level, QString *error) const;
    bool validateParent(qint64 goalId, qint64 parentId, QString *error) const;
    bool wouldCreateCycle(qint64 goalId, qint64 parentId) const;

    GoalRepository m_goals;
    CoreValueRepository m_coreValues;
    PrincipleRepository m_principles;
};

} // namespace PersonOS
