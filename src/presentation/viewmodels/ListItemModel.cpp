#include "presentation/viewmodels/ListItemModel.h"

namespace PersonOS {

ListItemModel::ListItemModel(QObject *parent) : QAbstractListModel(parent) {}

int ListItemModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant ListItemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size()))
        return {};
    const QVariantMap &row = m_rows.at(index.row()).toMap();
    switch (role) {
    case UidRole: return row.value(QStringLiteral("uid"));
    case TitleRole: return row.value(QStringLiteral("title"));
    case SubtitleRole: return row.value(QStringLiteral("subtitle"));
    case DetailRole: return row.value(QStringLiteral("detail"));
    case BadgeRole: return row.value(QStringLiteral("badge"));
    case BadgeToneRole: return row.value(QStringLiteral("badgeTone"));
    case PayloadRole: return row.value(QStringLiteral("payload"));
    case RetractableRole: return row.value(QStringLiteral("retractable"), false);
    case ValueRole: return row.value(QStringLiteral("value"), 0);
    case IsDefaultRole: return row.value(QStringLiteral("isDefault"), false);
    case KindRole: return row.value(QStringLiteral("kind"), QString());
    default: return {};
    }
}

QHash<int, QByteArray> ListItemModel::roleNames() const
{
    return {{UidRole, "uid"},
            {TitleRole, "title"},
            {SubtitleRole, "subtitle"},
            {DetailRole, "detail"},
            {BadgeRole, "badge"},
            {BadgeToneRole, "badgeTone"},
            {PayloadRole, "payload"},
            {RetractableRole, "retractable"},
            {ValueRole, "value"},
            {IsDefaultRole, "isDefault"},
            {KindRole, "kind"}};
}

void ListItemModel::replace(const QVariantList &rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
    emit countChanged();
}

void ListItemModel::clear()
{
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit countChanged();
}

} // namespace PersonOS
