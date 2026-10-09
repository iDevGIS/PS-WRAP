import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

DialogView {
    id: pinDialog
    property var consoleIndex
    title: qsTr("Set console pin")
    buttonText: qsTr("Set")
    buttonEnabled: pin.acceptableInput
    onAccepted: {
        Chiaki.setConsolePin(consoleIndex, pin.text.trim());
        stack.pop();
    }
    Item {
        GridLayout {
            anchors {
                top: parent.top
                horizontalCenter: parent.horizontalCenter
                topMargin: 20
            }
            columns: 2
            rowSpacing: 10
            columnSpacing: 20
                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Remote Play PIN (4 digits):")
                        }

                        // PS-WRAP: ใส่ PIN ด้วยจอยได้ (↑↓ ←→ ✕ ○) — เดิม TextField พิมพ์ได้แค่คีย์บอร์ด
                        C.PinPad {
                            id: pin
                            focus: true
                            hidden: Chiaki.settings.streamerMode
                            onAccepted: pinDialog.accepted()
                            onCanceled: pinDialog.close()
                        }
        }

    }
}