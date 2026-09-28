import QtQuick
import QtQuick.Controls.Basic
import PersonOS

// 统一按钮（token 色 + 可见键盘焦点环 + 减少动效）
Button {
    id: root
    // primary / secondary / danger / ghost
    property string variant: "secondary"

    property color bgColor: {
        switch (variant) {
        case "primary": return ThemeTokens.colorAccent
        case "danger": return ThemeTokens.colorError
        case "ghost": return "transparent"
        default: return ThemeTokens.bgLayer
        }
    }
    property color fgColor: {
        switch (variant) {
        case "primary":
        case "danger": return ThemeTokens.onAccent
        default: return ThemeTokens.textPrimary
        }
    }

    implicitHeight: 34
    leftPadding: ThemeTokens.spacingMd
    rightPadding: ThemeTokens.spacingMd
    topPadding: ThemeTokens.spacingSm
    bottomPadding: ThemeTokens.spacingSm

    contentItem: Text {
        text: root.text
        font.pixelSize: ThemeTokens.fontSizeBody
        color: root.enabled ? root.fgColor : ThemeTokens.textSecondary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        radius: ThemeTokens.radiusSm
        color: root.enabled ? (root.down || root.checked ? Qt.darker(root.bgColor, 1.08)
                                                         : root.bgColor)
                            : Qt.alpha(ThemeTokens.bgLayer, 0.5)
        border.width: variant === "ghost" ? 1 : 0
        border.color: root.variant === "ghost" ? ThemeTokens.borderColor : "transparent"

        // 可见键盘焦点环（对比度与宽度符合 WCAG 焦点可见要求）
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.width: root.activeFocus ? ThemeTokens.focusWidth : 0
            border.color: ThemeTokens.focusColor
        }
    }

    Behavior on implicitHeight {
        enabled: !ThemeTokens.reducedMotion
        NumberAnimation { duration: ThemeTokens.motionFast }
    }
}
