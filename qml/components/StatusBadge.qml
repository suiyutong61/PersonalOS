import QtQuick
import PersonOS

// 状态徽标（颜色 + 文字共同表达状态；颜色不是唯一信号）
Rectangle {
    id: root
    property string text: ""
    // info / success / warning / error / neutral
    property string tone: "neutral"
    property color textColor: {
        switch (tone) {
        case "success": return ThemeTokens.colorSuccessText
        case "warning": return ThemeTokens.colorWarningText
        case "error": return ThemeTokens.colorError
        case "info": return ThemeTokens.colorAccent
        default: return ThemeTokens.textSecondary
        }
    }
    property color bgColor: {
        switch (tone) {
        case "success": return Qt.alpha(ThemeTokens.colorSuccess, 0.16)
        case "warning": return Qt.alpha(ThemeTokens.colorWarning, 0.16)
        case "error": return Qt.alpha(ThemeTokens.colorError, 0.14)
        case "info": return Qt.alpha(ThemeTokens.colorAccent, 0.14)
        default: return ThemeTokens.bgLayer
        }
    }

    implicitWidth: label.implicitWidth + ThemeTokens.spacingMd
    implicitHeight: label.implicitHeight + ThemeTokens.spacingSm
    radius: ThemeTokens.radiusSm
    color: bgColor
    border.width: 1
    border.color: Qt.alpha(textColor, 0.35)

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        font.pixelSize: ThemeTokens.fontSizeCaption
        color: root.textColor
    }
}
