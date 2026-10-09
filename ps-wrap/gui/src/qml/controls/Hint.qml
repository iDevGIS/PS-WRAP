import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaking

// PS-WRAP: ค่า default / คำอธิบายสั้นท้ายแถว (คอลัมน์ขวาของ SettingsSection)
Label {
    Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
    Layout.maximumWidth: 300
    wrapMode: Text.WordWrap
    font.pixelSize: Theme.fontCaption
    color: Theme.textMuted
}
