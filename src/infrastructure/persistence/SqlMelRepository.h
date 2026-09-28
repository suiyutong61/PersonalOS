#pragma once

#include <QSqlDatabase>

#include "application/ports/MelRepository.h"
#include "domain/foundation/Clock.h"

// SQLite MEL 仓储（数据库设计 §4.1）
// 转移与进度事件幂等键由数据库 UNIQUE 兜底；更新走 revision 乐观并发。
namespace PersonOS::Infrastructure {

class SqlMelRepository final : public Application::MelRepository
{
public:
    explicit SqlMelRepository(QSqlDatabase database, const Domain::Clock &clock);

    std::optional<Domain::Mel> findByUid(const Domain::Uid &uid) override;
    Application::SaveResult insert(const Domain::Mel &mel) override;
    Application::SaveResult update(const Domain::Mel &mel, int expectedRevision) override;

    Application::SaveResult insertTask(const Domain::MelTask &task) override;
    Application::SaveResult updateTask(const Domain::MelTask &task, int expectedRevision) override;
    std::vector<Domain::MelTask> tasksOf(const Domain::Uid &melId) override;

    Application::SaveResult appendTransition(const Domain::MelTransition &transition) override;
    Application::SaveResult appendProgressEvent(const Domain::Mel &mel,
                                                const Domain::MelTask &task, double amount,
                                                const std::string &note,
                                                const std::string &idempotencyKey) override;
    bool existsIdempotencyKey(const std::string &key) override;
    bool hasTransition(const Domain::Uid &melId, const std::string &trigger) override;

    std::vector<Domain::Mel> findDue(const std::string &nowIso, int limit) override;
    std::optional<std::string> lastProgressAtIso(const Domain::Uid &melId) override;
    int actualMinutesOf(const Domain::Uid &melId) override;
    Application::SaveResult insertTaskMethod(const Domain::MelTaskMethod &method) override;
    std::vector<Domain::MelTaskMethod> taskMethodsOf(const Domain::Uid &melId) override;
    Application::SaveResult insertPrediction(const Domain::MelPrediction &prediction) override;
    std::optional<Domain::MelPrediction> latestPredictionOf(const Domain::Uid &melId) override;
    std::vector<Domain::Mel> findActive(const Domain::Uid &userId, int limit) override;

private:
    Application::SaveResult writeFailure(const char *operation, const class QSqlQuery &query) const;
    qint64 resolveMelPk(const Domain::Uid &melId, bool *found = nullptr) const;
    qint64 resolveUserId(const Domain::Uid &userId, bool *found = nullptr) const;
    std::optional<qint64> resolvePk(const char *sql, const Domain::Uid &uid) const;

    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
