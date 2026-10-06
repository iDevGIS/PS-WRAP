import QtQuick

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
    readonly property int buttons: controller ? controller.buttons : 0   // QML ไม่มี type uint; mask สูงสุด 1<<17 พอใน int

    implicitWidth: 320
    implicitHeight: implicitWidth / aspect
    height: width / aspect
    visible: controller !== null
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
        opacity: overlay.controller ? overlay.controller.l2 : 0
        visible: opacity > 0.02
        smooth: true
    }
    Image {
        anchors.fill: parent
        source: "qrc:/icons/overlay/ds-B7.svg"
        sourceSize: Qt.size(width, height)
        fillMode: Image.PreserveAspectFit
        opacity: overlay.controller ? overlay.controller.r2 : 0
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
            x: overlay.controller ? overlay.controller.leftX * overlay.maxOffset * overlay.unit : 0
            y: overlay.controller ? overlay.controller.leftY * overlay.maxOffset * overlay.unit : 0
        }
    }
    Image {
        anchors.fill: parent
        source: "qrc:/icons/overlay/ds-axis-r.svg"
        sourceSize: Qt.size(width, height)
        fillMode: Image.PreserveAspectFit
        smooth: true
        transform: Translate {
            x: overlay.controller ? overlay.controller.rightX * overlay.maxOffset * overlay.unit : 0
            y: overlay.controller ? overlay.controller.rightY * overlay.maxOffset * overlay.unit : 0
        }
    }
}
