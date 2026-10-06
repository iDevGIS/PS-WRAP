import QtQuick

import org.streetpea.chiaking

// PS-WRAP: overlay นาฬิกา + เวลาเล่น ขณะสตรีม — การ์ดกระจกแบบเดียวกับ MicSpectrumOverlay
//  [ 21:47 ] | ● PLAYTIME
//            |   1:23:45
// วาดที่ขนาดออกแบบ 248×72 เสมอ — StreamView ย่อขยายด้วย scale (สัดส่วนคงที่ คมทุกขนาด)
// ถูกบันทึกลงคลิปด้วย (ทุกอย่างใน QML scene) → ตัวเลข tabular (ไม่กระตุกตอนเลขเปลี่ยน) + คอนทราสต์สูงบนภาพเกมทุกแบบ
Item {
    id: root

    // true เฉพาะตอน overlay โผล่จริง → timer เดินเฉพาะตอนนั้น
    property bool active: false
    property real overlayOpacity: 0.92
    // เวลาเริ่มเล่น (ms epoch) — StreamView ตั้งตอน session connected · 0 = ยังไม่เชื่อมต่อ (แสดง --:--)
    property double startMs: 0

    property date now: new Date()
    readonly property int elapsedSec: startMs > 0 ? Math.max(0, Math.floor((now.getTime() - startMs) / 1000)) : -1

    readonly property color ice: "#7fe8ff"

    component ClockDigits: Text {
        font.pixelSize: 32
        font.weight: Font.DemiBold
        font.features: { "tnum": 1 }
        color: Theme.text
        style: Text.Raised
        styleColor: Qt.rgba(0, 0, 0, 0.35)
    }

    implicitWidth: 248
    implicitHeight: 72
    width: implicitWidth
    height: implicitHeight
    opacity: overlayOpacity

    function formatElapsed(sec) {
        if (sec < 0)
            return "--:--";
        const pad = (n) => (n < 10 ? "0" : "") + n;
        const h = Math.floor(sec / 3600), m = Math.floor(sec / 60) % 60, s = sec % 60;
        return h > 0 ? h + ":" + pad(m) + ":" + pad(s) : pad(m) + ":" + pad(s);
    }

    Timer {
        // ตั้งให้ตรงขอบวินาที (ไม่ drift ห่างจากนาฬิการะบบ)
        interval: 1000 - (new Date().getMilliseconds())
        running: root.active
        repeat: true
        triggeredOnStart: true
        onTriggered: {
            root.now = new Date();
            interval = Math.max(50, 1000 - root.now.getMilliseconds());
        }
    }

    // ---------- การ์ดกระจก ----------
    Rectangle {
        id: card
        anchors.fill: parent
        radius: 16
        color: Qt.rgba(8 / 255, 12 / 255, 18 / 255, 0.66)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.09)
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 }
            height: parent.height * 0.5
            radius: parent.radius - 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.07) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
            }
        }
        // เส้นเรืองแสงด้านล่าง (accent จางๆ)
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: parent.radius; rightMargin: parent.radius }
            height: 1
            color: Theme.accent
            opacity: 0.45
        }
    }

    // ---------- เวลาท้องถิ่น (ซ้าย) — โคลอนกะพริบช้าๆ ตามวินาที ----------
    Row {
        id: clockRow
        anchors { left: parent.left; leftMargin: 18; verticalCenter: parent.verticalCenter }
        spacing: 0
        ClockDigits { text: Qt.formatTime(root.now, "HH") }
        ClockDigits {
            text: ":"
            opacity: root.now.getSeconds() % 2 === 0 ? 1.0 : 0.35
            Behavior on opacity { NumberAnimation { duration: 300 } }
        }
        ClockDigits { text: Qt.formatTime(root.now, "mm") }
    }

    // ---------- เส้นคั่น ----------
    Rectangle {
        id: divider
        anchors { left: clockRow.right; leftMargin: 16; verticalCenter: parent.verticalCenter }
        width: 1
        height: parent.height - 30
        color: Qt.rgba(1, 1, 1, 0.14)
    }

    // ---------- เวลาเล่น (ขวา) ----------
    Column {
        anchors { left: divider.right; leftMargin: 16; right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
        spacing: 1
        Row {
            spacing: 6
            Rectangle {
                id: playDot
                anchors.verticalCenter: parent.verticalCenter
                width: 6
                height: 6
                radius: 3
                color: root.startMs > 0 ? Theme.success : Theme.textMuted
            }
            Text {
                text: qsTr("PLAYTIME")
                font.pixelSize: 10
                font.weight: Font.Bold
                font.letterSpacing: 1.6
                color: Theme.textMuted
            }
        }
        Text {
            text: root.formatElapsed(root.elapsedSec)
            font.pixelSize: 22
            font.weight: Font.DemiBold
            font.features: { "tnum": 1 }
            color: root.ice
            style: Text.Raised
            styleColor: Qt.rgba(0, 0, 0, 0.35)
        }
    }
}
