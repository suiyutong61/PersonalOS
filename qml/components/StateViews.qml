import QtQuick
import QtQuick.Layouts
import PersonOS

// 页面状态视图（requirements 10.8：正常/空/加载/离线/失败/AI等待/知识不足/冲突）
// 每个页面统一经由此组件表达非正常状态；正常内容由页面自行布置（visible 绑定）。
ColumnLayout {
    id: root
    // idle / loading / ready / empty / error / offline / ai_waiting /
    // knowledge_limited / conflict
    property string pageState: "idle"
    property string message: ""            // 错误/离线等状态的可读说明
    property int contentCount: 0           // ready 且为 0 时显示空状态

    readonly property bool showLoading: pageState === "loading"
    readonly property bool showEmpty: pageState === "empty"
                                      || (pageState === "ready" && contentCount === 0)
    readonly property bool showError: pageState === "error"
    readonly property bool showOffline: pageState === "offline"
    readonly property bool showAiWaiting: pageState === "ai_waiting"
    readonly property bool showKnowledgeLimited: pageState === "knowledge_limited"
    readonly property bool showConflict: pageState === "conflict"

    spacing: ThemeTokens.spacingSm

    Item {
        Layout.preferredHeight: ThemeTokens.spacingMd
        visible: !root.showLoading && !root.showEmpty && !root.showError
                 && !root.showOffline && !root.showAiWaiting && !root.showKnowledgeLimited
                 && !root.showConflict
    }

    ColumnLayout {
        visible: root.showLoading
        spacing: ThemeTokens.spacingSm
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("加载中…")
            font.pixelSize: ThemeTokens.fontSizeBody
            color: ThemeTokens.textSecondary
        }
    }

    EmptyState {
        visible: root.showEmpty
        Layout.fillWidth: true
        title: qsTr("暂无内容")
        hint: qsTr("完成首次操作后，这里会展示数据")
    }

    InfoBanner {
        visible: root.showError
        Layout.fillWidth: true
        tone: "error"
        text: qsTr("加载失败：%1").arg(root.message)
    }

    InfoBanner {
        visible: root.showOffline
        Layout.fillWidth: true
        tone: "warning"
        // 优先展示具体指引(如"未配置模型连接"),通用话术仅为兜底
        text: root.message !== "" ? root.message
                                  : qsTr("离线：本地功能仍可用，依赖外部 AI 的操作需等待网络恢复")
    }

    InfoBanner {
        visible: root.showAiWaiting
        Layout.fillWidth: true
        tone: "info"
        text: qsTr("AI 处理中，结果就绪后会更新…")
    }

    InfoBanner {
        visible: root.showKnowledgeLimited
        Layout.fillWidth: true
        tone: "warning"
        text: qsTr("知识库支持有限：%1").arg(root.message)
    }

    InfoBanner {
        visible: root.showConflict
        Layout.fillWidth: true
        tone: "warning"
        text: qsTr("数据冲突：%1").arg(root.message)
    }
}
