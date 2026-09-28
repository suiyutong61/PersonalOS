#pragma once

#include <QSqlDatabase>

#include "application/ports/StateRepository.h"
#include "domain/foundation/Clock.h"

namespace PersonOS::Infrastructure {

class SqlStateRepository final : public Application::StateRepository
{
public:
    explicit SqlStateRepository(QSqlDatabase database, const Domain::Clock &clock);

    std::optional<Domain::StateDefinition> findDefinitionByCode(
        const std::string &code) override;
    Application::SaveResult insertDefinition(const Domain::StateDefinition &definition) override;
    std::vector<Domain::StateDefinition> allDefinitions() override;

    Application::SaveResult appendEvent(const Domain::StateEvent &event) override;
    bool existsIdempotencyKey(const std::string &key) override;
    std::optional<Domain::StateEvent> latestValid(const Domain::Uid &userId,
                                                  const std::string &definitionCode,
                                                  const std::string &nowIso) override;
    std::vector<Application::RecentStateEvent> recentEvents(const Domain::Uid &userId,
                                                           int limit) override;
    std::optional<std::string> preference(const Domain::Uid &userId,
                                          const std::string &key) override;
    Application::SaveResult setPreference(const Domain::Uid &userId,
                                          const std::string &key,
                                          const std::string &valueJson) override;

private:
    Application::SaveResult writeFailure(const char *operation,
                                         const class QSqlQuery &query) const;

    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
