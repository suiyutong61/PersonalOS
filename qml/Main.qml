import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import PersonOS

// Personal OS 导航壳（requirements 10.8 信息架构；DR-031 设计系统）
// 九页全部由页面级 ViewModel 驱动；旧 ApplicationService 门面已退役，
// 页面不直接访问 Repository/数据库。
ApplicationWindow {
    id: window
    width: 1000
    height: 720
    // 最小宽度 1000(用户指定):等于默认宽度,即窗口横向不可再缩小
    minimumWidth: 1000
    // 最小高度 760(用户指定):到达后窗口边框不可再缩小
    // (右栏 ScrollView 仅作极端情况兜底)
    minimumHeight: 760
    visible: true
    title: qsTr("Personal OS")

    // 设计令牌统一入口（DR-031：页面不得散落常量）
    color: ThemeTokens.bgBase

    // 应用内全局提醒横幅（覆盖层，位于页面之上；Toast 自身 z:100）
    Toast {
        anchors.top: parent.top
        anchors.topMargin: ThemeTokens.spacingLg
        anchors.horizontalCenter: parent.horizontalCenter
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: ThemeTokens.spacingSm
        spacing: ThemeTokens.spacingSm

        // 侧边导航（九页信息架构）
        Rectangle {
            Layout.preferredWidth: 168
            Layout.fillHeight: true
            color: ThemeTokens.bgCard
            radius: ThemeTokens.radiusMd
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: ThemeTokens.spacingSm
                spacing: ThemeTokens.spacingXs

                Text {
                    text: qsTr("Personal OS")
                    font.pixelSize: ThemeTokens.fontSizeSection
                    font.weight: Font.DemiBold
                    color: ThemeTokens.textPrimary
                    Layout.bottomMargin: ThemeTokens.spacingSm
                }

                Repeater {
                    model: [
                        { label: qsTr("首页总览"), glyph: "⌂" },
                        { label: qsTr("目标与路线"), glyph: "◎" },
                        { label: qsTr("当前 MEL"), glyph: "▶" },
                        { label: qsTr("日历与提醒"), glyph: "▤" },
                        { label: qsTr("验收与复盘"), glyph: "✓" },
                        { label: qsTr("历史与成就"), glyph: "✦" },
                        { label: qsTr("AI 助手"), glyph: "✦" },
                        { label: qsTr("知识库"), glyph: "▣" },
                        { label: qsTr("设置"), glyph: "⚙" }
                    ]
                    NavItem {
                        Layout.fillWidth: true
                        text: modelData.label
                        glyph: modelData.glyph
                        active: stack.currentIndex === index
                        onClicked: stack.currentIndex = index
                    }
                }
            }
        }

        // 页面区
        StackLayout {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: 0

            // 切页刷新:各页只在启动时 onCompleted 刷一次,而数据会被
            // 其他页的业务事件改变(结算→复盘/成就、导入→检索、建目标→
            // 总览计数),不刷新则跨页数据陈旧、空页成死路
            onCurrentIndexChanged: {
                const pages = [dashboardPage, goalRoutePage, melPage, calendarPage,
                               reviewPage, historyAchievementPage, advisorPage,
                               knowledgePage, settingsPage]
                if (currentIndex >= 0 && currentIndex < pages.length)
                    pages[currentIndex].refresh()
            }

            DashboardPage { id: dashboardPage }
            GoalRoutePage { id: goalRoutePage }
            MelPage { id: melPage }
            CalendarPage { id: calendarPage }
            ReviewPage { id: reviewPage }
            HistoryAchievementPage { id: historyAchievementPage }
            AdvisorPage { id: advisorPage }
            KnowledgePage { id: knowledgePage }
            SettingsPage { id: settingsPage }
        }
    }
}
