// 日历与提醒（requirements R3.3.1；DR-024）：提醒规则管理（新建/停用/启用）、
// 待投递/Deadline 与投递历史投影。
// 提醒不得自动追加任务或改变 MEL；投递失败不改变业务状态。
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import PersonOS

Item {
    id: root

    CalendarViewModel {
        id: vm
    }

    // 供 Main.qml 切页刷新调用(数据跨页变更后保持新鲜)
    function refresh() { vm.refresh() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: ThemeTokens.spacingLg
        spacing: ThemeTokens.spacingMd

        PageHeader {
            title: qsTr("日历与提醒")
            subtitle: qsTr("提醒规则与待处理事项（由 Person OS 内部调度保存）")
        }

        StateViews {
            Layout.fillWidth: true
            pageState: vm.pageState
            message: vm.lastError
            contentCount: vm.remindersModel.count + vm.dueModel.count
                         + vm.deliveriesModel.count
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: vm.pageState === "ready"
            spacing: ThemeTokens.spacingMd

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: ThemeTokens.spacingSm
                ColumnLayout {
                    anchors.fill: parent
                    spacing: ThemeTokens.spacingSm
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("提醒规则")
                            font.pixelSize: ThemeTokens.fontSizeSection
                            font.weight: Font.DemiBold
                            color: ThemeTokens.textPrimary
                        }
                        AppButton {
                            text: qsTr("＋ 新建提醒")
                            variant: "primary"
                            onClicked: createReminderDialog.open()
                        }
                    }
                    ListView {
                        id: reminderList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: ThemeTokens.spacingXs
                        model: vm.remindersModel
                        delegate: RowLayout {
                            required property string uid
                            required property string kind
                            required property string title
                            required property string subtitle
                            required property string badge
                            required property string badgeTone
                            width: reminderList.width
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: ThemeTokens.spacingXs
                                Text {
                                    Layout.fillWidth: true
                                    text: title
                                    font.pixelSize: ThemeTokens.fontSizeBody
                                    color: ThemeTokens.textPrimary
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: subtitle
                                    elide: Text.ElideRight
                                    font.pixelSize: ThemeTokens.fontSizeCaption
                                    color: ThemeTokens.textSecondary
                                }
                            }
                            StatusBadge {
                                text: badge
                                tone: badgeTone
                            }
                            AppButton {
                                // kind 保留原始键：badge 中文化后不再用中文串判断
                                text: kind === "enabled" ? qsTr("停用") : qsTr("启用")
                                variant: "ghost"
                                onClicked: vm.setRuleEnabled(uid, kind !== "enabled")
                            }
                        }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: ThemeTokens.spacingSm
                ColumnLayout {
                    anchors.fill: parent
                    spacing: ThemeTokens.spacingSm
                    Text {
                        text: qsTr("待处理事项")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    ListView {
                        id: dueList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: ThemeTokens.spacingXs
                        model: vm.dueModel
                        delegate: RowLayout {
                            required property string title
                            required property string subtitle
                            required property string badge
                            required property string badgeTone
                            width: dueList.width
                            Text {
                                Layout.fillWidth: true
                                text: title
                                font.pixelSize: ThemeTokens.fontSizeBody
                                color: ThemeTokens.textPrimary
                            }
                            Text {
                                text: subtitle
                                font.pixelSize: ThemeTokens.fontSizeCaption
                                color: ThemeTokens.textSecondary
                            }
                            StatusBadge {
                                text: badge
                                tone: badgeTone
                            }
                        }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: ThemeTokens.spacingSm
                ColumnLayout {
                    anchors.fill: parent
                    spacing: ThemeTokens.spacingSm
                    Text {
                        text: qsTr("投递记录")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    ListView {
                        id: deliveryList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: ThemeTokens.spacingXs
                        model: vm.deliveriesModel
                        delegate: ColumnLayout {
                            required property string title
                            required property string subtitle
                            required property string badge
                            required property string badgeTone
                            width: deliveryList.width
                            spacing: ThemeTokens.spacingXs
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: title
                                    font.pixelSize: ThemeTokens.fontSizeBody
                                    color: ThemeTokens.textPrimary
                                }
                                StatusBadge {
                                    text: badge
                                    tone: badgeTone
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: subtitle !== ""
                                text: subtitle
                                elide: Text.ElideRight
                                font.pixelSize: ThemeTokens.fontSizeCaption
                                color: ThemeTokens.textSecondary
                            }
                        }
                    }
                }
            }
        }

        AppButton {
            // empty 也可见:数据可能由其他页的业务事件产生(切页自动刷新兜底),
            // 否则空页上零控件成为死路
            visible: vm.pageState !== "loading"
            text: qsTr("刷新")
            onClicked: vm.refresh()
        }
    }

    Dialog {
        id: createReminderDialog
        anchors.centerIn: parent
        modal: true
        width: Math.min(root.width * 0.7, 520)
        title: qsTr("新建 Deadline 提醒")
        ColumnLayout {
            anchors.fill: parent
            spacing: ThemeTokens.spacingMd
            Text {
                Layout.fillWidth: true
                text: qsTr("到点后应用内横幅提醒（程序运行期间）。每个 MEL 可建多条不同提前量的提醒。")
                wrapMode: Text.Wrap
                color: ThemeTokens.textSecondary
            }
            Text {
                text: qsTr("选择 MEL")
                font.pixelSize: ThemeTokens.fontSizeBody
                font.weight: Font.DemiBold
                color: ThemeTokens.textPrimary
            }
            ComboBox {
                id: melPicker
                Layout.fillWidth: true
                model: vm.activeMelsModel
                textRole: "title"
                valueRole: "uid"
            }
            RowLayout {
                spacing: ThemeTokens.spacingSm
                Text {
                    text: qsTr("提前")
                    color: ThemeTokens.textSecondary
                }
                SpinBox {
                    id: offsetPicker
                    from: 0
                    to: 1440
                    stepSize: 10
                    value: 0
                }
                Text {
                    text: qsTr("分钟提醒")
                    color: ThemeTokens.textSecondary
                }
            }
        }
        footer: DialogButtonBox {
            standardButtons: DialogButtonBox.Ok | DialogButtonBox.Cancel
            onAccepted: {
                if (melPicker.currentValue)
                    vm.createMelReminder(melPicker.currentValue, offsetPicker.value)
                createReminderDialog.close()
            }
            onRejected: createReminderDialog.close()
        }
    }

    Component.onCompleted: vm.refresh()
}
