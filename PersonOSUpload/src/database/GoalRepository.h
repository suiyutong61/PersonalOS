#pragma once

#include <optional>
#include <vector>

#include <QString>

#include "models/Goal.h"

namespace PersonOS {

// 目标仓库（README 3.3.4）。仅提供读写原语；
// 层级校验、环检测等修改权限逻辑在 CoreAndGoalService（2.6），不在此层。
class GoalRepository
{
public:
    std::optional<Goal> getById(qint64 id) const;
    std::vector<Goal> getAll() const;                // ORDER BY priority DESC, id
    std::vector<Goal> getChildren(qint64 parentId) const; // parentId=0 → parent_id IS NULL（根）
    std::vector<Goal> getByLevel(const QString &level) const;
    qint64 create(const Goal &g);                    // 返回新 id；失败返回 0
    bool update(const Goal &g);

    QString lastError() const { return m_lastError; }

private:
    void fail(const QString &context, const QString &message) const;

    mutable QString m_lastError;
};

} // namespace PersonOS
