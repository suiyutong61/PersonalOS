// 日历与提醒（requirements R3.3.1；DR-024）：提醒规则与待投递/Deadline 投影
// 提醒不得自动追加任务或改变 MEL；投递失败不改变业务状态。
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
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
                    Text {
                        text: qsTr("已启用提醒")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    ListView {
                        id: reminderList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: ThemeTokens.spacingXs
                        model: vm.remindersModel
                        delegate: RowLayout {
                            required property string title
                            required property string subtitle
                            required property string badge
                            width: reminderList.width
                            Text {
                                Layout.fillWidth: true
                                text: title
                                font.pixelSize: ThemeTokens.fontSizeBody
                                color: ThemeTokens.textPrimary
                            }
                            Text {
                                text: subtitle
                                elide: Text.ElideRight
                                font.pixelSize: ThemeTokens.fontSizeCaption
                                color: ThemeTokens.textSecondary
                            }
                            StatusBadge {
                                text: badge
                                tone: "info"
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

    Component.onCompleted: vm.refresh()
}
