import QtQuick
import QtQuick.Layouts
import QtQuick.Effects

// 面板
// 深色 / 浅色双主题保持可用。
//
// 动画：
//   · 根面板       —— 边框颜色缓慢脉动（ColorAnimation loops: Infinite）
//   · 整体入场     —— opacity 0→1 + scale 0.98→1.0，弹性 easing
//   · toolbar 按钮 —— hover 放大 + ColorAnimation + 脉冲边框
//   · tree 列表    —— 分隔线展开 + 项逐个淡入
//   · log 面板     —— 属性行逐个淡入
//   · center logo  —— 持续旋转 + 发光边框脉冲 + 呼吸 scale + 脉冲光环
//   · center 标题  —— 入场放大回弹
//   · clock        —— 每秒数字闪烁 + scale 脉冲 + 发光背景呼吸
//   · status       —— 消息滑入

Rectangle {
    id: root

    readonly property bool dark: msTheme === "dark"
    readonly property color bgColor:     dark ? "#2a1d12" : "#fff7ed"
    readonly property color panelColor:   dark ? "#3a2818" : "#ffedd5"
    readonly property color accentColor:  dark ? "#d97706" : "#ea580c"
    readonly property color titleColor:   dark ? "#f0c9a6" : "#9a3412"
    readonly property color textColor:    dark ? "#d9b491" : "#7c2d12"
    readonly property color borderColor:  dark ? "#b45309" : "#fb923c"
    readonly property color glowColor:    dark ? "#fbbf24" : "#f97316"

    // 根面板颜色：使用主题背景色。
    color: bgColor

    // —— 阴影层：先绘制，落在背景之下，提供 elevation 层次感 ——
    RectangularShadow {
        anchors.fill: parent
        color: root.dark ? "#66000000" : "#22000000"
        offset: Qt.vector2d(0, 2)
        blur: 10
        radius: 6  // match bgLayer.radius
    }

    // —— 背景层：圆角 + 细边框，空闲静止（克制不吵） ——
    Rectangle {
        id: bgLayer
        anchors.fill: parent
        radius: 6
        color: root.bgColor
        border.width: 1
        border.color: root.borderColor
    }

    // —— 整体入场（opacity + scale，弹性回弹） ——
    opacity: 0
    scale: 0.98
    Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
    Behavior on scale   { NumberAnimation { duration: 360; easing.type: Easing.OutBack } }
    Component.onCompleted: { root.opacity = 1; root.scale = 1.0 }

    // —— 根据 panelId 选择内容组件 ——
    Loader {
        id: contentLoader
        anchors.fill: parent
        asynchronous: false
        sourceComponent: {
            switch (msPanelId) {
                case "toolbar":  return toolbarContent
                case "tree":     return treeContent
                case "log":      return logContent
                case "status":   return statusContent
                case "clock":    return clockContent
                case "center":   return centerContent
                default:         return defaultContent
            }
        }
    }

    // =========================================================================
    // 组件定义
    // =========================================================================

    // —— 默认样本：QML panel 展示，入场逐级淡入 ——
    Component {
        id: defaultContent
        Column {
            anchors.centerIn: parent
            spacing: 6

            Text {
                id: t1
                width: Math.max(180, implicitWidth)
                horizontalAlignment: Text.AlignCenter
                text: msTextTitle
                color: root.titleColor
                font.bold: true
                font.pixelSize: 15
                opacity: 0
                Behavior on opacity { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
                Component.onCompleted: t1.opacity = 1
            }

            Text {
                id: t2
                width: parent.width
                horizontalAlignment: Text.AlignCenter
                text: msTextBody
                color: root.textColor
                font.pixelSize: 12
                opacity: 0
                Behavior on opacity { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
                Component.onCompleted: t2.opacity = 1
            }

            Rectangle {
                id: hintBox
                width: 210
                height: 26
                radius: 4
                anchors.horizontalCenter: parent.horizontalCenter
                color: root.panelColor
                border.width: 1
                border.color: root.accentColor
                opacity: 0; scale: 0.9
                Behavior on opacity { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
                Behavior on scale   { NumberAnimation { duration: 320; easing.type: Easing.OutBack } }
                Component.onCompleted: { hintBox.opacity = 1; hintBox.scale = 1.0 }

                Text {
                    anchors.centerIn: parent
                    text: msTextHint
                    color: root.titleColor
                    font.pixelSize: 11
                }
            }
        }
    }

    // —— top.toolbar：Banner + Select Tool（hover 放大 + 脉冲边框）——
    Component {
        id: toolbarContent
        Row {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 12

            Text {
                id: titleLabel
                anchors.verticalCenter: parent.verticalCenter
                text: msTextTitle
                color: root.titleColor
                font.bold: true
                font.pixelSize: 14
                opacity: 0; x: -12
                Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                Behavior on x       { NumberAnimation { duration: 360; easing.type: Easing.OutCubic } }
                Component.onCompleted: { titleLabel.opacity = 1; titleLabel.x = 0 }
            }

            Item {
                id: toolButton
                width: Math.max(120, selectLabel.implicitWidth + 28)
                height: 32
                anchors.verticalCenter: parent.verticalCenter

                // hover 放大（同时也负责入场 scale 过渡）
                scale: pressArea.containsMouse ? 1.06 : 1.0
                Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutBack } }

                // 默认渐变背景层（accent → glow）
                Rectangle {
                    id: toolBtnBg
                    anchors.fill: parent
                    radius: 6
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: root.accentColor }
                        GradientStop { position: 1.0; color: root.glowColor }
                    }
                    border.width: 1
                    border.color: pressArea.containsMouse ? root.glowColor : root.accentColor
                    Behavior on border.color { ColorAnimation { duration: 120 } }
                }

                // hover / pressed 覆盖层（克制：一次性淡入，不再无限脉冲）
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: pressArea.pressed ? (root.dark ? "#4a3420" : "#fed7aa") : root.panelColor
                    opacity: (pressArea.containsMouse || pressArea.pressed) ? 0.85 : 0
                    Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
                }

                // hover 发光阴影
                RectangularShadow {
                    anchors.fill: toolBtnBg
                    color: root.glowColor
                    offset: Qt.vector2d(0, 0)
                    blur: 8
                    radius: 6  // match toolBtnBg.radius
                    opacity: pressArea.containsMouse ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
                }

                // 入场：淡入 + 放大（Component.onCompleted 触发 Behavior）
                opacity: 0; scale: 0.88
                Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                Component.onCompleted: { toolButton.opacity = 1; toolButton.scale = 1.0 }

                Text {
                    id: selectLabel
                    anchors.centerIn: parent
                    text: msTextBody
                    color: root.titleColor
                    font.pixelSize: 12
                    font.bold: pressArea.containsMouse
                    Behavior on color { ColorAnimation { duration: 120 } }
                }

                MouseArea {
                    id: pressArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                }
            }

            Item { width: 1; height: 1; Layout.fillWidth: true }
        }
    }

    // —— left.tree：Tools 面板（分隔线展开 + 项逐个淡入）——
    Component {
        id: treeContent
        Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 6

            Text {
                id: treeTitle
                text: msTextTitle
                color: root.titleColor
                font.bold: true
                font.pixelSize: 13
                opacity: 0; y: -6
                Behavior on opacity { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
                Behavior on y       { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
                Component.onCompleted: { treeTitle.opacity = 1; treeTitle.y = 0 }
            }

            Rectangle {
                id: sep1
                width: parent.width; height: 2; radius: 1
                color: root.accentColor
                opacity: 0

                // 入场：宽度从 0 展开 + 淡入（入场后静止，不再持续脉冲）
                Behavior on width   { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
                Behavior on opacity { NumberAnimation { duration: 240;} }
                Component.onCompleted: { sep1.width = sep1.parent.width; sep1.opacity = 0.8 }
            }

            Column {
                id: toolList
                width: parent.width
                spacing: 4

                Repeater {
                    model: ["Laser Head", "Focus Lens", "Gas Nozzle"]

                    // Item 作为容器：Row 和 MouseArea 叠在里面，两者都合法用 anchors.fill
                    Item {
                        id: rowDelegate
                        width: parent.width
                        height: 22
                        opacity: 0; x: 12
                        Behavior on opacity { NumberAnimation { duration: 280+ rowDelegate._index * 100; easing.type: Easing.OutCubic } }
                        Behavior on x { NumberAnimation { duration: 320+ rowDelegate._index * 100; easing.type: Easing.OutCubic } }
                        property int _index: index
                        Component.onCompleted: { rowDelegate.opacity = 1; rowDelegate.x = 0 }

                        // 视觉层：Row 排列图标 + 文字
                        Row {
                            id: contentRow
                            anchors.fill: parent  // Item 里 anchors.fill 合法
                            spacing: 6

                            Rectangle {
                                id: dot
                                width: 8; height: 8; radius: 2
                                color: root.accentColor
                                Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutBack } }
                                Behavior on color { ColorAnimation { duration: 140 } }
                            }

                            Text {
                                id: rowText
                                text: modelData
                                color: root.textColor
                                font.pixelSize: 12
                                Behavior on color { ColorAnimation { duration: 120 } }
                            }
                        }

                        // 交互层：MouseArea 覆盖整个 Item（Row 外面）
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
                            onContainsMouseChanged: {
                                dot.color = containsMouse ? root.glowColor : root.accentColor
                                dot.scale = containsMouse ? 1.4 : 1.0
                                rowText.color = containsMouse ? root.titleColor : root.textColor
                            }
                        }
                    }
                }
            }

            Item { width: 1; height: 1; Layout.fillHeight: true }
        }
    }

    // —— left.log：Properties 面板（属性行逐个淡入）——
    Component {
        id: logContent
        Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 6

            Text {
                id: logTitle
                text: msTextTitle
                color: root.titleColor
                font.bold: true
                font.pixelSize: 13
                opacity: 0; y: -6
                Behavior on opacity { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }
                Behavior on y       { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
                Component.onCompleted: { logTitle.opacity = 1; logTitle.y = 0 }
            }

            Rectangle {
                id: sep2
                width: parent.width; height: 2; radius: 1
                color: root.accentColor; opacity: 0

                Behavior on width   { NumberAnimation { duration: 360; easing.type: Easing.OutCubic } }
                Behavior on opacity { NumberAnimation { duration: 240;} }
                Component.onCompleted: { sep2.width = sep2.parent.width; sep2.opacity = 0.7 }
            }

            // 属性行 1: Width
            Row {
                id: row1; width: parent.width; spacing: 8; height: 22
                opacity: 0; x: 12
                Behavior on opacity { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
                Behavior on x       { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                Component.onCompleted: { row1.opacity = 1; row1.x = 0 }

                Text { text: "Width:"; color: root.textColor; font.pixelSize: 12; width: 64 }
                Text { text: "120.0 mm"; color: root.titleColor; font.pixelSize: 12; font.bold: true }
            }

            // 属性行 2: Height
            Row {
                id: row2; width: parent.width; spacing: 8; height: 22
                opacity: 0; x: 12
                Behavior on opacity { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
                Behavior on x       { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                Component.onCompleted: { row2.opacity = 1; row2.x = 0 }

                Text { text: "Height:"; color: root.textColor; font.pixelSize: 12; width: 64 }
                Text { text: "80.0 mm"; color: root.titleColor; font.pixelSize: 12; font.bold: true }
            }

            // 属性行 3: Material
            Row {
                id: row3; width: parent.width; spacing: 8; height: 22
                opacity: 0; x: 12
                Behavior on opacity { NumberAnimation { duration: 280; easing.type: Easing.OutCubic } }
                Behavior on x       { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                Component.onCompleted: { row3.opacity = 1; row3.x = 0 }

                Text { text: "Material:"; color: root.textColor; font.pixelSize: 12; width: 64 }
                Text { text: "Stainless Steel"; color: root.titleColor; font.pixelSize: 12 }
            }

            Item { width: 1; height: 1; Layout.fillHeight: true }
        }
    }

    // —— bottom.status：状态栏（消息滑入 + 呼吸背景）——
    Component {
        id: statusContent
        Item {
            anchors.fill: parent

            // 背景呼吸条
            Rectangle {
                id: statusBar
                anchors.fill: parent
                color: root.panelColor

                // 状态栏呼吸：极轻幅度、慢速（克制，不喧宾夺主）
                NumberAnimation on opacity {
                    from: 0.05; to: 0.15
                    duration: 2400; loops: Animation.Infinite; easing.type: Easing.InOutSine
                }
            }

            Text {
                id: statusText
                anchors.centerIn: parent
                width: parent.width - 20
                horizontalAlignment: Text.AlignLeft
                verticalAlignment: Text.AlignVCenter
                text: msStatusText.length > 0 ? msStatusText : msTextTitle
                color: root.titleColor
                font.pixelSize: 12
                elide: Text.ElideRight
                opacity: 0; x: 14

                Behavior on opacity { NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }
                Behavior on x       { NumberAnimation { duration: 360; easing.type: Easing.OutCubic } }
                Component.onCompleted: { statusText.opacity = 1; statusText.x = 0 }

                // 消息变化时脉冲一下
                NumberAnimation on scale { duration: 260; easing.type: Easing.OutBack }
            }
        }
    }

    // —— bottom.clock：实时时钟（发光呼吸背景 + 每秒脉冲）——
    Component {
        id: clockContent
        Item {
            anchors.fill: parent

            // 发光呼吸背景
            Rectangle {
                id: clockGlow
                anchors.centerIn: parent
                width: Math.max(implicitWidth + 20, 110)
                height: 30
                radius: 6
                color: root.panelColor
                border.width: 1
                border.color: root.accentColor
                opacity: 0.5

                // 时钟背景轻微呼吸（动态元素，保留但幅度收窄、不再边框脉冲）
                NumberAnimation on opacity {
                    from: 0.4; to: 0.6; duration: 2200; loops: Animation.Infinite; easing.type: Easing.InOutSine
                }
            }

            Text {
                id: clockText
                anchors.centerIn: parent
                text: new Date().toLocaleTimeString()
                color: root.titleColor
                font.pixelSize: 13
                font.bold: true

                Behavior on scale { NumberAnimation { duration: 180; easing.type: Easing.OutBack } }
                Behavior on color { ColorAnimation { duration: 140 } }

                Timer {
                    interval: 1000; repeat: true; running: true
                    onTriggered: {
                        clockText.text = new Date().toLocaleTimeString()
                        clockText.scale = 1.15
                        clockText.color = root.glowColor
                    }
                }

                opacity: 0
                Behavior on opacity { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
                Component.onCompleted: { clockText.opacity = 1 }
            }
        }
    }

    // —— center：主工作区（logo 旋转 + 发光边框脉冲 + 脉冲光环 + 标题放大回弹）——
    Component {
        id: centerContent
        Column {
            anchors.centerIn: parent
            spacing: 14

            // 旋转发光 logo
            Item {
                id: logoHolder
                width: 88; height: 88
                opacity: 0
                Behavior on opacity { NumberAnimation { duration: 480; easing.type: Easing.OutCubic } }
                Component.onCompleted: { logoHolder.opacity = 1 }

                // logo 柔和发光（持续但克制：不闪烁，只提供 elevation 光晕）
                RectangularShadow {
                    anchors.fill: outerRing
                    color: root.glowColor
                    offset: Qt.vector2d(0, 0)
                    blur: 14
                    radius: outerRing.radius
                    spread: 0.3
                    opacity: 0.65
                }

                Rectangle {
                    id: outerRing
                    anchors.centerIn: parent
                    width: 84; height: 84; radius: 10
                    color: root.panelColor
                    border.width: 1
                    border.color: root.accentColor

                    // 入场：Behavior 响应 scale 变化
                    scale: 0.4
                    Behavior on scale { NumberAnimation { duration: 600; easing.type: Easing.OutBack } }
                    Component.onCompleted: { outerRing.scale = 1.0 }

                    // 持续轻微呼吸：幅度收窄、节奏放慢（克制）
                    Timer {
                        interval: 3000; repeat: true; running: true
                        onTriggered: outerRing.scale = (outerRing.scale > 1.0) ? 0.98 : 1.02
                    }

                    Rectangle {
                        id: innerBox
                        anchors.centerIn: parent
                        width: 48; height: 48; radius: 6
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: root.glowColor }
                            GradientStop { position: 1.0; color: root.accentColor }
                        }

                        // 持续旋转：自定义属性驱动 transform.Rotation
                        property real spinAngle: 0
                        transform: Rotation {
                            origin.x: innerBox.width / 2
                            origin.y: innerBox.height / 2
                            angle: innerBox.spinAngle
                        }
                        NumberAnimation on spinAngle {
                            from: 0; to: 360
                            duration: 6000; loops: Animation.Infinite
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "▲"
                            color: root.bgColor
                            font.pixelSize: 24
                            font.bold: true
                        }
                    }
                }

                // 外层脉冲光环（扩散淡出）
                Rectangle {
                    id: pulseRing
                    anchors.centerIn: parent
                    width: 100; height: 100; radius: 50
                    border.width: 2
                    border.color: root.glowColor
                    opacity: 0
                    color: "transparent"

                    SequentialAnimation {
                        loops: Animation.Infinite
                        ParallelAnimation {
                            NumberAnimation { target: pulseRing; property: "scale"; from: 0.85; to: 1.18; duration: 1800; easing.type: Easing.OutSine }
                            NumberAnimation { target: pulseRing; property: "opacity"; from: 0.35; to: 0.0; duration: 1800; easing.type: Easing.OutSine }
                        }
                        PauseAnimation { duration: 1200 }
                    }
                }
            }

            // 标题：放大回弹入场
            Text {
                id: centerTitle
                text: msTextTitle
                color: root.titleColor
                font.bold: true
                font.pixelSize: 18
                horizontalAlignment: Text.AlignHCenter
                width: Math.max(280, implicitWidth)
                opacity: 0; scale: 0.85
                Behavior on opacity { NumberAnimation { duration: 420; easing.type: Easing.OutCubic } }
                Behavior on scale   { NumberAnimation { duration: 520; easing.type: Easing.OutBack } }
                Component.onCompleted: { centerTitle.opacity = 1; centerTitle.scale = 1.0 }
            }

            // 副标题：从底部滑入
            Text {
                id: centerHint
                text: msTextHint
                color: root.textColor
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                width: Math.max(280, implicitWidth)
                opacity: 0; y: 12
                Behavior on opacity { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
                Behavior on y       { NumberAnimation { duration: 480; easing.type: Easing.OutCubic } }
                Component.onCompleted: { centerHint.opacity = 1; centerHint.y = 0 }
            }
        }
    }
}
