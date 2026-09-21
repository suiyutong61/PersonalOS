#pragma once

#include <optional>
#include <vector>

#include <QString>

#include "models/Version.h"

namespace PersonOS {

// 版本仓库（README 3.3.4 / 2.6.14）
// Personal OS 自身版本：stable/experiment/archive（2.6.17）
class VersionRepository
{
public:
    qint64 create(const Version &v);                 // 返回新 id；失败返回 0
    std::optional<Version> getLatest() const;        // 最新版本（id 最大）
    std::vector<Version> getAll() const;             // ORDER BY id DESC

    QString lastError() const { return m_lastError; }

private:
    void fail(const QString &context, const QString &message) const;

    mutable QString m_lastError;
};

} // namespace PersonOS
