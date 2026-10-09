import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaking

// PS-WRAP: การ์ดหมวดย่อยในหน้า Settings — หัว (ไอคอน + ชื่อ + คำอธิบาย) + GridLayout 3 คอลัมน์
// (ชื่อ setting | control | ค่า default) · ลูกที่ประกาศใน SettingsSection { ... } ไปอยู่ใน grid
Rectangle {
    id: section
    property string title
    property string description
    property url icon
    property bool stretch: false             // true = grid กว้างเต็มการ์ด (เช่นรายชื่อเครื่อง)
    property int columns: 3                  // คอลัมน์ตอนกว้างพอ
    property int compactColumns: 1           // คอลัมน์ตอนจอแคบ (1 = ชื่ออยู่บน control)
    default property alias content: grid.data
    readonly property bool settingsSection: true
    // PS-WRAP: จอแคบ → ซ้อนแนวตั้ง · อ่านจาก C.SettingsPage ที่ครอบอยู่ (settingsPageCompact)
    readonly property bool compact: {
        let p = parent;
        while (p) {
            if (p.settingsPageCompact !== undefined)
                return p.settingsPageCompact;
            p = p.parent;
        }
        return false;
    }
    readonly property int pad: 20

    Layout.fillWidth: true
    implicitWidth: inner.implicitWidth + 2 * pad
    implicitHeight: inner.implicitHeight + 2 * pad
    radius: Theme.radiusCard
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    ColumnLayout {
        id: inner
        x: section.pad
        y: section.pad
        width: section.width - 2 * section.pad
        spacing: Theme.space3

        RowLayout {
            visible: section.title.length > 0
            Layout.fillWidth: true
            spacing: Theme.space3

            Rectangle {
                visible: section.icon != ""
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                Layout.alignment: Qt.AlignTop
                radius: 10
                color: Qt.rgba(0, 0.655, 1, 0.16)    // Theme.accent จางๆ
                Image {
                    anchors.centerIn: parent
                    width: 18
                    height: 18
                    sourceSize: Qt.size(18, 18)
                    source: section.icon
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: section.title
                    color: Theme.text
                    font.weight: Font.DemiBold
                }
                Label {
                    visible: text.length > 0
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0     // ห่อบรรทัดตามการ์ด ไม่ดันการ์ดให้กว้าง
                    text: section.description
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontCaption
                }
            }
        }

        Rectangle {
            visible: section.title.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        GridLayout {
            id: grid
            readonly property bool compactLayout: section.compact   // RowLabel อ่านเพื่อเว้นบน
            Layout.fillWidth: section.stretch
            columns: section.compact ? section.compactColumns : section.columns
            rowSpacing: section.compact ? Theme.space2 : 10
            columnSpacing: 24
        }
    }
}
