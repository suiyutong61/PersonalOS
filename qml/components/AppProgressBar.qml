import QtQuick
import QtQuick.Controls.Basic
import PersonOS

// 统一进度条（长期覆盖率与 MEL 执行率必须分开标注，见页面标题/标签）
ProgressBar {
    id: root
    implicitHeight: 8

    background: Rectangle {
        implicitHeight: 8
        radius: 4
        color: ThemeTokens.bgLayer
    }
    contentItem: Item {
        implicitHeight: 8
        Rectangle {
            width: root.visualPosition * parent.width
            height: parent.height
            radius: 4
            color: root.barColor
            Behavior on width {
                enabled: !ThemeTokens.reducedMotion
                NumberAnimation { duration: ThemeTokens.motionNormal }
            }
        }
    }

    property color barColor: ThemeTokens.colorAccent
}
