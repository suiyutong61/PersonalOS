// 设置（requirements 10.8；DR-025/026）：主题、减少动效、模型连接与备份
// API Key 只写系统凭据库，SQLite 只存引用；界面不显示明文密钥。
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PersonOS

Item {
    id: root

    SettingsViewModel {
        id: vm
    }

    // 供 Main.qml 切页刷新调用(数据跨页变更后保持新鲜)
    function refresh() { vm.refresh() }

    // 主题控制直接作用于设计令牌（深浅色 / 减少动效）
    Binding {
        target: ThemeTokens
        property: "darkMode"
        value: vm.darkMode
    }
    Binding {
        target: ThemeTokens
        property: "reducedMotion"
        value: vm.reducedMotion
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: ThemeTokens.spacingLg
        spacing: ThemeTokens.spacingMd

        PageHeader {
            title: qsTr("设置")
            subtitle: qsTr("模型 API、通知、主题、备份与数据管理")
        }

        StateViews {
            Layout.fillWidth: true
            pageState: vm.pageState
            message: vm.lastError
            contentCount: 1
        }

        InfoBanner {
            Layout.fillWidth: true
            visible: vm.notice !== ""
            tone: "success"
            text: vm.notice
        }

        // 内容区用 ScrollView 承载自然高度：窗口不够高时整体滚动，
        // 而不是把卡片高度压缩导致内容互相叠压、表单被底边切断。
        ScrollView {
            id: settingsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: vm.pageState === "ready" || vm.pageState === "conflict"
                     || vm.pageState === "error" || vm.pageState === "offline"
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: settingsScroll.availableWidth
                spacing: ThemeTokens.spacingSm

                Card {
                    Layout.fillWidth: true
                    padding: ThemeTokens.spacingMd
                    ColumnLayout {
                        id: appearanceContent
                        anchors.fill: parent
                        spacing: ThemeTokens.spacingSm
                        Text {
                            text: qsTr("外观与动效")
                            font.pixelSize: ThemeTokens.fontSizeSection
                            font.weight: Font.DemiBold
                            color: ThemeTokens.textPrimary
                        }
                        CheckBox {
                            text: qsTr("深色主题")
                            checked: vm.darkMode
                            onToggled: vm.darkMode = checked
                        }
                        CheckBox {
                            text: qsTr("减少非必要动画")
                            checked: vm.reducedMotion
                            onToggled: vm.reducedMotion = checked
                        }
                    }
                }

                Card {
                    Layout.fillWidth: true
                    padding: ThemeTokens.spacingSm
                    ColumnLayout {
                        id: modelContent
                        anchors.fill: parent
                        spacing: ThemeTokens.spacingSm
                        Text {
                            text: qsTr("模型连接（凭据只存系统凭据库，测试通过后再启用）")
                            font.pixelSize: ThemeTokens.fontSizeSection
                            font.weight: Font.DemiBold
                            color: ThemeTokens.textPrimary
                        }
                        ListView {
                            id: connectionList
                            Layout.fillWidth: true
                            Layout.preferredHeight: 120
                            clip: true
                            model: vm.connectionsModel
                            delegate: RowLayout {
                                required property string uid
                                required property string title
                                required property string subtitle
                                required property string detail
                                required property string badge
                                required property string badgeTone
                                required property bool isDefault
                                width: connectionList.width
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: title
                                        elide: Text.ElideRight
                                        font.pixelSize: ThemeTokens.fontSizeBody
                                        color: ThemeTokens.textPrimary
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: qsTr("%1 · %2").arg(subtitle).arg(detail)
                                        elide: Text.ElideRight
                                        font.pixelSize: ThemeTokens.fontSizeCaption
                                        color: ThemeTokens.textSecondary
                                    }
                                }
                                StatusBadge {
                                    text: badge
                                    tone: badgeTone
                                }
                                AppButton {
                                    text: qsTr("测试")
                                    onClicked: vm.testConnection(uid)
                                }
                                AppButton {
                                    text: badge === "已启用" ? qsTr("停用") : qsTr("启用")
                                    onClicked: vm.setConnectionEnabled(uid, badge !== "已启用")
                                }
                                AppButton {
                                    text: qsTr("编辑")
                                    onClicked: vm.startEdit(uid)
                                }
                                AppButton {
                                    text: qsTr("设为默认")
                                    variant: "ghost"
                                    visible: !isDefault
                                    onClicked: vm.setDefaultConnection(uid)
                                }
                                AppButton {
                                    text: qsTr("删除")
                                    variant: "danger"
                                    onClicked: vm.removeConnection(uid)
                                }
                            }
                        }
                        // 表单纵向堆叠：窄窗口下每个字段仍有完整宽度，
                        // 避免横向挤压导致占位文字截断或换行重叠。
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: ThemeTokens.spacingXs
                            Text {
                                visible: vm.editingUid !== ""
                                text: qsTr("正在编辑连接（保存后需重新测试并启用）")
                                font.pixelSize: ThemeTokens.fontSizeCaption
                                color: ThemeTokens.textSecondary
                            }
                            TextField {
                                id: connName
                                Layout.fillWidth: true
                                text: vm.editingName
                                placeholderText: qsTr("连接名称")
                            }
                            TextField {
                                id: connEndpoint
                                Layout.fillWidth: true
                                text: vm.editingEndpoint
                                placeholderText: qsTr("API 基础地址（如 https://api.deepseek.com）")
                            }
                            TextField {
                                id: connModel
                                Layout.fillWidth: true
                                text: vm.editingModel
                                placeholderText: qsTr("模型标识（如 deepseek-v4-pro）")
                            }
                            TextField {
                                id: connKey
                                Layout.fillWidth: true
                                placeholderText: vm.editingUid !== ""
                                    ? qsTr("API Key（留空表示保持不变）")
                                    : qsTr("API Key（写入系统凭据库，不落库/日志）")
                                echoMode: TextInput.Password
                            }
                            AppButton {
                                text: vm.editingUid !== "" ? qsTr("保存修改") : qsTr("保存连接")
                                variant: "primary"
                                onClicked: {
                                    vm.saveConnection(connName.text, connEndpoint.text,
                                                      connModel.text, connKey.text)
                                    connKey.text = ""
                                }
                            }
                            AppButton {
                                visible: vm.editingUid !== ""
                                text: qsTr("取消编辑")
                                variant: "ghost"
                                onClicked: {
                                    vm.cancelEdit()
                                    connKey.text = ""
                                }
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
                        Text {
                            text: qsTr("文献处理工具（可选）")
                            font.pixelSize: ThemeTokens.fontSizeSection
                            font.weight: Font.DemiBold
                            color: ThemeTokens.textPrimary
                        }
                        Text {
                            text: qsTr("论文 PDF 提取与扫描件 OCR 使用；留空时自动探测常见安装位置")
                            font.pixelSize: ThemeTokens.fontSizeCaption
                            color: ThemeTokens.textSecondary
                        }
                        TextField {
                            id: toolPdfToText
                            Layout.fillWidth: true
                            text: vm.currentPdfToTextPath()
                            placeholderText: qsTr("pdftotext 路径")
                        }
                        TextField {
                            id: toolTesseract
                            Layout.fillWidth: true
                            text: vm.currentTesseractPath()
                            placeholderText: qsTr("tesseract 路径（如 D:/Tesseract-OCR/tesseract.exe）")
                        }
                        AppButton {
                            text: qsTr("保存工具路径")
                            onClicked: vm.saveToolPaths(toolPdfToText.text, toolTesseract.text)
                        }
                    }
                }

                Card {
                    Layout.fillWidth: true
                    padding: ThemeTokens.spacingMd
                    ColumnLayout {
                        id: backupContent
                        anchors.fill: parent
                        spacing: ThemeTokens.spacingSm
                        Text {
                            text: qsTr("备份")
                            font.pixelSize: ThemeTokens.fontSizeSection
                            font.weight: Font.DemiBold
                            color: ThemeTokens.textPrimary
                        }
                        // 纵向堆叠与连接表单一致：窄窗口下路径框不被按钮挤压
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: ThemeTokens.spacingXs
                            TextField {
                                id: backupPath
                                Layout.fillWidth: true
                                placeholderText: qsTr("备份保存路径（如 D:/backup/personos.db）")
                            }
                            AppButton {
                                text: qsTr("立即备份")
                                variant: "primary"
                                onClicked: vm.createBackup(backupPath.text)
                            }
                        }
                        ListView {
                            id: backupList
                            Layout.fillWidth: true
                            Layout.preferredHeight: 120
                            clip: true
                            model: vm.backupsModel
                            delegate: RowLayout {
                                required property string uid
                                required property string title
                                required property string subtitle
                                required property string badge
                                required property string badgeTone
                                width: backupList.width
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
                                    tone: badgeTone
                                }
                                AppButton {
                                    text: qsTr("校验")
                                    onClicked: vm.verifyBackup(uid)
                                }
                                AppButton {
                                    text: qsTr("恢复")
                                    variant: "primary"
                                    onClicked: {
                                        restoreDialog.backupUid = uid
                                        restoreDialog.open()
                                    }
                                }
                            }
                        }

                        // 恢复二次确认:整体切换数据库,操作不可撤销
                        // (恢复前数据保留在 .pre-restore-* 快照)
                        Dialog {
                            id: restoreDialog
                            property string backupUid: ""
                            anchors.centerIn: parent
                            modal: true
                            title: qsTr("恢复备份")
                            Text {
                                text: qsTr("将用所选备份整体替换当前数据库。"
                                          + "恢复前的数据会保留为快照文件。确定继续？")
                                wrapMode: Text.WordWrap
                            }
                            footer: DialogButtonBox {
                                standardButtons: DialogButtonBox.Yes | DialogButtonBox.No
                                onAccepted: {
                                    vm.restoreBackup(restoreDialog.backupUid)
                                    restoreDialog.close()
                                }
                                onRejected: restoreDialog.close()
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
                        Text {
                            text: qsTr("本地向量索引")
                            font.pixelSize: ThemeTokens.fontSizeSection
                            font.weight: Font.DemiBold
                            color: ThemeTokens.textPrimary
                        }
                        Text {
                            text: qsTr("知识条目的语义检索向量（本地模型生成，零 API 成本）。"
                                      + "模型更换或索引异常时可重建；重建不影响原始知识，"
                                      + "失败也不影响正常检索。")
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            font.pixelSize: ThemeTokens.fontSizeCaption
                            color: ThemeTokens.textSecondary
                        }
                        AppButton {
                            text: qsTr("重建向量索引")
                            onClicked: vm.rebuildVectorIndex()
                        }
                    }
                }

                AppButton {
                    text: qsTr("刷新")
                    onClicked: vm.refresh()
                }
            }
        }
    }

    Component.onCompleted: vm.refresh()
}
