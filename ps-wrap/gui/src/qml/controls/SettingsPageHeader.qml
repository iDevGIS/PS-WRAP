import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaking

// PS-WRAP: หัวของแต่ละหน้าใน Settings — ไอคอนบนแผ่นสี accent + ชื่อหน้า + คำอธิบายสั้น
RowLayout {
    id: header
    property string title
    property string subtitle
    property url icon
    spacing: Theme.space4

    Label { id: probe; visible: false }   // ขนาดฟอนต์ปกติของหน้าต่าง (ย่อตาม uiScale)

    Rectangle {
        Layout.preferredWidth: Math.round(probe.font.pixelSize * 2.3)
        Layout.preferredHeight: Layout.preferredWidth
        Layout.alignment: Qt.AlignVCenter
        radius: Math.round(width * 0.28)
        color: Theme.accent
        Image {
            anchors.centerIn: parent
            width: Math.round(parent.width * 0.55)
            height: width
            sourceSize: Qt.size(width, height)
            source: header.icon
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        Label {
            text: header.title
            color: Theme.text
            font.pixelSize: Math.round(probe.font.pixelSize * 1.3)
            font.weight: Font.DemiBold
        }
        Label {
            visible: text.length > 0
            Layout.fillWidth: true
            Layout.preferredWidth: 0     // ให้ห่อบรรทัดตามความกว้างที่มี ไม่ดันหน้ากว้าง
            text: header.subtitle
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            font.pixelSize: Theme.fontLabel
        }
    }
}
