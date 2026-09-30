// R1 分层交互：大目标列表 → 目标详情/新建 → 路线 → 阶段详情。
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PersonOS

Item {
    id: root
    property int viewIndex: 0 // 0列表 1详情 2新建 3路线 4阶段
    property var routeNodes: []
    property int routeGraphWidth: 0
    property int routeGraphHeight: 0
    property string lastRouteGuidance: ""
    property string routeAiReply: ""
    property string lastStageGuidance: ""
    property string stageAiReply: ""

    GoalRouteViewModel { id: vm }
    function refresh() { vm.refresh() }
    function openGoal(uid) { vm.selectGoal(uid); viewIndex = 1 }
    function goBack() { viewIndex = viewIndex === 4 ? 3 : (viewIndex === 3 ? 1 : 0) }
    function rebuildRouteGraph() {
        let raw = []
        try { raw = JSON.parse(vm.routeGraphJson) } catch (e) { raw = [] }
        const byTitle = {}
        for (let i = 0; i < raw.length; ++i) byTitle[raw[i].title] = raw[i]
        function levelOf(node, visiting) {
            if (node._level !== undefined) return node._level
            if (visiting[node.title]) return 0
            visiting[node.title] = true
            let level = 0
            const deps = node.depends_on || []
            for (let i = 0; i < deps.length; ++i) {
                if (byTitle[deps[i]]) level = Math.max(level, levelOf(byTitle[deps[i]], visiting) + 1)
            }
            delete visiting[node.title]
            node._level = level
            return level
        }
        const rows = {}
        let maxLevel = 0
        for (let i = 0; i < raw.length; ++i) {
            const level = levelOf(raw[i], {})
            maxLevel = Math.max(maxLevel, level)
            if (!rows[level]) rows[level] = 0
            raw[i].gx = 24 + level * 236
            raw[i].gy = 24 + rows[level] * 118
            rows[level]++
        }
        let maxRows = 1
        for (const key in rows) maxRows = Math.max(maxRows, rows[key])
        routeGraphWidth = Math.max(620, 48 + (maxLevel + 1) * 236)
        routeGraphHeight = 48 + maxRows * 118
        routeNodes = raw
        routeCanvas.requestPaint()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: ThemeTokens.spacingLg
        spacing: ThemeTokens.spacingMd

        RowLayout {
            Layout.fillWidth: true
            AppButton {
                visible: root.viewIndex !== 0
                text: qsTr("← 返回")
                variant: "ghost"
                onClicked: root.goBack()
            }
            PageHeader {
                Layout.fillWidth: true
                title: root.viewIndex === 0 ? qsTr("我的大目标")
                       : root.viewIndex === 1 ? qsTr("大目标详情")
                       : root.viewIndex === 2 ? qsTr("添加新目标")
                       : root.viewIndex === 3 ? qsTr("学习路线") : qsTr("阶段详情")
                subtitle: root.viewIndex === 0 ? qsTr("选择一个目标查看详情，或建立新的长期方向")
                          : root.viewIndex === 2 ? qsTr("先说明想成为什么，细节可以交给 AI 一起完善")
                          : root.viewIndex === 3 ? qsTr("检查粗粒度阶段、前置条件和阶段内的必要内容")
                          : root.viewIndex === 4 ? qsTr("安排这一阶段的可验证结果、内部任务、资料与完成标准")
                          : ""
            }
            AppButton {
                text: qsTr("刷新")
                enabled: vm.pageState !== "loading" && vm.aiState !== "ai_waiting"
                         && vm.stageAiState !== "ai_waiting"
                onClicked: vm.refresh()
            }
        }

        StateViews {
            Layout.fillWidth: true
            pageState: vm.pageState
            message: vm.lastError
            contentCount: vm.goalsModel.count
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.viewIndex

            // 第一层：大目标列表
            ColumnLayout {
                spacing: ThemeTokens.spacingMd
                AppButton {
                    text: qsTr("＋ 添加新目标")
                    variant: "primary"
                    onClicked: root.viewIndex = 2
                }
                ListView {
                    id: goalsList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: ThemeTokens.spacingSm
                    model: vm.goalsModel
                    delegate: Card {
                        required property string uid
                        required property string title
                        required property string subtitle
                        required property string badge
                        required property string badgeTone
                        width: goalsList.width
                        padding: ThemeTokens.spacingMd
                        RowLayout {
                            anchors.fill: parent
                            spacing: ThemeTokens.spacingMd
                            ColumnLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: title
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: ThemeTokens.fontSizeSection
                                    font.weight: Font.DemiBold
                                    color: ThemeTokens.textPrimary
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: subtitle !== ""
                                    text: subtitle
                                    elide: Text.ElideRight
                                    color: ThemeTokens.textSecondary
                                }
                            }
                            StatusBadge { text: badge; tone: badgeTone }
                            AppButton { text: qsTr("查看详情"); onClicked: root.openGoal(uid) }
                        }
                    }
                }
            }

            // 第二层：大目标详情
            ScrollView {
                id: goalDetailScroll
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: goalDetailScroll.availableWidth
                    spacing: ThemeTokens.spacingMd
                    Card {
                        Layout.fillWidth: true
                        padding: ThemeTokens.spacingLg
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: ThemeTokens.spacingSm
                            Text {
                                Layout.fillWidth: true
                                text: vm.selectedGoalTitle
                                wrapMode: Text.WordWrap
                                font.pixelSize: ThemeTokens.fontSizePageTitle
                                font.weight: Font.DemiBold
                                color: ThemeTokens.textPrimary
                            }
                            Text {
                                Layout.fillWidth: true
                                text: vm.selectedGoalDescription === ""
                                      ? qsTr("尚未补充目标背景。你仍可在规划路线时告诉 AI 你的要求。")
                                      : vm.selectedGoalDescription
                                wrapMode: Text.WordWrap
                                color: ThemeTokens.textSecondary
                            }
                        }
                    }
                    Card {
                        Layout.fillWidth: true
                        padding: ThemeTokens.spacingLg
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: ThemeTokens.spacingSm
                            Text {
                                text: qsTr("路线阶段")
                                font.pixelSize: ThemeTokens.fontSizeSection
                                font.weight: Font.DemiBold
                                color: ThemeTokens.textPrimary
                            }
                            Text {
                                Layout.fillWidth: true
                                text: vm.subjectsModel.count === 0
                                      ? qsTr("还没有路线。让 AI 根据大目标生成 3～5 个实用阶段。")
                                      : qsTr("当前路线有 %1 个阶段，进入后可以查看和提出修改意见。").arg(vm.subjectsModel.count)
                                wrapMode: Text.WordWrap
                                color: ThemeTokens.textSecondary
                            }
                            AppButton {
                                text: vm.subjectsModel.count === 0 ? qsTr("生成学习路线")
                                                                  : qsTr("查看学习路线")
                                variant: "primary"
                                onClicked: root.viewIndex = 3
                            }
                        }
                    }
                }
            }

            // 新建大目标
            Card {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                padding: ThemeTokens.spacingLg
                ColumnLayout {
                    anchors.fill: parent
                    spacing: ThemeTokens.spacingMd
                    Text {
                        text: qsTr("我最终想成为什么？")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    TextField {
                        id: newGoalTitle
                        Layout.fillWidth: true
                        placeholderText: qsTr("例如：成为优秀的 Java 后端工程师")
                    }
                    TextArea {
                        id: newGoalDescription
                        Layout.fillWidth: true
                        Layout.preferredHeight: 120
                        placeholderText: qsTr("可选：你的基础、用途、期限、偏好，或者你希望系统特别考虑的事情")
                        wrapMode: TextArea.Wrap
                    }
                    AppButton {
                        text: qsTr("确定大目标")
                        variant: "primary"
                        enabled: newGoalTitle.text.trim() !== ""
                        onClicked: {
                            vm.createGoal(newGoalTitle.text, newGoalDescription.text)
                            newGoalTitle.text = ""
                            newGoalDescription.text = ""
                            root.viewIndex = 1
                        }
                    }
                }
            }

            // 路线层：关系图 -> 文字说明 -> 用户与 AI 共同调整
            ScrollView {
                id: routeScroll
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: routeScroll.availableWidth
                    spacing: ThemeTokens.spacingMd
                    Card {
                        Layout.fillWidth: true
                        padding: ThemeTokens.spacingMd
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: ThemeTokens.spacingSm
                            Text {
                                text: vm.selectedGoalTitle
                                font.pixelSize: ThemeTokens.fontSizeSection
                                font.weight: Font.DemiBold
                                color: ThemeTokens.textPrimary
                            }
                            Text {
                                Layout.fillWidth: true
                                text: vm.selectedGoalDescription === ""
                                      ? qsTr("先让 AI 根据目标与知识库生成一版可执行路线，再按你的实际情况调整。")
                                      : vm.selectedGoalDescription
                                wrapMode: Text.Wrap
                                color: ThemeTokens.textSecondary
                            }
                        }
                    }
                    Text {
                        text: qsTr("路线阶段")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    RowLayout {
                        spacing: ThemeTokens.spacingMd
                        Text { text: qsTr("→ 必须先完成"); color: ThemeTokens.textSecondary }
                        Text { text: qsTr("同一列 = 可以并行推进"); color: ThemeTokens.colorSuccess }
                        Item { Layout.fillWidth: true }
                        Text { text: qsTr("点击节点查看阶段详情"); color: ThemeTokens.textSecondary }
                    }
                    Card {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(220, root.routeGraphHeight + ThemeTokens.spacingMd * 2)
                        visible: root.routeNodes.length > 0
                        padding: ThemeTokens.spacingSm
                        Flickable {
                            anchors.fill: parent
                            clip: true
                            contentWidth: Math.max(width, root.routeGraphWidth)
                            contentHeight: Math.max(height, root.routeGraphHeight)
                            Item {
                                width: root.routeGraphWidth
                                height: root.routeGraphHeight
                                Canvas {
                                    id: routeCanvas
                                    anchors.fill: parent
                                    onPaint: {
                                        const ctx = getContext("2d")
                                        ctx.clearRect(0, 0, width, height)
                                        ctx.strokeStyle = ThemeTokens.colorAccent
                                        ctx.fillStyle = ThemeTokens.colorAccent
                                        ctx.lineWidth = 2
                                        const nodes = root.routeNodes
                                        const byTitle = {}
                                        for (let i = 0; i < nodes.length; ++i) byTitle[nodes[i].title] = nodes[i]
                                        for (let i = 0; i < nodes.length; ++i) {
                                            const to = nodes[i]
                                            const deps = to.depends_on || []
                                            for (let d = 0; d < deps.length; ++d) {
                                                const from = byTitle[deps[d]]
                                                if (!from) continue
                                                const x1 = from.gx + 180, y1 = from.gy + 38
                                                const x2 = to.gx, y2 = to.gy + 38
                                                ctx.beginPath(); ctx.moveTo(x1, y1); ctx.lineTo(x2 - 10, y2); ctx.stroke()
                                                ctx.beginPath(); ctx.moveTo(x2, y2)
                                                ctx.lineTo(x2 - 11, y2 - 6); ctx.lineTo(x2 - 11, y2 + 6)
                                                ctx.closePath(); ctx.fill()
                                            }
                                        }
                                    }
                                }
                                Repeater {
                                    model: root.routeNodes
                                    Rectangle {
                                        required property var modelData
                                        x: modelData.gx
                                        y: modelData.gy
                                        width: 180
                                        height: 76
                                        radius: ThemeTokens.radiusMd
                                        color: ThemeTokens.bgLayer
                                        border.width: 2
                                        border.color: modelData.relationship === "parallel"
                                                      ? ThemeTokens.colorSuccess : ThemeTokens.colorAccent
                                        Column {
                                            anchors.fill: parent
                                            anchors.margins: ThemeTokens.spacingSm
                                            spacing: 3
                                            Text {
                                                width: parent.width
                                                text: modelData.title
                                                elide: Text.ElideRight
                                                font.weight: Font.DemiBold
                                                color: ThemeTokens.textPrimary
                                            }
                                            Text {
                                                width: parent.width
                                                text: modelData.relationship === "parallel"
                                                      ? qsTr("可并行")
                                                      : ((modelData.depends_on || []).length > 0
                                                         ? qsTr("有前置") : qsTr("起点"))
                                                color: ThemeTokens.textSecondary
                                            }
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                vm.selectStage(modelData.uid || "")
                                                root.viewIndex = 4
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Text {
                        visible: vm.subjectsModel.count > 0
                        text: qsTr("路线文字说明")
                        font.pixelSize: ThemeTokens.fontSizeSection
                        font.weight: Font.DemiBold
                        color: ThemeTokens.textPrimary
                    }
                    Repeater {
                        model: vm.subjectsModel
                        Card {
                            required property string title
                            required property string subtitle
                            required property string detail
                            required property string badge
                            width: routeScroll.availableWidth
                            padding: ThemeTokens.spacingMd
                            ColumnLayout {
                                anchors.fill: parent
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
                                    text: detail === "" ? subtitle : detail
                                    wrapMode: Text.Wrap
                                    color: ThemeTokens.textSecondary
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
                                text: qsTr("把你的想法告诉 AI")
                                font.pixelSize: ThemeTokens.fontSizeSection
                                font.weight: Font.DemiBold
                                color: ThemeTokens.textPrimary
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("你可以要求删减、合并、调整顺序或改变并行关系。AI 会生成新版本，是否采用仍由你决定。")
                                wrapMode: Text.Wrap
                                color: ThemeTokens.textSecondary
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: userMessage.implicitHeight + ThemeTokens.spacingMd * 2
                                visible: root.lastRouteGuidance !== ""
                                radius: ThemeTokens.radiusMd
                                color: ThemeTokens.bgLayer
                                Text {
                                    id: userMessage
                                    anchors.fill: parent
                                    anchors.margins: ThemeTokens.spacingMd
                                    text: qsTr("我的想法：%1").arg(root.lastRouteGuidance)
                                    wrapMode: Text.Wrap
                                    color: ThemeTokens.textPrimary
                                }
                            }
                            InfoBanner {
                                Layout.fillWidth: true
                                visible: root.routeAiReply !== ""
                                tone: "info"
                                text: root.routeAiReply
                            }
                            TextArea {
                                id: routeGuidance
                                Layout.fillWidth: true
                                Layout.preferredHeight: 90
                                placeholderText: qsTr("例如：我已经会基础；希望先做项目；把两个阶段合并；这两部分可以并行……")
                                wrapMode: TextArea.Wrap
                            }
                            AppButton {
                                text: vm.aiState === "ai_waiting" ? qsTr("AI 正在调整路线…")
                                                                  : (vm.subjectsModel.count === 0
                                                                     ? qsTr("让 AI 生成路线")
                                                                     : qsTr("发送给 AI 并生成新方案"))
                                variant: "primary"
                                enabled: vm.aiState !== "ai_waiting"
                                onClicked: {
                                    root.lastRouteGuidance = routeGuidance.text.trim()
                                    root.routeAiReply = qsTr("AI 正在结合你的想法和知识库重新规划……")
                                    vm.aiGenerateRoute(root.lastRouteGuidance)
                                }
                            }
                        }
                    }
                    Repeater {
                        model: vm.routesModel
                        RowLayout {
                            required property string uid
                            required property string badge
                            required property bool retractable
                            width: routeScroll.availableWidth
                            Text { Layout.fillWidth: true; text: qsTr("路线状态：%1").arg(badge); color: ThemeTokens.textSecondary }
                            AppButton {
                                visible: retractable
                                text: qsTr("确认采用这条路线")
                                variant: "primary"
                                onClicked: vm.confirmRoute(uid)
                            }
                        }
                    }
                }
            }

            // 阶段层：回答"如何真正完成这一阶段"——可验证结果、内部任务、项目练习、
            // 完成标准、资料（AI 推荐+逐条确认）与用户—AI 调整。判断走原始键
            // （kind/retractable/stageDetailConfirmable/stageRouteConfirmed/stageMissing），
            // 展示文案走 vm 属性与 badge 中文标签。
            ScrollView {
                id: stageScroll
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: stageScroll.availableWidth
                    spacing: ThemeTokens.spacingMd
                    Card {
                        Layout.fillWidth: true
                        padding: ThemeTokens.spacingLg
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: ThemeTokens.spacingSm
                            Text {
                                Layout.fillWidth: true
                                text: vm.stageInfoTitle
                                wrapMode: Text.WordWrap
                                font.pixelSize: ThemeTokens.fontSizePageTitle
                                font.weight: Font.DemiBold
                                color: ThemeTokens.textPrimary
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: vm.stageInfoKeyContents !== ""
                                text: qsTr("阶段内包含：%1").arg(vm.stageInfoKeyContents)
                                wrapMode: Text.Wrap
                                color: ThemeTokens.textSecondary
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: vm.stageInfoDescription !== ""
                                text: vm.stageInfoDescription
                                wrapMode: Text.Wrap
                                color: ThemeTokens.textSecondary
                            }
                        }
                    }
                    InfoBanner {
                        Layout.fillWidth: true
                        visible: vm.stageMissing
                        tone: "warning"
                        text: qsTr("该阶段已被新版本路线取代。请返回路线层查看最新路线与阶段。")
                    }
                    InfoBanner {
                        Layout.fillWidth: true
                        visible: !vm.stageMissing && !vm.stageRouteConfirmed
                        tone: "info"
                        text: qsTr("路线尚未确认。确认路线后，才能生成这一阶段的详情安排（候选路线可能随重新规划而改变）。")
                    }
                    InfoBanner {
                        Layout.fillWidth: true
                        visible: !vm.stageMissing && vm.stageRouteConfirmed
                                 && vm.stageDetailVersion === 0
                        tone: "info"
                        text: qsTr("还没有阶段详情。让 AI 结合知识库生成一版候选：可验证结果、少量内部任务、项目练习与完成标准；你提意见调整，满意后再确认。")
                    }
                    Card {
                        Layout.fillWidth: true
                        visible: vm.stageDetailVersion > 0
                        padding: ThemeTokens.spacingMd
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: ThemeTokens.spacingSm
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("阶段内容安排")
                                    font.pixelSize: ThemeTokens.fontSizeSection
                                    font.weight: Font.DemiBold
                                    color: ThemeTokens.textPrimary
                                }
                                StatusBadge {
                                    text: vm.stageDetailBadge
                                    tone: vm.stageDetailConfirmable ? "warning" : "success"
                                }
                            }
                            // 按 kind 分节展示（行序已按 outcome→task→project→criterion 分组）
                            ListView {
                                Layout.fillWidth: true
                                height: contentHeight
                                interactive: false
                                spacing: ThemeTokens.spacingSm
                                model: vm.stageDetailModel
                                section.property: "kind"
                                section.delegate: Component {
                                    Text {
                                        width: parent ? parent.width : 0
                                        text: section === "outcome" ? qsTr("可验证结果")
                                              : section === "task" ? qsTr("内部任务（按顺序）")
                                              : section === "project" ? qsTr("项目与练习")
                                              : qsTr("完成标准")
                                        font.pixelSize: ThemeTokens.fontSizeBody
                                        font.weight: Font.DemiBold
                                        color: ThemeTokens.textPrimary
                                    }
                                }
                                delegate: Card {
                                    required property string kind
                                    required property string title
                                    required property string subtitle
                                    required property string detail
                                    width: ListView.view ? ListView.view.width : 0
                                    padding: ThemeTokens.spacingSm
                                    ColumnLayout {
                                        anchors.fill: parent
                                        spacing: ThemeTokens.spacingXs
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text {
                                                Layout.fillWidth: true
                                                text: title
                                                wrapMode: Text.WordWrap
                                                font.weight: kind === "task" ? Font.DemiBold
                                                                             : Font.Normal
                                                color: ThemeTokens.textPrimary
                                            }
                                            StatusBadge {
                                                visible: kind === "task" && subtitle !== ""
                                                text: subtitle
                                                tone: "info"
                                            }
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            visible: detail !== ""
                                            text: detail
                                            wrapMode: Text.WordWrap
                                            color: ThemeTokens.textSecondary
                                        }
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: vm.stageDetailRationale !== ""
                                text: qsTr("安排依据：%1").arg(vm.stageDetailRationale)
                                wrapMode: Text.Wrap
                                color: ThemeTokens.textSecondary
                            }
                            AppButton {
                                visible: vm.stageDetailConfirmable
                                text: qsTr("确认采用这版阶段详情")
                                variant: "primary"
                                enabled: vm.stageAiState !== "ai_waiting"
                                onClicked: vm.confirmStageDetail()
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: !vm.stageDetailConfirmable
                                text: qsTr("已确认，本阶段内容以此为准。")
                                color: ThemeTokens.textSecondary
                            }
                        }
                    }
                    Card {
                        Layout.fillWidth: true
                        visible: vm.stageMaterialsModel.count > 0
                        padding: ThemeTokens.spacingMd
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: ThemeTokens.spacingSm
                            Text {
                                text: qsTr("资料（来自知识库）")
                                font.pixelSize: ThemeTokens.fontSizeSection
                                font.weight: Font.DemiBold
                                color: ThemeTokens.textPrimary
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("AI 根据本阶段目标从知识库推荐资料并说明理由。你可以逐条采用或排除，也可以随时改主意。")
                                wrapMode: Text.Wrap
                                color: ThemeTokens.textSecondary
                            }
                            Repeater {
                                model: vm.stageMaterialsModel
                                Card {
                                    required property string uid
                                    required property string kind
                                    required property string title
                                    required property string subtitle
                                    required property string detail
                                    required property string badge
                                    required property string badgeTone
                                    width: stageScroll.availableWidth
                                    padding: ThemeTokens.spacingSm
                                    ColumnLayout {
                                        anchors.fill: parent
                                        spacing: ThemeTokens.spacingXs
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text {
                                                Layout.fillWidth: true
                                                text: detail === "" ? title
                                                                    : qsTr("%1 · %2").arg(title, detail)
                                                elide: Text.ElideRight
                                                font.weight: Font.DemiBold
                                                color: ThemeTokens.textPrimary
                                            }
                                            StatusBadge { text: badge; tone: badgeTone }
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            visible: subtitle !== ""
                                            text: qsTr("推荐理由：%1").arg(subtitle)
                                            wrapMode: Text.Wrap
                                            color: ThemeTokens.textSecondary
                                        }
                                        RowLayout {
                                            visible: kind === "pending" || kind === "rejected"
                                            AppButton {
                                                text: qsTr("采用")
                                                variant: "primary"
                                                enabled: kind !== "accepted"
                                                onClicked: vm.respondStageMaterial(uid, "accepted")
                                            }
                                            AppButton {
                                                text: qsTr("排除")
                                                variant: "ghost"
                                                enabled: kind !== "rejected"
                                                onClicked: vm.respondStageMaterial(uid, "rejected")
                                            }
                                        }
                                        RowLayout {
                                            visible: kind === "accepted"
                                            AppButton {
                                                text: qsTr("改主意：排除")
                                                variant: "ghost"
                                                onClicked: vm.respondStageMaterial(uid, "rejected")
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Card {
                        Layout.fillWidth: true
                        visible: !vm.stageMissing
                        padding: ThemeTokens.spacingMd
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: ThemeTokens.spacingSm
                            Text {
                                text: qsTr("对这个阶段的想法告诉 AI")
                                font.pixelSize: ThemeTokens.fontSizeSection
                                font.weight: Font.DemiBold
                                color: ThemeTokens.textPrimary
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("你可以要求更换资料、调整内部顺序、增加或删减任务与练习。AI 会生成新版本，旧版本保留，是否采用仍由你确认。")
                                wrapMode: Text.Wrap
                                color: ThemeTokens.textSecondary
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: stageUserMessage.implicitHeight + ThemeTokens.spacingMd * 2
                                visible: root.lastStageGuidance !== ""
                                radius: ThemeTokens.radiusMd
                                color: ThemeTokens.bgLayer
                                Text {
                                    id: stageUserMessage
                                    anchors.fill: parent
                                    anchors.margins: ThemeTokens.spacingMd
                                    text: qsTr("我的想法：%1").arg(root.lastStageGuidance)
                                    wrapMode: Text.Wrap
                                    color: ThemeTokens.textPrimary
                                }
                            }
                            InfoBanner {
                                Layout.fillWidth: true
                                visible: root.stageAiReply !== ""
                                tone: "info"
                                text: root.stageAiReply
                            }
                            TextArea {
                                id: stageGuidance
                                Layout.fillWidth: true
                                Layout.preferredHeight: 90
                                placeholderText: qsTr("例如：把项目练习提前；任务 2 和 3 合并；希望用视频资料……")
                                wrapMode: TextArea.Wrap
                                enabled: vm.stageAiState !== "ai_waiting"
                            }
                            AppButton {
                                text: vm.stageAiState === "ai_waiting"
                                      ? qsTr("AI 正在生成阶段详情…")
                                      : (vm.stageDetailVersion === 0
                                         ? qsTr("让 AI 生成阶段详情")
                                         : qsTr("发送给 AI 并生成新版本"))
                                variant: "primary"
                                enabled: vm.stageAiState !== "ai_waiting"
                                         && vm.stageRouteConfirmed
                                onClicked: {
                                    root.lastStageGuidance = stageGuidance.text.trim()
                                    root.stageAiReply = qsTr("AI 正在结合你的想法和知识库安排这一阶段……")
                                    vm.aiGenerateStageDetail(root.lastStageGuidance)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    Connections {
        target: vm
        function onSelectionChanged() { root.rebuildRouteGraph() }
        function onAiStateChanged() {
            if (vm.aiState === "idle" && root.routeAiReply !== "") {
                if (vm.pageState === "error" || vm.pageState === "offline"
                        || vm.pageState === "conflict") {
                    root.routeAiReply = vm.lastError
                } else {
                    root.routeAiReply = qsTr("AI 已生成一版新路线。请检查上面的关系图和文字说明；满意后再确认采用。")
                    routeGuidance.text = ""
                }
            }
        }
        function onStageAiStateChanged() {
            if (vm.stageAiState === "idle" && root.stageAiReply !== "") {
                if (vm.pageState === "error" || vm.pageState === "offline"
                        || vm.pageState === "conflict") {
                    root.stageAiReply = vm.lastError
                } else {
                    root.stageAiReply = qsTr("AI 已生成一版新的阶段详情。请检查上面的内容安排与资料推荐；满意后再确认采用。")
                    stageGuidance.text = ""
                }
            }
        }
        function onPageStateChanged() {
            if ((vm.pageState === "error" || vm.pageState === "offline"
                    || vm.pageState === "conflict") && root.routeAiReply !== "")
                root.routeAiReply = vm.lastError
            if ((vm.pageState === "error" || vm.pageState === "offline"
                    || vm.pageState === "conflict") && root.stageAiReply !== "")
                root.stageAiReply = vm.lastError
        }
    }
    Component.onCompleted: {
        vm.refresh()
        root.rebuildRouteGraph()
    }
}
