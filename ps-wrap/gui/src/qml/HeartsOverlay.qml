import QtQuick

import org.streetpea.chiaking

// PS-WRAP Link: รีแอคชันลอยแบบ TikTok — เพื่อนกดรัวๆ บนหน้าเว็บ → Chiaki.link.reaction(name, color, kind, count)
// kind = heart ❤️ · flower 🌸 · rock 🤘 · wai 🙏 — ภาพ Twemoji SVG (CC-BY 4.0) ใน res/react/ (ไม่พึ่งฟอนต์ emoji ของเครื่อง)
// วางชิดขอบขวาล่างของภาพ · ไม่รับเมาส์ (ไม่ต้องอยู่ใน overlay hit rects) · ติดไปในภาพที่ส่งให้เพื่อน/คลิปด้วย
//        ♥
//      ♥    ♥     ← ลอยขึ้น ส่ายซ้ายขวา จางหาย
//   ♥ 128          ← ยอดรวมรอบนี้ (หายเองเมื่อเงียบ 4 วิ)
Item {
    id: root

    readonly property var link: Chiaki.link !== undefined ? Chiaki.link : null
    property int maxLive: 48          // ลอยพร้อมกันได้เท่านี้ เกินก็ทิ้ง (กันเฟรมตก)
    property int burstTotal: 0
    property int live: 0

    width: 180
    height: 420

    function spawn(kind, delay) {
        if (live >= maxLive)
            return;
        live++;
        heartComp.createObject(root, {
            kind: kind,
            startDelay: delay,
            drift: (Math.random() * 2 - 1) * 46,
            size: 26 + Math.random() * 16,
            rise: root.height * (0.7 + Math.random() * 0.3),
            duration: 2200 + Math.random() * 900
        });
    }

    Connections {
        target: root.link
        ignoreUnknownSignals: true
        function onReaction(name, color, kind, count) {
            // ชุดเดียวมาหลายดวง → ทยอยปล่อยห่างกันนิดๆ ให้ดูเป็นสาย
            for (let i = 0; i < count; ++i)
                root.spawn(kind, i * 90);
            root.burstTotal += count;
            quiet.restart();
        }
    }
    Timer {
        id: quiet
        interval: 4000
        onTriggered: root.burstTotal = 0
    }

    // ยอดรวม
    Rectangle {
        anchors { right: parent.right; bottom: parent.bottom }
        width: countRow.implicitWidth + 20
        height: 30
        radius: 15
        color: Qt.rgba(8 / 255, 12 / 255, 18 / 255, 0.72)
        border.width: 1
        border.color: Qt.rgba(1, 0.23, 0.42, 0.45)
        opacity: root.burstTotal > 0 ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 300 } }
        Row {
            id: countRow
            anchors.centerIn: parent
            spacing: 6
            Image {
                width: 16
                height: 16
                sourceSize: Qt.size(32, 32)
                source: "qrc:/icons/react/heart.svg"
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: root.burstTotal
                color: "white"
                font.pixelSize: 14
                font.weight: Font.Bold
                font.features: { "tnum": 1 }
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    Component {
        id: heartComp
        Item {
            id: h
            property string kind: "heart"
            property int startDelay: 0
            property real drift: 0
            property real size: 32
            property real rise: 300
            property int duration: 2500
            property real t: 0          // 0 → 1 ตลอดอายุ
            width: size
            height: size
            x: root.width - 60 - size / 2 + Math.sin(t * Math.PI * 2.2) * drift * t
            y: root.height - 40 - size - t * rise
            opacity: t < 0.08 ? t / 0.08 : t > 0.6 ? Math.max(0, 1 - (t - 0.6) / 0.4) : 1
            scale: t < 0.12 ? 0.4 + t / 0.12 * 0.75 : 1.15 - Math.min(0.15, (t - 0.12))
            visible: t > 0

            Image {
                anchors.fill: parent
                sourceSize: Qt.size(64, 64)
                source: "qrc:/icons/react/" + h.kind + ".svg"
            }

            SequentialAnimation {
                running: true
                PauseAnimation { duration: h.startDelay }
                NumberAnimation { target: h; property: "t"; from: 0.001; to: 1; duration: h.duration; easing.type: Easing.OutQuad }
                ScriptAction { script: { root.live--; h.destroy(); } }
            }
        }
    }
}
