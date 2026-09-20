import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// Personal OS 导航壳（README 3.3.6）
// appService 由 C++ 注入（ApplicationService，QML 唯一入口门面）
ApplicationWindow {
    id: window
    width: 960
    height: 680
    minimumWidth: 640
    minimumHeight: 480
    visible: true
    title: qsTr("Personal OS")

    property bool lightMode: Application.styleHints.colorScheme === Qt.Light
    property color bg: lightMode ? "#f4f4f6" : "#1f1f1f"
    property color panel: lightMode ? "#ffffff" : "#2a2a2a"
    property color textColor: lightMode ? "#222222" : "#e7e7e7"
    property color muted: lightMode ? "#888888" : "#9a9a9a"
    property color accent: "#4a7dff"

    color: bg

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        // 补录提醒横幅（3.0.3 循环触发时机）
        Rectangle {
            Layout.fillWidth: true
            visible: appService.pendingReviewDates.length > 0
            color: lightMode ? "#fff3cd" : "#5c4a00"
            radius: 6
            implicitHeight: visible ? 34 : 0
            Text {
                anchors.centerIn: parent
                text: qsTr("⚠ 你有 %1 天未复盘（%2），请及时补录")
                          .arg(appService.pendingReviewDates.length)
                          .arg(appService.pendingReviewDates.join(", "))
                color: lightMode ? "#7a5c00" : "#ffe08a"
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            // 侧边导航
            Rectangle {
                Layout.preferredWidth: 120
                Layout.fillHeight: true
                color: panel
                radius: 8
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6
                    Repeater {
                        model: [qsTr("今日"), qsTr("目标"), qsTr("状态"), qsTr("复盘")]
                        Button {
                            Layout.fillWidth: true
                            text: modelData
                            checkable: true
                            checked: stack.currentIndex === index
                            onClicked: stack.currentIndex = index
                        }
                    }
                }
            }

            // 页面区
            StackLayout {
                id: stack
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: 0

                TodayPage {}
                GoalsPage {}
                StatePage {}
                ReviewPage {}
            }
        }

        // 底部错误提示
        Text {
            Layout.fillWidth: true
            visible: appService.lastError !== ""
            text: qsTr("⚠ %1").arg(appService.lastError)
            color: "#d9534f"
            wrapMode: Text.Wrap
        }
    }

    Component.onCompleted: appService.init()
}
