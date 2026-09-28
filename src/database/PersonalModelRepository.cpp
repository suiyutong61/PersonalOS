#include "database/PersonalModelRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include "database/RepoUtil.h"

namespace PersonOS {

namespace {

const QString kColumns = QStringLiteral(
    "key, value, value_text, prior_source, evidence_ref, updated_at");

PersonalModelParam paramFromQuery(const QSqlQuery &q)
{
    PersonalModelParam p;
    p.key = q.value(QStringLiteral("key")).toString();
    const QVariant v = q.value(QStringLiteral("value"));
    if (!v.isNull())
        p.value = v.toDouble();
    p.valueText = q.value(QStringLiteral("value_text")).toString();
    p.priorSource = q.value(QStringLiteral("prior_source")).toString();
    p.evidenceRef = q.value(QStringLiteral("evidence_ref")).toString();
    p.updatedAt = q.value(QStringLiteral("updated_at")).toString();
    return p;
}

} // namespace

void PersonalModelRepository::fail(const QString &context, const QString &message) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, message);
}

std::optional<PersonalModelParam> PersonalModelRepository::get(const QString &key) const
{
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM personal_model_params WHERE key=?").arg(kColumns), {key});
    if (!q.exec()) {
        fail(QStringLiteral("get"), q.lastError().text());
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;
    return paramFromQuery(q);
}

std::vector<PersonalModelParam> PersonalModelRepository::getAll() const
{
    std::vector<PersonalModelParam> out;
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM personal_model_params ORDER BY key").arg(kColumns));
    if (!q.exec()) {
        fail(QStringLiteral("getAll"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.push_back(paramFromQuery(q));
    return out;
}

bool PersonalModelRepository::upsert(const PersonalModelParam &p)
{
    auto q = RepoUtil::query(
        QStringLiteral(
            "INSERT INTO personal_model_params(key, value, value_text, prior_source, evidence_ref) "
            "VALUES (?,?,?,?,?) "
            "ON CONFLICT(key) DO UPDATE SET "
            "value=excluded.value, value_text=excluded.value_text, "
            "prior_source=excluded.prior_source, evidence_ref=excluded.evidence_ref, "
            "updated_at=datetime('now','localtime')"),
        {p.key, RepoUtil::nullableReal(p.value), RepoUtil::nullableText(p.valueText),
         p.priorSource, RepoUtil::nullableText(p.evidenceRef)});
    if (!q.exec()) {
        fail(QStringLiteral("upsert"), q.lastError().text());
        return false;
    }
    return true;
}

} // namespace PersonOS
