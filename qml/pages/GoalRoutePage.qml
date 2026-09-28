// 目标与路线（requirements 10.8；R1）：目标列表、内容地图与覆盖率、路线确认
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PersonOS

Item {
    id: root

    GoalRouteViewModel {
        id: vm
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: ThemeTokens.spacingLg
        spacing: ThemeTokens.spacingMd

        PageHeader {
            title: qsTr("目标与路线")
            subtitle: qsTr("长期目标、内容地图覆盖与路线确认")
        }

        StateViews {
            Layout.fillWidth: true
            pageState: vm.pageState
            message: vm.lastError
            contentCount: vm.goalsModel.count
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: vm.pageState === "ready" || vm.pageState === "empty"
            spacing: ThemeTokens.spacingMd

            // 目标列表
            Card {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                padding: ThemeTokens.spacingSm
                ColumnLayout {
                    anchors.fill: parent
                    spacing: ThemeTokens.spacingSm
                    Text {
                        text: qsTr("学习目标")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    ListView {
                        id: goalList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: ThemeTokens.spacingXs
                        model: vm.goalsModel
                        delegate: Item {
                            required property string uid
                            required property string title
                            required property string badge
                            width: goalList.width
                            height: 44
                            Rectangle {
                                anchors.fill: parent
                                radius: ThemeTokens.radiusSm
                                color: goalList.currentIndex === index
                                       ? Qt.alpha(ThemeTokens.colorAccent, 0.12) : "transparent"
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: ThemeTokens.spacingSm
                                Text {
                                    Layout.fillWidth: true
                                    text: title
                                    elide: Text.ElideRight
                                    font.pixelSize: ThemeTokens.fontSizeBody
                                    color: ThemeTokens.textPrimary
                                }
                                StatusBadge {
                                    text: badge === "—" ? qsTr("未建目录") : badge
                                    tone: badge === "—" ? "neutral" : "info"
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    goalList.currentIndex = index
                                    vm.selectGoal(uid)
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: ThemeTokens.spacingXs
                        TextField {
                            id: newGoalTitle
                            Layout.fillWidth: true
                            placeholderText: qsTr("新目标标题")
                        }
                        AppButton {
                            text: qsTr("创建")
                            variant: "primary"
                            onClicked: {
                                vm.createGoal(newGoalTitle.text)
                                newGoalTitle.text = ""
                            }
                        }
                    }
                }
            }

            // 详情：内容地图 / 覆盖率 / 路线
            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ColumnLayout {
                    anchors.fill: parent
                    spacing: ThemeTokens.spacingSm

                    Text {
                        text: vm.selectedGoalTitle === "" ? qsTr("未选择目标")
                                                          : vm.selectedGoalTitle
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    InfoBanner {
                        Layout.fillWidth: true
                        visible: vm.coverageText !== ""
                        tone: "info"
                        text: qsTr("内容覆盖率：%1").arg(vm.coverageText)
                    }

                    // 内容地图
                    Text {
                        text: qsTr("内容地图（确认后才作为进度依据）")
                        font.pixelSize: ThemeTokens.fontSizeCaption
                        color: ThemeTokens.textSecondary
                    }
                    ListView {
                        id: mapList
                        Layout.fillWidth: true
                        Layout.preferredHeight: 110
                        clip: true
                        model: vm.mapsModel
                        delegate: RowLayout {
                            required property string uid
                            required property string title
                            required property string subtitle
                            required property string badge
                            width: mapList.width
                            Text {
                                Layout.fillWidth: true
                                text: title
                                elide: Text.ElideRight
                                font.pixelSize: ThemeTokens.fontSizeBody
                                color: ThemeTokens.textPrimary
                            }
                            Text {
                                text: subtitle
                                font.pixelSize: ThemeTokens.fontSizeCaption
                                color: ThemeTokens.textSecondary
                            }
                            StatusBadge {
                                text: badge === "confirmed" ? qsTr("已确认") : qsTr("草稿")
                                tone: badge === "confirmed" ? "success" : "neutral"
                            }
                            AppButton {
                                visible: badge === "draft"
                                text: qsTr("确认")
                                variant: "primary"
                                onClicked: vm.confirmMap(uid)
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: ThemeTokens.spacingXs
                        TextField {
                            id: newMapTitle
                            Layout.fillWidth: true
                            placeholderText: qsTr("新目录名称（如：概率论教材）")
                        }
                        AppButton {
                            text: qsTr("创建目录")
                            onClicked: {
                                vm.createMap(newMapTitle.text)
                                newMapTitle.text = ""
                            }
                        }
                    }
                    TextArea {
                        id: nodeLines
                        Layout.fillWidth: true
                        Layout.preferredHeight: 60
                        placeholderText: qsTr("章/节节点，每行一个（写入最近的草稿目录）")
                    }
                    AppButton {
                        text: qsTr("添加节点")
                        onClicked: vm.addNodes(nodeLines.text)
                    }

                    // 路线
                    Text {
                        text: qsTr("路线（候选需用户确认后生效）")
                        font.pixelSize: ThemeTokens.fontSizeCaption
                        color: ThemeTokens.textSecondary
                    }
                    ListView {
                        id: routeList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: vm.routesModel
                        delegate: RowLayout {
                            required property string uid
                            required property string title
                            required property string badge
                            width: routeList.width
                            Text {
                                Layout.fillWidth: true
                                text: title
                                font.pixelSize: ThemeTokens.fontSizeBody
                                color: ThemeTokens.textPrimary
                            }
                            StatusBadge {
                                text: badge
                                tone: badge === "proposed" ? "warning" : "neutral"
                            }
                            AppButton {
                                visible: badge === "proposed"
                                text: qsTr("确认路线")
                                variant: "primary"
                                onClicked: vm.confirmRoute(uid)
                            }
                        }
                    }
                    TextField {
                        id: routeRationale
                        Layout.fillWidth: true
                        placeholderText: qsTr("路线依据（为什么这样规划）")
                    }
                    TextArea {
                        id: routeStages
                        Layout.fillWidth: true
                        Layout.preferredHeight: 60
                        placeholderText: qsTr("路线阶段，每行一个")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: ThemeTokens.spacingSm
                        AppButton {
                            text: qsTr("提出路线草案")
                            onClicked: {
                                vm.proposeRoute(routeRationale.text, routeStages.text)
                                routeRationale.text = ""
                                routeStages.text = ""
                            }
                        }
                        AppButton {
                            text: vm.aiState === "ai_waiting" ? qsTr("AI 生成中…")
                                                              : qsTr("AI 生成路线（知识校准）")
                            variant: "primary"
                            enabled: vm.aiState !== "ai_waiting"
                            onClicked: vm.aiGenerateRoute()
                        }
                    }
                }
            }
        }
    }

    Component.onCompleted: vm.refresh()
}
