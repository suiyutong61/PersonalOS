#include "database/CalibrationRepository.h"

#include <QSqlError>
#include <QSqlQuery>

#include "database/RepoUtil.h"

namespace PersonOS {

namespace {

const QString kColumns = QStringLiteral(
    "id, param_key, old_value, new_value, data_window, rule, trigger, created_at");

CalibrationRecord recordFromQuery(const QSqlQuery &q)
{
    CalibrationRecord r;
    r.id = q.value(QStringLiteral("id")).toLongLong();
    r.paramKey = q.value(QStringLiteral("param_key")).toString();
    r.oldValue = q.value(QStringLiteral("old_value")).toString();
    r.newValue = q.value(QStringLiteral("new_value")).toString();
    r.dataWindow = q.value(QStringLiteral("data_window")).toString();
    r.rule = q.value(QStringLiteral("rule")).toString();
    r.trigger = q.value(QStringLiteral("trigger")).toString();
    r.createdAt = q.value(QStringLiteral("created_at")).toString();
    return r;
}

} // namespace

void CalibrationRepository::fail(const QString &context, const QString &message) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, message);
}

qint64 CalibrationRepository::append(const CalibrationRecord &r)
{
    auto q = RepoUtil::query(
        QStringLiteral(
            "INSERT INTO calibration_records(param_key, old_value, new_value, data_window, "
            "rule, trigger) VALUES (?,?,?,?,?,?)"),
        {r.paramKey, RepoUtil::nullableText(r.oldValue), RepoUtil::nullableText(r.newValue),
         r.dataWindow, r.rule, r.trigger});
    if (!q.exec()) {
        fail(QStringLiteral("append"), q.lastError().text());
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

std::vector<CalibrationRecord> CalibrationRepository::getByParam(const QString &key) const
{
    std::vector<CalibrationRecord> out;
    auto q = RepoUtil::query(
        QStringLiteral("SELECT %1 FROM calibration_records WHERE param_key=? ORDER BY id")
            .arg(kColumns),
        {key});
    if (!q.exec()) {
        fail(QStringLiteral("getByParam"), q.lastError().text());
        return out;
    }
    while (q.next())
        out.push_back(recordFromQuery(q));
    return out;
}

} // namespace PersonOS
