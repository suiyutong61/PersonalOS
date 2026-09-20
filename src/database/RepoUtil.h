#pragma once

#include <optional>

#include <QSqlQuery>
#include <QVariant>

#include "database/DatabaseManager.h"

// 仓库层公共工具：
// - query()：统一从 DatabaseManager 取连接并绑定参数
// - nullable*()：外键列"0 → NULL"映射。foreign_keys=ON 时 0 号外键
//   会违反约束，因此关联 id 为 0 时必须绑定 NULL。
namespace PersonOS::RepoUtil {

inline QSqlQuery query(const QString &sql, const QVariantList &values = {})
{
    QSqlQuery q(DatabaseManager::instance().database());
    q.prepare(sql);
    for (const QVariant &v : values)
        q.addBindValue(v);
    return q;
}

inline QVariant nullableId(qint64 id)
{
    return id == 0 ? QVariant() : QVariant(id);
}

inline QVariant nullableText(const QString &s)
{
    return s.isEmpty() ? QVariant() : QVariant(s);
}

inline QVariant nullableInt(const std::optional<int> &v)
{
    return v ? QVariant(*v) : QVariant();
}

inline QVariant nullableReal(const std::optional<double> &v)
{
    return v ? QVariant(*v) : QVariant();
}

} // namespace PersonOS::RepoUtil
