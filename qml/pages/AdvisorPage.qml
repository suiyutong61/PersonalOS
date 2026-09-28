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

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: vm.pageState === "ready" || vm.pageState === "ai_waiting"
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
                    delegate: Card {
                        required property string title
                        required property string subtitle
                        required property string badge
                        required property string badgeTone
                        required property string detail
                        width: historyList.width
                        height: 64
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
                                text: qsTr("结果：%1").arg(detail)
                                elide: Text.ElideRight
                                font.pixelSize: ThemeTokens.fontSizeCaption
                                color: ThemeTokens.textSecondary
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
                        text: qsTr("发送")
                        variant: "primary"
                        onClicked: {
                            vm.send(questionInput.text)
                            questionInput.text = ""
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
            visible: vm.pageState === "ready" || vm.pageState === "ai_waiting"
            text: qsTr("刷新")
            onClicked: vm.refresh()
        }
    }

    Component.onCompleted: vm.refresh()
}
