// 当前 MEL（requirements 10.8；R3）：任务、MEL 执行率、进度上报、结算
// 执行完成与能力验收分开；进度条不暗示掌握。
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PersonOS

Item {
    id: root

    MelViewModel {
        id: vm
    }

    // 供 Main.qml 切页刷新调用(数据跨页变更后保持新鲜)
    function refresh() { vm.refresh() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: ThemeTokens.spacingLg
        spacing: ThemeTokens.spacingMd

        PageHeader {
            title: qsTr("当前 MEL")
            subtitle: qsTr("本轮任务与执行进度")
        }

        StateViews {
            Layout.fillWidth: true
            pageState: vm.pageState
            message: vm.lastError
            contentCount: vm.tasksModel.count
        }

        RowLayout {
            Layout.fillWidth: true
            visible: vm.pageState === "empty"
            spacing: ThemeTokens.spacingSm
            AppButton {
                text: vm.aiState === "ai_waiting" ? qsTr("AI 生成中…") : qsTr("AI 生成首个 MEL")
                variant: "primary"
                enabled: vm.aiState !== "ai_waiting"
                onClicked: vm.aiGenerateMel()
            }
            AppButton {
                text: qsTr("刷新")
                onClicked: vm.refresh()
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: vm.pageState === "ready" || vm.pageState === "conflict"
                || vm.pageState === "error" || vm.pageState === "offline"
            spacing: ThemeTokens.spacingSm

            Card {
                Layout.fillWidth: true
                padding: ThemeTokens.spacingMd
                ColumnLayout {
                    anchors.fill: parent
                    spacing: ThemeTokens.spacingSm
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: vm.melTitle
                            font.pixelSize: ThemeTokens.fontSizeSection
                            font.weight: Font.DemiBold
                            color: ThemeTokens.textPrimary
                        }
                        StatusBadge {
                            text: vm.melStateLabel
                            tone: vm.melState === "active" ? "success" : "neutral"
                        }
                    }
                    Text {
                        text: qsTr("Deadline：%1").arg(vm.melDeadline)
                        font.pixelSize: ThemeTokens.fontSizeCaption
                        color: ThemeTokens.textSecondary
                    }
                    AppProgressBar {
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        value: parseFloat(vm.melProgress) || 0
                    }
                    Text {
                        text: qsTr("MEL 执行率：%1（约定任务完成情况，不表示已掌握）")
                                  .arg(vm.melProgress)
                        font.pixelSize: ThemeTokens.fontSizeCaption
                        color: ThemeTokens.textSecondary
                    }
                }
            }

            // 任务列表
            ListView {
                id: taskList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: ThemeTokens.spacingSm
                model: vm.tasksModel
                delegate: Card {
                    required property string uid
                    required property string title
                    required property string subtitle
                    required property string badge
                    required property string detail
                    width: taskList.width
                    height: 64
                    padding: ThemeTokens.spacingSm
                    RowLayout {
                        anchors.fill: parent
                        spacing: ThemeTokens.spacingSm
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: title
                                font.pixelSize: ThemeTokens.fontSizeBody
                                color: ThemeTokens.textPrimary
                            }
                            Text {
                                text: qsTr("%1 · %2").arg(subtitle).arg(detail)
                                font.pixelSize: ThemeTokens.fontSizeCaption
                                color: ThemeTokens.textSecondary
                            }
                        }
                        StatusBadge {
                            text: badge
                            tone: badge === "100%" ? "success" : "info"
                        }
                        AppButton {
                            text: qsTr("更新进度")
                            onClicked: progressDialog.openFor(uid, title)
                        }
                    }
                }
            }

            // 状态命令
            RowLayout {
                Layout.fillWidth: true
                spacing: ThemeTokens.spacingSm
                AppButton {
                    text: qsTr("标记执行完成")
                    variant: "primary"
                    onClicked: vm.completeExecution()
                }
                AppButton {
                    text: qsTr("结算")
                    onClicked: vm.settle()
                }
                AppButton {
                    text: vm.melState === "paused" ? qsTr("恢复") : qsTr("暂停")
                    onClicked: vm.pauseResume()
                }
                AppButton {
                    text: vm.aiState === "ai_waiting" ? qsTr("AI 处理中…")
                                                      : qsTr("AI 生成 MEL")
                    variant: "primary"
                    enabled: vm.aiState !== "ai_waiting"
                    onClicked: vm.aiGenerateMel()
                }
                AppButton {
                    text: qsTr("AI 方法建议")
                    enabled: vm.aiState !== "ai_waiting"
                    onClicked: vm.aiSuggestMethods()
                }
                AppButton {
                    text: qsTr("刷新")
                    onClicked: vm.refresh()
                }
            }
        }
    }

    Dialog {
        id: progressDialog
        anchors.centerIn: parent
        title: qsTr("更新任务进度")
        standardButtons: Dialog.Ok | Dialog.Cancel
        property string taskUid: ""
        property string taskTitle: ""

        function openFor(uid, title) {
            taskUid = uid
            taskTitle = title
            progressValue.value = 0
            progressNote.text = ""
            open()
        }

        ColumnLayout {
            spacing: ThemeTokens.spacingSm
            Text {
                text: progressDialog.taskTitle
                font.pixelSize: ThemeTokens.fontSizeBody
                color: ThemeTokens.textPrimary
            }
            SpinBox {
                id: progressValue
                from: 0
                to: 100
                stepSize: 5
                value: 0
            }
            TextField {
                id: progressNote
                placeholderText: qsTr("本次学到的内容/章节（可选）")
            }
        }
        onAccepted: vm.recordProgress(taskUid, progressValue.value, progressNote.text)
    }

    Component.onCompleted: vm.refresh()
}
