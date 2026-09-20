#include "services/Items.h"

#include <QHash>
#include <QStringList>

namespace PersonOS {

namespace {

const QHash<QString, QString> kStatusText = {
    {QStringLiteral("planned"), QStringLiteral("待开始")},
    {QStringLiteral("started"), QStringLiteral("进行中")},
    {QStringLiteral("completed"), QStringLiteral("已完成")},
    {QStringLiteral("partial"), QStringLiteral("部分完成")},
    {QStringLiteral("delayed"), QStringLiteral("已延期")},
    {QStringLiteral("skipped"), QStringLiteral("已跳过")},
    {QStringLiteral("cancelled"), QStringLiteral("已取消")},
    {QStringLiteral("interrupted"), QStringLiteral("已中断")},
};

const QHash<QString, QString> kLevelText = {
    {QStringLiteral("vision"), QStringLiteral("愿景")},
    {QStringLiteral("long_term"), QStringLiteral("长期")},
    {QStringLiteral("annual"), QStringLiteral("年度")},
    {QStringLiteral("quarterly"), QStringLiteral("季度")},
    {QStringLiteral("monthly"), QStringLiteral("月度")},
    {QStringLiteral("weekly"), QStringLiteral("本周")},
};

} // namespace

TaskItem::TaskItem(const Task &t, QObject *parent)
    : QObject(parent)
    , m_id(t.id)
    , m_title(t.title)
    , m_status(t.status)
    , m_statusText(kStatusText.value(t.status, t.status))
    , m_done(t.isDone())
{
    QStringList parts;
    if (t.plannedMinutes)
        parts << QStringLiteral("计划 %1 分钟").arg(*t.plannedMinutes);
    if (t.actualMinutes)
        parts << QStringLiteral("实际 %1 分钟").arg(*t.actualMinutes);
    m_planText = parts.join(QStringLiteral(" · "));
}

} // namespace PersonOS
