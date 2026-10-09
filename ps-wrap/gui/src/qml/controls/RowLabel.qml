import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaking

// PS-WRAP: ชื่อ setting (คอลัมน์ซ้ายของ SettingsSection) — กว้าง 13.5 เท่าฟอนต์ = 270 px ที่ uiScale 1.0
Label {
    Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
    Layout.preferredWidth: Math.round(13.5 * font.pixelSize)
    Layout.maximumWidth: Math.round(13.5 * font.pixelSize)
    Layout.topMargin: parent && parent.compactLayout ? Theme.space2 : 0   // จอแคบ: เว้นก่อนชื่อ setting ถัดไป
    wrapMode: Text.WordWrap
    color: Theme.text
}
