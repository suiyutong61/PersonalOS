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

            // 汇报进度：MEL 页主循环——文字汇报给 AI，AI 出调整候选
            Card {
                Layout.fillWidth: true
                visible: vm.melState === "active" || vm.melState === "paused"
                padding: ThemeTokens.spacingMd
                ColumnLayout {
                    anchors.fill: parent
                    spacing: ThemeTokens.spacingSm
                    Text {
                        text: qsTr("汇报进度")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("直接说这次做了什么、结果怎样、花了多久。AI 会判断各任务进度和下一步；先给你看变更，确认后才写入。")
                        wrapMode: Text.Wrap
                        font.pixelSize: ThemeTokens.fontSizeCaption
                        color: ThemeTokens.textSecondary
                    }
                    TextArea {
                        id: reportText
                        Layout.fillWidth: true
                        Layout.preferredHeight: 80
                        placeholderText: qsTr("例如：今天照着文档部署好了 Nginx，能通过域名访问；排查 502 花了约 40 分钟。")
                        wrapMode: TextArea.Wrap
                        enabled: vm.aiState !== "ai_waiting"
                    }
                    RowLayout {
                        spacing: ThemeTokens.spacingSm
                        Item { Layout.fillWidth: true }
                        AppButton {
                            text: vm.aiState === "ai_waiting" ? qsTr("AI 分析中…")
                                                              : qsTr("汇报给 AI")
                            variant: "primary"
                            enabled: vm.aiState !== "ai_waiting"
                            onClicked: {
                                vm.reportProgress(reportText.text)
                            }
                        }
                    }
                }
            }

            // AI 调整建议（进度审查候选：一键采用）
            Card {
                Layout.fillWidth: true
                visible: vm.reviewVisible
                padding: ThemeTokens.spacingMd
                ColumnLayout {
                    anchors.fill: parent
                    spacing: ThemeTokens.spacingSm
                    Text {
                        text: qsTr("AI 准备这样更新")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: vm.reviewNotice !== ""
                        text: vm.reviewNotice
                        wrapMode: Text.Wrap
                        color: ThemeTokens.textPrimary
                    }
                    Repeater {
                        model: vm.reviewModel
                        ColumnLayout {
                            required property string title
                            required property string subtitle
                            required property string badge
                            Layout.fillWidth: true
                            spacing: ThemeTokens.spacingXs
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: title
                                    font.weight: Font.DemiBold
                                    color: ThemeTokens.textPrimary
                                }
                                StatusBadge { text: badge; tone: "info" }
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: subtitle !== ""
                                text: subtitle
                                wrapMode: Text.Wrap
                                font.pixelSize: ThemeTokens.fontSizeCaption
                                color: ThemeTokens.textSecondary
                            }
                        }
                    }
                    RowLayout {
                        spacing: ThemeTokens.spacingSm
                        AppButton {
                            text: qsTr("确认更新")
                            variant: "primary"
                            onClicked: vm.adoptReview()
                        }
                        AppButton {
                            text: qsTr("放弃")
                            onClicked: vm.abandonReview()
                        }
                    }
                }
            }

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
                            tone: vm.melState === "active" ? "success"
                                  : (vm.melState === "draft"
                                     || vm.melState === "awaiting_confirmation")
                                    ? "warning" : "neutral"
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
                    }
                }
            }

            // 状态命令
            RowLayout {
                Layout.fillWidth: true
                spacing: ThemeTokens.spacingSm
                // 候选态：确认并激活是唯一正式动作（执行命令仅活跃后可用）
                AppButton {
                    visible: vm.melState === "draft"
                             || vm.melState === "awaiting_confirmation"
                    text: qsTr("确认并激活")
                    variant: "primary"
                    onClicked: vm.confirmMel()
                }
                AppButton {
                    visible: vm.melState === "execution_complete"
                             || vm.melState === "overdue"
                    text: qsTr("结算")
                    enabled: vm.aiState !== "ai_waiting"
                    onClicked: vm.settle()
                }
                AppButton {
                    visible: vm.melState === "active" || vm.melState === "paused"
                    text: vm.melState === "paused" ? qsTr("恢复") : qsTr("暂停")
                    enabled: vm.aiState !== "ai_waiting"
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
                    visible: vm.melState === "active" || vm.melState === "paused"
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

    Connections {
        target: vm
        function onProgressReviewGenerated() {
            reportText.text = ""
        }
    }

    Component.onCompleted: vm.refresh()
}
