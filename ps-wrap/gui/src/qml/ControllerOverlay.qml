import QtQuick
import QtQuick.Shapes

import org.streetpea.chiaking

// PS-WRAP: overlay จอย DualSense ขณะสตรีม — พอร์ตจาก BudToZaiDualSenseTracker (SVG + ปุ่มกดสว่าง + สติ๊กเลื่อน)
// ภาพแตกเป็นชิ้นที่ viewBox เดียวกัน (1138×765) → วางซ้อนกันขนาดเท่ากันจะตรงตำแหน่งพอดี
// ข้อมูลสดจาก QmlController.buttons/leftX/… (property ที่ PS-WRAP เพิ่มใน C++)
Item {
    id: overlay
    property var controller: Chiaki.controllers.length > 0 ? Chiaki.controllers[0] : null
    property real overlayOpacity: 0.9
    readonly property real aspect: 1138 / 765
    readonly property real unit: width / 1138          // 1 หน่วย viewBox = กี่ px
    readonly property real maxOffset: 15                // จาก JS ของ tracker (หน่วย viewBox)
    // PS-WRAP Link: ปุ่มของเพื่อนที่ถือจอยรวมเข้าด้วย (แบบเดียวกับที่ส่งเข้า PS5: ปุ่ม OR, สติ๊ก/ไกเอาค่าที่มากกว่า)
    readonly property var linkPad: Chiaki.link !== undefined && Chiaki.link && Chiaki.link.pad ? Chiaki.link.pad : null
    readonly property bool linkActive: !!linkPad && !!linkPad.active
    function pick(a, b) { return Math.abs(b) > Math.abs(a) ? b : a }
    readonly property real lx: pick(controller ? controller.leftX : 0, linkActive ? linkPad.lx : 0)
    readonly property real ly: pick(controller ? controller.leftY : 0, linkActive ? linkPad.ly : 0)
    readonly property real rx: pick(controller ? controller.rightX : 0, linkActive ? linkPad.rx : 0)
    readonly property real ry: pick(controller ? controller.rightY : 0, linkActive ? linkPad.ry : 0)
    readonly property real l2v: Math.max(controller ? controller.l2 : 0, linkActive ? linkPad.l2 : 0)
    readonly property real r2v: Math.max(controller ? controller.r2 : 0, linkActive ? linkPad.r2 : 0)
    readonly property int buttons: (controller ? controller.buttons : 0) | (linkActive ? linkPad.buttons : 0)   // QML ไม่มี type uint; mask สูงสุด 1<<17 พอใน int
    // PS-WRAP: สีไฟจอยที่เครื่องสั่ง (เกมเปลี่ยนสีได้ เช่น เลือด/ตำรวจ) · ยังไม่มีสี/ไม่ได้สตรีม = ซ่อน
    // previewColor: หน้า Controllers ทดสอบสีไฟ (ชนะสีจากเครื่อง)
    property color previewColor: "transparent"
    readonly property color lightbarColor: previewColor.a > 0 ? previewColor : (Chiaki.session ? Chiaki.session.lightbarColor : "transparent")
    readonly property bool lightbarOn: lightbarColor.a > 0 && (lightbarColor.r + lightbarColor.g + lightbarColor.b) > 0.3   // PS5 ส่ง 13,13,13 = ไฟดับ

    implicitWidth: 320
    implicitHeight: implicitWidth / aspect
    height: width / aspect
    visible: controller !== null || linkActive
    opacity: overlayOpacity

    // Gamepad API index (ไฟล์ ds-B<n>.svg) → bitmask ของ chiaki (lib/include/chiaki/controller.h)
    readonly property var buttonMap: [
        { n: 0,  mask: 1 << 0 },   // ✕ CROSS
        { n: 1,  mask: 1 << 1 },   // ◯ MOON
        { n: 2,  mask: 1 << 2 },   // □ BOX
        { n: 3,  mask: 1 << 3 },   // △ PYRAMID
        { n: 4,  mask: 1 << 8 },   // L1
        { n: 5,  mask: 1 << 9 },   // R1
        { n: 8,  mask: 1 << 13 },  // Create/Share
        { n: 9,  mask: 1 << 12 },  // Options
        { n: 10, mask: 1 << 10 },  // L3
        { n: 11, mask: 1 << 11 },  // R3
        { n: 12, mask: 1 << 6 },   // ↑
        { n: 13, mask: 1 << 7 },   // ↓
        { n: 14, mask: 1 << 4 },   // ←
        { n: 15, mask: 1 << 5 },   // →
        { n: 16, mask: 1 << 15 },  // PS
        { n: 17, mask: 1 << 14 }   // Touchpad
    ]

    Image {
        id: base
        anchors.fill: parent
        source: "qrc:/icons/overlay/ds-base.svg"
        sourceSize: Qt.size(width, height)
        fillMode: Image.PreserveAspectFit
        smooth: true
    }

    // PS-WRAP: แถบไฟ 2 ข้าง touchpad (ตำแหน่งตาม viewBox ของ ds-B17.svg: ขอบ touchpad x 343..795, y 28..287)
    // ชั้นล่าง = แสงฟุ้งเส้นหนาโปร่ง · ชั้นบน = เส้นไฟ
    Shape {
        id: lightbar
        anchors.fill: parent
        visible: overlay.lightbarOn
        preferredRendererType: Shape.CurveRenderer
        readonly property real u: overlay.unit
        readonly property color glow: Qt.rgba(overlay.lightbarColor.r, overlay.lightbarColor.g, overlay.lightbarColor.b, 0.35)

        ShapePath {
            strokeColor: lightbar.glow
            strokeWidth: 28 * lightbar.u
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: 339 * lightbar.u; startY: 74 * lightbar.u
            PathLine { x: 362 * lightbar.u; y: 236 * lightbar.u }
        }
        ShapePath {
            strokeColor: lightbar.glow
            strokeWidth: 28 * lightbar.u
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: 799 * lightbar.u; startY: 74 * lightbar.u
            PathLine { x: 779 * lightbar.u; y: 230 * lightbar.u }
        }
        ShapePath {
            strokeColor: overlay.lightbarColor
            strokeWidth: 11 * lightbar.u
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: 339 * lightbar.u; startY: 74 * lightbar.u
            PathLine { x: 362 * lightbar.u; y: 236 * lightbar.u }
        }
        ShapePath {
            strokeColor: overlay.lightbarColor
            strokeWidth: 11 * lightbar.u
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: 799 * lightbar.u; startY: 74 * lightbar.u
            PathLine { x: 779 * lightbar.u; y: 230 * lightbar.u }
        }
    }

    // ปุ่มดิจิทัล: โชว์ชิ้นสี pressed ซ้อนทับเมื่อ bit ติด
    Repeater {
        model: overlay.buttonMap
        delegate: Image {
            required property var modelData
            anchors.fill: parent
            source: "qrc:/icons/overlay/ds-B" + modelData.n + ".svg"
            sourceSize: Qt.size(width, height)
            fillMode: Image.PreserveAspectFit
            visible: (overlay.buttons & modelData.mask) !== 0
            smooth: true
        }
    }

    // L2/R2 analog: opacity ตามแรงกด
    Image {
        anchors.fill: parent
        source: "qrc:/icons/overlay/ds-B6.svg"
        sourceSize: Qt.size(width, height)
        fillMode: Image.PreserveAspectFit
        opacity: overlay.l2v
        visible: opacity > 0.02
        smooth: true
    }
    Image {
        anchors.fill: parent
        source: "qrc:/icons/overlay/ds-B7.svg"
        sourceSize: Qt.size(width, height)
        fillMode: Image.PreserveAspectFit
        opacity: overlay.r2v
        visible: opacity > 0.02
        smooth: true
    }

    // สติ๊ก: เลื่อนตามแกน (เหมือน translate ใน tracker)
    Image {
        anchors.fill: parent
        source: "qrc:/icons/overlay/ds-axis-l.svg"
        sourceSize: Qt.size(width, height)
        fillMode: Image.PreserveAspectFit
        smooth: true
        transform: Translate {
            x: overlay.lx * overlay.maxOffset * overlay.unit
            y: overlay.ly * overlay.maxOffset * overlay.unit
        }
    }
    Image {
        anchors.fill: parent
        source: "qrc:/icons/overlay/ds-axis-r.svg"
        sourceSize: Qt.size(width, height)
        fillMode: Image.PreserveAspectFit
        smooth: true
        transform: Translate {
            x: overlay.rx * overlay.maxOffset * overlay.unit
            y: overlay.ry * overlay.maxOffset * overlay.unit
        }
    }
}
