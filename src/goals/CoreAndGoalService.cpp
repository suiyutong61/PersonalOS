#include "goals/CoreAndGoalService.h"

namespace PersonOS {

namespace {

// 目标层级（README 2.2.4）：vision → long_term → annual → quarterly → monthly → weekly
const QStringList kValidLevels = {QStringLiteral("vision"),   QStringLiteral("long_term"),
                                  QStringLiteral("annual"),   QStringLiteral("quarterly"),
                                  QStringLiteral("monthly"),  QStringLiteral("weekly")};

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

// ---------------- 目标 ----------------

QList<Goal> CoreAndGoalService::goalHierarchy() const
{
    const std::vector<Goal> all = m_goals.getAll();
    QList<Goal> out;
    out.reserve(static_cast<qsizetype>(all.size()));
    for (const Goal &g : all)
        out.append(g);
    return out;
}

std::vector<Goal> CoreAndGoalService::goalsByLevel(const QString &level) const
{
    return m_goals.getByLevel(level);
}

std::optional<Goal> CoreAndGoalService::goal(qint64 id) const
{
    return m_goals.getById(id);
}

bool CoreAndGoalService::validateLevel(const QString &level, QString *error) const
{
    if (!kValidLevels.contains(level)) {
        setError(error, QStringLiteral("无效的目标层级: %1（合法值: %2）")
                            .arg(level, kValidLevels.join(QStringLiteral("/"))));
        return false;
    }
    return true;
}

bool CoreAndGoalService::wouldCreateCycle(qint64 goalId, qint64 parentId) const
{
    qint64 cursor = parentId;
    while (cursor != 0) {
        if (cursor == goalId)
            return true; // 沿父链向上遇到了目标自身 → 环
        const auto parent = m_goals.getById(cursor);
        if (!parent)
            return false; // 链断裂：父目标不存在，由 validateParent 报错
        cursor = parent->parentId;
    }
    return false;
}

bool CoreAndGoalService::validateParent(qint64 goalId, qint64 parentId, QString *error) const
{
    if (parentId == 0)
        return true;
    if (parentId == goalId) {
        setError(error, QStringLiteral("目标不能以自身为父目标"));
        return false;
    }
    if (!m_goals.getById(parentId)) {
        setError(error, QStringLiteral("父目标不存在（id=%1）").arg(parentId));
        return false;
    }
    if (wouldCreateCycle(goalId, parentId)) {
        setError(error, QStringLiteral("父子关系不能构成环"));
        return false;
    }
    return true;
}

qint64 CoreAndGoalService::createGoal(const Goal &g, QString *error)
{
    if (error)
        error->clear();
    if (!g.isValid()) {
        setError(error, QStringLiteral("目标标题不能为空"));
        return 0;
    }
    if (!validateLevel(g.level, error))
        return 0;
    if (!validateParent(0, g.parentId, error))
        return 0;
    return m_goals.create(g);
}

bool CoreAndGoalService::updateGoal(const Goal &g, QString *error)
{
    if (error)
        error->clear();
    if (!g.isValid()) {
        setError(error, QStringLiteral("目标标题不能为空"));
        return false;
    }
    if (!validateLevel(g.level, error))
        return false;
    if (!m_goals.getById(g.id)) {
        setError(error, QStringLiteral("目标不存在（id=%1）").arg(g.id));
        return false;
    }
    if (!validateParent(g.id, g.parentId, error))
        return false;
    return m_goals.update(g);
}

// ---------------- 核心价值 ----------------

std::vector<CoreValue> CoreAndGoalService::coreValues() const
{
    return m_coreValues.getAll();
}

qint64 CoreAndGoalService::createCoreValue(const CoreValue &v, QString *error)
{
    if (error)
        error->clear();
    if (v.name.trimmed().isEmpty()) {
        setError(error, QStringLiteral("核心价值名称不能为空"));
        return 0;
    }
    const qint64 id = m_coreValues.create(v);
    if (id == 0)
        setError(error, m_coreValues.lastError()); // 含重名（name UNIQUE）
    return id;
}

bool CoreAndGoalService::updateCoreValue(const CoreValue &v, QString *error)
{
    if (error)
        error->clear();
    if (v.name.trimmed().isEmpty()) {
        setError(error, QStringLiteral("核心价值名称不能为空"));
        return false;
    }
    if (!m_coreValues.update(v)) {
        setError(error, m_coreValues.lastError());
        return false;
    }
    return true;
}

// ---------------- 原则 ----------------

std::vector<Principle> CoreAndGoalService::principles() const
{
    return m_principles.getAll();
}

qint64 CoreAndGoalService::createPrinciple(const Principle &p, QString *error)
{
    if (error)
        error->clear();
    if (p.text.trimmed().isEmpty()) {
        setError(error, QStringLiteral("原则内容不能为空"));
        return 0;
    }
    const qint64 id = m_principles.create(p);
    if (id == 0)
        setError(error, m_principles.lastError());
    return id;
}

bool CoreAndGoalService::updatePrinciple(const Principle &p, QString *error)
{
    if (error)
        error->clear();
    if (p.text.trimmed().isEmpty()) {
        setError(error, QStringLiteral("原则内容不能为空"));
        return false;
    }
    if (!m_principles.update(p)) {
        setError(error, m_principles.lastError());
        return false;
    }
    return true;
}

} // namespace PersonOS
