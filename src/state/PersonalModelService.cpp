#include "state/PersonalModelService.h"

#include <QStringLiteral>

namespace PersonOS {

namespace {

// 仅在参数不存在时写入（已存在的参数——含用户覆盖值——不被重置）
void seedDefault(PersonalModelRepository &repo, const QString &key,
                 const std::optional<double> &value, const QString &text)
{
    if (repo.get(key))
        return;
    PersonalModelParam p;
    p.key = key;
    p.value = value;
    p.valueText = text;
    p.priorSource = QStringLiteral("population_default");
    repo.upsert(p);
}

} // namespace

std::optional<PersonalModelParam> PersonalModelService::get(const QString &key) const
{
    return m_repo.get(key);
}

QList<PersonalModelParam> PersonalModelService::all() const
{
    const auto v = m_repo.getAll();
    return QList<PersonalModelParam>(v.begin(), v.end());
}

bool PersonalModelService::set(const PersonalModelParam &p, QString *error)
{
    if (error)
        error->clear();
    if (!p.isValid()) {
        if (error)
            *error = QStringLiteral("参数名不能为空");
        return false;
    }
    if (!p.value && p.valueText.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("参数值不能为空（value 与 valueText 至少填一个）");
        return false;
    }
    if (!m_repo.upsert(p)) {
        if (error)
            *error = m_repo.lastError();
        return false;
    }
    return true;
}

bool PersonalModelService::ensureDefaults(QString *error)
{
    if (error)
        error->clear();

    // 默认先验参数（design.md 1.10.1；Q1 已定"人群先验起步 → 校准接管"，
    // 具体数值为候选默认值，Step 6 起由校准更新）。
    // 时间容量单位：小时（Feasibility 计算时与 planned_minutes 换算）。
    seedDefault(m_repo, QStringLiteral("time_capacity"), 6.0, {});
    seedDefault(m_repo, QStringLiteral("energy_rhythm"), std::nullopt,
                QStringLiteral("default")); // 精力节律：default = 未校准（1.10.1 状态系数 1.0）
    // 上界阈值占位（population_default 候选 1.2，【待研究证据】领域②③文献到位后
    // 升级为 research_evidence + evidence_ref；机制见 design.md 1.10.1 / worklog 1.15）
    seedDefault(m_repo, QStringLiteral("workload_threshold"), 1.2, {});
    // 下界触发占位（design.md 1.10.2 候选阈值，同样待研究证据校准，worklog 1.16）
    seedDefault(m_repo, QStringLiteral("stall_threshold"), 0.4, {});
    seedDefault(m_repo, QStringLiteral("stall_quiet_days"), 3.0, {});

    if (!m_repo.lastError().isEmpty()) {
        if (error)
            *error = m_repo.lastError();
        return false;
    }
    return true;
}

} // namespace PersonOS
