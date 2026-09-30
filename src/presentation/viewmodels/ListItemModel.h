#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>

namespace PersonOS {

// 通用列表模型（DD-001 §11：QAbstractListModel；小数据量单用户场景统一角色）
// 角色：uid / title / subtitle / detail / badge / badgeTone / payload
class ListItemModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Role {
        UidRole = Qt::UserRole + 1,
        TitleRole,
        SubtitleRole,
        DetailRole,
        BadgeRole,
        BadgeToneRole,
        PayloadRole,
        RetractableRole,
        ValueRole,       // 数值角色(问卷作答值等;默认 0)
        IsDefaultRole,   // 布尔角色(默认连接徽标等;默认 false)
        KindRole,        // 原始类型键(过滤/门控用;展示层另用中文标签)
    };

    explicit ListItemModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // QML 侧直接读取条目数（页面状态视图 contentCount 用）
    int count() const { return rowCount(); }
    Q_SIGNAL void countChanged();

    // 全量替换（页面刷新用）；空列表 → 调用方置 pageState=empty
    void replace(const QVariantList &rows);
    void clear();

private:
    QVariantList m_rows;
};

} // namespace PersonOS
