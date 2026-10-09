import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaking

// PS-WRAP: ช่องใส่ PIN 4 หลักแบบหน้าใส่รหัสของ PS5 — ปุ่มจอยแต่ละปุ่ม = ตัวเลขหนึ่งตัว กดครั้งเดียวได้เลย
//   ←1 ↑2 →3 ↓4 · R1 5 · R2 6 · L1 7 · L2 8 · △ 9 · □ 0   (โหมด UI: R1=PageDown L1=PageUp △=Key_Yes □=Key_No
//   L2/R2 = F13/F14 จาก qmlcontroller.cpp) · ครบ 4 หลักแล้วยืนยันเอง · ○ ลบตัวล่าสุด (ไม่มีให้ลบ = ยกเลิก) · ✕ ยืนยัน
// คีย์บอร์ด: 0-9 / Backspace / Enter · เมาส์: คลิกแป้นตัวเลข
// API: text, acceptableInput, clear(), accepted(), canceled()
FocusScope {
    id: pad
    property bool hidden: false                    // streamer mode — แสดง • แทนตัวเลข
    property bool autoAccept: true                 // ครบ 4 หลักแล้วยืนยันเอง (แบบ PS5)
    property string entered: ""
    readonly property bool acceptableInput: entered.length === 4
    readonly property string text: acceptableInput ? entered : ""
    signal accepted()
    signal canceled()

    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight
    activeFocusOnTab: true

    // ตัวเลข → ปุ่มจอยที่กด (ลำดับแป้นแบบ PS5)
    readonly property var keypad: [
        { digit: 1, glyph: "◀" }, { digit: 2, glyph: "▲" }, { digit: 3, glyph: "▶" },
        { digit: 4, glyph: "▼" }, { digit: 5, glyph: "R1" }, { digit: 6, glyph: "R2" },
        { digit: 7, glyph: "L1" }, { digit: 8, glyph: "L2" }, { digit: 9, glyph: "△" },
        { digit: 0, glyph: "□" }
    ]

    function clear() {
        entered = "";
        acceptTimer.stop();
    }
    function typeDigit(d) {
        if (entered.length >= 4)
            return;
        entered += String(d);
        if (acceptableInput && autoAccept)
            acceptTimer.restart();
    }
    function backspace() {
        acceptTimer.stop();
        if (entered.length)
            entered = entered.slice(0, -1);
    }

    Timer {
        id: acceptTimer
        interval: 350          // ให้เห็นหลักที่ 4 ก่อนยืนยัน
        onTriggered: if (pad.acceptableInput) pad.accepted()
    }

    Keys.onPressed: (event) => {
        if (event.key >= Qt.Key_0 && event.key <= Qt.Key_9 && !(event.modifiers & ~Qt.KeypadModifier)) {
            typeDigit(event.key - Qt.Key_0);
            event.accepted = true;
            return;
        }
        switch (event.key) {
        case Qt.Key_Left: typeDigit(1); break;
        case Qt.Key_Up: typeDigit(2); break;
        case Qt.Key_Right: typeDigit(3); break;
        case Qt.Key_Down: typeDigit(4); break;
        case Qt.Key_PageDown: typeDigit(5); break;     // R1
        case Qt.Key_F14: typeDigit(6); break;          // R2
        case Qt.Key_PageUp: typeDigit(7); break;       // L1
        case Qt.Key_F13: typeDigit(8); break;          // L2
        case Qt.Key_Yes: typeDigit(9); break;          // △
        case Qt.Key_No: typeDigit(0); break;           // □
        case Qt.Key_Backspace:
        case Qt.Key_Delete:
            backspace();
            break;
        case Qt.Key_Escape:                            // ○
            if (entered.length)
                backspace();
            else
                canceled();
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (acceptableInput) {
                acceptTimer.stop();
                accepted();
            }
            break;
        default:
            return;
        }
        event.accepted = true;
    }

    ColumnLayout {
        id: column
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Theme.space4

        // ช่องแสดงตัวเลข 4 ช่อง — ช่องที่จะพิมพ์ถัดไปมีขอบ accent
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: Theme.space2
            Repeater {
                model: 4
                delegate: Rectangle {
                    required property int index
                    readonly property bool filled: index < pad.entered.length
                    readonly property bool next: index === pad.entered.length
                    Layout.preferredWidth: 56
                    Layout.preferredHeight: 56
                    radius: Theme.radiusControl
                    color: next && pad.activeFocus ? Theme.surfaceHover : Theme.surfaceRaised
                    border.width: next ? 2 : 1
                    border.color: next && pad.activeFocus ? Theme.accent : Theme.border
                    Behavior on border.color { ColorAnimation { duration: Theme.durFast } }
                    Label {
                        anchors.centerIn: parent
                        text: parent.filled ? (pad.hidden ? "•" : pad.entered.charAt(parent.index)) : ""
                        font.pixelSize: 28
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }
                }
            }
        }

        // แป้นตัวเลข (คลิกได้) — บอกว่าตัวเลขไหนคือปุ่มไหนบนจอย
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: keyGrid.implicitWidth + 2 * Theme.space3
            implicitHeight: keyGrid.implicitHeight + 2 * Theme.space3
            radius: Theme.radiusCard
            color: Theme.surface
            border.width: 1
            border.color: Theme.border

            GridLayout {
                id: keyGrid
                anchors.centerIn: parent
                columns: 3
                rowSpacing: 2
                columnSpacing: 2
                Repeater {
                    model: pad.keypad
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        Layout.preferredWidth: 84
                        Layout.preferredHeight: 44
                        Layout.column: index === 9 ? 1 : index % 3      // 0 อยู่กลางแถวล่าง
                        Layout.row: Math.floor(index / 3)
                        radius: Theme.radiusControl
                        color: keyMouse.pressed ? Theme.accent : (keyMouse.containsMouse ? Theme.surfaceHover : "transparent")
                        RowLayout {
                            anchors.centerIn: parent
                            spacing: Theme.space2
                            Label {
                                text: modelData.digit
                                font.pixelSize: 20
                                color: Theme.text
                            }
                            Rectangle {
                                Layout.preferredWidth: Math.max(22, glyphLabel.implicitWidth + 8)
                                Layout.preferredHeight: 20
                                radius: 4
                                color: "#d8dee6"
                                Label {
                                    id: glyphLabel
                                    anchors.centerIn: parent
                                    text: modelData.glyph
                                    font.pixelSize: 11
                                    font.weight: Font.Bold
                                    color: "#1d2631"
                                }
                            }
                        }
                        MouseArea {
                            id: keyMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: { pad.forceActiveFocus(); pad.typeDigit(modelData.digit); }
                        }
                    }
                }
            }
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("○ Delete  ·  ✕ OK")
            font.pixelSize: Theme.fontCaption
            color: Theme.textMuted
        }
    }
}
