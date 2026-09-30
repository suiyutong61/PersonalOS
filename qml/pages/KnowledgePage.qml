// 知识库（requirements 10.8；R4）：五库浏览、搜索与手动添加
// 论文经单文献处理核心导入；方法/贴士可手动添加；来源与版本可追溯。
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import PersonOS

Item {
    id: root

    KnowledgeViewModel {
        id: vm
    }

    // 供 Main.qml 切页刷新调用(数据跨页变更后保持新鲜)
    function refresh() { vm.refresh() }

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

        InfoBanner {
            Layout.fillWidth: true
            visible: vm.notice !== ""
            tone: "success"
            text: vm.notice
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: vm.pageState === "ready" || vm.pageState === "empty"
                     || vm.pageState === "conflict" || vm.pageState === "error"
                     || vm.pageState === "offline"
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
                    text: qsTr("导入论文")
                    variant: "primary"
                    onClicked: importPaperDialog.open()
                }
                AppButton {
                    text: qsTr("添加")
                    onClicked: addDialog.open()
                }
                AppButton {
                    text: qsTr("刷新")
                    onClicked: vm.refresh()
                }
            }

            // 五库分层浏览(状态库属个人数据页,不在此列)
            TabBar {
                id: kindTabs
                Layout.fillWidth: true
                property var kinds: ["", "paper", "plan", "method", "tip"]
                onCurrentIndexChanged: vm.setKind(kinds[currentIndex])
                TabButton { text: qsTr("全部") }
                TabButton { text: qsTr("论文") }
                TabButton { text: qsTr("方案") }
                TabButton { text: qsTr("方法") }
                TabButton { text: qsTr("贴士") }
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
                        required property string uid
                        required property string title
                        required property string subtitle
                        required property string badge
                        required property string badgeTone
                        required property string detail
                        required property string kind
                        required property bool retractable
                        width: itemList.width
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: title
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                font.pixelSize: ThemeTokens.fontSizeBody
                                color: ThemeTokens.textPrimary
                            }
                            Text {
                                text: qsTr("%1 · %2").arg(subtitle).arg(detail)
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                                font.pixelSize: ThemeTokens.fontSizeCaption
                                color: ThemeTokens.textSecondary
                            }
                        }
                        StatusBadge {
                            text: badge
                            tone: badgeTone
                        }
                        // 查看条目详情(摘要/步骤/证据片段)
                        AppButton {
                            text: qsTr("详情")
                            onClicked: vm.openItem(uid)
                        }
                        // 文献转方法(R4.4.1):AI 分析 → 候选方法/贴士入库
                        // 类型判断经 kind 角色(badge 为中文标签,不可比较)
                        AppButton {
                            text: qsTr("转方法")
                            visible: kind === "paper"
                            onClicked: vm.convertPaperToMethods(uid)
                        }
                        // AI 生成候选的物理删除(2026-09-29 用户决策;
                        // 论文/用户条目不可删,见 requirements R4.4 变更记录)
                        AppButton {
                            text: qsTr("删除")
                            variant: "danger"
                            visible: retractable
                            onClicked: {
                                deleteConfirmDialog.pendingUid = uid
                                deleteConfirmDialog.open()
                            }
                        }
                    }
                }
            }
        }
    }

    // 物理删除二次确认(不可逆)
    Dialog {
        id: deleteConfirmDialog
        title: qsTr("确认删除候选条目")
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true
        property string pendingUid: ""
        onAccepted: {
            vm.removeGeneratedItem(pendingUid)
            pendingUid = ""
        }
        Text {
            width: 380
            wrapMode: Text.WordWrap
            text: qsTr("将彻底删除该候选条目:版本、步骤、关系与检索索引一并清除,不可恢复。论文与用户条目不受影响。确定继续?")
            font.pixelSize: ThemeTokens.fontSizeBody
            color: ThemeTokens.textPrimary
        }
    }

    // 条目详情(摘要/主张/适用/局限/步骤/证据片段)
    Dialog {
        id: detailDialog
        title: vm.detailTitle
        standardButtons: Dialog.Close
        modal: true
        visible: vm.detailVisible
        onClosed: vm.closeDetail()

        ScrollView {
            implicitWidth: 660
            implicitHeight: 500
            clip: true
            ColumnLayout {
                width: 620
                spacing: ThemeTokens.spacingSm
                Text {
                    Layout.fillWidth: true
                    text: vm.detailMeta
                    wrapMode: Text.WordWrap
                    font.pixelSize: ThemeTokens.fontSizeCaption
                    color: ThemeTokens.textSecondary
                }
                Text {
                    text: qsTr("摘要")
                    font.pixelSize: ThemeTokens.fontSizeSection
                    font.weight: Font.DemiBold
                    color: ThemeTokens.textPrimary
                }
                Text {
                    Layout.fillWidth: true
                    text: vm.detailSummary
                    wrapMode: Text.WordWrap
                    font.pixelSize: ThemeTokens.fontSizeBody
                    color: ThemeTokens.textPrimary
                }
                ColumnLayout {
                    visible: vm.detailClaims.length > 0
                    spacing: ThemeTokens.spacingXs
                    Text {
                        text: qsTr("核心主张")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    Repeater {
                        model: vm.detailClaims
                        delegate: Text {
                            required property string modelData
                            text: "· " + modelData
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            font.pixelSize: ThemeTokens.fontSizeBody
                            color: ThemeTokens.textPrimary
                        }
                    }
                }
                ColumnLayout {
                    visible: vm.detailSteps.length > 0
                    spacing: ThemeTokens.spacingXs
                    Text {
                        text: qsTr("操作步骤")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    Repeater {
                        model: vm.detailSteps
                        delegate: Text {
                            required property string modelData
                            text: modelData
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            font.pixelSize: ThemeTokens.fontSizeBody
                            color: ThemeTokens.textPrimary
                        }
                    }
                }
                ColumnLayout {
                    visible: vm.detailApplicability.length > 0
                    spacing: ThemeTokens.spacingXs
                    Text {
                        text: qsTr("适用条件")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    Repeater {
                        model: vm.detailApplicability
                        delegate: Text {
                            required property string modelData
                            text: "· " + modelData
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            font.pixelSize: ThemeTokens.fontSizeBody
                            color: ThemeTokens.textPrimary
                        }
                    }
                }
                ColumnLayout {
                    visible: vm.detailLimitations.length > 0
                    spacing: ThemeTokens.spacingXs
                    Text {
                        text: qsTr("局限与风险")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    Repeater {
                        model: vm.detailLimitations
                        delegate: Text {
                            required property string modelData
                            text: "· " + modelData
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            font.pixelSize: ThemeTokens.fontSizeBody
                            color: ThemeTokens.textPrimary
                        }
                    }
                }
                ColumnLayout {
                    visible: vm.detailEvidence.length > 0
                    spacing: ThemeTokens.spacingXs
                    Text {
                        text: qsTr("原文证据片段")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    Repeater {
                        model: vm.detailEvidence
                        delegate: Text {
                            required property string modelData
                            text: "“" + modelData + "”"
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            font.pixelSize: ThemeTokens.fontSizeCaption
                            color: ThemeTokens.textSecondary
                        }
                    }
                }
            }
        }
    }

    // 论文导入:选文件 → 选领域(Research Base 01-43)→ 单文献核心入库
    FileDialog {
        id: importPaperDialog
        title: qsTr("选择论文文件（PDF 或纯文本）")
        nameFilters: [qsTr("论文文件 (*.pdf *.txt *.md)"), qsTr("所有文件 (*)")]
        onAccepted: {
            importDomainDialog.pendingFile = selectedFile
            importDomainDialog.open()
            vm.requestDomainSuggestions(selectedFile)
        }
    }

    Dialog {
        id: importDomainDialog
        title: qsTr("选择论文所属领域（决定编号）")
        standardButtons: Dialog.Ok | Dialog.Cancel
        modal: true
        property string pendingFile: ""
        // 43 个领域(Research Base 01-43):编号、slug、显示名
        property var domainSlugs: [
            "01-goal-self-regulation", "02-plan-feasibility", "03-time-management",
            "04-learning-science", "05-attention-focus", "06-sleep", "07-nutrition",
            "08-exercise", "09-stress-recovery", "10-emotion-regulation", "11-motivation",
            "12-habits", "13-procrastination", "14-cognition", "15-decision-making",
            "16-social-relationships", "17-environment-design", "18-digital-behavior",
            "19-knowledge-management", "20-creativity", "21-personality", "22-well-being",
            "23-leisure", "24-personal-finance", "25-health-monitoring", "26-n-of-1",
            "27-measurement", "28-statistics", "29-ai-human-ai", "30-personalization",
            "31-safety-ethics", "32-systems-engineering", "33-career-work",
            "34-identity-values-meaning", "35-life-administration", "36-life-transitions",
            "37-accessibility-neurodiversity", "38-personal-safety", "39-epistemic-health",
            "40-preventive-health", "41-substance-use", "42-sexuality-intimacy",
            "43-culture-equity"
        ]
        property var domainLabels: [
            "01 目标与自我调节", "02 计划可行性与资源分配", "03 时间管理与生产力",
            "04 学习科学", "05 注意力与专注", "06 睡眠", "07 营养与饮食",
            "08 身体活动与运动", "09 压力与恢复", "10 情绪调节", "11 动机",
            "12 习惯与行为改变", "13 拖延", "14 认知与认知表现", "15 决策",
            "16 社会与关系", "17 环境设计", "18 数字行为", "19 知识管理",
            "20 创造力与问题解决", "21 人格与个体差异", "22 幸福感与生活满意度",
            "23 休闲与娱乐", "24 个人财务", "25 健康监测", "26 自我实验(N-of-1)",
            "27 测量与量化", "28 统计与因果推断", "29 AI 与人机协作",
            "30 个性化与自适应系统", "31 安全、伦理与隐私", "32 系统工程",
            "33 职业与生涯发展", "34 身份、价值、意义与人生方向", "35 生活事务",
            "36 人生转折、角色与照护", "37 可访问性与神经多样性", "38 个人安全与生活韧性",
            "39 认知信息健康与信息素养", "40 预防性健康", "41 物质使用与成瘾行为",
            "42 性、亲密关系与生殖福祉", "43 文化、社会经济与公平性"
        ]
        ColumnLayout {
            spacing: ThemeTokens.spacingSm
            ComboBox {
                id: domainBox
                width: 480
                model: importDomainDialog.domainLabels
                currentIndex: 0
            }
            // 本地向量分类推荐(零成本,首次约 1-2 秒)
            Text {
                visible: vm.domainSuggestions.length > 0
                text: qsTr("本地推荐（按文件名）:")
                font.pixelSize: ThemeTokens.fontSizeCaption
                color: ThemeTokens.textSecondary
            }
            RowLayout {
                visible: vm.domainSuggestions.length > 0
                spacing: ThemeTokens.spacingXs
                Repeater {
                    model: vm.domainSuggestions
                    delegate: AppButton {
                        required property string modelData
                        text: modelData
                        variant: "ghost"
                        onClicked: {
                            const code = modelData.split(" ")[0]
                            for (let i = 0; i < importDomainDialog.domainSlugs.length; ++i)
                                if (importDomainDialog.domainSlugs[i].startsWith(code + "-"))
                                    domainBox.currentIndex = i
                        }
                    }
                }
            }
        }
        onAccepted: {
            vm.importPaperFile(pendingFile, domainSlugs[domainBox.currentIndex])
            pendingFile = ""
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
