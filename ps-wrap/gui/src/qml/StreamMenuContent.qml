import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

// PS-WRAP: เนื้อหาเมนูระหว่างสตรีม (v4 "cards") ใช้ร่วมกันทั้ง inline (Vulkan) และ StreamMenuWindow (OpenGL)
//  แถวบน: [⏻ End Stream] [● Record] | [🎤] [ไมค์ ▾] [🔊 ลำโพง ▾] ━━ 100%            15.6 Mbps · 0.0% loss   [✕]
//  การ์ด (Flow — ทั้งการ์ดห่อลงบรรทัดใหม่เมื่อแคบ ไม่มีอะไรล้นขอบ):
//    PICTURE (Zoom Stretch Glow Size Display) · QUALITY (Default HQ Spatial Advanced Custom FrameGen Renderer)
//    OVERLAY (Pad Cam Spectrum Clock Chat Stats Light Stack Move) · CAPTURE (Replay Length Save Screenshot Live 9:16)
//  จอย/คีย์บอร์ด: ลูกศรย้ายโฟกัสตามตำแหน่งบนจอ (navKey/moveFocus — ไม่ผูก KeyNavigation รายปุ่ม เลย์เอาต์ห่อได้อิสระ)
//    ✕/Enter = กด · ◯/Esc = ปิดเมนู · slider: ←→ ปรับค่า ↑↓ ย้ายโฟกัส
//  ความสูงเมนู = implicitHeight (StreamView/StreamMenuWindow ผูกตามนี้)
FocusScope {
    id: content
    property Item initialFocusItem: closeButton
    property bool overlayEnabled: true
    property bool webcamEnabled: false
    property bool dockEnabled: false   // PS-WRAP: Stack — overlay เรียงคอลัมน์เดียว (StreamView จัดให้)
    readonly property QtObject recorder: Chiaki.window ? Chiaki.window.recorder : null
    readonly property bool recording: !!recorder && recorder.recording
    readonly property bool micOverlayEnabled: !!Chiaki.window && Chiaki.window.micOverlay
    readonly property bool clockOverlayEnabled: !!Chiaki.window && !!Chiaki.window.clockOverlay
    readonly property bool chatOverlayEnabled: !!Chiaki.window && !!Chiaki.window.chatOverlay
    readonly property bool replayEnabled: !!Chiaki.window && !!Chiaki.window.replayEnabled
    readonly property bool replayActive: !!recorder && !!recorder.replayActive
    readonly property int replaySeconds: Chiaki.window && Chiaki.window.replaySeconds > 0 ? Chiaki.window.replaySeconds : 60
    readonly property var goLive: Chiaki.goLive !== undefined ? Chiaki.goLive : null
    readonly property bool liveOn: !!goLive && goLive.live
    readonly property bool connected: !!Chiaki.session && Chiaki.session.connected
    readonly property bool customPreset: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom
    function liveDotColor(state) {
        return state === "live" ? Theme.success : state === "error" ? Theme.danger : Theme.warning;
    }
    readonly property string micDevice: Chiaki.session ? Chiaki.session.audioInDevice : Chiaki.settings.audioInDevice
    readonly property string speakerDevice: Chiaki.session ? Chiaki.session.audioOutDevice : Chiaki.settings.audioOutDevice
    readonly property bool popupOpen: micDevicePopup.visible || sizePopup.visible || movePopup.visible   // StreamMenuWindow ปิด Shortcut Esc ระหว่างนี้
    function micDeviceLabel(name) { return name && name.length ? name : qsTr("Auto (Windows default)"); }
    onVisibleChanged: if (!visible) { micDevicePopup.close(); sizePopup.close(); movePopup.close(); }
    readonly property bool narrow: width < 760
    // ชื่ออุปกรณ์บนแถวบน: กว้างตามพื้นที่ (จอแคบเหลือแค่ไอคอน + ▾)
    readonly property int deviceTextWidth: width >= 1500 ? 170 : width >= 1180 ? 110 : 0
    function formatElapsed(sec) {
        const s = Math.max(0, Math.floor(sec || 0));
        const pad = (n) => (n < 10 ? "0" : "") + n;
        return pad(Math.floor(s / 3600)) + ":" + pad(Math.floor(s / 60) % 60) + ":" + pad(s % 60);
    }
    implicitHeight: deck.implicitHeight + Theme.space3 * 2 + 2

    signal closeRequested()
    signal displaySettingsRequested()
    signal placeboSettingsRequested()
    signal mainViewRequested()
    signal overlayToggled()
    signal overlayEditRequested()
    signal webcamToggled()
    signal webcamEditRequested()
    signal micEditRequested()
    signal clockEditRequested()
    signal chatEditRequested()
    signal dockToggled()          // PS-WRAP: Stack
    signal dockEditRequested()

    // ภาพหน้าจอรวม overlay QML ด้วย → รอเมนูเลื่อนลงจบ (250ms) ก่อนถ่าย
    Timer {
        id: screenshotDelay
        interval: 320
        onTriggered: if (Chiaki.session) Chiaki.window.takeScreenshot()
    }

    Keys.onMenuPressed: content.closeRequested()
    Keys.onEscapePressed: content.closeRequested()

    // ---------- โฟกัสตามตำแหน่ง (จอย/ลูกศร) ----------
    function collectNav(item, out) {
        if (!item || item.visible === false)
            return;
        if (item.navigable === true && item.enabled)
            out.push(item);
        const kids = item.children;
        for (let i = 0; i < kids.length; ++i)
            collectNav(kids[i], out);
    }
    function moveFocus(from, dx, dy) {
        let items = [];
        collectNav(deck, items);
        const a = from.mapToItem(content, 0, 0, from.width, from.height);
        const ax = a.x + a.width / 2, ay = a.y + a.height / 2;
        let best = null, bestScore = 1e9;
        for (let i = 0; i < items.length; ++i) {
            const it = items[i];
            if (it === from)
                continue;
            const b = it.mapToItem(content, 0, 0, it.width, it.height);
            const bx = b.x + b.width / 2, by = b.y + b.height / 2;
            let along, across;
            if (dx !== 0) {
                along = (bx - ax) * dx;
                // แถวเดียวกัน (ซ้อนกันแนวตั้ง) มาก่อน
                const vOverlap = Math.min(a.y + a.height, b.y + b.height) - Math.max(a.y, b.y);
                across = vOverlap > 0 ? 0 : Math.abs(by - ay);
                if (along <= 4) continue;
            } else {
                along = (by - ay) * dy;
                const overlap = Math.min(a.x + a.width, b.x + b.width) - Math.max(a.x, b.x);
                across = overlap > 0 ? 0 : Math.abs(bx - ax);
                if (along <= 4) continue;
            }
            const score = along + across * 3;
            if (score < bestScore) { bestScore = score; best = it; }
        }
        if (best)
            best.forceActiveFocus(Qt.TabFocusReason);
    }
    // ลูกศร = ย้ายโฟกัส · ✕/Enter/Space = กด · ◯/Esc = ปิดเมนู
    function navKey(item, event, horizontalAdjusts) {
        switch (event.key) {
        case Qt.Key_Left:  if (horizontalAdjusts) return; moveFocus(item, -1, 0); break;
        case Qt.Key_Right: if (horizontalAdjusts) return; moveFocus(item, 1, 0); break;
        case Qt.Key_Up:    moveFocus(item, 0, -1); break;
        case Qt.Key_Down:  moveFocus(item, 0, 1); break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Space:
            if (item.clicked) item.clicked(); else return;
            break;
        case Qt.Key_Escape: content.closeRequested(); break;
        default: return;
        }
        event.accepted = true;
    }

    component FocusRing: Rectangle {
        anchors.fill: parent
        anchors.margins: -Theme.focusMargin + 1
        radius: parent.radius + 2
        color: "transparent"
        border.width: Theme.focusWidth
        border.color: Theme.accent
    }

    // ปุ่มแถวบน: pill สูง 40 · on = เติม accent · danger = แดง
    component MenuButton: Button {
        id: mb
        property bool navigable: true
        property url iconSource: ""
        property bool danger: false
        property bool on: false
        property int glyph: 0           // 0 = ไม่มี · 1 = จุดอัด (แดง) · 2 = สี่เหลี่ยมหยุด
        property int maxTextWidth: -1   // >0 = ตัดข้อความยาวด้วย … · 0 = ซ่อนข้อความ (เหลือไอคอน)
        property bool caret: false
        readonly property bool showText: text.length > 0 && maxTextWidth !== 0
        flat: true
        focusPolicy: Qt.StrongFocus
        padding: 6
        leftPadding: showText ? (iconSource != "" || glyph > 0 ? 12 : 16) : 11
        rightPadding: showText ? 16 : (caret ? 9 : 11)
        font.pixelSize: Theme.fontLabel
        font.weight: Font.DemiBold
        Keys.onPressed: (event) => content.navKey(mb, event)
        background: Rectangle {
            implicitHeight: 40
            implicitWidth: 40
            radius: Theme.radiusChip
            color: mb.danger ? (mb.down ? "#b33a3a" : Theme.danger)
                 : mb.on ? Theme.accent
                 : mb.down ? Theme.surfaceHover
                 : mb.hovered ? Theme.surfaceRaised
                 : Qt.rgba(1, 1, 1, 0.04)
            border.width: mb.on || mb.danger ? 0 : 1
            border.color: Theme.border
            opacity: mb.enabled ? 1.0 : 0.4
            Behavior on color { ColorAnimation { duration: Theme.durFast } }
            FocusRing { visible: mb.activeFocus; border.color: mb.danger ? Theme.text : Theme.accent }
        }
        contentItem: RowLayout {
            spacing: 8
            Image {
                visible: mb.iconSource != ""
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                sourceSize: Qt.size(20, 20)
                source: mb.iconSource
                opacity: mb.enabled ? 1.0 : 0.5
            }
            Item {
                visible: mb.glyph > 0
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                Rectangle {
                    anchors.centerIn: parent
                    width: mb.glyph === 2 ? 12 : 14
                    height: width
                    radius: mb.glyph === 2 ? 2 : width / 2
                    color: mb.glyph === 2 ? Theme.accentText : Theme.danger
                    border.width: mb.glyph === 1 ? 2 : 0
                    border.color: Qt.rgba(1, 1, 1, 0.85)
                }
            }
            Label {
                visible: mb.showText
                text: mb.text
                font: mb.font
                color: mb.on || mb.danger ? Theme.accentText : Theme.text
                verticalAlignment: Text.AlignVCenter
                Layout.maximumWidth: mb.maxTextWidth > 0 ? mb.maxTextWidth : -1
                elide: Text.ElideRight
            }
            Label {
                visible: mb.caret
                text: "▾"
                font.pixelSize: Theme.fontCaption
                color: Theme.textMuted
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    // ไทล์ในการ์ด: ไอคอน (หรือตัวอักษรสั้นๆ) บน + ป้ายชื่อล่าง · on = เติม accent · ไม่ใช้ checkable (กดแล้ว binding ไม่หลุด)
    component Tile: AbstractButton {
        id: tile
        property bool navigable: true
        property url iconSource: ""
        property string glyph: ""
        property bool on: false
        property bool caret: false
        property color statusDot: "transparent"
        focusPolicy: Qt.StrongFocus
        implicitWidth: 72
        implicitHeight: 58
        font.pixelSize: 12
        font.weight: Font.DemiBold
        Keys.onPressed: (event) => content.navKey(tile, event)
        background: Rectangle {
            radius: 12
            color: tile.on ? Theme.accent
                 : tile.down ? Theme.surfaceHover
                 : tile.hovered ? Theme.surfaceRaised
                 : Qt.rgba(1, 1, 1, 0.035)
            border.width: tile.on ? 0 : 1
            border.color: tile.hovered ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.06)
            opacity: tile.enabled ? 1.0 : 0.35
            Behavior on color { ColorAnimation { duration: Theme.durFast } }
            FocusRing { visible: tile.activeFocus }
            Rectangle {
                visible: tile.statusDot.a > 0
                anchors { top: parent.top; right: parent.right; margins: 7 }
                width: 8; height: 8; radius: 4
                color: tile.statusDot
            }
        }
        contentItem: ColumnLayout {
            spacing: 3
            Item {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                Image {
                    anchors.centerIn: parent
                    visible: tile.iconSource != ""
                    width: 20; height: 20
                    sourceSize: Qt.size(20, 20)
                    source: tile.iconSource
                }
                Label {
                    anchors.centerIn: parent
                    visible: tile.iconSource == ""
                    text: tile.glyph
                    font.pixelSize: tile.glyph.length > 3 ? 13 : 15
                    font.weight: Font.Bold
                    font.features: { "tnum": 1 }
                    color: tile.on ? Theme.accentText : Theme.text
                }
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: tile.width - 8
                text: tile.text + (tile.caret ? " ▾" : "")
                font: tile.font
                elide: Text.ElideRight
                color: tile.on ? Theme.accentText : Theme.textMuted
            }
        }
    }

    // การ์ด: หัวข้อ + ตารางไทล์ (คอลัมน์ตายตัว → การ์ดกว้างคงที่ ห่อทั้งใบใน Flow)
    component Card: Rectangle {
        id: card
        property string title: ""
        property int columns: 3
        default property alias tiles: grid.data
        property alias extra: extraRow.data
        width: cardCol.implicitWidth + 20
        height: cardCol.implicitHeight + 18
        radius: 16
        color: Qt.rgba(1, 1, 1, 0.025)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.07)
        ColumnLayout {
            id: cardCol
            x: 10
            y: 8
            spacing: 6
            Label {
                text: card.title
                font.pixelSize: 11
                font.letterSpacing: 1.6
                font.weight: Font.DemiBold
                color: Theme.textMuted
                leftPadding: 2
            }
            GridLayout {
                id: grid
                columns: card.columns
                rowSpacing: 6
                columnSpacing: 6
            }
            RowLayout {
                id: extraRow
                visible: children.length > 0 && visibleChildren.length > 0
                Layout.fillWidth: true
                spacing: 6
            }
        }
    }

    component Caption: Label {
        font.pixelSize: Theme.fontCaption
        color: Theme.textMuted
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.surface
        opacity: 0.97
        Rectangle { anchors.top: parent.top; width: parent.width; height: 2; color: Theme.accent; opacity: 0.8 }
    }

    ColumnLayout {
        id: deck
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            topMargin: Theme.space3 + 2
            leftMargin: content.narrow ? Theme.space3 : Theme.space6
            rightMargin: content.narrow ? Theme.space3 : Theme.space6
        }
        spacing: Theme.space3

        // ---------- แถวบน: ควบคุมหลัก + stats + ปิด ----------
        RowLayout {
            id: topRow
            Layout.fillWidth: true
            spacing: Theme.space2

            MenuButton {
                id: closeButton
                danger: true
                iconSource: "qrc:/icons/menu/power.svg"
                text: Chiaki.session ? qsTr("End Stream") : qsTr("Main Menu")
                maxTextWidth: content.narrow ? 0 : -1
                onClicked: {
                    if (Chiaki.session)
                        Chiaki.window.close();
                    else
                        content.mainViewRequested();
                }
            }

            // อัดคลิป — ระหว่างอัดเป็นปุ่มแดง "■ 00:12:34" (กด = หยุด) · busy = กำลังปิดไฟล์
            MenuButton {
                id: recordButton
                danger: content.recording
                iconSource: content.recording ? "" : "qrc:/icons/menu/record.svg"
                glyph: content.recording ? 2 : 0
                text: content.recorder && content.recorder.busy ? qsTr("Saving…")
                    : content.recording ? content.formatElapsed(content.recorder.seconds)
                    : qsTr("Record")
                maxTextWidth: content.narrow && !content.recording ? 0 : -1
                // ไม่ disable ตอน busy (focus จอยจะหลุด) — กดซ้ำระหว่าง busy = ไม่ทำอะไร
                enabled: !!content.recorder && (content.recording || content.recorder.busy || content.connected)
                onClicked: if (!content.recorder.busy) Chiaki.window.toggleRecording()
            }

            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; Layout.leftMargin: 4; Layout.rightMargin: 4; color: Theme.border }

            // ไมค์: ปุ่มไอคอน = เปิด/ปิดเสียง (สีฟ้า = เปิด) · ถัดไปเลือกอุปกรณ์
            MenuButton {
                id: muteButton
                iconSource: "qrc:/icons/menu/mic.svg"
                on: !!Chiaki.session && !Chiaki.session.muted
                enabled: content.connected
                onClicked: Chiaki.session.muted = !Chiaki.session.muted
            }
            MenuButton {
                id: micDeviceButton
                caret: true
                text: content.micDeviceLabel(content.micDevice)
                maxTextWidth: content.deviceTextWidth > 0 ? content.deviceTextWidth : 60
                enabled: content.connected
                onClicked: { micDevicePopup.kind = "mic"; micDevicePopup.opener = micDeviceButton; micDevicePopup.open(); }
            }
            MenuButton {
                id: speakerDeviceButton
                iconSource: "qrc:/icons/menu/volume.svg"
                caret: true
                text: content.micDeviceLabel(content.speakerDevice)
                maxTextWidth: content.deviceTextWidth
                onClicked: { micDevicePopup.kind = "speaker"; micDevicePopup.opener = speakerDeviceButton; micDevicePopup.open(); }
            }
            Slider {
                id: volumeSlider
                property bool navigable: true
                Layout.preferredWidth: content.width >= 1180 ? 140 : 96
                from: 0
                to: 128
                stepSize: 1
                value: Chiaki.settings.audioVolume
                onMoved: Chiaki.settings.audioVolume = value
                Keys.onPressed: (event) => content.navKey(volumeSlider, event, true)   // ←→ ปรับค่า (ของ Slider)
                FocusRing { radius: Theme.radiusControl; visible: volumeSlider.activeFocus }
            }
            Label {
                Layout.preferredWidth: 40
                text: Math.round((volumeSlider.value / 128.0) * 100) + "%"
                font.pixelSize: Theme.fontCaption
                font.features: { "tnum": 1 }
                color: Theme.textMuted
            }

            Item { Layout.fillWidth: true }

            // stats บรรทัดเดียว — ตัดรายละเอียดเมื่อแคบ
            RowLayout {
                visible: !!Chiaki.session && content.width >= 900
                spacing: 6
                Label {
                    text: Chiaki.session ? Chiaki.session.measuredBitrate.toFixed(1) : "0.0"
                    color: Theme.accent
                    font.bold: true
                    font.pixelSize: 20
                    font.features: { "tnum": 1 }
                }
                Caption { text: "Mbps" }
                Caption {
                    visible: content.width >= 1280
                    text: {
                        const loss = ((Chiaki.session && isFinite(Chiaki.session.averagePacketLoss)) ? Chiaki.session.averagePacketLoss : 0) * 100;
                        return qsTr("· %1% loss · %2 dropped").arg(loss.toFixed(1)).arg(Chiaki.window.droppedFrames);
                    }
                }
                Caption {
                    visible: content.width >= 1600
                    color: Theme.text
                    text: {
                        if (!Chiaki.session) return "";
                        const host = Chiaki.settings.streamerMode ? "hidden" : Chiaki.session.host;
                        return (Chiaki.session.connected ? "· " : qsTr("· connecting ")) + "<b>" + host + "</b>";
                    }
                }
            }

            // ปิดเมนู (เหมือน Esc / ◯)
            MenuButton {
                id: dismissButton
                Layout.leftMargin: 4
                text: "✕"
                font.pixelSize: 16
                leftPadding: 13
                rightPadding: 13
                onClicked: content.closeRequested()
            }
        }

        // ---------- การ์ดตัวเลือก ----------
        Flow {
            id: cards
            Layout.fillWidth: true
            spacing: Theme.space2

            Card {
                title: qsTr("PICTURE")
                columns: 3
                Tile {
                    iconSource: "qrc:/icons/menu/zoom.svg"
                    text: qsTr("Zoom")
                    on: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Zoom
                    onClicked: Chiaki.window.videoMode = on ? ChiakiWindow.VideoMode.Normal : ChiakiWindow.VideoMode.Zoom
                }
                Tile {
                    iconSource: "qrc:/icons/menu/stretch.svg"
                    text: qsTr("Stretch")
                    on: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Stretch
                    onClicked: Chiaki.window.videoMode = on ? ChiakiWindow.VideoMode.Normal : ChiakiWindow.VideoMode.Stretch
                }
                // Ambient light — ขอบว่างรอบภาพเป็นแสงเบลอจากขอบเกม
                Tile {
                    glyph: "✦"
                    text: qsTr("Glow")
                    on: !!Chiaki.window.ambientLight
                    onClicked: Chiaki.window.ambientLight = !Chiaki.window.ambientLight
                }
                // ขนาดพื้นที่ภาพ 16:9 ตาม preset
                Tile {
                    id: sizeButton
                    glyph: "16:9"
                    text: qsTr("Size")
                    caret: true
                    onClicked: sizePopup.open()
                }
                Tile {
                    iconSource: "qrc:/icons/menu/display.svg"
                    text: qsTr("Display")
                    onClicked: content.displaySettingsRequested()
                }
                Tile {
                    iconSource: "qrc:/icons/menu/renderer.svg"
                    text: qsTr("Renderer")
                    visible: content.customPreset
                    onClicked: content.placeboSettingsRequested()
                }
                // ระดับซูม (เฉพาะโหมด Zoom) — แถวเสริมใต้ไทล์
                extra: [
                    Slider {
                        id: zoomFactor
                        property bool navigable: true
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        from: -1
                        to: 4
                        stepSize: 0.01
                        visible: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Zoom
                        value: Chiaki.window.ZoomFactor
                        onMoved: {
                            Chiaki.window.ZoomFactor = value
                            Chiaki.settings.sZoomFactor = value
                        }
                        Keys.onPressed: (event) => content.navKey(zoomFactor, event, true)
                        FocusRing { radius: Theme.radiusControl; visible: zoomFactor.activeFocus }
                    },
                    Caption {
                        visible: zoomFactor.visible
                        text: zoomFactor.value === -1 ? qsTr("No bars") : (zoomFactor.value >= 0 ? (zoomFactor.value + 1).toFixed(2) : zoomFactor.value.toFixed(2)) + "x"
                        font.features: { "tnum": 1 }
                    }
                ]
            }

            Card {
                title: qsTr("QUALITY")
                columns: 3
                Tile {
                    glyph: "SD"
                    text: qsTr("Default")
                    on: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Default
                    onClicked: { Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.Default; Chiaki.settings.videoPreset = ChiakiWindow.VideoPreset.Default; }
                }
                Tile {
                    iconSource: "qrc:/icons/menu/quality.svg"
                    text: qsTr("HQ")
                    on: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.HighQuality
                    onClicked: { Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.HighQuality; Chiaki.settings.videoPreset = ChiakiWindow.VideoPreset.HighQuality; }
                }
                Tile {
                    glyph: "HQ+S"
                    text: qsTr("Spatial")
                    on: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.HighQualitySpatial
                    onClicked: { Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.HighQualitySpatial; Chiaki.settings.videoPreset = ChiakiWindow.VideoPreset.HighQualitySpatial; }
                }
                Tile {
                    glyph: "HQ+A"
                    text: qsTr("Advanced")
                    on: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.HighQualityAdvancedSpatial
                    onClicked: { Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.HighQualityAdvancedSpatial; Chiaki.settings.videoPreset = ChiakiWindow.VideoPreset.HighQualityAdvancedSpatial; }
                }
                Tile {
                    glyph: "⚙"
                    text: qsTr("Custom")
                    on: content.customPreset
                    onClicked: { Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.Custom; Chiaki.settings.videoPreset = ChiakiWindow.VideoPreset.Custom; }
                }
                // Frame generation — เฟรมกลาง 60 → 120 · ต้อง Direct Mapping + Vulkan
                Tile {
                    glyph: "120"
                    text: qsTr("Frame Gen")
                    enabled: !!Chiaki.window.frameGenSupported
                    on: !!Chiaki.window.frameGen && !!Chiaki.window.frameGenSupported
                    statusDot: Chiaki.window.frameGenActive ? Theme.success : "transparent"
                    onClicked: Chiaki.window.frameGen = !Chiaki.window.frameGen
                }
            }

            Card {
                title: qsTr("OVERLAY")
                columns: 5
                Tile {
                    iconSource: "qrc:/icons/controller.svg"
                    text: qsTr("Pad")
                    on: content.overlayEnabled
                    onClicked: content.overlayToggled()
                }
                Tile {
                    iconSource: "qrc:/icons/menu/cam.svg"
                    text: qsTr("Cam")
                    on: content.webcamEnabled
                    onClicked: content.webcamToggled()
                }
                Tile {
                    iconSource: "qrc:/icons/menu/spectrum.svg"
                    text: qsTr("Spectrum")
                    on: content.micOverlayEnabled
                    onClicked: Chiaki.window.micOverlay = !Chiaki.window.micOverlay
                }
                Tile {
                    iconSource: "qrc:/icons/menu/clock.svg"
                    text: qsTr("Clock")
                    on: content.clockOverlayEnabled
                    onClicked: Chiaki.window.clockOverlay = !Chiaki.window.clockOverlay
                }
                Tile {
                    iconSource: "qrc:/icons/menu/chat.svg"
                    text: qsTr("Chat")
                    on: content.chatOverlayEnabled
                    onClicked: Chiaki.window.chatOverlay = !Chiaki.window.chatOverlay
                }
                Tile {
                    iconSource: "qrc:/icons/menu/stats.svg"
                    text: qsTr("Stats")
                    on: Chiaki.settings.showStreamStats
                    onClicked: Chiaki.settings.showStreamStats = !Chiaki.settings.showStreamStats
                }
                // Lightbar halo — แสงเรืองขอบภาพตามสีไฟจอยที่เกมสั่ง
                Tile {
                    glyph: "◉"
                    text: qsTr("Light")
                    on: !!Chiaki.window.lightbarHalo
                    onClicked: Chiaki.window.lightbarHalo = !Chiaki.window.lightbarHalo
                }
                // Stack — overlay ทั้งหมดเรียงคอลัมน์เดียว กว้างเท่ากัน
                Tile {
                    iconSource: "qrc:/icons/menu/menu.svg"
                    text: qsTr("Stack")
                    on: content.dockEnabled
                    onClicked: content.dockToggled()
                }
                // ย้าย/ย่อขยาย: Stack = ทั้งชุด · ไม่งั้นเลือกตัวจากรายการ (มีตัวเดียว = เข้าเลย)
                Tile {
                    id: moveButton
                    iconSource: "qrc:/icons/menu/move.svg"
                    text: content.dockEnabled ? qsTr("Arrange") : qsTr("Move")
                    caret: !content.dockEnabled && movePopup.choices().length > 1
                    enabled: !!Chiaki.session && (content.dockEnabled || movePopup.choices().length > 0)
                    onClicked: {
                        if (content.dockEnabled) { content.dockEditRequested(); return; }
                        const c = movePopup.choices();
                        if (c.length === 1) movePopup.run(c[0].act);
                        else movePopup.open();
                    }
                }
            }

            Card {
                title: qsTr("CAPTURE")
                columns: 3
                Tile {
                    iconSource: "qrc:/icons/menu/replay.svg"
                    text: qsTr("Replay")
                    on: content.replayEnabled
                    onClicked: Chiaki.window.replayEnabled = !content.replayEnabled
                }
                // ✕/Enter = วนความยาว 30 → 60 → 90 → 120
                Tile {
                    glyph: qsTr("%1s").arg(content.replaySeconds)
                    text: qsTr("Length")
                    onClicked: {
                        const steps = [30, 60, 90, 120];
                        const i = steps.indexOf(content.replaySeconds);
                        Chiaki.window.replaySeconds = steps[(i + 1) % steps.length];
                    }
                }
                Tile {
                    iconSource: "qrc:/icons/menu/save.svg"
                    text: qsTr("Save clip")
                    enabled: content.replayActive
                    onClicked: Chiaki.window.saveReplay()
                }
                Tile {
                    iconSource: "qrc:/icons/menu/screenshot.svg"
                    text: qsTr("Screenshot")
                    enabled: content.connected
                    // เมนูปิดก่อน แล้วค่อยถ่าย (ไม่ให้แถบเมนูติดไปในภาพ)
                    onClicked: { content.closeRequested(); screenshotDelay.restart(); }
                }
                // Go Live — เริ่ม/หยุดไลฟ์ไปทุกปลายทางที่เปิดใน Settings › Go Live (Ctrl+Shift+L)
                Tile {
                    iconSource: "qrc:/icons/menu/live.svg"
                    text: content.liveOn ? content.formatElapsed(content.goLive.seconds) : qsTr("Go Live")
                    font.features: { "tnum": 1 }
                    on: content.liveOn
                    statusDot: content.liveOn ? content.liveDotColor(content.goLive.state) : "transparent"
                    enabled: !!content.goLive && (content.liveOn || content.connected)
                    onClicked: content.goLive.toggle()
                }
                // preview ภาพแนวตั้ง 9:16 (VerticalPreviewWindow.qml)
                Tile {
                    iconSource: "qrc:/icons/menu/vertical.svg"
                    text: qsTr("9:16")
                    on: !!Chiaki.window && Chiaki.window.verticalPreview
                    onClicked: Chiaki.window.verticalPreview = !Chiaki.window.verticalPreview
                }
            }
        }

        // hotkey (บรรทัดเดียว ตัดท้ายเมื่อแคบ)
        Caption {
            Layout.fillWidth: true
            visible: content.width >= 1000
            elide: Text.ElideRight
            font.pixelSize: 12
            opacity: 0.7
            text: qsTr("Ctrl+O menu · F12 screenshot · Ctrl+Shift + R record · B save replay · K marker · S stats · O pad · E move pad · C cam · V move cam · M spectrum · T clock · H chat · L live · Esc = PS")
        }
    }

    // คลิกนอก popup (ในกรอบเมนู) = ปิด — Popup.CloseOnPressOutside ไม่ทำงานใน quick window offscreen
    MouseArea {
        anchors.fill: parent
        z: 1000
        visible: content.popupOpen
        onPressed: { micDevicePopup.close(); sizePopup.close(); movePopup.close(); }
    }

    // ---------- popup เลือก overlay ที่จะย้าย ----------
    Popup {
        id: movePopup
        function choices() {
            let c = [];
            if (content.overlayEnabled) c.push({ label: qsTr("Controller"), act: "pad" });
            if (content.webcamEnabled) c.push({ label: qsTr("Facecam"), act: "cam" });
            if (content.micOverlayEnabled) c.push({ label: qsTr("Mic spectrum"), act: "mic" });
            if (content.clockOverlayEnabled) c.push({ label: qsTr("Clock"), act: "clock" });
            if (content.chatOverlayEnabled) c.push({ label: qsTr("Chat"), act: "chat" });
            return c;
        }
        function run(act) {
            close();
            switch (act) {
            case "pad": content.overlayEditRequested(); break;
            case "cam": content.webcamEditRequested(); break;
            case "mic": content.micEditRequested(); break;
            case "clock": content.clockEditRequested(); break;
            case "chat": content.chatEditRequested(); break;
            }
        }
        property var items: []
        parent: content
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape
        padding: 6
        width: 240
        height: Math.min(moveList.contentHeight + topPadding + bottomPadding + movePopupTitle.height + 6, content.height - 8)
        x: {
            const bx = moveButton.mapToItem(content, 0, 0).x;
            return Math.max(Theme.space2, Math.min(bx, content.width - width - Theme.space2));
        }
        y: 4
        onAboutToShow: { items = choices(); moveList.currentIndex = 0; }
        onOpened: moveList.forceActiveFocus(Qt.TabFocusReason)
        onClosed: if (content.visible && moveButton.visible) moveButton.forceActiveFocus(Qt.TabFocusReason)
        background: Rectangle {
            radius: Theme.radiusControl
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.accent
        }
        contentItem: Column {
            spacing: 4
            Label {
                id: movePopupTitle
                leftPadding: 10
                topPadding: 2
                text: qsTr("MOVE / RESIZE")
                font.pixelSize: Theme.fontCaption
                font.letterSpacing: 1.5
                font.weight: Font.DemiBold
                color: Theme.textMuted
            }
            ListView {
                id: moveList
                width: parent.width
                height: movePopup.availableHeight - movePopupTitle.height - 4
                clip: true
                model: movePopup.items
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: 0
                Keys.onReturnPressed: movePopup.run(movePopup.items[currentIndex].act)
                Keys.onEnterPressed: movePopup.run(movePopup.items[currentIndex].act)
                Keys.onSpacePressed: movePopup.run(movePopup.items[currentIndex].act)
                Keys.onEscapePressed: movePopup.close()
                Keys.onLeftPressed: (event) => event.accepted = true
                Keys.onRightPressed: (event) => event.accepted = true
                delegate: ItemDelegate {
                    id: moveRow
                    required property int index
                    required property var modelData
                    width: ListView.view.width
                    height: 36
                    focusPolicy: Qt.NoFocus
                    highlighted: ListView.isCurrentItem
                    onClicked: movePopup.run(modelData.act)
                    background: Rectangle {
                        radius: Theme.radiusControl - 2
                        color: moveRow.highlighted ? Qt.rgba(0, 0.655, 1, 0.22) : moveRow.hovered ? Theme.surfaceHover : "transparent"
                        border.width: moveRow.highlighted ? Theme.focusWidth - 1 : 0
                        border.color: Theme.accent
                    }
                    contentItem: Label {
                        leftPadding: 8
                        text: moveRow.modelData.label
                        font.pixelSize: Theme.fontLabel
                        color: Theme.text
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }

    // ---------- popup เลือกไมค์ ----------
    // วางทับในกรอบเมนูเอง (ไม่ล้นขึ้นไปบนวิดีโอ) เพราะโหมด OpenGL เมนูเป็นหน้าต่างแยกที่สูงเท่าเมนู — รายการยาวเลื่อนได้
    Popup {
        id: micDevicePopup
        property string kind: "mic"            // "mic" | "speaker" — popup เดียวใช้ทั้งไมค์และลำโพง
        property Item opener: micDeviceButton
        readonly property var devices: [""].concat(kind === "mic" ? Chiaki.settings.availableAudioInDevices : Chiaki.settings.availableAudioOutDevices)
        readonly property string currentDevice: kind === "mic" ? content.micDevice : content.speakerDevice
        parent: content
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 6
        width: Math.min(420, content.width - 2 * Theme.space6)
        height: Math.min(micList.contentHeight + topPadding + bottomPadding + micPopupTitle.height + 6, content.height - 8)
        x: {
            const bx = opener.mapToItem(content, 0, 0).x;
            return Math.max(Theme.space2, Math.min(bx, content.width - width - Theme.space2));
        }
        y: 4
        onAboutToShow: {
            Chiaki.settings.refreshAudioDevices();
            micList.currentIndex = Math.max(0, devices.indexOf(currentDevice));
        }
        onOpened: {
            micList.forceActiveFocus(Qt.TabFocusReason);
            micList.positionViewAtIndex(micList.currentIndex, ListView.Contain);
        }
        onClosed: if (content.visible && opener) opener.forceActiveFocus(Qt.TabFocusReason)
        // รายการอัปเดตแบบ async หลัง refresh — คงตัวเลือกไว้ที่อุปกรณ์ปัจจุบัน
        onDevicesChanged: if (visible) micList.currentIndex = Math.max(0, devices.indexOf(currentDevice))

        function pick(index) {
            const name = index > 0 ? devices[index] : "";
            if (kind === "mic") {
                if (Chiaki.session)
                    Chiaki.session.audioInDevice = name;
                Chiaki.settings.audioInDevice = name;   // จำไว้ใช้สตรีมหน้า (Settings → Audio อ่านค่าเดียวกัน)
            } else {
                if (Chiaki.session)
                    Chiaki.session.audioOutDevice = name;   // สลับลำโพงทันที
                Chiaki.settings.audioOutDevice = name;
            }
            close();
        }

        background: Rectangle {
            radius: Theme.radiusControl
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.accent
        }

        contentItem: Column {
            spacing: 4
            Label {
                id: micPopupTitle
                leftPadding: 10
                topPadding: 2
                text: micDevicePopup.kind === "mic" ? qsTr("MICROPHONE") : qsTr("SPEAKER")
                font.pixelSize: Theme.fontCaption
                font.letterSpacing: 1.5
                font.weight: Font.DemiBold
                color: Theme.textMuted
            }
            ListView {
                id: micList
                width: parent.width
                height: micDevicePopup.availableHeight - micPopupTitle.height - 4
                clip: true
                model: micDevicePopup.devices
                keyNavigationWraps: false
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: 0
                ScrollBar.vertical: ScrollBar { policy: micList.contentHeight > micList.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
                Keys.onReturnPressed: micDevicePopup.pick(currentIndex)
                Keys.onEnterPressed: micDevicePopup.pick(currentIndex)
                Keys.onSpacePressed: micDevicePopup.pick(currentIndex)
                Keys.onEscapePressed: micDevicePopup.close()
                Keys.onLeftPressed: (event) => event.accepted = true    // กันโฟกัสหลุดออกจาก popup
                Keys.onRightPressed: (event) => event.accepted = true
                delegate: ItemDelegate {
                    id: micRow
                    required property int index
                    required property var modelData
                    readonly property bool current: modelData === micDevicePopup.currentDevice
                    width: ListView.view.width - 10
                    height: 36
                    focusPolicy: Qt.NoFocus
                    highlighted: ListView.isCurrentItem
                    onClicked: micDevicePopup.pick(index)
                    background: Rectangle {
                        radius: Theme.radiusControl - 2
                        color: micRow.highlighted ? Qt.rgba(0, 0.655, 1, 0.22) : micRow.hovered ? Theme.surfaceHover : "transparent"
                        border.width: micRow.highlighted ? Theme.focusWidth - 1 : 0
                        border.color: Theme.accent
                    }
                    contentItem: Row {
                        spacing: 8
                        leftPadding: 4
                        Label {
                            width: 16
                            anchors.verticalCenter: parent.verticalCenter
                            text: micRow.current ? "✓" : ""
                            color: Theme.accent
                            font.pixelSize: Theme.fontLabel
                            font.bold: true
                        }
                        Label {
                            width: micRow.width - 48
                            anchors.verticalCenter: parent.verticalCenter
                            text: content.micDeviceLabel(micRow.modelData)
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontLabel
                            font.weight: micRow.current ? Font.DemiBold : Font.Normal
                            color: Theme.text
                        }
                    }
                }
            }
        }
    }

    // ---------- popup ขนาดพื้นที่ภาพ (PS-WRAP) ----------
    // รายการมาจาก C++ ตอนเปิด (เฉพาะขนาดที่วางบนจอนี้ได้ + Fullscreen) — ↑↓ เลือก · ✕/Enter ยืนยัน · ◯/Esc ยกเลิก
    Popup {
        id: sizePopup
        property var sizes: []
        parent: content
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 6
        width: Math.min(340, content.width - 2 * Theme.space6)
        height: Math.min(sizeList.contentHeight + topPadding + bottomPadding + sizePopupTitle.height + 6, content.height - 8)
        x: {
            const bx = sizeButton.mapToItem(content, 0, 0).x;
            return Math.max(Theme.space2, Math.min(bx, content.width - width - Theme.space2));
        }
        y: 4
        onAboutToShow: {
            sizes = Chiaki.window.playerSizes();
            let cur = 0;
            for (let i = 0; i < sizes.length; i++)
                if (sizes[i].current) cur = i;
            sizeList.currentIndex = cur;
        }
        onOpened: {
            sizeList.forceActiveFocus(Qt.TabFocusReason);
            sizeList.positionViewAtIndex(sizeList.currentIndex, ListView.Contain);
        }
        onClosed: if (content.visible) sizeButton.forceActiveFocus(Qt.TabFocusReason)

        function label(s) { return s.height < 0 ? qsTr("Fullscreen") : qsTr("%1p").arg(s.height); }
        function pick(index) {
            const s = sizes[index];
            close();
            if (s)
                Chiaki.window.setPlayerSize(s.width, s.height);
        }

        background: Rectangle {
            radius: Theme.radiusControl
            color: Theme.surfaceRaised
            border.width: 1
            border.color: Theme.accent
        }

        contentItem: Column {
            spacing: 4
            Label {
                id: sizePopupTitle
                leftPadding: 10
                topPadding: 2
                text: qsTr("PICTURE SIZE")
                font.pixelSize: Theme.fontCaption
                font.letterSpacing: 1.5
                font.weight: Font.DemiBold
                color: Theme.textMuted
            }
            ListView {
                id: sizeList
                width: parent.width
                height: sizePopup.availableHeight - sizePopupTitle.height - 4
                clip: true
                model: sizePopup.sizes
                keyNavigationWraps: false
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: 0
                ScrollBar.vertical: ScrollBar { policy: sizeList.contentHeight > sizeList.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
                Keys.onReturnPressed: sizePopup.pick(currentIndex)
                Keys.onEnterPressed: sizePopup.pick(currentIndex)
                Keys.onSpacePressed: sizePopup.pick(currentIndex)
                Keys.onEscapePressed: sizePopup.close()
                Keys.onLeftPressed: (event) => event.accepted = true    // กันโฟกัสหลุดออกจาก popup
                Keys.onRightPressed: (event) => event.accepted = true
                delegate: ItemDelegate {
                    id: sizeRow
                    required property int index
                    required property var modelData
                    width: ListView.view.width - 10
                    height: 36
                    focusPolicy: Qt.NoFocus
                    highlighted: ListView.isCurrentItem
                    onClicked: sizePopup.pick(index)
                    background: Rectangle {
                        radius: Theme.radiusControl - 2
                        color: sizeRow.highlighted ? Qt.rgba(0, 0.655, 1, 0.22) : sizeRow.hovered ? Theme.surfaceHover : "transparent"
                        border.width: sizeRow.highlighted ? Theme.focusWidth - 1 : 0
                        border.color: Theme.accent
                    }
                    contentItem: Row {
                        spacing: 8
                        leftPadding: 4
                        Label {
                            width: 16
                            anchors.verticalCenter: parent.verticalCenter
                            text: sizeRow.modelData.current ? "✓" : ""
                            color: Theme.accent
                            font.pixelSize: Theme.fontLabel
                            font.bold: true
                        }
                        Label {
                            width: 96
                            anchors.verticalCenter: parent.verticalCenter
                            text: sizePopup.label(sizeRow.modelData)
                            font.pixelSize: Theme.fontLabel
                            font.weight: sizeRow.modelData.current ? Font.DemiBold : Font.Normal
                            color: Theme.text
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: sizeRow.modelData.height < 0 ? "" : sizeRow.modelData.width + " × " + sizeRow.modelData.height
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !!sizeRow.modelData.stream
                            text: qsTr("STREAM")
                            font.pixelSize: Theme.fontCaption
                            font.letterSpacing: 1
                            font.weight: Font.DemiBold
                            color: Theme.accent
                        }
                    }
                }
            }
        }
    }
}
