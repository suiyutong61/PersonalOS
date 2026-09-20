#pragma once

#include <optional>
#include <vector>

#include <QString>

#include "database/ChangeLogRepository.h"
#include "database/ProposalRepository.h"
#include "database/VersionRepository.h"
#include "models/Proposal.h"
#include "models/Version.h"

namespace PersonOS {

// Evolution 子系统（README 2.4.8 / 3.3.5 / 2.6.13）
// 职责：变更请求的生命周期管理 ——
//   propose（→review）→ decide（用户批准/拒绝）→ applyApproved（写 ChangeLog + 新 Version）
// 原则（2.6.27）：系统可以自我改进，但不能自行决定"为什么存在"；
// 所有变更必须经用户批准，且生效过程可追溯（NFR-05）。
class EvolutionService
{
public:
    // 提交变更请求（AI 草案或用户发起），状态置为 review 待审
    qint64 propose(const Proposal &p, QString *error = nullptr);

    // 待用户决定的提案（status=review）
    std::vector<Proposal> pendingProposals() const;

    // 用户决定：approved → status=approved；否则 rejected（2.6.19）
    bool decide(qint64 proposalId, bool approved, QString *error = nullptr);

    // 生效已批准的提案：写 ChangeLog（含提案 id）→ 新建 Version（自动编号）
    // → 提案 status=applied（2.6.13 生命周期收尾；观察/回滚为后续扩展）
    bool applyApproved(qint64 proposalId, QString *error = nullptr);

    std::vector<Version> versions() const;
    std::optional<Version> currentVersion() const;

private:
    QString nextVersionNumber() const;

    ProposalRepository m_proposals;
    VersionRepository m_versions;
    ChangeLogRepository m_changeLogs;
};

} // namespace PersonOS
