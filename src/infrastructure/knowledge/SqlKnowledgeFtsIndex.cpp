#include "infrastructure/knowledge/SqlKnowledgeFtsIndex.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "infrastructure/knowledge/CjkTokenize.h"

namespace PersonOS::Infrastructure {

SqlKnowledgeFtsIndex::SqlKnowledgeFtsIndex(QSqlDatabase database, const Domain::Clock &clock)
    : m_database(std::move(database)), m_clock(clock)
{}

Application::SaveResult SqlKnowledgeFtsIndex::indexVersion(
    const std::string &ownerType, const std::string &ownerUid, const std::string &title,
    const std::string &summary, const std::string &claims, const std::string &applicability,
    const std::string &limitations, const std::string &steps)
{
    // 覆盖式更新：先删除该 owner 的旧索引行
    QSqlQuery remove(m_database);
    remove.prepare(QStringLiteral(
        "DELETE FROM knowledge_fts_v6 WHERE owner_type=? AND owner_uid=?"));
    remove.addBindValue(QString::fromStdString(ownerType));
    remove.addBindValue(QString::fromStdString(ownerUid));
    if (!remove.exec())
        return {false, false,
                {Application::ErrorCode::Storage, "fts delete failed",
                 remove.lastError().text().toStdString(), false}};

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "INSERT INTO knowledge_fts_v6(owner_type, owner_uid, title, summary, claims, "
        "applicability, limitations, steps) VALUES(?,?,?,?,?,?,?,?)"));
    query.addBindValue(QString::fromStdString(ownerType));
    query.addBindValue(QString::fromStdString(ownerUid));
    // CJK 二元扩展（索引与查询两侧同一规则；unicode61 不切分中文）
    query.addBindValue(QString::fromStdString(Cjk::expandForIndex(title)));
    query.addBindValue(QString::fromStdString(Cjk::expandForIndex(summary)));
    query.addBindValue(QString::fromStdString(Cjk::expandForIndex(claims)));
    query.addBindValue(QString::fromStdString(Cjk::expandForIndex(applicability)));
    query.addBindValue(QString::fromStdString(Cjk::expandForIndex(limitations)));
    query.addBindValue(QString::fromStdString(Cjk::expandForIndex(steps)));
    if (!query.exec())
        return {false, false,
                {Application::ErrorCode::Storage, "fts insert failed",
                 query.lastError().text().toStdString(), false}};
    return {true, false, {}};
}

std::vector<Application::SearchHit> SqlKnowledgeFtsIndex::search(const std::string &query,
                                                                 int limit)
{
    std::vector<Application::SearchHit> out;
    QSqlQuery q(m_database);
    q.prepare(QStringLiteral(
        "SELECT owner_type, owner_uid, title, summary FROM knowledge_fts_v6 "
        "WHERE knowledge_fts_v6 MATCH ? ORDER BY rank LIMIT ?"));
    q.addBindValue(QString::fromStdString(Cjk::expandForQuery(query)));
    q.addBindValue(limit);
    if (!q.exec())
        return out;
    while (q.next()) {
        Application::SearchHit hit;
        hit.ownerType = q.value(0).toString().toStdString();
        hit.ownerUid = q.value(1).toString().toStdString();
        hit.title = q.value(2).toString().toStdString();
        hit.summary = q.value(3).toString().toStdString();
        out.push_back(std::move(hit));
    }
    return out;
}

Application::SaveResult SqlKnowledgeFtsIndex::rebuildAll()
{
    // 全量重建：从当前知识版本 + 方法步骤派生（向量/全文索引均为可重建数据）
    QSqlQuery clear(m_database);
    if (!clear.exec(QStringLiteral("DELETE FROM knowledge_fts_v6")))
        return {false, false,
                {Application::ErrorCode::Storage, "fts clear failed",
                 clear.lastError().text().toStdString(), false}};

    QSqlQuery versions(m_database);
    if (!versions.exec(QStringLiteral(
            "SELECT i.uid, i.library_type, i.title, v.summary, v.claims_json, "
            "v.applicability_json, v.limitations_json "
            "FROM knowledge_items_v5 i "
            "JOIN knowledge_versions_v5 v ON v.uid = i.current_version_uid "
            "WHERE i.status IN ('active','warned','candidate')")))
        return {false, false,
                {Application::ErrorCode::Storage, "fts rebuild select failed",
                 versions.lastError().text().toStdString(), false}};
    while (versions.next()) {
        const std::string itemUid = versions.value(0).toString().toStdString();
        const std::string type = versions.value(1).toString().toStdString();
        const std::string title = versions.value(2).toString().toStdString();
        const std::string summary = versions.value(3).toString().toStdString();
        const std::string claims = versions.value(4).toString().toStdString();
        const std::string applicability = versions.value(5).toString().toStdString();
        const std::string limitations = versions.value(6).toString().toStdString();
        const auto saved =
            indexVersion(type, itemUid, title, summary, claims, applicability, limitations, "");
        if (!saved.ok)
            return saved;
    }
    return {true, false, {}};
}

bool SqlKnowledgeFtsIndex::isConsistent()
{
    QSqlQuery count(m_database);
    if (!count.exec(QStringLiteral("SELECT COUNT(*) FROM knowledge_fts_v6")) || !count.next())
        return false;
    const qint64 ftsCount = count.value(0).toLongLong();
    QSqlQuery versionCount(m_database);
    if (!versionCount.exec(QStringLiteral(
            "SELECT COUNT(*) FROM knowledge_items_v5 WHERE status IN "
            "('active','warned','candidate')"))
        || !versionCount.next())
        return false;
    return ftsCount == versionCount.value(0).toLongLong();
}

} // namespace PersonOS::Infrastructure
