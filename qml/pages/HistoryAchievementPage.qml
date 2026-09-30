// 历史与成就（requirements 10.8；DR-031）：真实业务事件驱动的成就与复盘时间线
// 不使用签到/断签/排行榜等压力机制。
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PersonOS

Item {
    id: root

    HistoryAchievementViewModel {
        id: vm
    }

    // 供 Main.qml 切页刷新调用(数据跨页变更后保持新鲜)
    function refresh() { vm.refresh() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: ThemeTokens.spacingLg
        spacing: ThemeTokens.spacingMd

        PageHeader {
            title: qsTr("历史与成就")
            subtitle: qsTr("真实完成记录与里程碑（非虚假积分）")
        }

        StateViews {
            Layout.fillWidth: true
            pageState: vm.pageState
            message: vm.lastError
            contentCount: vm.achievementsModel.count + vm.timelineModel.count
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
                        text: qsTr("成就")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    ListView {
                        id: achievementList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: ThemeTokens.spacingXs
                        model: vm.achievementsModel
                        delegate: RowLayout {
                            required property string title
                            required property string subtitle
                            required property string badge
                            required property string detail
                            width: achievementList.width
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
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
                            }
                            Text {
                                text: detail
                                font.pixelSize: ThemeTokens.fontSizeCaption
                                color: ThemeTokens.textSecondary
                            }
                            StatusBadge {
                                text: badge
                                tone: "success"
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
                        text: qsTr("复盘时间线")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    ListView {
                        id: timelineList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: ThemeTokens.spacingXs
                        model: vm.timelineModel
                        delegate: RowLayout {
                            required property string title
                            required property string subtitle
                            required property string badge
                            required property string badgeTone
                            required property string detail
                            width: timelineList.width
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
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
                            }
                            Text {
                                visible: detail !== ""
                                text: detail
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
