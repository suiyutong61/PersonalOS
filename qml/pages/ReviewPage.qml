// 验收与复盘（requirements R3.4/R3.5；DR-022/036）
// 问卷可跳过、可修改；跳过/未回答不视为"无明显变化"确认；
// 三档掌握只描述本次检测表现。
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PersonOS

Item {
    id: root

    ReviewViewModel {
        id: vm
    }

    // 供 Main.qml 切页刷新调用(数据跨页变更后保持新鲜)
    function refresh() { vm.refresh() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: ThemeTokens.spacingLg
        spacing: ThemeTokens.spacingMd

        PageHeader {
            title: qsTr("验收与复盘")
            subtitle: qsTr("本轮复盘、拓展状态问卷与验收结果")
        }

        StateViews {
            Layout.fillWidth: true
            pageState: vm.pageState
            message: vm.lastError
            contentCount: 1
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: vm.pageState === "ready" || vm.pageState === "conflict"
                || vm.pageState === "error" || vm.pageState === "offline"
            spacing: ThemeTokens.spacingSm

            // 复盘内容（区分执行完成与能力掌握；不合并总分）
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
                            text: qsTr("本轮复盘")
                            font.pixelSize: ThemeTokens.fontSizeSection
                            font.weight: Font.DemiBold
                            color: ThemeTokens.textPrimary
                        }
                        StatusBadge {
                            text: vm.reviewStateLabel
                            tone: vm.reviewState === "closed" ? "success" : "neutral"
                        }
                    }
                    TextField {
                        Layout.fillWidth: true
                        text: vm.reviewSummary
                        onTextEdited: vm.reviewSummary = text
                        placeholderText: qsTr("完成比例、耗时偏差、验收结果、主要问题")
                    }
                    TextField {
                        Layout.fillWidth: true
                        text: vm.reviewNextActions
                        onTextEdited: vm.reviewNextActions = text
                        placeholderText: qsTr("下一动作：新 MEL / 暂停 / 改路线 / 结束目标")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        AppButton {
                            text: qsTr("提交复盘")
                            variant: "primary"
                            onClicked: vm.submitReview()
                        }
                        AppButton {
                            text: qsTr("关闭复盘")
                            onClicked: vm.closeReview()
                        }
                    }
                }
            }

            // 拓展状态问卷（可跳过；no_change 才是确认）
            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 240
                padding: ThemeTokens.spacingSm
                ColumnLayout {
                    anchors.fill: parent
                    spacing: ThemeTokens.spacingXs
                    Text {
                        text: qsTr("拓展状态问卷（可跳过，跳过不算确认）")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    ListView {
                        id: questionnaireList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 2
                        model: vm.questionnaireModel
                        delegate: RowLayout {
                            required property string uid
                            required property string title
                            required property string subtitle
                            required property string badgeTone
                            required property int value
                            width: questionnaireList.width
                            height: 34
                            Text {
                                Layout.fillWidth: true
                                text: title
                                font.pixelSize: ThemeTokens.fontSizeBody
                                color: ThemeTokens.textPrimary
                            }
                            StatusBadge {
                                text: subtitle
                                tone: badgeTone
                            }
                            SpinBox {
                                visible: badgeTone === "info"
                                from: 1
                                to: 5
                                // 绑定模型值:整模替换后不重置、不悄悄改回已存值
                                value: value
                                onValueChanged: vm.setQuestionnaireChoice(uid, "answer", value)
                            }
                            AppButton {
                                visible: badgeTone !== "info"
                                text: qsTr("回答")
                                onClicked: vm.setQuestionnaireChoice(uid, "answer", 3)
                            }
                            AppButton {
                                visible: badgeTone !== "success"
                                text: qsTr("无明显变化")
                                onClicked: vm.setQuestionnaireChoice(uid, "no_change", 0)
                            }
                            AppButton {
                                visible: badgeTone !== "neutral"
                                text: qsTr("跳过")
                                onClicked: vm.setQuestionnaireChoice(uid, "skipped", 0)
                            }
                        }
                    }
                    AppButton {
                        text: qsTr("提交问卷")
                        onClicked: vm.submitQuestionnaire()
                    }
                }
            }

            // 最近验收（三档结果展示入口）
            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 140
                padding: ThemeTokens.spacingSm
                ColumnLayout {
                    anchors.fill: parent
                    spacing: ThemeTokens.spacingXs
                    Text {
                        text: qsTr("最近验收（结果只描述本次检测表现）")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    ListView {
                        id: assessmentList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: vm.assessmentsModel
                        delegate: RowLayout {
                            required property string title
                            required property string badge
                            required property string badgeTone
                            required property string detail
                            width: assessmentList.width
                            Text {
                                Layout.fillWidth: true
                                text: title
                                font.pixelSize: ThemeTokens.fontSizeBody
                                color: ThemeTokens.textPrimary
                            }
                            Text {
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

            AppButton {
                text: qsTr("刷新")
                onClicked: vm.refresh()
            }
        }
    }

    Component.onCompleted: vm.refresh()
}
