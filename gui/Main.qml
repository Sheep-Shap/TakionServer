import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

ApplicationWindow {
    id: win
    width: 1180; height: 700; minimumWidth: 900; minimumHeight: 560
    visible: true; title: "TakionServerGUI"

    property int menuCursor: 0
    property int area: 0            // 0 меню, 1 правая панель
    property int rowIndex: 0
    property int lastDir: 1
    readonly property real sideW: Math.max(220, Math.min(330, width * 0.27))

    function tx(key) { const _l = backend.language; return backend.t(key) }

    function pageRows() {
        const p = stack.children[stack.currentIndex]
        return (p && p.rowItems) ? p.rowItems : []
    }
    function syncRows() {
        for (let pi = 0; pi < stack.children.length; ++pi) {
            const p = stack.children[pi]
            if (!p.rowItems) continue
            const rows = p.rowItems
            for (let i = 0; i < rows.length; ++i)
                rows[i].kbd = (area === 1 && pi === stack.currentIndex && i === rowIndex)
        }
    }
    onAreaChanged: syncRows()
    onRowIndexChanged: syncRows()

    function ensureRowVisible() {   
        const p = stack.children[stack.currentIndex]
        const r = pageRows()[rowIndex]
        if (area === 1 && p && p.scrollTo && r) p.scrollTo(r)
    }
    function hoverRow(item) {
        const rows = pageRows()
        for (let i = 0; i < rows.length; ++i)
            if (rows[i] === item) { area = 1; rowIndex = i; return }
    }
    function moveDown() {
        lastDir = 1
        if (area === 0) {
            const n = Math.min(menuCursor + 1, side.count - 1)
            menuCursor = n; side.currentIndex = n
        } else {
            const c = pageRows().length
            if (c) rowIndex = Math.min(rowIndex + 1, c - 1)
            ensureRowVisible()
        }
    }
    function moveUp() {
        lastDir = -1
        if (area === 0) {
            const n = Math.max(menuCursor - 1, 0)
            menuCursor = n; side.currentIndex = n
        } else {
            const c = pageRows().length
            if (c) rowIndex = Math.max(rowIndex - 1, 0)
            ensureRowVisible()
        }
    }
    function moveRight() {
        if (area === 0) { if (pageRows().length) { rowIndex = 0; area = 1; ensureRowVisible() } }
        else { const r = pageRows()[rowIndex]; if (r && r.adjustable) r.adjust(1) }
    }
    function moveLeft() {
        if (area !== 1) return
        const r = pageRows()[rowIndex]
        if (r && r.adjustable) r.adjust(-1)
        else area = 0
    }
    function activate() {
        if (area === 0) moveRight()
        else { const r = pageRows()[rowIndex]; if (r) r.clicked() }
    }

    Shortcut { sequences: ["Up"];    enabled: !dlg.visible; onActivated: win.moveUp() }
    Shortcut { sequences: ["Down"];  enabled: !dlg.visible; onActivated: win.moveDown() }
    Shortcut { sequences: ["Left"];  enabled: !dlg.visible; onActivated: win.moveLeft() }
    Shortcut { sequences: ["Right"]; enabled: !dlg.visible; onActivated: win.moveRight() }
    Shortcut { sequences: ["Return", "Enter", "Space"]; enabled: !dlg.visible; onActivated: win.activate() }
    Shortcut { sequences: ["Escape"]; enabled: !dlg.visible; onActivated: win.area = 0 }

    background: Item {
        id: bg
        readonly property real sc: height / 720

        Image {
            anchors.fill: parent
            source: "bg.jpg"
            fillMode: Image.PreserveAspectCrop
        }

        Rectangle { anchors.fill: parent; color: "#55000000" }

        Item {
            id: particles
            anchors.fill: parent
            layer.enabled: true
            layer.effect: MultiEffect { blurEnabled: true; blur: 0.2; blurMax: 10 }

            Repeater {
                model: 60
                delegate: Rectangle {
                    readonly property bool isRight: index % 3 !== 0
                    readonly property real g: (Math.random() + Math.random() + Math.random()) / 3
                    readonly property real nx: isRight ? 0.50 + g * 0.45 : g * 0.46
                    readonly property real ny: isRight
                        ? 0.74 - (nx - 0.50) * 0.60 + (Math.random() - 0.5) * 0.13
                        : 0.66 + (Math.random() - 0.5) * 0.09
                    readonly property real r: (isRight ? 1.2 + Math.pow(Math.random(), 2.2) * 6.5
                                                        : 1.0 + Math.pow(Math.random(), 2.0) * 3.0) * bg.sc

                    width: r * 2; height: r * 2; radius: r
                    x: nx * particles.width - r
                    y: ny * particles.height - r
                    color: Qt.hsla(isRight ? 0.50 + Math.random() * 0.10
                                           : 0.42 + Math.random() * 0.12, 0.85, 0.62, 1)
                    opacity: 0

                    transform: Translate {
                        SequentialAnimation on x {
                            loops: Animation.Infinite
                            NumberAnimation { to: 28; duration: 6000 + Math.random() * 6000; easing.type: Easing.InOutSine }
                            NumberAnimation { to: -28; duration: 6000 + Math.random() * 6000; easing.type: Easing.InOutSine }
                        }
                        SequentialAnimation on y {
                            loops: Animation.Infinite
                            NumberAnimation { to: -16; duration: 5000 + Math.random() * 5000; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 16; duration: 5000 + Math.random() * 5000; easing.type: Easing.InOutSine }
                        }
                    }
                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        PauseAnimation { duration: Math.random() * 4000 }
                        NumberAnimation { to: (isRight ? 0.35 : 0.25) + Math.random() * 0.55
                                          duration: 2500 + Math.random() * 2500; easing.type: Easing.InOutSine }
                        NumberAnimation { to: 0; duration: 2500 + Math.random() * 2500; easing.type: Easing.InOutSine }
                    }
                }
            }
        }
    }

    component PsPage: Item {
        id: page
        default property alias rows: col.data
        readonly property var rowItems: col.children
        signal rowHovered(var item)

        function scrollTo(item) {
            const top = item.y, bottom = item.y + item.height
            let t = flick.contentY
            if (top < t) t = top
            else if (bottom > t + flick.height) t = bottom - flick.height
            t = Math.max(0, Math.min(t, Math.max(0, flick.contentHeight - flick.height)))
            if (t !== flick.contentY) { scrollAnim.stop(); scrollAnim.to = t; scrollAnim.start() }
        }

        Flickable {
            id: flick
            anchors.fill: parent
            contentWidth: width
            contentHeight: col.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            NumberAnimation { id: scrollAnim; target: flick; property: "contentY"
                              duration: 180; easing.type: Easing.OutCubic }
            Column { id: col; width: flick.width }
        }
    }

    component PsRow: Item {
        id: row
        property string title
        property string subtitle
        property alias trailing: holder.data
        signal clicked()
        property bool kbd: false
        property bool adjustable: false
        signal adjust(int dir)
        function notifyHover() {
            let p = row.parent
            while (p && !p.rowHovered) p = p.parent
            if (p) p.rowHovered(row)
        }
        readonly property bool active: kbd
        onKbdChanged: {
            if (kbd) sheenAnim.restart()
            else { sheenAnim.stop(); band.opacity = 0 }
        }
        width: parent ? parent.width : 0
        height: Math.max(subtitle !== "" ? 96 : 72, textCol.implicitHeight + 28)

        Item {
            anchors.fill: parent; clip: true
            Rectangle {
                id: band
                width: 220; height: row.height * 3
                rotation: 20; opacity: 0
                x: -width; y: -row.height * 1.3
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#00ffffff" }
                    GradientStop { position: 0.5; color: "#55ffffff" }
                    GradientStop { position: 1.0; color: "#00ffffff" }
                }
            }
        }
        ParallelAnimation {
            id: sheenAnim
            NumberAnimation { target: band; property: "x"; from: -band.width; to: row.width
                              duration: 900; easing.type: Easing.InOutSine }
            NumberAnimation { target: band; property: "y"; from: -row.height * 1.3; to: -row.height * 0.7
                              duration: 900; easing.type: Easing.InOutSine }
            SequentialAnimation {
                NumberAnimation { target: band; property: "opacity"; from: 0; to: 1; duration: 150 }
                PauseAnimation { duration: 400 }
                NumberAnimation { target: band; property: "opacity"; to: 0; duration: 350 }
            }
        }

        Rectangle {
            anchors.fill: parent; radius: 4; color: "transparent"
            border.width: 2; border.color: "#cfd3ff"
            opacity: row.active ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
        }

        Column {
            id: textCol
            x: 16; anchors.verticalCenter: parent.verticalCenter; spacing: 6
            width: parent.width - holder.width - 48
            Text {
                width: parent.width
                elide: Text.ElideRight
                text: row.title; font.pixelSize: 22; font.weight: Font.DemiBold
                color: row.active ? "#ffffff" : "#d6d9ee"
                Behavior on color { ColorAnimation { duration: 250 } }
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                visible: row.subtitle !== ""; text: row.subtitle
                color: "#aab0dc"; font.pixelSize: 17
            }
        }
        Item {
            id: holder
            anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
            width: childrenRect.width; height: childrenRect.height
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                      leftMargin: 16; rightMargin: 16 }
            height: 1; color: "#22ffffff"
        }

        HoverHandler {
            id: hh
            onHoveredChanged: if (hovered) row.notifyHover()
            onPointChanged: if (hovered && !row.kbd) row.notifyHover()
        }
        TapHandler { onTapped: row.clicked() }
    }

    component PsSwitch: Rectangle {
        property bool checked: false
        width: 50; height: 24; radius: 12
        color: checked ? "#5ee1ff" : "#3a4180"
        Behavior on color { ColorAnimation { duration: 200 } }
        Rectangle {
            width: 18; height: 18; radius: 9; y: 3; color: "white"
            x: parent.checked ? 29 : 3
            Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        }
    }

    Text {
        x: 50; y: 28; text: "Takion Server"
        color: "#e8e9f5"; font.pixelSize: 34; font.weight: Font.DemiBold
    }

    // левое меню
    ListView {
        id: side
        x: 60; y: 130; width: win.sideW; height: parent.height - 160
        onCurrentIndexChanged: { win.rowIndex = 0; win.syncRows() }
        model: 3
        currentIndex: 0; spacing: 14; interactive: false
        property int lastHover: -1

        delegate: Item {
            id: it
            width: side.width; height: 56
            property bool kbd: win.area === 0 && win.menuCursor === index
            function enter(dir) { frameOut.stop(); it.playDrop(dir); frameIn.restart() }
            function leave() { frameIn.stop(); dropAnim.stop(); drop.opacity = 0; drop.p = 0; frameOut.restart() }
            onKbdChanged: { if (kbd) enter(win.lastDir); else leave() }
            Component.onCompleted: if (kbd) enter(1)
            property bool hot: false
            property int pendingDir: 1

            Timer {
                id: hoverTimer
                interval: 90
                onTriggered: { win.lastDir = it.pendingDir; win.area = 0; win.menuCursor = index }
            }

            function playDrop(dir) { drop.dir = dir; dropAnim.restart() }

            Rectangle {
                id: frame
                anchors.fill: parent; radius: 6; color: "transparent"
                border.width: 2; border.color: "#cfd3ff"
                opacity: 0
            }
            SequentialAnimation {
                id: frameIn
                PauseAnimation { duration: 380 }
                NumberAnimation { target: frame; property: "opacity"; to: 1; duration: 220
                                  easing.type: Easing.OutCubic }
            }
            NumberAnimation { id: frameOut; target: frame; property: "opacity"; to: 0; duration: 200 }

            Item {
                id: box
                anchors.fill: parent; anchors.margins: 2; clip: true

                Canvas {
                    id: drop
                    anchors.fill: parent
                    property int dir: 1
                    property real p: 0
                    opacity: 0
                    renderTarget: Canvas.FramebufferObject
                    onPChanged: requestPaint()
                    onDirChanged: requestPaint()

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        if (p <= 0.001) return

                        var W = width, H = height
                        var sag = H * 0.30
                        var yb = H / 2 + (H + sag * 0.6 - H / 2) * p
                        var thick = H * 0.55 * Math.min(1, p * 2.2)
                        var yt = Math.max(yb - thick, H / 2)

                        ctx.save()
                        ctx.translate(0, H / 2); ctx.scale(1, dir); ctx.translate(0, -H / 2)

                        var g = ctx.createLinearGradient(0, yt, 0, yb + sag)
                        g.addColorStop(0.0, "rgba(255,255,255,0.00)")
                        g.addColorStop(1.0, "rgba(255,255,255,0.42)")
                        ctx.fillStyle = g
                        ctx.beginPath()
                        ctx.moveTo(0, yb)
                        ctx.quadraticCurveTo(W / 2, yb + 2 * sag, W, yb)
                        ctx.lineTo(W, yt)
                        ctx.quadraticCurveTo(W / 2, yt + 2 * sag * 0.6, 0, yt)
                        ctx.closePath()
                        ctx.fill()

                        ctx.strokeStyle = "rgba(255,255,255,0.85)"
                        ctx.lineWidth = 1.5
                        ctx.beginPath()
                        ctx.moveTo(0, yb)
                        ctx.quadraticCurveTo(W / 2, yb + 2 * sag, W, yb)
                        ctx.stroke()
                        ctx.restore()

                        ctx.globalCompositeOperation = "destination-in"
                        var m = ctx.createLinearGradient(0, 0, W, 0)
                        m.addColorStop(0.00, "rgba(0,0,0,0)")
                        m.addColorStop(0.18, "rgba(0,0,0,1)")
                        m.addColorStop(0.82, "rgba(0,0,0,1)")
                        m.addColorStop(1.00, "rgba(0,0,0,0)")
                        ctx.fillStyle = m
                        ctx.fillRect(0, 0, W, H)
                        ctx.globalCompositeOperation = "source-over"
                    }
                }
            }
            ParallelAnimation {
                id: dropAnim
                NumberAnimation { target: drop; property: "p"; from: 0; to: 1
                                  duration: 480; easing.type: Easing.InOutCubic }
                SequentialAnimation {
                    NumberAnimation { target: drop; property: "opacity"; to: 1; duration: 110 }
                    PauseAnimation { duration: 170 }
                    NumberAnimation { target: drop; property: "opacity"; to: 0; duration: 200
                                      easing.type: Easing.InQuad }
                }
            }

            Text {
                x: 40; anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 52
                elide: Text.ElideRight
                text: win.tx(["menu_server", "menu_settings", "menu_log"][index])
                font.pixelSize: 22; font.weight: Font.DemiBold
                color: (it.ListView.isCurrentItem || it.hot) ? "#aab2e6" : "#7f86b5"
                Behavior on color { ColorAnimation { duration: 300 } }
            }
            MouseArea {
                anchors.fill: parent; hoverEnabled: true
                onPositionChanged: if (!it.kbd) hoverTimer.restart()
                onEntered: {
                    it.hot = true
                    it.pendingDir = (index >= win.menuCursor) ? 1 : -1
                    hoverTimer.restart()
                }
                onExited: { it.hot = false; hoverTimer.stop() }
                onClicked: { side.currentIndex = index; win.menuCursor = index; win.area = 0 }
            }
        }
    }

    Rectangle { x: win.sideW + 94; y: 130; width: 2; height: parent.height - 160; color: "#4b5290" }

    StackLayout {
        id: stack
        x: win.sideW + 156; y: 130
        width: parent.width - x - 60; height: parent.height - 160
        currentIndex: side.currentIndex

        PsPage {
            onRowHovered: (item) => win.hoverRow(item)
            PsRow {
                title: backend.running ? win.tx("stop") : win.tx("start")
                subtitle: backend.status
                onClicked: backend.toggleServer()
            }
            PsRow {
                title: "Registration PIN"
                trailing: Text { text: backend.pin; color: "#ffffff"; font.pixelSize: 24
                                 font.weight: Font.DemiBold }
            }
        }

        PsPage {
            onRowHovered: (item) => win.hoverRow(item)
            PsRow {
                title: win.tx("max_bitrate")
                adjustable: true
                onAdjust: (dir) => backend.maxMbps = Math.max(1, Math.min(100, backend.maxMbps + dir * 0.5))
                trailing: Row {
                    spacing: 16
                    Text { text: backend.maxMbps.toFixed(1) + " " + win.tx("unit_mbps"); color: "#fff"; font.pixelSize: 18
                           anchors.verticalCenter: parent.verticalCenter }
                    Slider { from: 1; to: 100; stepSize: 0.5
                             width: Math.max(120, Math.min(260, win.width * 0.2))
                             value: backend.maxMbps; onMoved: backend.maxMbps = value }
                }
            }
            PsRow {
                title: win.tx("min_bitrate")
                adjustable: true
                onAdjust: (dir) => backend.minMbps = Math.max(1, Math.min(100, backend.minMbps + dir * 0.5))
                trailing: Row {
                    spacing: 16
                    Text { text: backend.minMbps.toFixed(1) + " " + win.tx("unit_mbps"); color: "#fff"; font.pixelSize: 18
                           anchors.verticalCenter: parent.verticalCenter }
                    Slider { from: 1; to: 100; stepSize: 0.5
                             width: Math.max(120, Math.min(260, win.width * 0.2))
                             value: backend.minMbps; onMoved: backend.minMbps = value }
                }
            }
            PsRow {
                title: win.tx("adaptive")
                subtitle: win.tx("adaptive_sub")
                trailing: PsSwitch { checked: backend.adaptive }
                onClicked: backend.adaptive = !backend.adaptive
            }
            PsRow {
                title: win.tx("respect")
                subtitle: win.tx("respect_sub")
                trailing: PsSwitch { checked: backend.respectClient }
                onClicked: backend.respectClient = !backend.respectClient
            }
            PsRow {
                title: win.tx("controller")
                subtitle: win.tx("controller_sub")
                adjustable: true
                onAdjust: (dir) => backend.cycleController(dir)
                onClicked: backend.cycleController(1)
                trailing: Text { text: backend.controllerName; color: "#ffffff"; font.pixelSize: 20 }
            }
            PsRow {
                title: win.tx("language")
                subtitle: win.tx("language_sub")
                adjustable: true
                onAdjust: (dir) => backend.cycleLanguage()
                onClicked: backend.cycleLanguage()
                trailing: Text { text: backend.language === "ru" ? "Русский" : "English"
                                 color: "#ffffff"; font.pixelSize: 20 }
            }
            PsRow {
                title: win.tx("save")
                subtitle: win.tx("save_sub")
                onClicked: backend.saveSettings()
            }
        }

        Item {
            ScrollView {
                anchors.fill: parent
                TextArea {
                    id: logArea
                    readOnly: true; wrapMode: TextArea.NoWrap
                    color: "#d6d9ee"; font.family: "Consolas"; font.pixelSize: 14
                    background: Rectangle { color: "#26000000"; radius: 8 }
                }
            }
        }
    }

    Connections {
        target: backend
        function onLogLine(line) {
            logArea.append(line)
            if (logArea.length > 300000) logArea.remove(0, 100000)
        }
        function onLogCleared() { logArea.clear() }
        function onNotice(t, m) { dlg.title = t; dlg.msg = m; dlg.open() }
    }

    Popup {
        id: dlg
        property string title
        property string msg
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        anchors.centerIn: Overlay.overlay
        width: Math.min(460, win.width - 80)
        padding: 0

        Overlay.modal: Rectangle { color: "#99020a12" }

        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180 }
                NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: 220; easing.type: Easing.OutCubic }
            }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; to: 0; duration: 140 }
        }

        background: Rectangle {
            radius: 14
            color: "#ee141a40"
            border.width: 1
            border.color: "#55cfd3ff"
        }

        contentItem: FocusScope {
            id: dlgRoot
            implicitWidth: dlg.width
            implicitHeight: dlgCol.implicitHeight + 48
            focus: true

            Keys.onPressed: (e) => {
                if (!e.isAutoRepeat && (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Space)) {
                    dlg.close()
                    e.accepted = true
                }
            }

            Column {
                id: dlgCol
                x: 28; y: 24
                width: parent.width - 56
                spacing: 14

                Text {
                    width: parent.width
                    text: dlg.title
                    color: "#ffffff"
                    font.pixelSize: 22; font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                }
                Text {
                    width: parent.width
                    text: dlg.msg
                    color: "#d6d9ee"
                    font.pixelSize: 17
                    wrapMode: Text.Wrap
                }
                Item { width: 1; height: 6 }
                Item {
                    width: parent.width; height: 46
                    Rectangle {
                        anchors.right: parent.right
                        width: 130; height: 46; radius: 23
                        color: okArea.containsMouse ? "#1a8cff" : "#0070d1"
                        border.width: 2; border.color: "#cfd3ff"
                        Behavior on color { ColorAnimation { duration: 200 } }
                        Text {
                            anchors.centerIn: parent
                            text: "OK"
                            color: "#ffffff"
                            font.pixelSize: 18; font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: okArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: dlg.close()
                        }
                    }
                }
            }
        }

        onOpened: dlgRoot.forceActiveFocus()
    }
}
