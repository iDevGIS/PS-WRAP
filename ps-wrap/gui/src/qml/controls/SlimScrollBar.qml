import QtQuick
import QtQuick.Controls

import org.streetpea.chiaking

// PS-WRAP: scrollbar แบบบาง — ปกติ 4 px สีจาง, ชี้/ลาก = 8 px · กำลังลากเป็นสี accent · ไม่มีราง
// ใช้: ScrollBar.vertical: C.SlimScrollBar { }  (policy/visible ตั้งทับได้ตามปกติ)
ScrollBar {
    id: bar
    readonly property bool wide: hovered || pressed
    policy: ScrollBar.AsNeeded
    padding: 2
    minimumSize: 0.08
    implicitWidth: (wide ? 8 : 4) + leftPadding + rightPadding
    implicitHeight: (wide ? 8 : 4) + topPadding + bottomPadding
    Behavior on implicitWidth { NumberAnimation { duration: Theme.durFast } }
    Behavior on implicitHeight { NumberAnimation { duration: Theme.durFast } }

    background: Item { }

    contentItem: Rectangle {
        implicitWidth: 4
        implicitHeight: 4
        radius: Math.min(width, height) / 2
        color: bar.pressed ? Theme.accent : (bar.hovered ? Theme.textMuted : Theme.border)
        opacity: bar.policy === ScrollBar.AlwaysOn || bar.active || bar.hovered ? 1.0 : 0.0
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
        Behavior on opacity { NumberAnimation { duration: 300 } }
    }
}
