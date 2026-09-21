import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// 日终复盘页（README 3.3.6）：本地草案 / AI 分析 / 保存 / AI 提案区
Rectangle {
    color: window.panel
    radius: 8

    Flickable {
        anchors.fill: parent
        anchors.margins: 16
        contentHeight: col.implicitHeight
        clip: true

        ColumnLayout {
            id: col
            width: parent.width
            spacing: 8

            Text {
                text: qsTr("日终复盘")
                font.pixelSize: 22
                font.bold: true
                color: window.textColor
            }
            Text {
                text: appService.todayDate
                color: window.muted
                font.pixelSize: 12
            }

            RowLayout {
                spacing: 8
                Button {
                    text: qsTr("生成本地草案")
                    onClicked: appService.draftTodayReview()
                }
                Button {
                    id: aiButton
                    text: qsTr("AI 分析")
                    onClicked: {
                        aiButton.enabled = false
                        appService.aiAnalyzeTodayReview()
                    }
                }
                Button {
                    text: qsTr("保存复盘")
                    onClicked: appService.saveTodayReview()
                }
            }

            Text { text: qsTr("总结（计划 vs 实际）"); color: window.textColor }
            TextArea {
                id: summaryArea
                Layout.fillWidth: true
                Layout.preferredHeight: 90
                text: appService.reviewSummary
                onTextEdited: appService.reviewSummary = text
                wrapMode: Text.Wrap
            }
            Text { text: qsTr("发现的问题"); color: window.textColor }
            TextArea {
                id: problemsArea
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                text: appService.reviewProblems
                onTextEdited: appService.reviewProblems = text
                wrapMode: Text.Wrap
            }
            Text { text: qsTr("原因分析"); color: window.textColor }
            TextArea {
                id: causesArea
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                text: appService.reviewCauses
                onTextEdited: appService.reviewCauses = text
                wrapMode: Text.Wrap
            }
            Text { text: qsTr("下一步调整"); color: window.textColor }
            TextArea {
                id: actionsArea
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                text: appService.reviewNextActions
                onTextEdited: appService.reviewNextActions = text
                wrapMode: Text.Wrap
            }

            // ---- AI 变更提案区（2.7.17：AI 只提案，用户决定）----
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 8
                color: window.lightMode ? "#f0f0f3" : "#333333"
                radius: 6
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8
                    Text {
                        text: qsTr("系统改进提案（AI 建议 → 你决定）")
                        font.bold: true
                        color: window.textColor
                    }
                    RowLayout {
                        spacing: 8
                        TextField {
                            id: proposalDesc
                            Layout.fillWidth: true
                            placeholderText: qsTr("描述想改进的问题，如：学习任务完成率偏低")
                        }
                        Button {
                            text: qsTr("AI 生成提案")
                            onClicked: appService.aiGenerateProposal(proposalDesc.text)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: appService.proposalText
                        color: window.textColor
                        wrapMode: Text.Wrap
                        visible: appService.proposalText !== ""
                    }
                    RowLayout {
                        visible: appService.hasPendingProposal
                        spacing: 8
                        Button {
                            text: qsTr("✅ 批准并生效")
                            onClicked: appService.approveProposal()
                        }
                        Button {
                            text: qsTr("✖ 拒绝")
                            onClicked: appService.rejectProposal()
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: appService
        function onAiReviewReady(ok, error) {
            aiButton.enabled = true
        }
    }
}
