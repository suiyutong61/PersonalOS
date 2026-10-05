// AI 助手（requirements 10.8；DR-015/027）
// 诚实降级：未配置模型 → 离线提示；已配置 → 任务排队（ai_waiting），
// 不把排队/待处理显示为已完成；知识不足状态在结果中醒目标记。
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PersonOS

Item {
    id: root

    AdvisorViewModel {
        id: vm
    }

    // 供 Main.qml 切页刷新调用(数据跨页变更后保持新鲜)
    function refresh() { vm.refresh() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: ThemeTokens.spacingLg
        spacing: ThemeTokens.spacingMd

        PageHeader {
            title: qsTr("AI 助手")
            subtitle: qsTr("提问、规划与建议（重要操作需你确认后生效）")
        }

        StateViews {
            Layout.fillWidth: true
            pageState: vm.pageState
            message: vm.lastError
            contentCount: vm.historyModel.count
        }

        // 知识冲突警示（DR-028：与建议同时显示，不混同知识不足）
        InfoBanner {
            Layout.fillWidth: true
            visible: vm.latestConflict
            tone: "warning"
            text: qsTr("最近一次回答引用的知识存在重要冲突：结论已降低强度，请打开详情查看。")
        }

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            // empty 也显示输入区：无历史决策时用户必须能发出第一个问题，
            // 否则状态机死路（StateViews 同时展示"暂无内容"提示）
            visible: vm.pageState === "ready" || vm.pageState === "empty"
                       || vm.pageState === "ai_waiting" || vm.pageState === "conflict"
                       || vm.pageState === "error" || vm.pageState === "offline"
            padding: ThemeTokens.spacingSm
            ColumnLayout {
                anchors.fill: parent
                spacing: ThemeTokens.spacingSm

                ListView {
                    id: historyList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: ThemeTokens.spacingXs
                    model: vm.historyModel
                    footer: Item {
                        width: historyList.width
                        height: vm.pendingQuestion === "" ? 0 : pendingColumn.implicitHeight + ThemeTokens.spacingSm
                        visible: vm.pendingQuestion !== ""
                        ColumnLayout {
                            id: pendingColumn
                            width: parent.width
                            spacing: ThemeTokens.spacingXs
                            Rectangle {
                                Layout.alignment: Qt.AlignRight
                                Layout.maximumWidth: historyList.width * 0.82
                                implicitWidth: pendingText.implicitWidth + ThemeTokens.spacingMd * 2
                                implicitHeight: pendingText.implicitHeight + ThemeTokens.spacingSm * 2
                                radius: ThemeTokens.radiusMd
                                color: ThemeTokens.colorAccent
                                Text {
                                    id: pendingText
                                    anchors.fill: parent
                                    anchors.margins: ThemeTokens.spacingSm
                                    text: vm.pendingQuestion
                                    wrapMode: Text.WordWrap
                                    color: ThemeTokens.onAccent
                                    font.pixelSize: ThemeTokens.fontSizeBody
                                }
                            }
                            Rectangle {
                                Layout.alignment: Qt.AlignLeft
                                implicitWidth: waitingText.implicitWidth + ThemeTokens.spacingMd * 2
                                implicitHeight: waitingText.implicitHeight + ThemeTokens.spacingSm * 2
                                radius: ThemeTokens.radiusMd
                                color: ThemeTokens.bgLayer
                                border.color: ThemeTokens.borderColor
                                Text {
                                    id: waitingText
                                    anchors.centerIn: parent
                                    text: qsTr("AI 正在思考…")
                                    color: ThemeTokens.textSecondary
                                    font.pixelSize: ThemeTokens.fontSizeBody
                                }
                            }
                        }
                    }
                    delegate: Card {
                        required property string uid
                        required property string title
                        required property string subtitle
                        required property string badge
                        required property string badgeTone
                        required property string detail
                        width: historyList.width
                        height: 112
                        padding: ThemeTokens.spacingSm
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("问题：%1").arg(subtitle)
                                    elide: Text.ElideRight
                                    font.pixelSize: ThemeTokens.fontSizeBody
                                    color: ThemeTokens.textPrimary
                                }
                                StatusBadge {
                                    text: badge
                                    tone: badgeTone
                                }
                            }
                            Text {
                                visible: detail !== ""
                                Layout.fillWidth: true
                                // 两行预览,完整内容在详情弹层查看
                                text: qsTr("结果：%1").arg(detail)
                                wrapMode: Text.WordWrap
                                elide: Text.ElideRight
                                maximumLineCount: 2
                                font.pixelSize: ThemeTokens.fontSizeCaption
                                color: ThemeTokens.textSecondary
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                AppButton {
                                    text: qsTr("详情")
                                    onClicked: vm.openDetail(uid)
                                }
                                AppButton {
                                    text: qsTr("删除")
                                    onClicked: {
                                        deleteDialog.answerUid = uid
                                        deleteDialog.open()
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: ThemeTokens.spacingSm
                    TextField {
                        id: questionInput
                        Layout.fillWidth: true
                        placeholderText: qsTr("输入你的问题…")
                    }
                    AppButton {
                        text: vm.pageState === "ai_waiting" ? qsTr("AI 回答中…") : qsTr("发送")
                        variant: "primary"
                        enabled: vm.pageState !== "ai_waiting"
                                 && questionInput.text.trim() !== ""
                        onClicked: vm.send(questionInput.text)
                    }
                    Connections {
                        target: vm
                        function onAnswerGenerated() {
                            questionInput.text = ""
                        }
                        function onDataChanged() {
                            Qt.callLater(function() { historyList.positionViewAtEnd() })
                        }
                        function onPendingQuestionChanged() {
                            Qt.callLater(function() { historyList.positionViewAtEnd() })
                        }
                    }
                    AppButton {
                        text: qsTr("取消最新任务")
                        onClicked: vm.cancelLatestPending()
                    }
                }
            }
        }

        AppButton {
            visible: vm.pageState === "ready" || vm.pageState === "empty"
                       || vm.pageState === "ai_waiting" || vm.pageState === "conflict"
                       || vm.pageState === "error" || vm.pageState === "offline"
            text: qsTr("刷新")
            onClicked: vm.refresh()
        }
    }

    // 详情弹层:完整问题与回答(可滚动、自动换行)
    Dialog {
        id: detailDialog
        visible: vm.detailVisible
        anchors.centerIn: parent
        modal: true
        width: Math.min(parent.width * 0.8, 720)
        height: Math.min(parent.height * 0.7, 560)
        title: qsTr("回答详情")
        ColumnLayout {
            anchors.fill: parent
            spacing: ThemeTokens.spacingSm
            InfoBanner {
                Layout.fillWidth: true
                visible: vm.detailConflict
                tone: "warning"
                text: qsTr("该回答引用的知识存在重要冲突：结论已降低强度，依据可在知识库核实。")
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("问题")
                font.pixelSize: ThemeTokens.fontSizeSection
                font.weight: Font.DemiBold
                color: ThemeTokens.textPrimary
            }
            Text {
                Layout.fillWidth: true
                text: vm.detailQuestion
                wrapMode: Text.WordWrap
                font.pixelSize: ThemeTokens.fontSizeBody
                color: ThemeTokens.textPrimary
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("回答")
                font.pixelSize: ThemeTokens.fontSizeSection
                font.weight: Font.DemiBold
                color: ThemeTokens.textPrimary
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                Text {
                    width: detailDialog.availableWidth
                    text: vm.detailAnswer
                    wrapMode: Text.WordWrap
                    font.pixelSize: ThemeTokens.fontSizeBody
                    color: ThemeTokens.textSecondary
                }
            }
        }
        footer: DialogButtonBox {
            standardButtons: DialogButtonBox.Close
            onRejected: vm.closeDetail()
        }
        onClosed: vm.closeDetail()
    }

    // 删除二次确认:物理删除,不可恢复
    Dialog {
        id: deleteDialog
        property string answerUid: ""
        anchors.centerIn: parent
        modal: true
        title: qsTr("删除回答")
        Text {
            text: qsTr("将永久删除这条咨询回答及其底层任务记录（不可恢复）。"
                      + "历史审计事件会保留。确定删除？")
            wrapMode: Text.WordWrap
        }
        footer: DialogButtonBox {
            standardButtons: DialogButtonBox.Yes | DialogButtonBox.No
            onAccepted: {
                vm.deleteAnswer(deleteDialog.answerUid)
                deleteDialog.close()
            }
            onRejected: deleteDialog.close()
        }
    }

    Component.onCompleted: vm.refresh()
}
