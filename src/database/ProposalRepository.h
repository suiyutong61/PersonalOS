#pragma once

#include <optional>
#include <vector>

#include <QString>

#include "models/Proposal.h"

namespace PersonOS {

// Proposal / ChangeRequest 仓库（README 3.3.4 / 2.6.12）
// 状态机：draft → review → approved / rejected → applied（2.6.13）
class ProposalRepository
{
public:
    qint64 create(const Proposal &p);                // 返回新 id；失败返回 0
    std::optional<Proposal> getById(qint64 id) const;
    std::vector<Proposal> getByStatus(const QString &status) const;
    bool setStatus(qint64 id, const QString &status, const QString &decidedAt);

    QString lastError() const { return m_lastError; }

private:
    void fail(const QString &context, const QString &message) const;

    mutable QString m_lastError;
};

} // namespace PersonOS
