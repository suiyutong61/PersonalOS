#include "evolution/EvolutionService.h"

#include <QDateTime>
#include <QRegularExpression>

#include "models/ChangeLog.h"

namespace PersonOS {

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QString nowString()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

} // namespace

qint64 EvolutionService::propose(const Proposal &p, QString *error)
{
    if (error)
        error->clear();
    if (p.targetType.trimmed().isEmpty()) {
        setError(error, QStringLiteral("提案缺少修改对象类型（target_type）"));
        return 0;
    }
    if (p.proposedValue.trimmed().isEmpty()) {
        setError(error, QStringLiteral("提案缺少建议值（proposed_value）"));
        return 0;
    }
    if (p.reason.trimmed().isEmpty()) {
        setError(error, QStringLiteral("提案缺少理由（reason，NFR-04 可解释）"));
        return 0;
    }
    Proposal draft = p;
    draft.status = QStringLiteral("review"); // 提交即进入待审（2.6.13）
    const qint64 id = m_proposals.create(draft);
    if (id == 0)
        setError(error, m_proposals.lastError());
    return id;
}

std::vector<Proposal> EvolutionService::pendingProposals() const
{
    return m_proposals.getByStatus(QStringLiteral("review"));
}

bool EvolutionService::decide(qint64 proposalId, bool approved, QString *error)
{
    if (error)
        error->clear();
    const auto p = m_proposals.getById(proposalId);
    if (!p) {
        setError(error, QStringLiteral("提案不存在（id=%1）").arg(proposalId));
        return false;
    }
    if (p->status != QStringLiteral("review")) {
        setError(error, QStringLiteral("只能决定待审（review）提案，当前状态为 %1").arg(p->status));
        return false;
    }
    if (!m_proposals.setStatus(proposalId,
                               approved ? QStringLiteral("approved")
                                        : QStringLiteral("rejected"),
                               nowString())) {
        setError(error, m_proposals.lastError());
        return false;
    }
    return true;
}

bool EvolutionService::applyApproved(qint64 proposalId, QString *error)
{
    if (error)
        error->clear();
    const auto p = m_proposals.getById(proposalId);
    if (!p) {
        setError(error, QStringLiteral("提案不存在（id=%1）").arg(proposalId));
        return false;
    }
    if (p->status != QStringLiteral("approved")) {
        setError(error, QStringLiteral("只能生效已批准（approved）的提案，当前状态为 %1")
                            .arg(p->status));
        return false;
    }

    // 1. 新版本（2.6.14：父版本链 + 描述）
    Version v;
    v.versionNumber = nextVersionNumber();
    if (const auto latest = m_versions.getLatest())
        v.parentVersion = latest->versionNumber;
    v.description = QStringLiteral("提案 #%1：%2").arg(proposalId).arg(p->reason.left(80));
    v.status = QStringLiteral("stable");
    const qint64 versionId = m_versions.create(v);
    if (versionId == 0) {
        setError(error, m_versions.lastError());
        return false;
    }

    // 2. 变更留痕（2.6.21：结果可反向追溯 提案→版本→依据）
    ChangeLog log;
    log.targetType = p->targetType;
    log.targetId = p->targetId;
    log.beforeSummary = p->currentValue;
    log.afterSummary = p->proposedValue;
    log.reason = p->reason;
    log.proposalId = proposalId;
    log.versionId = versionId;
    if (m_changeLogs.append(log) == 0) {
        setError(error, m_changeLogs.lastError());
        return false;
    }

    // 3. 提案 → applied
    if (!m_proposals.setStatus(proposalId, QStringLiteral("applied"), nowString())) {
        setError(error, m_proposals.lastError());
        return false;
    }
    return true;
}

std::vector<Version> EvolutionService::versions() const
{
    return m_versions.getAll();
}

std::optional<Version> EvolutionService::currentVersion() const
{
    return m_versions.getLatest();
}

QString EvolutionService::nextVersionNumber() const
{
    // v0.1 → v0.2 → ...（MVP 阶段全部为 0.x）
    static const QRegularExpression re(QStringLiteral("^v0\\.(\\d+)$"));
    const auto latest = m_versions.getLatest();
    if (latest) {
        const auto match = re.match(latest->versionNumber);
        if (match.hasMatch())
            return QStringLiteral("v0.%1").arg(match.captured(1).toInt() + 1);
    }
    return QStringLiteral("v0.1");
}

} // namespace PersonOS
