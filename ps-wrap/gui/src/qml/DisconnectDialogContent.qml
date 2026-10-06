import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaking

// PS-WRAP: เนื้อหา dialog "Disconnect Session" ใช้ร่วมกันทั้ง Popup (Vulkan) และ Window แยก (OpenGL)
// - ปุ่มหลัก/ค่าเริ่มต้น = Disconnect (คงเครื่องเปิด)  · Sleep เป็นปุ่มรอง — เดิม upstream focus ที่ Sleep ก่อน กดพลาดทีเดียวเครื่องหลับ
// - ธีมเดียวกับแอป (Theme) · นำทาง: ←/→ สลับปุ่ม, Enter เลือก, Esc/◯ ยกเลิก
Rectangle {
    id: card
    property Item defaultButton: disconnectButton

    signal sleepRequested()
    signal disconnectRequested()
    signal cancelRequested()

    implicitWidth: Math.max(520, body.implicitWidth + Theme.space8 * 2)
    implicitHeight: body.implicitHeight + Theme.space8 * 2
    radius: Theme.radiusCard
    color: Theme.surface
    border.width: 1
    border.color: Theme.border

    Rectangle { anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 1; height: 2; radius: 1; color: Theme.accent; opacity: 0.8 }

    component DialogButton: Button {
        id: db
        property bool primary: false
        Layout.preferredWidth: 220
        Layout.preferredHeight: 56
        font.pixelSize: Theme.fontBody
        font.weight: Font.DemiBold
        background: Rectangle {
            radius: Theme.radiusControl
            color: db.primary ? (db.down ? Theme.accentPressed : Theme.accent)
                              : (db.down ? Theme.surfaceHover : db.hovered ? Theme.surfaceRaised : Theme.bg)
            border.width: db.primary ? 0 : 1
            border.color: Theme.border
            Behavior on color { ColorAnimation { duration: Theme.durFast } }
            Rectangle {
                anchors.fill: parent
                anchors.margins: -Theme.focusMargin
                radius: parent.radius + Theme.focusMargin
                color: "transparent"
                border.width: Theme.focusWidth
                border.color: db.primary ? Theme.text : Theme.accent
                visible: db.activeFocus
            }
        }
        contentItem: Label {
            text: db.text
            font: db.font
            color: db.primary ? Theme.accentText : Theme.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        Keys.onReturnPressed: clicked()
        Keys.onEscapePressed: card.cancelRequested()
    }

    ColumnLayout {
        id: body
        anchors.centerIn: parent
        spacing: Theme.space2

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Disconnect Session")
            font.bold: true
            font.pixelSize: Theme.fontTitle
            color: Theme.text
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.space1
            text: qsTr("Keep the console on, or put it to sleep?")
            font.pixelSize: Theme.fontLabel
            color: Theme.textMuted
        }

        RowLayout {
            Layout.topMargin: Theme.space6
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.space4

            DialogButton {
                id: disconnectButton
                primary: true
                text: qsTr("Disconnect")
                KeyNavigation.right: sleepButton
                onClicked: card.disconnectRequested()
            }
            DialogButton {
                id: sleepButton
                text: qsTr("Sleep console")
                KeyNavigation.left: disconnectButton
                onClicked: card.sleepRequested()
            }
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.space3
            text: qsTr("Esc / ◯ cancel")
            font.pixelSize: Theme.fontCaption
            color: Theme.textMuted
            opacity: 0.8
        }
    }
}
