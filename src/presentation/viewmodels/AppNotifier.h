#pragma once

#include <QObject>
#include <QString>

#include "application/ports/OperationPorts.h"

// 应用内全局提醒横幅单例（R3.3.1 应用内回退通道；DR-024）。
// QML 经 qmlRegisterSingletonInstance（main.cpp 与 tst_qml_smoke 装配）
// 以 "AppNotifier" 类型名使用；同时实现 NotificationPort——
// deliver() 即显示横幅。sequence 单调递增，供 QML 检测新提醒。
namespace PersonOS::Presentation {

class AppNotifier final : public QObject, public Application::NotificationPort
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString tone READ tone NOTIFY changed)
    Q_PROPERTY(int sequence READ sequence NOTIFY changed)

public:
    static AppNotifier &instance();

    Q_INVOKABLE void show(const QString &text, const QString &tone);
    Q_INVOKABLE void dismiss();   // 用户手动关闭（仅隐藏横幅，不触碰投递记录）

    bool deliver(const std::string &title, const std::string &body) override;

    QString text() const { return m_text; }
    QString tone() const { return m_tone; }
    int sequence() const { return m_sequence; }

signals:
    void changed();

private:
    AppNotifier() = default;

    QString m_text;
    QString m_tone = QStringLiteral("warning");
    int m_sequence = 0;
};

} // namespace PersonOS::Presentation
