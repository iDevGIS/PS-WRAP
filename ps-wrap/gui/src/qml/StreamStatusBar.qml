import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaking

import "controls" as C

// PS-WRAP: แถบสถานะระหว่างเล่น — ชิปชุดเดียวกับแถบล่างหน้าแรก (ไมค์ ลำโพง กล้อง จอย อัด ภาพหน้าจอ เน็ต)
// โผล่เมื่อเลื่อนเมาส์ไปขอบล่างของหน้าต่าง (Chiaki.window.pointerAtBottom) · ปักหมุด = ค้างไว้ตลอด
// ไม่ใช้ hover ของ QML: เมาส์นอก overlay ไปเกม QML จึงไม่รู้ว่าออกแล้ว (hover ค้าง) — แถบอยู่ในแถบล่างที่ C++ วัดเสมอ
// เมาส์บนแถบไป QML (StreamView ส่งกรอบเป็น overlay hit rect) · ที่อื่นยังเข้าเกมตามปกติ
// จอยอย่างเดียว: ทุกอย่างในแถบนี้มีในเมนูสตรีมอยู่แล้ว (ปุ่ม ≡ ซ้ายสุดเปิดเมนูนั้น)
Item {
    id: bar

    property bool allowed: true          // StreamView: มีสตรีม ไม่ได้โหลด/เออเรอร์ เมนูไม่เปิด
    property bool pinned: false
    signal pinToggled()
    signal menuRequested()
    signal grabRequested()               // เปิดเมนูลำโพง: ขอ input จากเกม (เมาส์/จอยไปที่เมนู)
    signal releaseRequested()

    readonly property bool menuOpen: speakerMenu.visible
    function closeMenu() { speakerMenu.close(); }
    readonly property bool shown: allowed && (pinned || Chiaki.window.pointerAtBottom || menuOpen || hideDelay.running)
    readonly property real loss: (Chiaki.session && isFinite(Chiaki.session.averagePacketLoss)) ? Chiaki.session.averagePacketLoss : 0

    // เมาส์ออกจากแถบ/ขอบล่างแล้วรอแป๊บก่อนซ่อน (กันกระพริบตอนเมาส์แตะขอบเข้าออก)
    Timer { id: hideDelay; interval: 900 }
    onShownChanged: if (!shown) hideDelay.stop()
    Connections {
        target: Chiaki.window
        function onPointerAtBottomChanged() { if (!Chiaki.window.pointerAtBottom && bar.allowed) hideDelay.restart(); }
    }
    onAllowedChanged: if (!allowed) closeMenu()   // dialog จบสตรีม / เมนูสตรีมเปิด → เมนูลำโพงไม่ค้าง

    implicitWidth: card.width
    implicitHeight: card.height
    width: implicitWidth
    height: implicitHeight
    visible: opacity > 0
    opacity: shown ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 160 } }

    Rectangle {
        id: card
        width: row.implicitWidth + 16
        height: 52
        radius: height / 2
        color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.94)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.09)
        transform: Translate { y: bar.shown ? 0 : 12; Behavior on y { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } } }

        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 6

            Chip {
                iconSource: "qrc:/icons/menu/menu.svg"
                tip: qsTr("Stream menu")
                onClicked: bar.menuRequested()
            }

            Rectangle { width: 1; height: 24; color: Theme.border }

            Chip {
                readonly property bool muted: !Chiaki.session || Chiaki.session.muted
                iconSource: "qrc:/icons/menu/mic.svg"
                active: !muted
                showDot: true
                text: muted ? qsTr("Muted") : qsTr("Mic on")
                tip: qsTr("Microphone: click to mute or unmute")
                onClicked: if (Chiaki.session) Chiaki.session.muted = !Chiaki.session.muted
            }

            Chip {
                id: speakerChip
                readonly property int vol: Chiaki.settings.audioVolume
                iconSource: vol > 0 ? "qrc:/icons/menu/volume.svg" : "qrc:/icons/menu/volume-off.svg"
                active: vol > 0
                text: Math.round(vol * 100 / 128) + "%"
                tip: qsTr("Speaker and volume")
                caret: true
                onClicked: {
                    Chiaki.settings.refreshAudioDevices();
                    const current = Chiaki.session ? Chiaki.session.audioOutDevice : Chiaki.settings.audioOutDevice;
                    let items = [{ label: qsTr("Auto (Windows default)"), value: "" }];
                    const devs = Chiaki.settings.availableAudioOutDevices;
                    for (let i = 0; i < devs.length; ++i)
                        items.push({ label: devs[i], value: devs[i] });
                    if (current !== "" && devs.indexOf(current) < 0)
                        items.push({ label: current, value: current });
                    speakerMenu.items = items;
                    speakerMenu.current = current;
                    bar.grabRequested();
                    speakerMenu.openAbove(speakerChip);
                }
            }

            Chip {
                iconSource: "qrc:/icons/menu/cam.svg"
                active: Chiaki.window.camOverlay
                showDot: true
                tip: active ? qsTr("Facecam: on") : qsTr("Facecam: off")
                onClicked: Chiaki.window.camOverlay = !Chiaki.window.camOverlay
            }

            Chip {
                readonly property int count: Chiaki.controllers.length
                iconSource: "qrc:/icons/controller.svg"
                active: count > 0
                showDot: true
                text: count > 1 ? "×" + count : ""
                tip: count > 0 ? qsTr("Controller overlay: click to show or hide") : qsTr("No controller")
                onClicked: Chiaki.window.padOverlay = !Chiaki.window.padOverlay
            }

            Rectangle { width: 1; height: 24; color: Theme.border }

            Chip {
                readonly property QtObject rec: Chiaki.window.recorder
                readonly property bool recording: !!rec && rec.recording
                iconSource: "qrc:/icons/menu/record.svg"
                active: recording
                showDot: recording
                dotColor: Theme.danger
                text: recording ? "%1:%2".arg(Math.floor(rec.seconds / 60)).arg(("0" + rec.seconds % 60).slice(-2)) : ""
                tip: recording ? qsTr("Stop recording") : qsTr("Start recording")
                onClicked: if (!rec || !rec.busy || recording) Chiaki.window.toggleRecording()
            }

            Chip {
                iconSource: "qrc:/icons/menu/screenshot.svg"
                tip: qsTr("Take a screenshot")
                onClicked: Chiaki.window.takeScreenshot()
            }

            // Go Live: กด = เริ่ม/หยุดไลฟ์ไปทุกปลายทางที่เปิดไว้ (Settings › Go Live) · ระหว่างไลฟ์แสดงเวลา
            Chip {
                readonly property var gl: Chiaki.goLive !== undefined ? Chiaki.goLive : null
                readonly property bool live: !!gl && gl.live
                iconSource: "qrc:/icons/menu/live.svg"
                active: live
                showDot: true
                dotColor: !live ? Theme.textMuted : gl.state === "live" ? Theme.danger : gl.state === "error" ? Theme.danger : Theme.warning
                text: live ? "%1:%2".arg(Math.floor(gl.seconds / 60)).arg(("0" + gl.seconds % 60).slice(-2)) : qsTr("Live")
                opacity: !!gl && (live || (!!Chiaki.session && Chiaki.session.connected)) ? 1.0 : 0.5
                onClicked: if (gl && (live || (Chiaki.session && Chiaki.session.connected))) gl.toggle()
            }

            Chip {
                iconSource: "qrc:/icons/menu/stats.svg"
                active: true
                showDot: true
                dotColor: bar.loss > 0.05 ? Theme.danger : (bar.loss > 0.01 ? Theme.warning : Theme.success)
                text: Chiaki.session ? qsTr("%1 Mbps").arg(Chiaki.session.measuredBitrate.toFixed(1)) : ""
                tip: qsTr("Network: %1 ms · loss %2% — click for stream stats")
                         .arg(Chiaki.session ? Chiaki.session.rttMs.toFixed(0) : "–")
                         .arg((bar.loss * 100).toFixed(1))
                onClicked: Chiaki.settings.showStreamStats = !Chiaki.settings.showStreamStats
            }

            Rectangle { width: 1; height: 24; color: Theme.border }

            Chip {
                iconSource: bar.pinned ? "qrc:/icons/menu/pin-accent.svg" : "qrc:/icons/menu/pin.svg"
                tip: bar.pinned ? qsTr("Unpin: show only when the mouse is at the bottom") : qsTr("Pin: keep this bar on screen")
                onClicked: bar.pinToggled()
            }
        }
    }

    C.DeviceMenu {
        id: speakerMenu
        title: qsTr("SPEAKER")
        showVolume: true
        volume: Chiaki.settings.audioVolume
        onVolumeMoved: (value) => Chiaki.settings.audioVolume = value
        onPicked: (value) => {
            if (Chiaki.session)
                Chiaki.session.audioOutDevice = value;   // สลับลำโพงทันที
            Chiaki.settings.audioOutDevice = value;
            close();
        }
        onClosed: bar.releaseRequested()
    }

    // ชิปหน้าตาเดียวกับ BarChip ในหน้าแรก (เล็กลงนิด ใช้ซ้อนบนเกม)
    component Chip: Rectangle {
        id: chip
        property string iconSource
        property string text
        property string tip
        property bool active: true
        property bool showDot: false
        property bool caret: false
        property color dotColor: Theme.success
        signal clicked()
        radius: height / 2
        color: chipMouse.containsMouse ? Theme.surfaceRaised : Qt.rgba(1, 1, 1, 0.04)
        border.width: 1
        border.color: chipMouse.containsMouse ? Theme.accent : Theme.border
        implicitHeight: 36
        implicitWidth: chipRow.implicitWidth + 24
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
        RowLayout {
            id: chipRow
            anchors.centerIn: parent
            spacing: 6
            Image {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                sourceSize: Qt.size(20, 20)
                fillMode: Image.PreserveAspectFit
                source: chip.iconSource
                opacity: chip.active ? 1.0 : 0.45
            }
            Rectangle {
                visible: chip.showDot
                width: 8; height: 8; radius: 4
                color: chip.active ? chip.dotColor : Theme.textMuted
            }
            Label {
                visible: chip.text !== ""
                text: chip.text
                font.pixelSize: Theme.fontCaption
                font.weight: Font.DemiBold
                font.features: { "tnum": 1 }
                color: chip.active ? Theme.text : Theme.textMuted
            }
            Label {
                visible: chip.caret
                text: "▾"
                font.pixelSize: Theme.fontCaption
                color: Theme.textMuted
            }
        }
        MouseArea {
            id: chipMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.clicked()
        }
    }
}
