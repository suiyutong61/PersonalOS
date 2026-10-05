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
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(ListItemModel *connectionsModel READ connectionsModel CONSTANT)
    Q_PROPERTY(ListItemModel *backupsModel READ backupsModel CONSTANT)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY themeChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY themeChanged)
    Q_PROPERTY(bool remindersEnabled READ remindersEnabled NOTIFY dataChanged)
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
    // 文献处理工具路径(QSettings 持久化;留空则自动探测)
    Q_INVOKABLE void saveToolPaths(const QString &pdftotext, const QString &tesseract);
    Q_INVOKABLE QString currentPdfToTextPath();
    Q_INVOKABLE QString currentTesseractPath();
    Q_INVOKABLE void createBackup(const QString &targetPath);
    // 备份校验(SHA-256 对比;结果经 notice 展示)
    Q_INVOKABLE void verifyBackup(const QString &uid);
    // 备份恢复(整体切换:关闭连接→恢复前快照→换入→重开迁移→失败回退;
    // 成功后在 UI 线程执行启动引导+缓存失效并补记状态)
    Q_INVOKABLE void restoreBackup(const QString &uid);
    // 重建本地向量索引（后台：清空当前模型向量 → 全量回填 → 立即重种领域行）
    Q_INVOKABLE void rebuildVectorIndex();
    // 整体提醒开关（QSettings 持久化；关闭时周期扫描保持静默，规则保留）
    Q_INVOKABLE void setRemindersEnabled(bool enabled);

    QString pageState() const { return m_pageState; }
    QString lastError() const { return m_lastError; }
    QString notice() const { return m_notice; }
    ListItemModel *connectionsModel() { return &m_connectionsModel; }
    ListItemModel *backupsModel() { return &m_backupsModel; }

    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool value);
    bool reducedMotion() const { return m_reducedMotion; }
    void setReducedMotion(bool value);
    bool remindersEnabled() const { return m_remindersEnabled; }

    QString editingUid() const { return m_editingUid; }
    QString editingName() const { return m_editingName; }
    QString editingEndpoint() const { return m_editingEndpoint; }
    QString editingModel() const { return m_editingModel; }

signals:
    void pageStateChanged();
    void lastErrorChanged();
    void noticeChanged();
    void themeChanged();
    void dataChanged();
    void editingChanged();

private:
    void setState(const QString &state);
    void setError(const QString &message);
    void setNotice(const QString &message);

    QString m_pageState = QStringLiteral("idle");
    QString m_lastError;
    QString m_notice;
    bool m_darkMode = false;
    bool m_reducedMotion = false;
    bool m_remindersEnabled = true;
    QString m_editingUid;
    QString m_editingName;
    QString m_editingEndpoint;
    QString m_editingModel;
    ListItemModel m_connectionsModel;
    ListItemModel m_backupsModel;
};

} // namespace PersonOS
