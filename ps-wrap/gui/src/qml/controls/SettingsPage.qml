import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaking

// PS-WRAP: โครงหน้าใน Settings — หัวหน้าอยู่กับที่ + การ์ด SettingsSection เรียงลงใน Flickable
// ลูกที่ประกาศใน SettingsPage { ... } ไปอยู่ใน ColumnLayout ของการ์ด · flick ใช้กับ ensureItemVisibleInFlick ของ SettingsDialog
Item {
    id: page
    property alias title: header.title
    property alias subtitle: header.subtitle
    property alias icon: header.icon
    readonly property alias flick: flick
    default property alias content: body.data
    // PS-WRAP: กว้างไม่พอวาง ชื่อ | control | default (≈33.5 เท่าฟอนต์ + ขอบ/hint) → การ์ดซ้อนแนวตั้ง
    readonly property bool settingsPageCompact: width < Math.round(33.5 * probe.font.pixelSize) + 402

    Label { id: probe; visible: false }

    SettingsPageHeader {
        id: header
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            topMargin: Theme.space6
            leftMargin: Theme.space8
            rightMargin: Theme.space8
        }
    }

    Flickable {
        id: flick
        anchors {
            top: header.bottom
            topMargin: Theme.space4
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        clip: true
        contentWidth: body.x + body.width + Theme.space6
        contentHeight: body.y + body.implicitHeight + Theme.space8
        flickableDirection: Flickable.AutoFlickIfNeeded
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: SlimScrollBar {
            policy: ScrollBar.AlwaysOn
            visible: flick.contentHeight > flick.height
        }

        ColumnLayout {
            id: body
            x: Theme.space8
            y: Theme.space2              // เผื่อวงแหวน focus ของ control แถวบนสุด
            width: Math.max(implicitWidth, flick.width - Theme.space8 - Theme.space6)
            spacing: Theme.space4
        }
    }
}
