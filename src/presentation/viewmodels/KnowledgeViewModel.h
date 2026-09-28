#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <vector>

#include "domain/knowledge/Knowledge.h"
#include "presentation/viewmodels/ListItemModel.h"

// 知识库页 ViewModel（DD-001 §11；五库浏览只读投影 + 用户导入命令）
// 五库共享骨架：论文/方案/方法/贴士/协议以类型徽标区分；来源与版本可追溯。
namespace PersonOS {

class KnowledgeViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pageState READ pageState NOTIFY pageStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(ListItemModel *itemsModel READ itemsModel CONSTANT)
    QML_ELEMENT

public:
    explicit KnowledgeViewModel(QObject *parent = nullptr);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void search(const QString &text);
    // 手动添加一条方法/贴士（用户实践方法进入方法库前的最低形式）
    Q_INVOKABLE void addItem(const QString &kind, const QString &title, const QString &summary);

    QString pageState() const { return m_pageState; }
    QString lastError() const { return m_lastError; }
    ListItemModel *itemsModel() { return &m_itemsModel; }

signals:
    void pageStateChanged();
    void lastErrorChanged();
    void dataChanged();

private:
    void setState(const QString &state);
    void setError(const QString &message);
    void showItems(const std::vector<Domain::KnowledgeItem> &items);

    QString m_pageState = QStringLiteral("idle");
    QString m_lastError;
    ListItemModel m_itemsModel;
};

} // namespace PersonOS
