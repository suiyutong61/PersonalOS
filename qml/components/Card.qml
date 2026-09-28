import QtQuick
import PersonOS

// 卡片容器（DR-031：统一圆角/边框/阴影，不散落常量）
// 从内容推导隐式尺寸:布局中不再需要手动绑定 preferredHeight,
// 滚动布局下也不会因零高度导致内容互相叠压。
Rectangle {
    id: root
    radius: ThemeTokens.radiusMd
    color: ThemeTokens.bgCard
    border.width: 1
    border.color: ThemeTokens.borderColor

    // 可选：内容吸附与背景层级
    property int padding: ThemeTokens.spacingMd

    default property alias content: container.data
    property alias contentItem: container

    implicitWidth: container.implicitWidth + root.padding * 2
    implicitHeight: container.implicitHeight + root.padding * 2

    Item {
        id: container
        anchors.fill: parent
        anchors.margins: root.padding
        // 子项通常用 anchors.fill 吸附容器,其隐式尺寸不回写容器;
        // 这里显式取各子项隐式尺寸的最大并集(绑定会跟踪每个子项的变化)。
        implicitWidth: {
            var w = 0
            for (var i = 0; i < children.length; ++i) {
                const child = children[i]
                if (child.implicitWidth + child.x > w)
                    w = child.implicitWidth + child.x
            }
            return w
        }
        implicitHeight: {
            var h = 0
            for (var i = 0; i < children.length; ++i) {
                const child = children[i]
                if (child.implicitHeight + child.y > h)
                    h = child.implicitHeight + child.y
            }
            return h
        }
    }
}
