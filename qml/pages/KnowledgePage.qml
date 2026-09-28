// 知识库（requirements 10.8；R4）：五库浏览、搜索与手动添加
// 论文经单文献处理核心导入；方法/贴士可手动添加；来源与版本可追溯。
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PersonOS

Item {
    id: root

    KnowledgeViewModel {
        id: vm
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: ThemeTokens.spacingLg
        spacing: ThemeTokens.spacingMd

        PageHeader {
            title: qsTr("知识库")
            subtitle: qsTr("论文、方案、方法、贴士与协议（共享身份与版本）")
        }

        StateViews {
            Layout.fillWidth: true
            pageState: vm.pageState
            message: vm.lastError
            contentCount: vm.itemsModel.count
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: vm.pageState === "ready" || vm.pageState === "empty"
            spacing: ThemeTokens.spacingSm

            RowLayout {
                Layout.fillWidth: true
                spacing: ThemeTokens.spacingSm
                TextField {
                    id: searchInput
                    Layout.fillWidth: true
                    placeholderText: qsTr("按标题搜索（留空显示最近条目）")
                }
                AppButton {
                    text: qsTr("搜索")
                    onClicked: vm.search(searchInput.text)
                }
                AppButton {
                    text: qsTr("添加")
                    variant: "primary"
                    onClicked: addDialog.open()
                }
                AppButton {
                    text: qsTr("刷新")
                    onClicked: vm.refresh()
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: ThemeTokens.spacingSm
                ListView {
                    id: itemList
                    anchors.fill: parent
                    clip: true
                    spacing: ThemeTokens.spacingXs
                    model: vm.itemsModel
                    delegate: RowLayout {
                        required property string title
                        required property string subtitle
                        required property string badge
                        required property string badgeTone
                        required property string detail
                        width: itemList.width
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
                            tone: badgeTone
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: addDialog
        anchors.centerIn: parent
        title: qsTr("添加知识条目")
        standardButtons: Dialog.Ok | Dialog.Cancel

        ColumnLayout {
            spacing: ThemeTokens.spacingSm
            ComboBox {
                id: kindBox
                model: [qsTr("方法"), qsTr("贴士"), qsTr("方案")]
                currentIndex: 0
            }
            TextField {
                id: addTitle
                placeholderText: qsTr("标题")
            }
            TextArea {
                id: addSummary
                placeholderText: qsTr("摘要（论文请通过导入文件添加）")
            }
        }
        onAccepted: {
            const kinds = ["method", "tip", "plan"]
            vm.addItem(kinds[kindBox.currentIndex], addTitle.text, addSummary.text)
            addTitle.text = ""
            addSummary.text = ""
        }
    }

    Component.onCompleted: vm.refresh()
}
