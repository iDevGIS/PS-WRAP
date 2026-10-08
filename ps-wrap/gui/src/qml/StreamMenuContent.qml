import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

// PS-WRAP: เนื้อหาเมนูระหว่างสตรีม (v3 "control deck") ใช้ร่วมกันทั้ง inline (Vulkan) และ StreamMenuWindow (OpenGL)
//  แถว 1: [⏻ End Stream] [● Record] [🎤 Mic] 🔊 ━━━ 100%                       3.2 Mbps · 0.0% loss · 3 dropped · host
//  แถว 2 (Flow — ห่อบรรทัดเมื่อแคบ ไม่เลื่อนแนวนอน): FIT (Zoom|Stretch|Glow|Size) · QUALITY (Default|HQ|HQ+S|HQ+A|Custom|Frame Gen) · Display · Renderer · OVERLAY (Pad|Cam|Spectrum|Clock|Chat|Stack|Move…/Arrange|Stats|Light) · CAPTURE (Replay|60s|Save|Screenshot)
//  แถว 3: hotkey hint (ซ่อนเมื่อแคบ)
//  ความสูงเมนู = implicitHeight (StreamView/StreamMenuWindow ผูกตามนี้) · คง id/signals/KeyNavigation ของ upstream
FocusScope {
    id: content
    property Item initialFocusItem: closeButton
    property bool overlayEnabled: true
    property bool webcamEnabled: false
    property bool dockEnabled: false   // PS-WRAP: Stack — overlay เรียงคอลัมน์เดียว (StreamView จัดให้)
    // PS-WRAP: อัดคลิป + mic spectrum — ผูกกับ Chiaki.window ตรงๆ (เหมือนปุ่ม Mic/Stats) จึงใช้ได้ทั้ง inline และ window แยก
    readonly property QtObject recorder: Chiaki.window ? Chiaki.window.recorder : null
    readonly property bool recording: !!recorder && recorder.recording
    readonly property bool micOverlayEnabled: !!Chiaki.window && Chiaki.window.micOverlay
    readonly property bool clockOverlayEnabled: !!Chiaki.window && !!Chiaki.window.clockOverlay
    readonly property bool chatOverlayEnabled: !!Chiaki.window && !!Chiaki.window.chatOverlay
    // PS-WRAP: Instant Replay (Chiaki.window.replayEnabled/replaySeconds/saveReplay + recorder.replayActive)
    readonly property bool replayEnabled: !!Chiaki.window && !!Chiaki.window.replayEnabled
    readonly property bool replayActive: !!recorder && !!recorder.replayActive
    readonly property int replaySeconds: Chiaki.window && Chiaki.window.replaySeconds > 0 ? Chiaki.window.replaySeconds : 60
    // PS-WRAP: Go Live (Chiaki.goLive — pswraplive.h) · state: off/connecting/live/reconnecting/error
    readonly property var goLive: Chiaki.goLive !== undefined ? Chiaki.goLive : null
    readonly property bool liveOn: !!goLive && goLive.live
    function liveDotColor(state) {
        return state === "live" ? Theme.success : state === "error" ? Theme.danger : Theme.warning;
    }
    // PS-WRAP: ไมค์ที่ใช้อยู่ ("" = Auto) — ระหว่างสตรีมอ่านจาก session (เปลี่ยนสด), นอกนั้นจาก settings
    readonly property string micDevice: Chiaki.session ? Chiaki.session.audioInDevice : Chiaki.settings.audioInDevice
    readonly property bool popupOpen: micDevicePopup.visible || sizePopup.visible   // StreamMenuWindow ปิด Shortcut Esc ระหว่างนี้ (ให้ Esc ปิดแค่ popup)
    function micDeviceLabel(name) { return name && name.length ? name : qsTr("Auto (Windows default)"); }
    onVisibleChanged: if (!visible) micDevicePopup.close()
    readonly property bool narrow: width < 760
    function formatElapsed(sec) {
        const s = Math.max(0, Math.floor(sec || 0));
        const pad = (n) => (n < 10 ? "0" : "") + n;
        return pad(Math.floor(s / 3600)) + ":" + pad(Math.floor(s / 60) % 60) + ":" + pad(s % 60);
    }
    implicitHeight: deck.implicitHeight + Theme.space4 * 2 + 2

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

    // ปุ่ม: pill เดี่ยว (primary) หรือ segmented (อยู่ในกลุ่ม) · checkable = toggle เติม accent
    component MenuButton: Button {
        id: mb
        property url iconSource: ""
        property bool danger: false
        property bool segmented: false
        property int glyph: 0           // PS-WRAP: 0 = ไม่มี · 1 = จุดอัด (แดง) · 2 = สี่เหลี่ยมหยุด — ใช้แทน icon (ไม่ต้องเพิ่ม svg)
        property int maxTextWidth: -1   // PS-WRAP: >0 = ตัดข้อความยาวด้วย … (ชื่ออุปกรณ์)
        property bool caret: false      // PS-WRAP: ▾ ท้ายปุ่ม = เปิดรายการ
        property color statusDot: "transparent"   // PS-WRAP: จุดสถานะเล็กหลัง icon (Go Live) — transparent = ไม่แสดง
        flat: true
        padding: segmented ? 6 : 8
        leftPadding: iconSource != "" || glyph > 0 ? 12 : (segmented ? 14 : 18)
        rightPadding: segmented ? 14 : 18
        font.pixelSize: Theme.fontLabel
        font.weight: Font.DemiBold
        background: Rectangle {
            implicitHeight: mb.segmented ? 36 : 44
            radius: Theme.radiusChip
            color: mb.danger ? (mb.down ? "#b33a3a" : Theme.danger)
                 : mb.checked ? Theme.accent
                 : mb.down ? Theme.surfaceHover
                 : mb.hovered ? Theme.surfaceRaised
                 : (mb.segmented ? "transparent" : Qt.rgba(1, 1, 1, 0.04))
            border.width: mb.segmented || mb.checked || mb.danger ? 0 : 1
            border.color: Theme.border
            opacity: mb.enabled ? 1.0 : 0.4
            Behavior on color { ColorAnimation { duration: Theme.durFast } }
            Rectangle {
                anchors.fill: parent
                anchors.margins: -Theme.focusMargin
                radius: parent.radius
                color: "transparent"
                border.width: Theme.focusWidth
                border.color: mb.danger ? Theme.text : Theme.accent
                visible: mb.activeFocus
            }
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
                    opacity: mb.enabled ? 1.0 : 0.5
                }
            }
            Rectangle {
                visible: mb.statusDot.a > 0
                Layout.preferredWidth: 10
                Layout.preferredHeight: 10
                radius: 5
                color: mb.statusDot
                border.width: 1
                border.color: Qt.rgba(0, 0, 0, 0.35)
            }
            Label {
                text: mb.text
                font: mb.font
                color: mb.checked || mb.danger ? Theme.accentText : Theme.text
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

    // กลุ่มปุ่มแบบ segmented: แคปซูลเดียว สูง 44 เท่ากันทุกกลุ่ม ป้ายชื่ออยู่ "ใน" แคปซูล (baseline ตรงกันเสมอ)
    component Segment: Rectangle {
        id: seg
        property string label: ""
        default property alias items: segRow.data
        width: segLayout.implicitWidth + 8
        height: 44
        radius: Theme.radiusChip
        color: Qt.rgba(1, 1, 1, 0.04)
        border.width: 1
        border.color: Theme.border
        RowLayout {
            id: segLayout
            anchors.fill: parent
            anchors.margins: 4
            spacing: 0
            Label {
                visible: seg.label.length > 0
                Layout.leftMargin: 12
                Layout.rightMargin: 10
                text: seg.label
                font.pixelSize: Theme.fontCaption
                font.letterSpacing: 1.5
                font.weight: Font.DemiBold
                color: Theme.textMuted
            }
            RowLayout {
                id: segRow
                spacing: 2
            }
        }
    }

    component Divider: Rectangle {
        Layout.preferredWidth: 1
        Layout.preferredHeight: 26
        Layout.leftMargin: 6
        Layout.rightMargin: 6
        color: Theme.border
    }

    component Caption: Label {
        font.pixelSize: Theme.fontCaption
        color: Theme.textMuted
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.surface
        opacity: 0.96
        Rectangle { anchors.top: parent.top; width: parent.width; height: 2; color: Theme.accent; opacity: 0.8 }
    }

    ColumnLayout {
        id: deck
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            topMargin: Theme.space4 + 2
            leftMargin: Theme.space6
            rightMargin: Theme.space6
        }
        spacing: Theme.space3

        // ---------- แถว 1: ควบคุมหลัก + stats ----------
        RowLayout {
            id: topRow
            Layout.fillWidth: true
            spacing: Theme.space3

            MenuButton {
                id: closeButton
                danger: true
                iconSource: "qrc:/icons/menu/power.svg"
                text: Chiaki.session ? qsTr("End Stream") : qsTr("Main Menu")
                activeFocusOnTab: true
                onClicked: {
                    if (Chiaki.session)
                        Chiaki.window.close();
                    else
                        content.mainViewRequested();
                }
                KeyNavigation.right: recordButton
                KeyNavigation.down: zoomButton
                Keys.onReturnPressed: clicked()
                Keys.onEscapePressed: content.closeRequested()
            }

            Divider {}

            // PS-WRAP: อัดคลิป — ระหว่างอัดเป็นปุ่มแดง "■ 00:12:34" (กด = หยุด) · busy = กำลังปิดไฟล์
            MenuButton {
                id: recordButton
                danger: content.recording
                iconSource: content.recording ? "" : "qrc:/icons/menu/record.svg"
                glyph: content.recording ? 2 : 0
                text: content.recorder && content.recorder.busy ? qsTr("Saving…")
                    : content.recording ? content.formatElapsed(content.recorder.seconds)
                    : qsTr("Record")
                // ไม่ disable ตอน busy (focus จอยจะหลุดจากปุ่ม) — กดซ้ำระหว่าง busy = ไม่ทำอะไร
                enabled: !!content.recorder && (content.recording || content.recorder.busy || (!!Chiaki.session && Chiaki.session.connected))
                onClicked: if (!content.recorder.busy) Chiaki.window.toggleRecording()
                KeyNavigation.left: closeButton
                KeyNavigation.right: muteButton
                KeyNavigation.down: zoomButton
                Keys.onReturnPressed: clicked()
                Keys.onEscapePressed: content.closeRequested()
            }

            MenuButton {
                id: muteButton
                iconSource: "qrc:/icons/menu/mic.svg"
                text: qsTr("Mic")
                checkable: true
                enabled: Chiaki.session && Chiaki.session.connected
                checked: Chiaki.session && !Chiaki.session.muted
                onToggled: Chiaki.session.muted = !Chiaki.session.muted
                KeyNavigation.left: recordButton
                KeyNavigation.right: micDeviceButton
                KeyNavigation.down: zoomButton
                Keys.onReturnPressed: toggled()
                Keys.onEscapePressed: content.closeRequested()
            }

            // PS-WRAP: เลือกไมค์ระหว่างสตรีม — ✕/Enter เปิดรายการ · ↑↓ เลือก · ✕/Enter ยืนยัน · ◯/Esc ยกเลิก
            MenuButton {
                id: micDeviceButton
                caret: true
                maxTextWidth: content.narrow ? 90 : 170
                text: content.micDeviceLabel(content.micDevice)
                enabled: Chiaki.session && Chiaki.session.connected
                onClicked: micDevicePopup.open()
                KeyNavigation.left: muteButton
                KeyNavigation.right: volumeSlider
                KeyNavigation.down: zoomButton
                Keys.onReturnPressed: clicked()
                Keys.onEscapePressed: content.closeRequested()
                ToolTip.visible: hovered && !micDevicePopup.visible
                ToolTip.delay: 600
                ToolTip.text: qsTr("Microphone: %1").arg(text)
            }

            Image {
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                Layout.leftMargin: Theme.space2
                sourceSize: Qt.size(22, 22)
                source: "qrc:/icons/menu/volume.svg"
                opacity: 0.8
            }

            Slider {
                id: volumeSlider
                Layout.preferredWidth: content.narrow ? 110 : 160
                from: 0
                to: 128
                stepSize: 1
                value: Chiaki.settings.audioVolume
                onMoved: Chiaki.settings.audioVolume = value
                KeyNavigation.down: zoomButton
                KeyNavigation.up: micDeviceButton
                Keys.onEscapePressed: content.closeRequested()
                // ซ้าย/ขวา = ปรับค่า (ของ Slider เอง) · ออกจาก slider ด้วย Tab/↓ หรือ Enter → Mic
                Keys.onReturnPressed: muteButton.forceActiveFocus(Qt.TabFocusReason)
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -Theme.focusMargin
                    radius: Theme.radiusControl
                    color: "transparent"
                    border.width: Theme.focusWidth
                    border.color: Theme.accent
                    visible: volumeSlider.activeFocus
                }
            }

            Label {
                Layout.preferredWidth: 44
                text: Math.round((volumeSlider.value / 128.0) * 100) + "%"
                font.pixelSize: Theme.fontLabel
                color: Theme.textMuted
            }

            Item { Layout.fillWidth: true }

            // stats แบบบรรทัดเดียว (Mbps เด่น · loss/dropped · host)
            RowLayout {
                id: statsBlock
                visible: Chiaki.session
                spacing: 6
                Label {
                    text: Chiaki.session ? Chiaki.session.measuredBitrate.toFixed(1) : "0.0"
                    color: Theme.accent
                    font.bold: true
                    font.pixelSize: Theme.fontTitle
                }
                Caption { text: "Mbps" }
                Caption {
                    Layout.leftMargin: 6
                    text: {
                        const loss = ((Chiaki.session && isFinite(Chiaki.session.averagePacketLoss)) ? Chiaki.session.averagePacketLoss : 0) * 100;
                        return qsTr("· %1% loss · %2 dropped").arg(loss.toFixed(1)).arg(Chiaki.window.droppedFrames);
                    }
                }
                Caption {
                    visible: !content.narrow
                    Layout.leftMargin: 6
                    color: Theme.text
                    text: {
                        if (!Chiaki.session) return "";
                        const host = Chiaki.settings.streamerMode ? "hidden" : Chiaki.session.host;
                        return (Chiaki.session.connected ? "· " : qsTr("· connecting ")) + "<b>" + host + "</b>";
                    }
                }
            }
        }

        // ---------- แถว 2: ตัวเลือก (ห่อบรรทัดอัตโนมัติ) ----------
        Flow {
            id: optionRow
            Layout.fillWidth: true
            spacing: Theme.space3

            Segment {
                label: qsTr("FIT")
                MenuButton {
                    id: zoomButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/zoom.svg"
                    text: qsTr("Zoom")
                    checkable: true
                    checked: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Zoom
                    onToggled: Chiaki.window.videoMode = Chiaki.window.videoMode == ChiakiWindow.VideoMode.Zoom ? ChiakiWindow.VideoMode.Normal : ChiakiWindow.VideoMode.Zoom
                    KeyNavigation.up: muteButton
                    KeyNavigation.right: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Zoom ? zoomFactor : stretchButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                }
                Slider {
                    id: zoomFactor
                    Layout.preferredWidth: 100
                    from: -1
                    to: 4
                    stepSize: 0.01
                    visible: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Zoom
                    value: Chiaki.window.ZoomFactor
                    onMoved: {
                        Chiaki.window.ZoomFactor = value
                        Chiaki.settings.sZoomFactor = value
                    }
                    KeyNavigation.up: muteButton
                    Keys.onReturnPressed: stretchButton.forceActiveFocus(Qt.TabFocusReason)
                    Keys.onEscapePressed: content.closeRequested()
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -Theme.focusMargin
                        radius: Theme.radiusControl
                        color: "transparent"
                        border.width: Theme.focusWidth
                        border.color: Theme.accent
                        visible: zoomFactor.activeFocus
                    }
                }
                Caption {
                    visible: zoomFactor.visible
                    Layout.rightMargin: 6
                    text: zoomFactor.value === -1 ? qsTr("No bars") : (zoomFactor.value >= 0 ? (zoomFactor.value + 1).toFixed(2) : zoomFactor.value.toFixed(2)) + "x"
                }
                MenuButton {
                    id: stretchButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/stretch.svg"
                    text: qsTr("Stretch")
                    checkable: true
                    checked: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Stretch
                    onToggled: Chiaki.window.videoMode = Chiaki.window.videoMode == ChiakiWindow.VideoMode.Stretch ? ChiakiWindow.VideoMode.Normal : ChiakiWindow.VideoMode.Stretch
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Zoom ? zoomFactor : zoomButton
                    KeyNavigation.right: glowButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                }
                // PS-WRAP: Ambient light — ขอบว่างรอบภาพเป็นแสงเบลอจากขอบเกม (เห็นผลเมื่อมีขอบ เช่น จอกว้าง / หน้าต่างไม่ใช่ 16:9)
                MenuButton {
                    id: glowButton
                    segmented: true
                    text: qsTr("Glow")
                    checkable: true
                    checked: !!Chiaki.window.ambientLight
                    onToggled: Chiaki.window.ambientLight = !Chiaki.window.ambientLight
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: stretchButton
                    KeyNavigation.right: sizeButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Ambient light: fill the empty bars around the picture with a soft glow from the game")
                }
                // PS-WRAP: ขนาดพื้นที่ภาพ 16:9 ตาม preset (ไม่มีขอบดำ คลิปอัดได้ขนาดตามชื่อ) — ✕/Enter เปิดรายการ
                MenuButton {
                    id: sizeButton
                    segmented: true
                    caret: true
                    text: qsTr("Size")
                    onClicked: sizePopup.open()
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: glowButton
                    KeyNavigation.right: defaultButton
                    Keys.onReturnPressed: clicked()
                    Keys.onEscapePressed: content.closeRequested()
                    ToolTip.visible: hovered && !sizePopup.visible
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Set the picture to an exact 16:9 size — no black bars, screenshots match the size")
                }
            }

            Segment {
                label: qsTr("QUALITY")
                MenuButton {
                    id: defaultButton
                    segmented: true
                    text: qsTr("Default")
                    checkable: true
                    checked: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Default
                    onToggled: {
                        Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.Default
                        Chiaki.settings.videoPreset = ChiakiWindow.VideoPreset.Default
                    }
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: sizeButton
                    KeyNavigation.right: highQualityButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                }
                MenuButton {
                    id: highQualityButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/quality.svg"
                    text: qsTr("HQ")
                    checkable: true
                    checked: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.HighQuality
                    onToggled: {
                        Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.HighQuality
                        Chiaki.settings.videoPreset = ChiakiWindow.VideoPreset.HighQuality
                    }
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: defaultButton
                    KeyNavigation.right: highQualitySpatialButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                }
                MenuButton {
                    id: highQualitySpatialButton
                    segmented: true
                    text: content.narrow ? qsTr("HQ+S") : qsTr("HQ + Spatial")
                    checkable: true
                    checked: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.HighQualitySpatial
                    onToggled: {
                        Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.HighQualitySpatial
                        Chiaki.settings.videoPreset = ChiakiWindow.VideoPreset.HighQualitySpatial
                    }
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: highQualityButton
                    KeyNavigation.right: highQualityAdvancedSpatialButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                }
                MenuButton {
                    id: highQualityAdvancedSpatialButton
                    segmented: true
                    text: content.narrow ? qsTr("HQ+A") : qsTr("HQ + Adv")
                    checkable: true
                    checked: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.HighQualityAdvancedSpatial
                    onToggled: {
                        Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.HighQualityAdvancedSpatial
                        Chiaki.settings.videoPreset = ChiakiWindow.VideoPreset.HighQualityAdvancedSpatial
                    }
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: highQualitySpatialButton
                    KeyNavigation.right: customButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                }
                MenuButton {
                    id: customButton
                    segmented: true
                    text: qsTr("Custom")
                    checkable: true
                    checked: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom
                    onToggled: {
                        Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.Custom
                        Chiaki.settings.videoPreset = ChiakiWindow.VideoPreset.Custom
                    }
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: highQualityAdvancedSpatialButton
                    KeyNavigation.right: frameGenButton.enabled ? frameGenButton : displaySettingsButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                }
                // PS-WRAP: Frame generation — เฟรมกลางระหว่างเฟรมจริง (60 → 120) · ต้อง Direct Mapping + Vulkan + จอเร็วกว่าสตรีม
                MenuButton {
                    id: frameGenButton
                    segmented: true
                    text: content.narrow ? qsTr("FG") : (Chiaki.window.frameGenActive ? qsTr("Frame Gen · 120") : qsTr("Frame Gen"))
                    checkable: true
                    enabled: !!Chiaki.window.frameGenSupported
                    checked: !!Chiaki.window.frameGen && !!Chiaki.window.frameGenSupported
                    onToggled: Chiaki.window.frameGen = !Chiaki.window.frameGen
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: customButton
                    KeyNavigation.right: displaySettingsButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: !Chiaki.window.frameGenSupported
                        ? qsTr("Frame generation needs Frame Delivery = Direct Mapping and the Vulkan renderer (Settings › Video)")
                        : qsTr("Frame generation: adds an in-between frame for each stream frame (60 → 120 fps) on displays faster than the stream. Adds about half a frame of delay.")
                }
            }

            Segment {
                MenuButton {
                    id: displaySettingsButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/display.svg"
                    text: qsTr("Display")
                    onClicked: content.displaySettingsRequested()
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: frameGenButton.enabled ? frameGenButton : customButton
                    KeyNavigation.right: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom ? placeboSettingsButton : overlayButton
                    Keys.onReturnPressed: clicked()
                    Keys.onEscapePressed: content.closeRequested()
                }
                MenuButton {
                    id: placeboSettingsButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/renderer.svg"
                    text: qsTr("Renderer")
                    visible: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom
                    onClicked: content.placeboSettingsRequested()
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: displaySettingsButton
                    KeyNavigation.right: overlayButton
                    Keys.onReturnPressed: clicked()
                    Keys.onEscapePressed: content.closeRequested()
                }
            }

            Segment {
                label: qsTr("OVERLAY")
                MenuButton {
                    id: overlayButton
                    segmented: true
                    iconSource: "qrc:/icons/controller.svg"
                    text: qsTr("Pad")
                    checkable: true
                    checked: content.overlayEnabled
                    onToggled: content.overlayToggled()
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom ? placeboSettingsButton : displaySettingsButton
                    KeyNavigation.right: camButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                }
                MenuButton {
                    id: camButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/cam.svg"
                    text: qsTr("Cam")
                    checkable: true
                    checked: content.webcamEnabled
                    onToggled: content.webcamToggled()
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: overlayButton
                    KeyNavigation.right: micVizButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                }
                // PS-WRAP: mic spectrum overlay
                MenuButton {
                    id: micVizButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/spectrum.svg"
                    text: qsTr("Spectrum")
                    checkable: true
                    checked: content.micOverlayEnabled
                    onToggled: Chiaki.window.micOverlay = !Chiaki.window.micOverlay
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: camButton
                    KeyNavigation.right: clockButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                }
                // PS-WRAP: นาฬิกา + เวลาเล่น (ClockOverlay.qml)
                MenuButton {
                    id: clockButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/clock.svg"
                    text: qsTr("Clock")
                    checkable: true
                    checked: content.clockOverlayEnabled
                    onToggled: Chiaki.window.clockOverlay = !Chiaki.window.clockOverlay
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: micVizButton
                    KeyNavigation.right: chatButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                }
                // PS-WRAP: แชทไลฟ์บนจอ (YouTube / Twitch — ตั้งแหล่งใน Settings › Go Live › Chat on screen)
                MenuButton {
                    id: chatButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/chat.svg"
                    text: qsTr("Chat")
                    checkable: true
                    checked: !!Chiaki.window && !!Chiaki.window.chatOverlay
                    onToggled: Chiaki.window.chatOverlay = !Chiaki.window.chatOverlay
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: clockButton
                    KeyNavigation.right: stackButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                }
                // PS-WRAP: Stack — overlay ที่เปิดอยู่ทั้งหมดเรียงเป็นคอลัมน์เดียว กว้างเท่ากัน · ปุ่มถัดไปกลายเป็น Arrange (แก้ทั้งชุด)
                MenuButton {
                    id: stackButton
                    segmented: true
                    text: qsTr("Stack")
                    checkable: true
                    checked: content.dockEnabled
                    onToggled: content.dockToggled()
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: chatButton
                    KeyNavigation.right: editOverlayButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Stack: line up every overlay in one column at the same width — move and resize them all at once")
                }
                MenuButton {
                    id: editOverlayButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/move.svg"
                    text: content.dockEnabled ? qsTr("Arrange") : qsTr("Move")
                    enabled: Chiaki.session
                    onClicked: content.dockEnabled ? content.dockEditRequested() : content.overlayEditRequested()
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: stackButton
                    KeyNavigation.right: content.dockEnabled ? statsButton : (content.webcamEnabled ? camEditButton : (content.micOverlayEnabled ? micEditButton : (content.clockOverlayEnabled ? clockEditButton : (content.chatOverlayEnabled ? chatEditButton : statsButton))))
                    Keys.onReturnPressed: clicked()
                    Keys.onEscapePressed: content.closeRequested()
                }
                MenuButton {
                    id: camEditButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/move.svg"
                    text: qsTr("Move cam")
                    visible: content.webcamEnabled && !content.dockEnabled
                    enabled: Chiaki.session
                    onClicked: content.webcamEditRequested()
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: editOverlayButton
                    KeyNavigation.right: content.micOverlayEnabled ? micEditButton : (content.clockOverlayEnabled ? clockEditButton : (content.chatOverlayEnabled ? chatEditButton : statsButton))
                    Keys.onReturnPressed: clicked()
                    Keys.onEscapePressed: content.closeRequested()
                }
                MenuButton {
                    id: micEditButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/move.svg"
                    text: qsTr("Move mic")
                    visible: content.micOverlayEnabled && !content.dockEnabled
                    enabled: Chiaki.session
                    onClicked: content.micEditRequested()
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: content.webcamEnabled ? camEditButton : editOverlayButton
                    KeyNavigation.right: content.clockOverlayEnabled ? clockEditButton : (content.chatOverlayEnabled ? chatEditButton : statsButton)
                    Keys.onReturnPressed: clicked()
                    Keys.onEscapePressed: content.closeRequested()
                }
                MenuButton {
                    id: clockEditButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/move.svg"
                    text: qsTr("Move clock")
                    visible: content.clockOverlayEnabled && !content.dockEnabled
                    enabled: Chiaki.session
                    onClicked: content.clockEditRequested()
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: content.micOverlayEnabled ? micEditButton : (content.webcamEnabled ? camEditButton : editOverlayButton)
                    KeyNavigation.right: content.chatOverlayEnabled ? chatEditButton : statsButton
                    Keys.onReturnPressed: clicked()
                    Keys.onEscapePressed: content.closeRequested()
                }
                MenuButton {
                    id: chatEditButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/move.svg"
                    text: qsTr("Move chat")
                    visible: content.chatOverlayEnabled && !content.dockEnabled
                    enabled: Chiaki.session
                    onClicked: content.chatEditRequested()
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: content.clockOverlayEnabled ? clockEditButton : (content.micOverlayEnabled ? micEditButton : (content.webcamEnabled ? camEditButton : editOverlayButton))
                    KeyNavigation.right: statsButton
                    Keys.onReturnPressed: clicked()
                    Keys.onEscapePressed: content.closeRequested()
                }
                MenuButton {
                    id: statsButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/stats.svg"
                    text: qsTr("Stats")
                    checkable: true
                    checked: Chiaki.settings.showStreamStats
                    onToggled: Chiaki.settings.showStreamStats = !Chiaki.settings.showStreamStats
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: content.dockEnabled ? editOverlayButton : (content.chatOverlayEnabled ? chatEditButton : (content.clockOverlayEnabled ? clockEditButton : (content.micOverlayEnabled ? micEditButton : (content.webcamEnabled ? camEditButton : editOverlayButton))))
                    KeyNavigation.right: haloButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                }
                // PS-WRAP: Lightbar halo — แสงเรืองขอบภาพตามสีไฟจอยที่เกมสั่ง (สีเดียวกับแถบไฟบน Pad overlay)
                MenuButton {
                    id: haloButton
                    segmented: true
                    text: qsTr("Light")
                    checkable: true
                    checked: !!Chiaki.window.lightbarHalo
                    onToggled: Chiaki.window.lightbarHalo = !Chiaki.window.lightbarHalo
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: statsButton
                    KeyNavigation.right: replayButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Lightbar halo: glow around the picture in the controller light color set by the game")
                }
            }

            // PS-WRAP: CAPTURE — Instant Replay (เปิด/ปิด · ความยาว 30/60/90/120 วิ กดวน · เซฟ) + ภาพหน้าจอ
            Segment {
                label: qsTr("CAPTURE")
                MenuButton {
                    id: replayButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/replay.svg"
                    text: qsTr("Replay")
                    checkable: true
                    checked: content.replayEnabled
                    onToggled: Chiaki.window.replayEnabled = !content.replayEnabled
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: haloButton
                    KeyNavigation.right: replayLengthButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                }
                // ✕/Enter = วนความยาว 30 → 60 → 90 → 120 → 30 (ซ้าย/ขวาไว้ย้าย focus เหมือนปุ่มอื่น)
                MenuButton {
                    id: replayLengthButton
                    segmented: true
                    text: qsTr("%1s").arg(content.replaySeconds)
                    font.features: { "tnum": 1 }
                    onClicked: {
                        const steps = [30, 60, 90, 120];
                        const i = steps.indexOf(content.replaySeconds);
                        Chiaki.window.replaySeconds = steps[(i + 1) % steps.length];
                    }
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: replayButton
                    KeyNavigation.right: saveReplayButton
                    Keys.onReturnPressed: clicked()
                    Keys.onEscapePressed: content.closeRequested()
                }
                MenuButton {
                    id: saveReplayButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/save.svg"
                    text: qsTr("Save")
                    enabled: content.replayActive
                    onClicked: Chiaki.window.saveReplay()
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: replayLengthButton
                    KeyNavigation.right: screenshotButton
                    Keys.onReturnPressed: clicked()
                    Keys.onEscapePressed: content.closeRequested()
                }
                MenuButton {
                    id: screenshotButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/screenshot.svg"
                    text: qsTr("Screenshot")
                    enabled: !!Chiaki.session && Chiaki.session.connected
                    // เมนูปิดก่อน แล้วค่อยถ่าย (ไม่ให้แถบเมนูติดไปในภาพ)
                    onClicked: {
                        content.closeRequested();
                        screenshotDelay.restart();
                    }
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: saveReplayButton
                    KeyNavigation.right: liveButton
                    Keys.onReturnPressed: clicked()
                    Keys.onEscapePressed: content.closeRequested()
                }
                // PS-WRAP: Go Live — เริ่ม/หยุดไลฟ์ไปทุกปลายทางที่เปิดใน Settings › Go Live (Ctrl+Shift+L)
                // จุดสี = สถานะรวม (เขียว live · เหลือง กำลังต่อ/ต่อใหม่ · แดง error) · ไม่มีปลายทาง → C++ แจ้ง toast เอง
                MenuButton {
                    id: liveButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/live.svg"
                    text: content.liveOn ? content.formatElapsed(content.goLive.seconds) : qsTr("Live")
                    font.features: { "tnum": 1 }
                    // ไม่ checkable: กดแล้ว start() อาจไม่สำเร็จ (ไม่มีปลายทาง/HDR) — สีตาม goLive.live เท่านั้น ไม่ให้ปุ่มสลับเอง
                    checked: content.liveOn
                    statusDot: content.liveOn ? content.liveDotColor(content.goLive.state) : "transparent"
                    enabled: !!content.goLive && (content.liveOn || (!!Chiaki.session && Chiaki.session.connected))
                    onClicked: content.goLive.toggle()
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: screenshotButton
                    KeyNavigation.right: verticalButton
                    Keys.onReturnPressed: clicked()
                    Keys.onEscapePressed: content.closeRequested()
                }
                // PS-WRAP: หน้าต่าง preview ภาพแนวตั้ง 9:16 (VerticalPreviewWindow.qml)
                MenuButton {
                    id: verticalButton
                    segmented: true
                    iconSource: "qrc:/icons/menu/vertical.svg"
                    text: qsTr("9:16")
                    checkable: true
                    checked: !!Chiaki.window && Chiaki.window.verticalPreview
                    onToggled: Chiaki.window.verticalPreview = !Chiaki.window.verticalPreview
                    KeyNavigation.up: muteButton
                    KeyNavigation.left: liveButton
                    Keys.onReturnPressed: toggled()
                    Keys.onEscapePressed: content.closeRequested()
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Vertical 9:16 preview for Shorts / TikTok / Reels")
                }
            }
        }

        // ---------- แถว 3: hotkey hint ----------
        // แบบย่อ: "Ctrl+Shift +" ครั้งเดียวแล้วตามด้วยตัวอักษร (ห่อได้ 2 บรรทัดเมื่อแคบ ไม่ล้นแนวนอน)
        Caption {
            Layout.fillWidth: true
            visible: !content.narrow
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            opacity: 0.8
            text: qsTr("Ctrl+O menu · F12 screenshot · Ctrl+Shift + R record · B save replay · K marker · S stats · O pad · E move pad · C cam · V move cam · M spectrum · T clock · H chat · L live · Esc = PS")
        }
    }

    // ---------- popup เลือกไมค์ ----------
    // วางทับในกรอบเมนูเอง (ไม่ล้นขึ้นไปบนวิดีโอ) เพราะโหมด OpenGL เมนูเป็นหน้าต่างแยกที่สูงเท่าเมนู — รายการยาวเลื่อนได้
    Popup {
        id: micDevicePopup
        readonly property var devices: [""].concat(Chiaki.settings.availableAudioInDevices)
        parent: content
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 6
        width: Math.min(420, content.width - 2 * Theme.space6)
        height: Math.min(micList.contentHeight + topPadding + bottomPadding + micPopupTitle.height + 6, content.height - 8)
        x: {
            const bx = micDeviceButton.mapToItem(content, 0, 0).x;
            return Math.max(Theme.space2, Math.min(bx, content.width - width - Theme.space2));
        }
        y: 4
        onAboutToShow: {
            Chiaki.settings.refreshAudioDevices();
            micList.currentIndex = Math.max(0, devices.indexOf(content.micDevice));
        }
        onOpened: {
            micList.forceActiveFocus(Qt.TabFocusReason);
            micList.positionViewAtIndex(micList.currentIndex, ListView.Contain);
        }
        onClosed: if (content.visible) micDeviceButton.forceActiveFocus(Qt.TabFocusReason)
        // รายการอัปเดตแบบ async หลัง refresh — คงตัวเลือกไว้ที่อุปกรณ์ปัจจุบัน
        onDevicesChanged: if (visible) micList.currentIndex = Math.max(0, devices.indexOf(content.micDevice))

        function pick(index) {
            const name = index > 0 ? devices[index] : "";
            if (Chiaki.session)
                Chiaki.session.audioInDevice = name;
            Chiaki.settings.audioInDevice = name;   // จำไว้ใช้สตรีมหน้า (Settings → Audio อ่านค่าเดียวกัน)
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
                text: qsTr("MICROPHONE")
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
                    readonly property bool current: modelData === content.micDevice
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
