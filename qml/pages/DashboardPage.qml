// 首页总览（requirements 10.8：概览与高频入口；渐进展开）
// 长期覆盖率与 MEL 执行率分开标注（DR-020），不合并为一个百分比。
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PersonOS

Item {
    id: root

    DashboardViewModel {
        id: vm
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: ThemeTokens.spacingLg
        spacing: ThemeTokens.spacingMd

        PageHeader {
            title: qsTr("首页总览")
            subtitle: qsTr("当前最重要的信息与快捷操作")
        }

        StateViews {
            Layout.fillWidth: true
            pageState: vm.pageState
            message: vm.lastError
            contentCount: vm.goalCount + (vm.activeMelTitle !== "" ? 1 : 0)
        }

        RowLayout {
            Layout.fillWidth: true
            visible: vm.pageState === "ready"
            spacing: ThemeTokens.spacingMd

            // 当前 MEL 卡片（MEL 执行率单独标注）
            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                ColumnLayout {
                    anchors.fill: parent
                    spacing: ThemeTokens.spacingSm
                    Text {
                        text: qsTr("当前 MEL")
                        font.pixelSize: ThemeTokens.fontSizeCaption
                        color: ThemeTokens.textSecondary
                    }
                    Text {
                        text: vm.activeMelTitle === "" ? qsTr("暂无进行中的 MEL")
                                                       : vm.activeMelTitle
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    AppProgressBar {
                        visible: vm.activeMelTitle !== ""
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        value: parseFloat(vm.activeMelProgress) || 0
                    }
                    Text {
                        visible: vm.activeMelTitle !== ""
                        text: qsTr("MEL 执行率：%1（不表示已掌握）").arg(vm.activeMelProgress)
                        font.pixelSize: ThemeTokens.fontSizeCaption
                        color: ThemeTokens.textSecondary
                    }
                }
            }

            // 长期目标摘要卡片（长期覆盖率单独标注）
            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                ColumnLayout {
                    anchors.fill: parent
                    spacing: ThemeTokens.spacingSm
                    Text {
                        text: qsTr("长期目标")
                        font.pixelSize: ThemeTokens.fontSizeCaption
                        color: ThemeTokens.textSecondary
                    }
                    Text {
                        text: vm.goalCount > 0 ? qsTr("%1 个学习目标").arg(vm.goalCount)
                                               : qsTr("尚未建立学习目标")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    Text {
                        visible: vm.coverageText !== ""
                        text: qsTr("内容覆盖率：%1（表示学到哪里）").arg(vm.coverageText)
                        font.pixelSize: ThemeTokens.fontSizeCaption
                        color: ThemeTokens.textSecondary
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }
        }

        // 待处理提醒
        InfoBanner {
            Layout.fillWidth: true
            visible: vm.pageState === "ready" && vm.dueReminderCount > 0
            tone: "warning"
            text: qsTr("待处理提醒：%1 条").arg(vm.dueReminderCount)
        }

        // 快捷入口
        RowLayout {
            Layout.fillWidth: true
            visible: vm.pageState === "ready"
            spacing: ThemeTokens.spacingSm
            AppButton {
                text: qsTr("去当前 MEL")
                variant: "primary"
                onClicked: stack.currentIndex = 2
            }
            AppButton {
                text: qsTr("去目标与路线")
                onClicked: stack.currentIndex = 1
            }
            AppButton {
                text: qsTr("刷新")
                onClicked: vm.refresh()
            }
        }
    }

    Component.onCompleted: vm.refresh()
}
