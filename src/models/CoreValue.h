#pragma once

#include <QString>

// 核心价值（README 2.3.2-1 / 2.6.4）
// 对应表 core_values。修改频率最低，只能由用户主动修改。
namespace PersonOS {

struct CoreValue
{
    qint64 id = 0;
    QString name;                 // 唯一，如"成长""长期主义"
    QString description;
    int sortOrder = 0;
    QString createdAt;
};

} // namespace PersonOS
