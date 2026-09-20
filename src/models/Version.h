#pragma once

#include <QString>

// 版本（README 2.6.14 / 2.6.17）
// 对应表 versions。Personal OS 自身版本：stable/experiment/archive。
// 重要 Strategy、Policy、规则变化必须产生新版本并关联 ChangeLog。
namespace PersonOS {

struct Version
{
    qint64 id = 0;
    QString versionNumber;                            // v0.1 / v1.0 ...
    QString parentVersion;
    QString description;
    QString status = QStringLiteral("stable");        // stable/experiment/archive
    QString createdAt;
};

} // namespace PersonOS
