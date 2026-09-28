#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "presentation/viewmodels/ListItemModel.h"

// 设置页 ViewModel（DD-001 §11；DR-025/026）：模型连接、备份与主题控制。
// 凭据只显示引用/掩码；备份包含数据库一致性快照与 SHA-256 校验记录。
namespace PersonOS {

class SettingsViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString pageState READ pageState NOTIFY pageStateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(ListItemModel *connectionsModel READ connectionsModel CONSTANT)
    Q_PROPERTY(ListItemModel *backupsModel READ backupsModel CONSTANT)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY themeChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY themeChanged)
    Q_PROPERTY(QString editingUid READ editingUid NOTIFY editingChanged)
    Q_PROPERTY(QString editingName READ editingName NOTIFY editingChanged)
    Q_PROPERTY(QString editingEndpoint READ editingEndpoint NOTIFY editingChanged)
    Q_PROPERTY(QString editingModel READ editingModel NOTIFY editingChanged)
    QML_ELEMENT

public:
    explicit SettingsViewModel(QObject *parent = nullptr);

    Q_INVOKABLE void refresh();
    // 添加/更新模型连接（凭据只保存系统凭据库引用，不落 SQLite/日志）
    Q_INVOKABLE void saveConnection(const QString &displayName, const QString &endpoint,
                                    const QString &model, const QString &apiKey);
    Q_INVOKABLE void setConnectionEnabled(const QString &uid, bool enabled);
    Q_INVOKABLE void testConnection(const QString &uid);
    Q_INVOKABLE void removeConnection(const QString &uid);
    Q_INVOKABLE void startEdit(const QString &uid);
    Q_INVOKABLE void cancelEdit();
    Q_INVOKABLE void setDefaultConnection(const QString &uid);
    Q_INVOKABLE void createBackup(const QString &targetPath);

    QString pageState() const { return m_pageState; }
    QString lastError() const { return m_lastError; }
    ListItemModel *connectionsModel() { return &m_connectionsModel; }
    ListItemModel *backupsModel() { return &m_backupsModel; }

    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool value);
    bool reducedMotion() const { return m_reducedMotion; }
    void setReducedMotion(bool value);

    QString editingUid() const { return m_editingUid; }
    QString editingName() const { return m_editingName; }
    QString editingEndpoint() const { return m_editingEndpoint; }
    QString editingModel() const { return m_editingModel; }

signals:
    void pageStateChanged();
    void lastErrorChanged();
    void themeChanged();
    void dataChanged();
    void editingChanged();

private:
    void setState(const QString &state);
    void setError(const QString &message);

    QString m_pageState = QStringLiteral("idle");
    QString m_lastError;
    bool m_darkMode = false;
    bool m_reducedMotion = false;
    QString m_editingUid;
    QString m_editingName;
    QString m_editingEndpoint;
    QString m_editingModel;
    ListItemModel m_connectionsModel;
    ListItemModel m_backupsModel;
};

} // namespace PersonOS
