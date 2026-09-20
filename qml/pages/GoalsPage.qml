import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// 目标层级页（README 3.3.6）：层级树（缩进展示）+ 新建目标
Rectangle {
    color: window.panel
    radius: 8

    // ---- 页面级数据与函数（根级作用域，供各子项调用）----
    ListModel {
        id: parentOptions
    }

    function rebuildParentOptions() {
        parentOptions.clear()
        parentOptions.append({ value: 0, label: qsTr("无父级（根目标）") })
        for (var i = 0; i < appService.goals.length; ++i)
            parentOptions.append({ value: appService.goals[i].id,
                                   label: appService.goals[i].title })
    }

    Component.onCompleted: rebuildParentOptions()
    Connections {
        target: appService
        function onGoalsChanged() {
            rebuildParentOptions()
            goalParent.currentIndex = 0
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        Text {
            text: qsTr("目标层级")
            font.pixelSize: 22
            font.bold: true
            color: window.textColor
        }

        ListView {
            id: goalList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: appService.goals

            delegate: Rectangle {
                required property var modelData
                width: goalList.width
                height: 34
                color: "transparent"

                RowLayout {
                    anchors.fill: parent
                    spacing: 6
                    Item {
                        Layout.preferredWidth: modelData.depth * 20
                        height: 1
                    }
                    Text {
                        text: modelData.title
                        font.pixelSize: 14
                        color: window.textColor
                        Layout.fillWidth: true
                    }
                    Text {
                        text: modelData.levelText
                        font.pixelSize: 11
                        color: window.muted
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: goalTitle
                Layout.fillWidth: true
                placeholderText: qsTr("新目标标题")
            }
            ComboBox {
                id: goalLevel
                textRole: "label"
                model: ListModel {
                    ListElement { value: "weekly"; label: qsTr("本周") }
                    ListElement { value: "monthly"; label: qsTr("月度") }
                    ListElement { value: "quarterly"; label: qsTr("季度") }
                    ListElement { value: "annual"; label: qsTr("年度") }
                    ListElement { value: "long_term"; label: qsTr("长期") }
                    ListElement { value: "vision"; label: qsTr("愿景") }
                }
                property string value: currentIndex >= 0 ? model.get(currentIndex).value : ""
            }
            ComboBox {
                id: goalParent
                textRole: "label"
                model: parentOptions
                property int value: currentIndex >= 0 ? model.get(currentIndex).value : 0
            }
            Button {
                text: qsTr("添加")
                onClicked: {
                    appService.createGoal(goalTitle.text, goalLevel.value, goalParent.value, 50)
                    goalTitle.text = ""
                }
            }
        }
    }
}
