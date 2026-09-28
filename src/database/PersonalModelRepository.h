#pragma once

#include <optional>
#include <vector>

#include <QString>

#include "models/PersonalModelParam.h"

namespace PersonOS {

// 个人模型参数仓库（design.md 1.9.2）
// key-value 风格，upsert 语义：同 key 只更新不新增。
class PersonalModelRepository
{
public:
    std::optional<PersonalModelParam> get(const QString &key) const;
    std::vector<PersonalModelParam> getAll() const; // ORDER BY key
    bool upsert(const PersonalModelParam &p);

    QString lastError() const { return m_lastError; }

private:
    void fail(const QString &context, const QString &message) const;

    mutable QString m_lastError;
};

} // namespace PersonOS
