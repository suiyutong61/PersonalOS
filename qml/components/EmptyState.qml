import QtQuick
import QtQuick.Layouts
import PersonOS

// 空内容状态（requirements 10.8：空/加载/失败等状态统一设计）
ColumnLayout {
    id: root
    property string title: qsTr("暂无内容")
    property string hint: ""

    spacing: ThemeTokens.spacingSm

    Text {
        Layout.alignment: Qt.AlignHCenter
        text: "◌"
        font.pixelSize: ThemeTokens.fontSizeNumber
        color: ThemeTokens.textSecondary
    }
    Text {
        Layout.alignment: Qt.AlignHCenter
        text: root.title
        font.pixelSize: ThemeTokens.fontSizeSection
        color: ThemeTokens.textPrimary
    }
    Text {
        Layout.alignment: Qt.AlignHCenter
        visible: root.hint !== ""
        text: root.hint
        font.pixelSize: ThemeTokens.fontSizeCaption
        color: ThemeTokens.textSecondary
    }
}
