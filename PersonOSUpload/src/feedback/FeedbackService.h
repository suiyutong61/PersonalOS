#pragma once

#include <vector>

#include <QHash>
#include <QString>

#include "database/EventRepository.h"
#include "models/Event.h"

namespace PersonOS {

// Feedback 子系统（README 2.4.6 / 3.3.5）
// 职责：现实事件流的记录、查询与轻量聚合。
// 数据流（2.4.6.2）：Execution → Event → 本层查询/聚合 → Review 使用。
class FeedbackService
{
public:
    qint64 recordEvent(const Event &e, QString *error = nullptr);

    std::vector<Event> eventsOfDay(const QString &date) const;          // YYYY-MM-DD
    std::vector<Event> eventsOfRange(const QString &from, const QString &to) const; // 含端点
    QHash<QString, int> eventCountsByType(const QString &date) const;   // 轻量聚合

    // 修正事件（仅 type/title/description），必须给原因；留痕流程见 EventRepository::correct
    bool correctEvent(qint64 id, const Event &fix, const QString &reason,
                      QString *error = nullptr);

private:
    bool validate(const Event &e, QString *error) const;

    EventRepository m_events;
};

} // namespace PersonOS
