import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Shapes

import org.streetpea.chiaking

// PS-WRAP: mic spectrum overlay ขณะสตรีม — visualizer เสียงไมค์แบบ "glass HUD"
//  [mic badge + LIVE] | แท่ง spectrum สะท้อนบน/ล่าง (32 band, log 60 Hz–12 kHz) + เส้น waveform เรืองแสงกลางการ์ด | peak meter แนวตั้ง
// ข้อมูลจาก Chiaki.window.micMeter (C++ ทำ FFT + smoothing แล้ว, ส่ง updated() ~60 Hz)
// วาดที่ขนาดออกแบบ 320×110 เสมอ — StreamView ย่อขยายด้วย scale (เหมือน StatsOverlay) จึงคมทุกขนาดและสัดส่วนคงที่
// perf: Repeater ของ Rectangle (GPU ล้วน ไม่มี Canvas) · อ่าน list จาก C++ ครั้งเดียวต่อเฟรมใน onUpdated · glow = MultiEffect blur ชั้นเดียว
// ถูกบันทึกลงคลิปด้วย (ทุกอย่างใน QML scene) → สีต้องอ่านง่ายบนภาพเกมทั้งสว่าง/มืด
Item {
    id: root

    // true เฉพาะตอน overlay โผล่จริง → เปิด FFT ฝั่ง C++ (micMeter.enabled) · false = ไม่กิน CPU
    property bool active: false
    property real overlayOpacity: 0.92
    // ข้อความตอน idle ("" = อัตโนมัติ Mic muted / Mic off) — MicPreviewDialog ใช้ "Listening…"
    property string idleText: ""

    readonly property QtObject meter: Chiaki.window ? Chiaki.window.micMeter : null
    readonly property bool live: !!meter && meter.live
    property bool muted: !!Chiaki.session && Chiaki.session.muted   // MicPreviewDialog override ได้
    // คลิกวงไมค์ = ขอสลับ mute · muteClickable=false เมื่อมี MouseArea อื่นทับ (StreamView ใช้ hitBadge() เอง)
    property bool muteClickable: false
    signal toggleMuteRequested()
    function hitBadge(px, py) {   // พิกัดใน root (ก่อน scale)
        const p = micDisc.mapFromItem(root, px, py);
        return p.x >= -10 && p.y >= -10 && p.x <= micDisc.width + 10 && p.y <= micDisc.height + 10;
    }
    property bool badgeHover: false
    readonly property int bandCount: 32
    readonly property int waveCount: 64

    // buffer ต่อเฟรม (คัดลอกจาก meter ครั้งเดียวใน onUpdated — delegate อ่านจาก JS array นี้ ไม่แปลง QVariantList ซ้ำ 32 รอบ)
    property var bands: []
    property var wavePoints: []
    property real level: 0
    property real peak: 0

    // 0 = idle (หายใจช้าๆ) · 1 = live — crossfade ระหว่างสองสถานะ
    property real liveMix: live ? 1 : 0
    Behavior on liveMix { NumberAnimation { duration: 450; easing.type: Easing.InOutQuad } }
    // เฟสของ shimmer ตอน idle (วิ่งเฉพาะตอนไม่ live และ overlay โผล่)
    property real phase: 0
    NumberAnimation on phase {
        from: 0
        to: Math.PI * 2
        duration: 3200
        loops: Animation.Infinite
        running: root.active && root.liveMix < 1
    }

    readonly property color cyan: Theme.accent          // #00a7ff
    readonly property color ice: "#7fe8ff"
    readonly property color violet: "#8b5cff"
    readonly property color magenta: "#ff3dcf"

    implicitWidth: 320
    implicitHeight: 110
    width: implicitWidth
    height: implicitHeight
    opacity: overlayOpacity

    function lerpColor(a, b, t) {
        return Qt.rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, 1);
    }
    // สีปลายแท่งตามความดัง: cyan → violet → magenta
    function tipColor(v) {
        return v < 0.5 ? lerpColor(cyan, violet, v * 2) : lerpColor(violet, magenta, (v - 0.5) * 2);
    }

    function syncMeter() {
        if (meter)
            meter.enabled = root.active;
        if (!root.active) {
            bands = [];
            wavePoints = [];
            level = 0;
            peak = 0;
        }
    }
    onActiveChanged: syncMeter()
    onMeterChanged: syncMeter()
    Component.onCompleted: syncMeter()
    Component.onDestruction: if (meter) meter.enabled = false

    function pull() {
        bands = meter.bands;
        level = meter.level;
        peak = meter.peak;
        const w = meter.wave;
        const n = w.length;
        if (n < 2) {
            wavePoints = [];
            return;
        }
        const W = spectrum.width, mid = spectrum.height / 2, amp = spectrum.height * 0.36;
        let pts = new Array(n);
        for (let i = 0; i < n; ++i)
            pts[i] = Qt.point(i * W / (n - 1), mid - Math.max(-1, Math.min(1, w[i])) * amp);
        wavePoints = pts;
    }
    Connections {
        target: root.meter
        enabled: root.active
        function onUpdated() { root.pull() }
    }

    // ---------- การ์ดกระจก ----------
    Rectangle {
        id: card
        anchors.fill: parent
        radius: 16
        color: Qt.rgba(8 / 255, 12 / 255, 18 / 255, 0.66)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.09)
        // แสงสะท้อนด้านบน (glass sheen)
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 }
            height: parent.height * 0.5
            radius: parent.radius - 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.07) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
            }
        }
        // เส้นขอบเรืองแสงด้านล่าง ตามระดับเสียง
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: parent.radius; rightMargin: parent.radius }
            height: 1
            color: root.cyan
            opacity: 0.15 + 0.6 * root.level * root.liveMix
        }
    }

    // ---------- mic badge (ซ้าย) ----------
    Item {
        id: badge
        x: 12
        width: 40
        anchors { top: parent.top; bottom: parent.bottom }

        // วงแหวนเต้นตามระดับเสียง
        Rectangle {
            id: pulseRing
            readonly property real s: 1 + 0.35 * root.level * root.liveMix
            anchors.centerIn: micDisc
            width: micDisc.width * s
            height: width
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: root.live ? root.cyan : Theme.textMuted
            opacity: root.live ? 0.25 + 0.5 * root.level : 0.15
        }
        Rectangle {
            id: micDisc
            anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 22 }
            width: 34
            height: 34
            radius: 17
            color: root.badgeHover ? Qt.rgba(0, 0.655, 1, 0.32) : (root.live ? Qt.rgba(0, 0.655, 1, 0.18) : Qt.rgba(1, 1, 1, 0.05))
            border.width: root.badgeHover ? 2 : 1
            border.color: root.live || root.badgeHover ? root.cyan : Theme.border
            MouseArea {
                anchors.fill: parent
                anchors.margins: -10
                enabled: root.muteClickable
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onContainsMouseChanged: root.badgeHover = containsMouse
                onClicked: root.toggleMuteRequested()
            }
            Behavior on color { ColorAnimation { duration: 300 } }
            Image {
                anchors.centerIn: parent
                width: 18
                height: 18
                sourceSize: Qt.size(36, 36)
                source: "qrc:/icons/menu/mic.svg"
                opacity: root.live ? 1.0 : 0.45
            }
            // ขีดทแยง = ปิดไมค์
            Rectangle {
                anchors.centerIn: parent
                width: 26
                height: 2
                radius: 1
                rotation: -45
                color: Theme.danger
                visible: !root.live
                antialiasing: true
            }
        }
        Label {
            anchors { horizontalCenter: micDisc.horizontalCenter; top: micDisc.bottom; topMargin: 9 }
            text: root.live ? qsTr("LIVE") : (root.muted ? qsTr("MUTED") : qsTr("OFF"))
            font.pixelSize: 9
            font.weight: Font.Bold
            font.letterSpacing: 1.6
            color: root.live ? root.ice : Theme.textMuted
        }
        // จุด live กระพริบเบาๆ
        Rectangle {
            anchors { right: micDisc.right; top: micDisc.top; rightMargin: -2; topMargin: -2 }
            width: 8
            height: 8
            radius: 4
            color: Theme.success
            border.width: 1
            border.color: Qt.rgba(0, 0, 0, 0.6)
            visible: root.live
            SequentialAnimation on opacity {
                running: root.active && root.live
                loops: Animation.Infinite
                NumberAnimation { from: 1; to: 0.35; duration: 900; easing.type: Easing.InOutSine }
                NumberAnimation { from: 0.35; to: 1; duration: 900; easing.type: Easing.InOutSine }
            }
        }
    }

    // ---------- spectrum + waveform ----------
    Item {
        id: spectrum
        anchors { left: badge.right; right: meterCol.left; top: parent.top; bottom: parent.bottom; leftMargin: 10; rightMargin: 10; topMargin: 12; bottomMargin: 12 }

        // เส้นกึ่งกลาง
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 1
            color: root.cyan
            opacity: 0.18
        }

        Item {
            id: viz
            anchors.fill: parent

            Repeater {
                model: root.bandCount
                delegate: Item {
                    id: bar
                    required property int index
                    readonly property real slot: spectrum.width / root.bandCount
                    readonly property real liveV: Math.max(0, Math.min(1, root.bands.length > index ? root.bands[index] : 0))
                    // idle: คลื่นหายใจช้าๆ วิ่งจากซ้ายไปขวา
                    readonly property real idleV: 0.05 + 0.035 * (1 + Math.sin(root.phase * 2 - index * 0.38))
                    readonly property real v: root.liveMix * liveV + (1 - root.liveMix) * idleV
                    // peak cap: ค้างไว้แล้วค่อยๆ ตก
                    property real cap: 0
                    onVChanged: cap = Math.max(v, cap - 0.012)

                    x: index * slot
                    width: slot
                    height: spectrum.height

                    Rectangle {
                        readonly property real h: Math.max(3, bar.v * spectrum.height)
                        anchors.centerIn: parent
                        width: Math.max(2, bar.slot * 0.58)
                        height: h
                        radius: width / 2
                        antialiasing: true
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: root.tipColor(bar.v) }
                            GradientStop { position: 0.5; color: root.liveMix > 0.5 ? root.ice : root.cyan }
                            GradientStop { position: 1.0; color: root.tipColor(bar.v) }
                        }
                        opacity: 0.55 + 0.45 * root.liveMix
                    }
                    // caps บน/ล่าง
                    Rectangle {
                        visible: root.liveMix > 0.5 && bar.cap > 0.08
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: spectrum.height / 2 - bar.cap * spectrum.height / 2 - 4
                        width: Math.max(2, bar.slot * 0.58)
                        height: 2
                        radius: 1
                        color: "white"
                        opacity: 0.75
                    }
                    Rectangle {
                        visible: root.liveMix > 0.5 && bar.cap > 0.08
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: spectrum.height / 2 + bar.cap * spectrum.height / 2 + 2
                        width: Math.max(2, bar.slot * 0.58)
                        height: 2
                        radius: 1
                        color: "white"
                        opacity: 0.35
                    }
                }
            }

            // waveform เส้นบางเรืองแสง
            Shape {
                anchors.fill: parent
                visible: root.liveMix > 0.01 && root.wavePoints.length > 1
                opacity: root.liveMix
                asynchronous: true
                ShapePath {
                    strokeColor: Qt.rgba(1, 1, 1, 0.92)
                    strokeWidth: 1.4
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    PathPolyline { path: root.wavePoints }
                }
            }
        }

        // bloom: blur ของชั้น viz ซ้อนข้างหลัง (MultiEffect ชั้นเดียว)
        MultiEffect {
            source: viz
            anchors.fill: viz
            z: -1
            blurEnabled: true
            blurMax: 24
            blur: 1.0
            brightness: 0.15
            saturation: 0.3
            opacity: 0.35 + 0.5 * root.level * root.liveMix
        }

        // ป้ายตอน idle
        Rectangle {
            anchors.centerIn: parent
            visible: root.liveMix < 0.99
            opacity: 1 - root.liveMix
            radius: height / 2
            implicitHeight: 22
            implicitWidth: idleLabel.implicitWidth + 22
            color: Qt.rgba(8 / 255, 12 / 255, 18 / 255, 0.78)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.1)
            Label {
                id: idleLabel
                anchors.centerIn: parent
                text: root.idleText.length ? root.idleText : (root.muted ? qsTr("Mic muted") : qsTr("Mic off"))
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 0.6
                color: Theme.textMuted
            }
        }
    }

    // ---------- peak meter (ขวา) ----------
    Item {
        id: meterCol
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom; rightMargin: 14; topMargin: 14; bottomMargin: 14 }
        width: 6
        Rectangle {
            anchors.fill: parent
            radius: 3
            color: Qt.rgba(1, 1, 1, 0.07)
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: parent.height * Math.max(0, Math.min(1, root.level)) * root.liveMix
            radius: 3
            gradient: Gradient {
                GradientStop { position: 0.0; color: root.magenta }
                GradientStop { position: 0.5; color: root.violet }
                GradientStop { position: 1.0; color: root.cyan }
            }
        }
        // peak tick (ค่อยๆ ตก) — แดงเมื่อเกือบ clip
        Rectangle {
            readonly property real p: Math.max(0, Math.min(1, root.peak)) * root.liveMix
            visible: p > 0.02
            x: -2
            width: parent.width + 4
            height: 2
            radius: 1
            y: parent.height - p * parent.height - 1
            color: p > 0.92 ? Theme.danger : "white"
        }
    }
}
