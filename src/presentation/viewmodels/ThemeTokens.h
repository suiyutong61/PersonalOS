#pragma once

#include <QColor>
#include <QObject>
#include <QtQml/qqmlregistration.h>

// 设计系统令牌（DR-031 / requirements 10.8）——C++ 单例（QML_SINGLETON）
// 页面不得自行散落颜色和尺寸常量；属性绑定支持深浅色切换与减少动效。
// 无障碍：正文与背景对比度 ≥ 4.5:1、大号文字 ≥ 3:1（WCAG 2.2）；
// 颜色不是唯一信息表达方式（配合图标/文字/状态徽标）。
namespace PersonOS {

class ThemeTokens : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY themeChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion
                   NOTIFY themeChanged)
    Q_PROPERTY(QColor bgBase READ bgBase NOTIFY themeChanged)
    Q_PROPERTY(QColor bgCard READ bgCard NOTIFY themeChanged)
    Q_PROPERTY(QColor bgLayer READ bgLayer NOTIFY themeChanged)
    Q_PROPERTY(QColor textPrimary READ textPrimary NOTIFY themeChanged)
    Q_PROPERTY(QColor textSecondary READ textSecondary NOTIFY themeChanged)
    Q_PROPERTY(QColor borderColor READ borderColor NOTIFY themeChanged)
    Q_PROPERTY(QColor colorAccent READ colorAccent CONSTANT)
    Q_PROPERTY(QColor onAccent READ onAccent CONSTANT)
    Q_PROPERTY(QColor colorSuccess READ colorSuccess CONSTANT)
    Q_PROPERTY(QColor colorSuccessText READ colorSuccessText NOTIFY themeChanged)
    Q_PROPERTY(QColor colorWarning READ colorWarning CONSTANT)
    Q_PROPERTY(QColor colorWarningText READ colorWarningText NOTIFY themeChanged)
    Q_PROPERTY(QColor colorError READ colorError CONSTANT)
    Q_PROPERTY(QColor colorDomainLearning READ colorDomainLearning CONSTANT)
    Q_PROPERTY(QColor colorDomainHealth READ colorDomainHealth CONSTANT)
    Q_PROPERTY(QColor colorDomainLife READ colorDomainLife CONSTANT)
    Q_PROPERTY(QColor focusColor READ focusColor NOTIFY themeChanged)
    Q_PROPERTY(int focusWidth READ focusWidth CONSTANT)
    Q_PROPERTY(int fontSizePageTitle READ fontSizePageTitle CONSTANT)
    Q_PROPERTY(int fontSizeSection READ fontSizeSection CONSTANT)
    Q_PROPERTY(int fontSizeBody READ fontSizeBody CONSTANT)
    Q_PROPERTY(int fontSizeCaption READ fontSizeCaption CONSTANT)
    Q_PROPERTY(int fontSizeNumber READ fontSizeNumber CONSTANT)
    Q_PROPERTY(int spacingXs READ spacingXs CONSTANT)
    Q_PROPERTY(int spacingSm READ spacingSm CONSTANT)
    Q_PROPERTY(int spacingMd READ spacingMd CONSTANT)
    Q_PROPERTY(int spacingLg READ spacingLg CONSTANT)
    Q_PROPERTY(int radiusSm READ radiusSm CONSTANT)
    Q_PROPERTY(int radiusMd READ radiusMd CONSTANT)
    Q_PROPERTY(int radiusLg READ radiusLg CONSTANT)
    Q_PROPERTY(qreal elevationCard READ elevationCard CONSTANT)
    Q_PROPERTY(qreal elevationFloat READ elevationFloat CONSTANT)
    Q_PROPERTY(int motionFast READ motionFast NOTIFY themeChanged)
    Q_PROPERTY(int motionNormal READ motionNormal NOTIFY themeChanged)
    Q_PROPERTY(int motionSlow READ motionSlow NOTIFY themeChanged)

public:
    explicit ThemeTokens(QObject *parent = nullptr);

    bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool value);
    bool reducedMotion() const { return m_reducedMotion; }
    void setReducedMotion(bool value);

    QColor bgBase() const { return m_darkMode ? QColor("#14161A") : QColor("#F5F6F8"); }
    QColor bgCard() const { return m_darkMode ? QColor("#1E2126") : QColor("#FFFFFF"); }
    QColor bgLayer() const { return m_darkMode ? QColor("#262A31") : QColor("#EEF0F3"); }
    QColor textPrimary() const { return m_darkMode ? QColor("#F2F4F7") : QColor("#1A1D23"); }
    QColor textSecondary() const { return m_darkMode ? QColor("#A6ADB8") : QColor("#4B5563"); }
    QColor borderColor() const { return m_darkMode ? QColor("#343A43") : QColor("#D9DDE3"); }

    QColor colorAccent() const { return QColor("#2F5FE0"); }
    QColor onAccent() const { return QColor("#FFFFFF"); }
    QColor colorSuccess() const { return QColor("#1F9D55"); }
    QColor colorSuccessText() const { return m_darkMode ? QColor("#5FCB8F") : QColor("#17703A"); }
    QColor colorWarning() const { return QColor("#C77B16"); }
    QColor colorWarningText() const { return m_darkMode ? QColor("#E8B25C") : QColor("#8A5A08"); }
    QColor colorError() const { return QColor("#C92A2A"); }
    QColor colorDomainLearning() const { return QColor("#2F5FE0"); }
    QColor colorDomainHealth() const { return QColor("#1F9D55"); }
    QColor colorDomainLife() const { return QColor("#8A5CF6"); }
    QColor focusColor() const { return m_darkMode ? QColor("#7DA5FF") : QColor("#2F5FE0"); }
    int focusWidth() const { return 2; }

    int fontSizePageTitle() const { return 22; }
    int fontSizeSection() const { return 16; }
    int fontSizeBody() const { return 14; }
    int fontSizeCaption() const { return 12; }
    int fontSizeNumber() const { return 28; }

    int spacingXs() const { return 4; }
    int spacingSm() const { return 8; }
    int spacingMd() const { return 16; }
    int spacingLg() const { return 24; }
    int radiusSm() const { return 6; }
    int radiusMd() const { return 10; }
    int radiusLg() const { return 16; }
    qreal elevationCard() const { return 0.10; }
    qreal elevationFloat() const { return 0.22; }

    int motionFast() const { return m_reducedMotion ? 0 : 120; }
    int motionNormal() const { return m_reducedMotion ? 0 : 200; }
    int motionSlow() const { return m_reducedMotion ? 0 : 320; }

signals:
    void themeChanged();

private:
    bool m_darkMode = false;
    bool m_reducedMotion = false;
};

} // namespace PersonOS
