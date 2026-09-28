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
    minimumWidth: 640
    minimumHeight: 480
    visible: true
    title: qsTr("Personal OS")

    // 设计令牌统一入口（DR-031：页面不得散落常量）
    color: ThemeTokens.bgBase

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

            DashboardPage {}
            GoalRoutePage {}
            MelPage {}
            CalendarPage {}
            ReviewPage {}
            HistoryAchievementPage {}
            AdvisorPage {}
            KnowledgePage {}
            SettingsPage {}
        }
    }
}
