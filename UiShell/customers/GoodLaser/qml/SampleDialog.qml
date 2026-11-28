import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

// GoodLaser 示例算法对话框：QDialog 外壳由 C++ 提供，这里只负责绘制内容。
// 全部动画只用 QtQuick 2.15 原生 Behavior / on Animations，一行 JS 都不写。
//
// 可欣赏的动画：
//   · 整体弹入（opacity + scale，OutBack 弹性）
//   · 标题 / 分隔线 入场淡入
//   · Run/Cancel 按钮 hover 放大 + ColorAnimation + 发光脉冲
//   · Slider 进度条 发光脉冲
//   · Slider 手柄 按下时放大 + 颜色脉冲
//   · Slider 值变 标签 scale 回弹 + 颜色闪烁
//
// C++ ↔ QML 接触点：
//   输入：两个上下文属性 msParams / msLabels
//   输出：accepted() / rejected() 信号 + targetValue / iterationsValue Q_PROPERTY

Rectangle {
    id: root

    // 当前 Qt 5.15.2 部署缺失 DoubleSpinBox，改用 TextField 接收浮点输入；parseFloat 兜底非数字。
    property real targetValue: { var v = parseFloat(targetSpin.text); return isNaN(v) ? 0.0 : v }
    property int iterationsValue: iterationsSlider.value

    signal accepted()
    signal rejected()

    readonly property bool dark: msTheme === "dark"
    readonly property color bgColor:     dark ? "#2a1d12" : "#fff7ed"
    readonly property color panelColor:   dark ? "#3a2818" : "#ffedd5"
    readonly property color accentColor:  dark ? "#d97706" : "#ea580c"
    readonly property color titleColor:   dark ? "#f0c9a6" : "#9a3412"
    readonly property color textColor:    dark ? "#d9b491" : "#7c2d12"
    readonly property color borderColor:  dark ? "#b45309" : "#fb923c"
    readonly property color glowColor:    dark ? "#fbbf24" : "#f97316"

    // 根透明：背景/阴影由独立层绘制，便于 RectangularShadow 分层渲染。
    color: "transparent"

    implicitWidth: 380
    implicitHeight: content.implicitHeight + 24

    // 阴影层：对话框 elevation
    RectangularShadow {
        anchors.fill: parent
        color: root.dark ? "#80000000" : "#30000000"
        offset: Qt.vector2d(0, 4)
        blur: 16
        radius: 10  // match dlgBg.radius
    }

    // 背景层：圆角 + 细边框
    Rectangle {
        id: dlgBg
        anchors.fill: parent
        radius: 10
        color: root.bgColor
        border.width: 1
        border.color: root.borderColor
    }

    // —— 弹入动画：opacity + scale，OutBack 弹性回弹 ——
    opacity: 0; scale: 0.92
    Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
    Behavior on scale   { NumberAnimation { duration: 480; easing.type: Easing.OutBack } }
    Component.onCompleted: { root.opacity = 1; root.scale = 1.0 }

    Column {
        id: content
        anchors.centerIn: parent
        width: parent.width - 48
        spacing: 14

        // —— 标题 ——
        Text {
            id: dlgTitle
            text: msLabels.windowTitle
            color: root.titleColor
            font.bold: true
            font.pixelSize: 16
            opacity: 0; y: -8
            Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
            Behavior on y       { NumberAnimation { duration: 360; easing.type: Easing.OutCubic } }
            Component.onCompleted: { dlgTitle.opacity = 1; dlgTitle.y = 0 }
        }

        Rectangle {
            id: dlgSep
            width: parent.width; height: 2; radius: 1
            color: root.accentColor; opacity: 0

            Behavior on width   { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
            Behavior on opacity { NumberAnimation { duration: 240;} }
            Component.onCompleted: { dlgSep.width = dlgSep.parent.width; dlgSep.opacity = 0.85 }
        }

        // —— Target value ——
        Column {
            width: parent.width; spacing: 6

            Text {
                text: msLabels.target
                color: root.titleColor
                font.pixelSize: 12; font.bold: true
            }

            TextField {
                id: targetSpin
                width: parent.width
                text: msParams.target !== undefined ? Number(msParams.target).toFixed(4) : "2.0000"
                color: root.titleColor
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                Behavior on color { ColorAnimation { duration: 140 } }
            }
        }

        // —— Iterations 滑块（自绘发光轨迹 + 脉冲标签）——
        Column {
            width: parent.width; spacing: 6

            Row {
                width: parent.width; spacing: 8

                Text {
                    id: iterationsLabel
                    text: msLabels.iterations + ": " + iterationsSlider.value
                    color: root.titleColor
                    font.pixelSize: 12; font.bold: true
                    Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutBack } }
                    Behavior on color { ColorAnimation { duration: 140 } }
                }

                Item { width: 1; height: 1; Layout.fillWidth: true }
            }

            // Slider 外层（自绘发光轨迹，盖在 Slider 上面）
            Item {
                width: parent.width; height: 24

                // 背景槽
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 4; radius: 2
                    color: root.panelColor
                }

                // 已填充部分（发光脉冲）
                Rectangle {
                    id: filledTrack
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(8, (iterationsSlider.value - iterationsSlider.from) / (iterationsSlider.to - iterationsSlider.from) * parent.width)
                    height: 4; radius: 2
                    color: root.accentColor
                    Behavior on width { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                    ColorAnimation on color {
                        from: root.glowColor; to: root.accentColor
                        duration: 2600; loops: Animation.Infinite; easing.type: Easing.InOutSine
                    }
                }

                // Slider 控件（透明背景）
                Slider {
                    id: iterationsSlider
                    anchors.fill: parent
                    from: 1; to: 100
                    value: Math.max(1, Math.min(100,
                               msParams.maxIterations !== undefined ? Number(msParams.maxIterations) : 20))
                    stepSize: 1

                    background: Rectangle { opacity: 0 }
                    handle: Rectangle {
                        width: 16; height: 16; radius: 8
                        color: root.glowColor
                        border.width: 2; border.color: root.titleColor

                        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutBack } }
                        Behavior on color { ColorAnimation { duration: 140 } }
                        scale: iterationsSlider.pressed ? 1.3 : 1.0

                        // 空闲静止，pressed 时靠 scale 放大反馈（克制）
                    }

                    onValueChanged: {
                        iterationsLabel.text = msLabels.iterations + ": " + Math.round(value)
                        iterationsLabel.scale = 1.12
                        iterationsLabel.color = root.glowColor
                    }
                }
            }
        }

        Rectangle {
            id: dlgSep2
            width: parent.width; height: 1
            color: root.glowColor; opacity: 0
            Behavior on opacity { NumberAnimation { duration: 300;} }
            Component.onCompleted: { dlgSep2.opacity = 0.35 }
        }

        // —— 固定显示的 Method + Tolerance ——
        Text {
            id: methodLine
            text: msLabels.methodFixed
                  .arg(msParams.tolerance !== undefined ? Number(msParams.tolerance).toFixed(6) : "1e-6")
            color: root.textColor; font.pixelSize: 11
            wrapMode: Text.Wrap
            opacity: 0
            Behavior on opacity { NumberAnimation { duration: 360; easing.type: Easing.OutCubic } }
            Component.onCompleted: { methodLine.opacity = 1 }
        }

        Item { width: 1; height: 1; Layout.fillHeight: true }

        // —— Run / Cancel 按钮行（hover 放大 + 发光脉冲）——
        Row {
            width: parent.width; spacing: 12

            // Run 按钮（主按钮：渐变背景 + hover 发光阴影）
            Item {
                id: runBtn
                width: Math.max(88, runLabel.implicitWidth + 28)
                height: 32

                Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutBack } }
                scale: runArea.containsMouse ? 1.06 : 1.0

                // 默认渐变背景（accent → glow）
                Rectangle {
                    id: runBtnBg
                    anchors.fill: parent
                    radius: 6
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: root.accentColor }
                        GradientStop { position: 1.0; color: root.glowColor }
                    }
                    border.width: 1
                    border.color: root.accentColor
                    Behavior on border.color { ColorAnimation { duration: 120 } }
                }

                // hover / pressed 覆盖层（一次性淡入，不再循环脉冲）
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: runArea.pressed ? (root.dark ? "#4a3420" : "#fed7aa") : root.panelColor
                    opacity: (runArea.containsMouse || runArea.pressed) ? 0.85 : 0
                    Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                }

                // hover 发光阴影
                RectangularShadow {
                    anchors.fill: runBtnBg
                    color: root.glowColor
                    offset: Qt.vector2d(0, 0)
                    blur: 8
                    radius: 6  // match runBtnBg.radius
                    opacity: runArea.containsMouse ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                }

                Text {
                    id: runLabel
                    anchors.centerIn: parent
                    text: msLabels.runButton
                    color: runArea.containsMouse ? root.titleColor : (root.dark ? "#2a1d12" : "#fff7ed")
                    font.pixelSize: 12; font.bold: true
                    Behavior on color { ColorAnimation { duration: 120 } }
                }

                MouseArea {
                    id: runArea
                    anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.accepted()
                }
            }

            // Cancel 按钮（次按钮：描边 + hover 填充，无循环脉冲）
            Item {
                id: cancelBtn
                width: Math.max(88, cancelLabel.implicitWidth + 28)
                height: 32

                Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutBack } }
                scale: cancelArea.containsMouse ? 1.05 : 1.0

                Rectangle {
                    id: cancelBtnBg
                    anchors.fill: parent
                    radius: 6
                    color: "transparent"
                    border.width: 1
                    border.color: cancelArea.containsMouse ? root.glowColor : root.accentColor
                    Behavior on border.color { ColorAnimation { duration: 120 } }
                }

                // hover / pressed 填充覆盖层
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: cancelArea.pressed ? (root.dark ? "#4a3420" : "#fed7aa") : root.panelColor
                    opacity: (cancelArea.containsMouse || cancelArea.pressed) ? 0.7 : 0
                    Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                }

                Text {
                    id: cancelLabel
                    anchors.centerIn: parent
                    text: msLabels.cancelButton
                    color: root.titleColor; font.pixelSize: 12
                }

                MouseArea {
                    id: cancelArea
                    anchors.fill: parent; hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.rejected()
                }
            }

            Item { width: 1; height: 1; Layout.fillWidth: true }
        }
    }
}
