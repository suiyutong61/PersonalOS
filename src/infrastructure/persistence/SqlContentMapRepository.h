#pragma once

#include <QSqlDatabase>

#include "application/ports/ContentMapRepository.h"
#include "domain/foundation/Clock.h"

namespace PersonOS::Infrastructure {

class SqlContentMapRepository final : public Application::ContentMapRepository
{
public:
    explicit SqlContentMapRepository(QSqlDatabase database, const Domain::Clock &clock);

    std::optional<Domain::ContentMap> findMapByUid(const Domain::Uid &uid) override;
    std::vector<Domain::ContentMap> mapsOfGoal(const Domain::Uid &goalId) override;
    Application::SaveResult insertMap(const Domain::ContentMap &map) override;
    Application::SaveResult updateMap(const Domain::ContentMap &map,
                                      int expectedRevision) override;

    std::optional<Domain::ContentNode> findNodeByUid(const Domain::Uid &uid) override;
    std::vector<Domain::ContentNode> nodesOfMap(const Domain::Uid &mapId) override;
    Application::SaveResult insertNode(const Domain::ContentNode &node) override;

    Application::SaveResult upsertProgress(const Domain::ContentProgress &progress) override;
    std::vector<Domain::ContentProgressRow> progressRowsOfMap(
        const Domain::Uid &mapId, const Domain::Uid &userId) override;

    Application::SaveResult appendProgressEvent(const Domain::Uid &eventUid,
                                                const Domain::Uid &userId,
                                                const Domain::Uid &goalId,
                                                const Domain::Uid &nodeUid,
                                                const std::string &nodeTitle,
                                                double amount,
                                                const std::string &idempotencyKey) override;

private:
    Application::SaveResult writeFailure(const char *operation,
                                         const class QSqlQuery &query) const;

    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
