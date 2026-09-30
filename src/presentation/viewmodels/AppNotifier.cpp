#include "presentation/viewmodels/AppNotifier.h"

namespace PersonOS::Presentation {

AppNotifier &AppNotifier::instance()
{
    static AppNotifier notifier;
    return notifier;
}

void AppNotifier::show(const QString &text, const QString &tone)
{
    m_text = text;
    m_tone = tone;
    ++m_sequence;
    emit changed();
}

void AppNotifier::dismiss()
{
    m_text.clear();
    emit changed();
}

bool AppNotifier::deliver(const std::string &title, const std::string &body)
{
    show(QString::fromStdString(title) + QStringLiteral("：")
             + QString::fromStdString(body),
         QStringLiteral("warning"));
    // 应用内横幅为 v1 唯一通道（用户决策）；Windows 系统通知适配器留待后续
    return true;
}

} // namespace PersonOS::Presentation
