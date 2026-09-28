import QtQuick
import PersonOS

// 提示条（警告/错误/信息；文本与图标共同表达，不单靠颜色）
Rectangle {
    id: root
    property string text: ""
    // info / warning / error / knowledge_limited
    property string tone: "info"

    implicitHeight: label.implicitHeight + ThemeTokens.spacingMd
    radius: ThemeTokens.radiusSm
    color: {
        switch (tone) {
        case "warning": return Qt.alpha(ThemeTokens.colorWarning, 0.14)
        case "error": return Qt.alpha(ThemeTokens.colorError, 0.10)
        default: return Qt.alpha(ThemeTokens.colorAccent, 0.10)
        }
    }
    border.width: 1
    border.color: {
        switch (tone) {
        case "warning": return Qt.alpha(ThemeTokens.colorWarning, 0.5)
        case "error": return Qt.alpha(ThemeTokens.colorError, 0.45)
        default: return Qt.alpha(ThemeTokens.colorAccent, 0.45)
        }
    }

    Text {
        id: label
        anchors.fill: parent
        anchors.margins: ThemeTokens.spacingSm
        verticalAlignment: Text.AlignVCenter
        text: root.text
        wrapMode: Text.WordWrap
        font.pixelSize: ThemeTokens.fontSizeBody
        color: {
            switch (root.tone) {
            case "warning": return ThemeTokens.colorWarningText
            case "error": return ThemeTokens.colorError
            default: return ThemeTokens.textPrimary
            }
        }
    }
}
