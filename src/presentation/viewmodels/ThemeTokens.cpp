#include "presentation/viewmodels/ThemeTokens.h"

namespace PersonOS {

ThemeTokens::ThemeTokens(QObject *parent) : QObject(parent) {}

void ThemeTokens::setDarkMode(bool value)
{
    if (m_darkMode == value)
        return;
    m_darkMode = value;
    emit themeChanged();
}

void ThemeTokens::setReducedMotion(bool value)
{
    if (m_reducedMotion == value)
        return;
    m_reducedMotion = value;
    emit themeChanged();
}

} // namespace PersonOS
