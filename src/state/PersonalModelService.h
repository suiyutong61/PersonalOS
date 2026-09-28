#pragma once

#include <optional>

#include <QList>
#include <QString>

#include "database/PersonalModelRepository.h"
#include "models/PersonalModelParam.h"

namespace PersonOS {

// Personal Model 维护（design.md 1.1：State System 内部组件 / 1.4.1 / 1.9.3）
// 职责：个人参数读改（用户覆盖）+ 默认先验参数初始化（Q1 已定：人群先验起步）。
// 校准更新（滚动窗口计算）属于 CalibrationService（Step 6），本服务只提供读写原语；
// 参数更新的留痕由校准路径写 CalibrationRecord（NFR-17）。
class PersonalModelService
{
public:
    std::optional<PersonalModelParam> get(const QString &key) const;
    QList<PersonalModelParam> all() const;

    // 手动设置/覆盖参数（key 非空；value 与 valueText 至少填一个）
    bool set(const PersonalModelParam &p, QString *error = nullptr);

    // 初始化默认先验参数（prior_source=population_default）；
    // 已存在的参数（含用户覆盖值）不被重置。
    bool ensureDefaults(QString *error = nullptr);

    QString lastError() const { return m_repo.lastError(); }

private:
    PersonalModelRepository m_repo;
};

} // namespace PersonOS
