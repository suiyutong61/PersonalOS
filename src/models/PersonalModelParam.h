#pragma once

#include <QString>

#include <optional>

// 个人模型参数（design.md 1.3.1-22 / FR-B-02）
// 对应表 personal_model_params（key 主键，key-value 风格，可扩展无需改表）。
// 个人参数是"个人校准"数据：先验来自研究证据/人群默认（Q1 已定），
// 随个人数据积累被校准值接管；与通用证据分离（FR-I-03：可覆盖先验，不得修改证据）。
namespace PersonOS {

struct PersonalModelParam
{
    QString key;                        // time_capacity / energy_rhythm / deviation_baseline / ...
    std::optional<double> value;        // 数值型参数
    QString valueText;                  // 文本型参数（如精力节律描述）
    QString priorSource = QStringLiteral("population_default"); // research_evidence / population_default / user_input
    QString evidenceRef;                // 软关联证据库（Q6 逻辑隔离）
    QString updatedAt;

    bool isValid() const { return !key.trimmed().isEmpty(); }
};

} // namespace PersonOS
