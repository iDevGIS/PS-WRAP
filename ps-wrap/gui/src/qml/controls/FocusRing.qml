import QtQuick

import org.streetpea.chiaking

// PS-WRAP: วงแหวน focus สำหรับคีย์/จอย ใส่เป็นลูกของ control ใดก็ได้: FocusRing { target: control }
Rectangle {
    required property Item target
    anchors.fill: parent
    anchors.margins: -Theme.focusMargin
    radius: Theme.radiusControl + Theme.focusMargin
    color: "transparent"
    border.width: Theme.focusWidth
    border.color: Theme.accent
    // TextField/TextArea ไม่ใช่ Control → ไม่มี visualFocus (เคยได้ undefined → warning + ring ไม่ขึ้น) ใช้ activeFocus แทน
    visible: !target ? false : (target.visualFocus !== undefined ? target.visualFocus : target.activeFocus)
    z: 10
}
