import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// 今日状态页（README 3.3.6）：睡眠/精力/专注/心情（1~5，0=不填）
Rectangle {
    color: window.panel
    radius: 8

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Text {
            text: qsTr("今日状态")
            font.pixelSize: 22
            font.bold: true
            color: window.textColor
        }

        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 8

            Text { text: qsTr("睡眠（小时）"); color: window.textColor }
            TextField {
                id: sleepInput
                Layout.fillWidth: true
                placeholderText: qsTr("如 7.5")
            }
            Text { text: qsTr("精力（1-5）"); color: window.textColor }
            SpinBox { id: energyInput; from: 0; to: 5 }
            Text { text: qsTr("专注（1-5）"); color: window.textColor }
            SpinBox { id: focusInput; from: 0; to: 5 }
            Text { text: qsTr("心情（1-5）"); color: window.textColor }
            SpinBox { id: moodInput; from: 0; to: 5 }
            Text { text: qsTr("备注"); color: window.textColor }
            TextField {
                id: noteInput
                Layout.fillWidth: true
                placeholderText: qsTr("今天的状态说明（可选）")
            }
        }

        Text {
            text: qsTr("评分 1~5；0 表示不填写")
            color: window.muted
            font.pixelSize: 11
        }

        Button {
            text: qsTr("保存状态")
            onClicked: appService.saveTodayState(sleepInput.text, energyInput.value,
                                                 focusInput.value, moodInput.value, noteInput.text)
        }

        Text {
            text: qsTr("状态只记录事实，不包含评价——评价在「复盘」页进行（2.3.5 原则3）")
            color: window.muted
            font.pixelSize: 11
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }

    // 加载已保存的当日状态
    Connections {
        target: appService
        function onStateChanged() {
            sleepInput.text = appService.stateSleep
            energyInput.value = appService.stateEnergy
            focusInput.value = appService.stateFocus
            moodInput.value = appService.stateMood
            noteInput.text = appService.stateNote
        }
    }
}
