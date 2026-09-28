#pragma once

#include <QSqlDatabase>

#include "application/ports/GoalRepository.h"
#include "domain/foundation/Clock.h"

// SQLite 目标仓储（数据库设计 §3.3 goals_v3；DD-001 §5.3）
// 更新使用 WHERE uid=? AND revision=? 乐观并发；连接由调用线程拥有。
namespace PersonOS::Infrastructure {

class SqlGoalRepository final : public Application::GoalRepository
{
public:
    explicit SqlGoalRepository(QSqlDatabase database, const Domain::Clock &clock);

    std::optional<Domain::Goal> findByUid(const Domain::Uid &uid) override;
    std::vector<Domain::Goal> findByUser(const Domain::Uid &userId) override;
    std::vector<Domain::Goal> findChildren(const Domain::Uid &parentGoalId) override;
    Application::SaveResult insert(const Domain::Goal &goal) override;
    Application::SaveResult update(const Domain::Goal &goal, int expectedRevision) override;

private:
    std::optional<Domain::Goal> fromQuery(class QSqlQuery &query) const;
    // SQL 错误挂在 QSqlQuery 上（QSqlDatabase::lastError 不随查询更新）
    Application::SaveResult writeFailure(const char *operation, const class QSqlQuery &query) const;

    QSqlDatabase m_database;
    const Domain::Clock &m_clock;
};

} // namespace PersonOS::Infrastructure
