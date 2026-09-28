import QtQuick
import QtQuick.Controls.Basic
import PersonOS

// 侧边导航项（稳定导航 + 选中态 + 可见焦点）
Button {
    id: root
    property string glyph: ""   // 简单符号标识（纯装饰，不影响可读性）
    property bool active: false

    implicitHeight: 36
    leftPadding: ThemeTokens.spacingMd
    rightPadding: ThemeTokens.spacingMd

    contentItem: Row {
        spacing: ThemeTokens.spacingSm
        Text {
            text: root.glyph
            visible: root.glyph !== ""
            font.pixelSize: ThemeTokens.fontSizeBody
            color: ThemeTokens.textSecondary
        }
        Text {
            text: root.text
            font.pixelSize: ThemeTokens.fontSizeBody
            font.weight: root.active ? Font.DemiBold : Font.Normal
            color: ThemeTokens.textPrimary
        }
    }

    background: Rectangle {
        radius: ThemeTokens.radiusSm
        color: root.active || root.hovered ? Qt.alpha(ThemeTokens.colorAccent, 0.12)
                                           : "transparent"
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.width: root.activeFocus ? ThemeTokens.focusWidth : 0
            border.color: ThemeTokens.focusColor
        }
    }
}
