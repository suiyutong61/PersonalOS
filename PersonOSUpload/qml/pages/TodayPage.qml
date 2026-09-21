import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// 今日任务页（README 3.3.6）：列表 + 状态流转 + 添加任务
Rectangle {
    color: window.panel
    radius: 8

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        Text {
            text: qsTr("今日计划")
            font.pixelSize: 22
            font.bold: true
            color: window.textColor
        }
        Text {
            text: appService.todayDate + " · " + appService.taskSummary
            color: window.muted
            font.pixelSize: 12
        }

        ListView {
            id: taskList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: appService.todayTasks

            delegate: Rectangle {
                required property var modelData
                width: taskList.width
                height: 64
                radius: 6
                color: window.lightMode ? "#f0f0f3" : "#333333"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: modelData.title
                            font.pixelSize: 15
                            color: window.textColor
                        }
                        Text {
                            text: modelData.planText
                            font.pixelSize: 12
                            color: window.muted
                        }
                    }
                    Text {
                        text: modelData.statusText
                        color: modelData.done ? "#6abf69" : window.accent
                        font.pixelSize: 12
                    }
                    Button {
                        visible: modelData.status === "planned" || modelData.status === "delayed"
                        text: qsTr("开始")
                        onClicked: appService.startTask(modelData.id)
                    }
                    Button {
                        visible: !modelData.done
                        text: qsTr("完成")
                        onClicked: completeDialog.openFor(modelData.id)
                    }
                    Button {
                        visible: !modelData.done
                        text: qsTr("跳过")
                        onClicked: appService.skipTask(modelData.id, qsTr("用户手动跳过"))
                    }
                    Button {
                        visible: !modelData.done
                        text: qsTr("取消")
                        onClicked: appService.cancelTask(modelData.id, qsTr("用户手动取消"))
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: newTitle
                Layout.fillWidth: true
                placeholderText: qsTr("新任务标题")
            }
            SpinBox {
                id: newMinutes
                from: 0
                to: 600
                stepSize: 15
                value: 60
            }
            Text {
                text: qsTr("分钟")
                color: window.muted
            }
            Button {
                text: qsTr("添加")
                onClicked: {
                    appService.addTask(newTitle.text, newMinutes.value)
                    newTitle.text = ""
                }
            }
        }
    }

    Dialog {
        id: completeDialog
        anchors.centerIn: parent
        title: qsTr("完成任务")
        standardButtons: Dialog.Ok | Dialog.Cancel
        property int taskId: 0

        function openFor(id) {
            taskId = id
            actualInput.text = ""
            open()
        }

        ColumnLayout {
            TextField {
                id: actualInput
                placeholderText: qsTr("实际用时（分钟）")
            }
        }
        onAccepted: appService.completeTask(taskId, parseInt(actualInput.text) || 0)
    }
}
