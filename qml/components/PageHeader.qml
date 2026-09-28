import QtQuick
import QtQuick.Layouts
import PersonOS

// 页面头部（统一标题层级；requirements 10.8 一致性）
ColumnLayout {
    id: root
    property string title: ""
    property string subtitle: ""

    spacing: ThemeTokens.spacingXs

    Text {
        text: root.title
        font.pixelSize: ThemeTokens.fontSizePageTitle
        font.weight: Font.DemiBold
        color: ThemeTokens.textPrimary
    }
    Text {
        visible: root.subtitle !== ""
        text: root.subtitle
        font.pixelSize: ThemeTokens.fontSizeCaption
        color: ThemeTokens.textSecondary
    }
}
