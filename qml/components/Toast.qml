// 应用内全局提醒横幅：AppNotifier 单例驱动，8 秒自动消失，可手动关闭。
// 覆盖在页面之上（z:100），不进入任何页面布局；reducedMotion 时无动画。
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PersonOS
import PersonOS.App

Rectangle {
    id: root
    z: 100
    width: Math.min(520, parent ? parent.width - ThemeTokens.spacingLg * 2 : 520)
    height: contentRow.implicitHeight + ThemeTokens.spacingMd * 2
    radius: ThemeTokens.radiusMd
    color: ThemeTokens.bgCard
    border.width: 1
    border.color: toneColor
    visible: false
    opacity: visible ? 1 : 0

    property bool shown: false
    property string bannerText: ""
    property string bannerTone: "warning"
    property color toneColor: bannerTone === "error" ? ThemeTokens.colorError
                              : bannerTone === "success" ? ThemeTokens.colorSuccess
                              : ThemeTokens.colorWarning

    // 序列号单调递增：每次 show() 触发一次
    property int seen: AppNotifier.sequence
    onSeenChanged: {
        if (AppNotifier.sequence > 0 && AppNotifier.text !== "") {
            bannerText = AppNotifier.text
            bannerTone = AppNotifier.tone
            shown = true
            hideTimer.restart()
        }
    }
    onShownChanged: {
        if (!shown)
            AppNotifier.dismiss()
    }

    Behavior on opacity {
        enabled: !ThemeTokens.reducedMotion
        NumberAnimation { duration: ThemeTokens.motionFast }
    }

    Timer {
        id: hideTimer
        interval: 8000
        onTriggered: root.shown = false
    }

    RowLayout {
        id: contentRow
        anchors.fill: parent
        anchors.margins: ThemeTokens.spacingMd
        spacing: ThemeTokens.spacingSm
        Text {
            Layout.fillWidth: true
            text: root.bannerText
            wrapMode: Text.WordWrap
            color: ThemeTokens.textPrimary
        }
        AppButton {
            text: qsTr("关闭")
            variant: "ghost"
            onClicked: root.shown = false
        }
    }
}
