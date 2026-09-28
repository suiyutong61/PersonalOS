#include "infrastructure/persistence/LegacyGoalReader.h"

#include <QSqlQuery>

namespace PersonOS::Infrastructure {

LegacyGoalReader::LegacyGoalReader(QSqlDatabase database) : m_database(std::move(database)) {}

std::optional<LegacyGoal> LegacyGoalReader::readById(qint64 id)
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, parent_id, level, title, status, priority FROM goals WHERE id=?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next())
        return std::nullopt;
    LegacyGoal goal;
    goal.id = query.value(0).toLongLong();
    goal.parentId = query.value(1).toLongLong(); // NULL → 0
    goal.level = query.value(2).toString();
    goal.title = query.value(3).toString();
    goal.status = query.value(4).toString();
    goal.priority = query.value(5).toInt();
    return goal;
}

std::vector<LegacyGoal> LegacyGoalReader::readAll()
{
    std::vector<LegacyGoal> out;
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral(
            "SELECT id, parent_id, level, title, status, priority FROM goals ORDER BY id")))
        return out;
    while (query.next()) {
        LegacyGoal goal;
        goal.id = query.value(0).toLongLong();
        goal.parentId = query.value(1).toLongLong();
        goal.level = query.value(2).toString();
        goal.title = query.value(3).toString();
        goal.status = query.value(4).toString();
        goal.priority = query.value(5).toInt();
        out.push_back(std::move(goal));
    }
    return out;
}

} // namespace PersonOS::Infrastructure
