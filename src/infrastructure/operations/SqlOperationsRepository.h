#pragma once

#include <QSqlDatabase>

#include "application/ports/OperationPorts.h"
#include "domain/foundation/Clock.h"

namespace PersonOS::Infrastructure {

class SqlOperationsRepository final : public Application::ReminderRepositoryPort,
                                      public Application::AchievementRepositoryPort,
                                      public Application::BackupRepositoryPort
{
public:
    explicit SqlOperationsRepository(QSqlDatabase database, const Domain::Clock &clock);

    // 日历事件
    Application::SaveResult insertEvent(const Domain::CalendarEvent &event);
    std::optional<Domain::CalendarEvent> findEvent(const Domain::Uid &uid);

    // 提醒
    std::optional<Domain::ReminderRule> findRule(const Domain::Uid &uid) override;
    Application::SaveResult insertRule(const Domain::ReminderRule &rule) override;
    Application::SaveResult updateRule(const Domain::ReminderRule &rule,
                                       int expectedRevision) override;
    std::vector<Domain::ReminderRule> enabledRules() override;
    Application::SaveResult insertDelivery(const Domain::ReminderDelivery &delivery) override;
    bool existsDeliveryKey(const std::string &idempotencyKey) override;
    std::vector<Domain::ReminderDelivery> pendingDeliveries(const std::string &nowIso) override;
    Application::SaveResult markDelivery(const Domain::Uid &uid, const std::string &status,
                                         const std::optional<std::string> &error) override;

    // 成就
    Application::SaveResult insert(const Domain::Achievement &achievement) override;
    std::vector<Domain::Achievement> listForUser(const Domain::Uid &userId,
                                                 int limit) override;
    bool exists(const Domain::Uid &userId, const std::string &type,
                const std::string &sourceType, const std::string &sourceUid) override;

    // 备份
    std::optional<Domain::BackupRecord> find(const Domain::Uid &uid) override;
    std::vector<Domain::BackupRecord> list(int limit) override;
    Application::SaveResult insert(const Domain::BackupRecord &record) override;
    Application::SaveResult update(const Domain::BackupRecord &record) override;

private:
    Application::SaveResult writeFailure(const char *operation,
                                         const class QSqlQuery &query) const;
    std::optional<qint64> resolvePk(const char *sql, const std::string &uid) const;

    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
