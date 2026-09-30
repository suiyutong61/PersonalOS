#pragma once

#include <QSqlDatabase>

#include "application/ports/SearchIndexPort.h"
#include "domain/foundation/Clock.h"

// FTS5 索引服务（数据库设计 §6：同步、全量重建、一致性检查）
namespace PersonOS::Infrastructure {

class SqlKnowledgeFtsIndex final : public Application::SearchIndexPort
{
public:
    explicit SqlKnowledgeFtsIndex(QSqlDatabase database, const Domain::Clock &clock);

    Application::SaveResult indexVersion(const std::string &ownerType,
                                         const std::string &ownerUid,
                                         const std::string &title,
                                         const std::string &summary,
                                         const std::string &claims,
                                         const std::string &applicability,
                                         const std::string &limitations,
                                         const std::string &steps) override;
    std::vector<Application::SearchHit> search(const std::string &query, int limit) override;
    Application::SaveResult rebuildAll() override;
    bool isConsistent() override;
    Application::SaveResult removeOwner(const std::string &ownerUid) override;

private:
    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
