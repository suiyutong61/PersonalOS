#pragma once

#include <QSqlDatabase>
#include <QString>

#include <optional>
#include <vector>

// 旧目标只读投影（DD-001 §2.1：旧代码迁移期兼容；数据库设计 §7 映射原则）
// 只读旧 goals 表供迁移报告与新旧投影核对使用；绝不写入。
namespace PersonOS::Infrastructure {

struct LegacyGoal
{
    qint64 id = 0;
    qint64 parentId = 0;
    QString level;    // 旧枚举（vision/long_term/...）；新模型不再使用该层级枚举
    QString title;
    QString status;
    int priority = 50;
};

class LegacyGoalReader
{
public:
    explicit LegacyGoalReader(QSqlDatabase database);

    std::vector<LegacyGoal> readAll();
    std::optional<LegacyGoal> readById(qint64 id);

private:
    QSqlDatabase m_database;
};

} // namespace PersonOS::Infrastructure
