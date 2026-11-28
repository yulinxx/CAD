import QtQuick
import QtQuick.Effects

// GoodLaser 关于框内容；外层 QDialog（模态、Esc、返回值）由 C++ 提供。
// 全部动画只用 QtQuick 2.15 原生 Behavior / on Animations
//
// 动画：
//   · 整体弹入（opacity + scale，OutBack 弹性）
//   · banner 入场 + 呼吸 + 发光边框脉冲
//   · logo 入场 scale 回弹 + 持续缓慢呼吸
//   · 产品名 scale 放大回弹
//   · 分隔线 0→满宽展开 + 发光脉冲
//   · 信息行 逐个滑入淡入
//   · 链接 hover 颜色变亮 + scale 放大
//   · 关闭按钮 hover 放大 + 发光脉冲边框

Rectangle {
    id: root

    signal closeRequested
    signal linkActivated(string url)

    readonly property bool dark: about.theme === "dark"
    readonly property color textColor: dark ? "#f6dcc2" : "#7c2d12"
    readonly property color mutedColor: dark ? "#d9b491" : "#9a3412"
    readonly property color accentColor: dark ? "#d97706" : "#ea580c"
    readonly property color linkColor: dark ? "#fbbf24" : "#c2410c"
    readonly property color glowColor:  dark ? "#fde68a" : "#f97316"
    readonly property int padding: 20

    implicitWidth: 460
    implicitHeight: content.implicitHeight + padding * 2

    // 根透明：背景/阴影由独立层绘制，便于 RectangularShadow 分层渲染。
    color: "transparent"

    // 阴影层：对话框 elevation
    RectangularShadow {
        anchors.fill: parent
        color: root.dark ? "#80000000" : "#30000000"
        offset: Qt.vector2d(0, 4)
        blur: 16
        radius: 10  // match aboutBg.radius
    }

    // 背景层：圆角 + 细边框
    Rectangle {
        id: aboutBg
        anchors.fill: parent
        radius: 10
        color: root.dark ? "#2a1d12" : "#fff7ed"
        border.width: 1
        border.color: root.dark ? "#4a3420" : "#fed7aa"
    }

    // —— 整体弹入动画 ——
    opacity: 0; scale: 0.94
    Behavior on opacity { NumberAnimation { duration: 360; easing.type: Easing.OutCubic } }
    Behavior on scale   { NumberAnimation { duration: 480; easing.type: Easing.OutBack } }
    Component.onCompleted: { root.opacity = 1; root.scale = 1.0 }

    Column {
        id: content
        x: root.padding
        y: root.padding
        width: root.width - root.padding * 2
        spacing: 12

        // —— 横幅（呼吸 + 发光脉冲）——
        Item {
            id: bannerWrapper
            width: parent.width
            height: bannerText.implicitHeight + 14

            // glow behind banner (replaces removed layer.effect: Glow)
            RectangularShadow {
                anchors.fill: parent
                color: root.glowColor
                offset: Qt.vector2d(0, 0)
                blur: 10
                radius: 6
                spread: 0.25
                opacity: 0.5
            }

            Rectangle {
                id: banner
                anchors.fill: parent
                radius: 6
                gradient: Gradient {
                    GradientStop { position: 0.0; color: root.dark ? "#3a2818" : "#ffedd5" }
                    GradientStop { position: 1.0; color: root.dark ? "#4a3420" : "#fed7aa" }
                }
                border.width: 1
                border.color: root.accentColor

                // 呼吸透明度（放轻放慢）
                NumberAnimation on opacity {
                    from: 0.97; to: 1.0; duration: 2400; loops: Animation.Infinite; easing.type: Easing.InOutSine
                }

                // 入场：scale + opacity
                opacity: 0; scale: 0.9
                Behavior on opacity { NumberAnimation { duration: 360; easing.type: Easing.OutCubic } }
                Behavior on scale   { NumberAnimation { duration: 480; easing.type: Easing.OutBack } }
                Component.onCompleted: { banner.opacity = 1; banner.scale = 1.0 }

                Text {
                    id: bannerText
                    anchors.centerIn: parent
                    width: parent.width - 16
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    text: about.banner
                    color: root.mutedColor
                    font.pixelSize: 12
                }
            }
        }

        // —— logo + 产品名 / 版本 ——
        Row {
            id: headerRow
            width: parent.width
            spacing: 14
            opacity: 0; x: -8
            Behavior on opacity { NumberAnimation { duration: 380; easing.type: Easing.OutCubic } }
            Behavior on x       { NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }
            Component.onCompleted: { headerRow.opacity = 1; headerRow.x = 0 }

            Image {
                id: logo
                objectName: "GoodLaser.about.logo"
                source: about.logo
                visible: about.logo !== "" && status === Image.Ready
                width: visible ? 64 : 0
                height: visible ? 64 : 0
                sourceSize.width: 128
                sourceSize.height: 128
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true

                // 入场放大回弹
                scale: 1.12
                Behavior on scale { NumberAnimation { duration: 600; easing.type: Easing.OutBack } }
                Component.onCompleted: { logo.scale = 1.0 }

                // 持续缓慢呼吸
                NumberAnimation on scale {
                    from: 0.985; to: 1.015
                    duration: 3200; loops: Animation.Infinite; easing.type: Easing.InOutSine
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - (logo.visible ? logo.width + parent.spacing : 0)
                spacing: 4

                Text {
                    id: productName
                    objectName: "GoodLaser.about.product"
                    width: parent.width
                    visible: text !== ""
                    text: about.product
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: root.textColor
                    font.bold: true
                    font.pixelSize: 18

                    // 入场 scale 放大回弹
                    opacity: 0; scale: 0.85
                    Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                    Behavior on scale   { NumberAnimation { duration: 480; easing.type: Easing.OutBack } }
                    Component.onCompleted: { productName.opacity = 1; productName.scale = 1.0 }
                }

                Text {
                    id: versionText
                    objectName: "GoodLaser.about.version"
                    width: parent.width
                    visible: text !== ""
                    text: about.version
                    textFormat: Text.PlainText
                    color: root.mutedColor
                    font.pixelSize: 12

                    opacity: 0
                    Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                    Component.onCompleted: { versionText.opacity = 1 }
                }
            }
        }

        // —— 分隔线（从 0 展开 + 发光脉冲）——
        Rectangle {
            id: mainSep
            width: parent.width; height: 2; radius: 1
            color: root.accentColor; opacity: 0

            Behavior on width   { NumberAnimation { duration: 500; easing.type: Easing.OutCubic } }
            Behavior on opacity { NumberAnimation { duration: 240;} }
            Component.onCompleted: { mainSep.width = mainSep.parent.width; mainSep.opacity = 0.85 }
        }

        // —— 信息行：逐个滑入淡入 ——
        Column {
            width: parent.width
            spacing: 6

            Repeater {
                model: about.rows

                delegate: Row {
                    id: rowDelegate
                    width: parent.width; spacing: 10
                    readonly property bool hasLabel: modelData.label !== ""
                    readonly property bool isLink: modelData.link !== ""
                    property int _index: index
                    opacity: 0; x: 10

                    // 错峰入场（每项 90ms）
                    Behavior on opacity { NumberAnimation {
                        duration: 300+ rowDelegate._index * 90
                        easing.type: Easing.OutCubic
                    } }
                    Behavior on x { NumberAnimation {
                        duration: 340+ rowDelegate._index * 90
                        easing.type: Easing.OutCubic
                    } }
                    Component.onCompleted: { rowDelegate.opacity = 1; rowDelegate.x = 0 }

                    Text {
                        id: rowLabel
                        visible: parent.hasLabel
                        width: visible ? 90 : 0
                        horizontalAlignment: Text.AlignRight
                        text: modelData.label
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: root.mutedColor; font.pixelSize: 12
                    }

                    Text {
                        id: rowValue
                        objectName: "GoodLaser.about." + modelData.key
                        width: parent.width - (rowLabel.visible ? rowLabel.width + parent.spacing : 0)
                        text: modelData.value
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        font.underline: parent.isLink
                        font.pixelSize: 12

                        // 链接 hover：颜色变亮 + scale 放大
                        color: {
                            if (parent.isLink && linkArea.containsMouse) return root.glowColor
                            if (parent.isLink) return root.linkColor
                            return root.textColor
                        }
                        scale: linkArea.containsMouse && parent.isLink ? 1.06 : 1.0

                        Behavior on color { ColorAnimation { duration: 140 } }
                        Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutBack } }

                        MouseArea {
                            id: linkArea
                            anchors.fill: parent
                            enabled: parent.parent.isLink
                            hoverEnabled: parent.parent.isLink
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: root.linkActivated(modelData.link)
                        }
                    }
                }
            }
        }

        // —— 关闭按钮（hover 放大 + 发光脉冲边框）——
        Item {
            width: parent.width
            height: closeButton.height

            Item {
                id: closeButton
                objectName: "GoodLaser.about.close"
                anchors.right: parent.right
                width: Math.max(88, closeLabel.implicitWidth + 28)
                height: 30

                Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutBack } }
                scale: closeArea.containsMouse ? 1.05 : 1.0

                Rectangle {
                    id: closeBtnBg
                    anchors.fill: parent
                    radius: 6
                    color: "transparent"
                    border.width: 1
                    border.color: closeArea.containsMouse ? root.glowColor : root.accentColor
                    Behavior on border.color { ColorAnimation { duration: 120 } }
                }

                // hover / pressed 填充覆盖层（一次性淡入，不循环脉冲）
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: closeArea.pressed ? (root.dark ? "#4a3420" : "#fed7aa")
                                            : (root.dark ? "#3a2818" : "#ffedd5")
                    opacity: (closeArea.containsMouse || closeArea.pressed) ? 0.7 : 0
                    Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                }

                // hover 发光阴影
                RectangularShadow {
                    anchors.fill: closeBtnBg
                    color: root.glowColor
                    offset: Qt.vector2d(0, 0)
                    blur: 8
                    radius: 6  // match closeBtnBg.radius
                    opacity: closeArea.containsMouse ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                }

                opacity: 0
                Behavior on opacity { NumberAnimation { duration: 360; easing.type: Easing.OutCubic } }
                Component.onCompleted: { closeButton.opacity = 1 }

                Text {
                    id: closeLabel
                    anchors.centerIn: parent
                    text: about.closeText
                    textFormat: Text.PlainText
                    color: root.textColor; font.pixelSize: 12
                }

                MouseArea {
                    id: closeArea
                    anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.closeRequested()
                }
            }
        }
    }
}
