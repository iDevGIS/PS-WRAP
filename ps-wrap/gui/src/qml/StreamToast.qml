import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaking

// PS-WRAP: toast เล็กๆ ระหว่างสตรีม (เช่น "Recording saved" / error) — ไม่ขโมย focus (จอยยังเล่นเกมต่อได้)
// ปุ่ม action (เช่น "Show in folder") กดได้ด้วยเมาส์เท่านั้น — StreamView ส่งกรอบ toast ให้ C++ เป็น hit rect ตอนโผล่
// ใช้: toast.show({ kind: "success" | "error" | "info", title, detail, actionText, action: function() {...}, timeout })
Item {
    id: toast

    property string kind: "info"
    property string title: ""
    property string detail: ""
    property string actionText: ""
    property var action: null
    readonly property bool shown: state === "shown"
    readonly property color kindColor: kind === "success" ? Theme.success : kind === "error" ? Theme.danger : Theme.accent

    function show(opts) {
        kind = opts.kind || "info";
        title = opts.title || "";
        detail = opts.detail || "";
        actionText = opts.actionText || "";
        action = opts.action || null;
        hideTimer.interval = opts.timeout || (kind === "error" ? 8000 : 6000);
        state = "shown";
        hideTimer.restart();
    }
    function hide() { state = ""; }

    implicitWidth: Math.min(card.implicitWidth, maxWidth)
    implicitHeight: card.implicitHeight
    property real maxWidth: 640
    width: implicitWidth
    height: implicitHeight
    visible: opacity > 0
    opacity: 0
    enabled: shown

    states: State {
        name: "shown"
        PropertyChanges { toast.opacity: 1; slide.y: 0 }
    }
    transitions: Transition {
        NumberAnimation { properties: "opacity,y"; duration: 220; easing.type: Easing.OutCubic }
    }

    Timer {
        id: hideTimer
        // เลื่อนเวลาออกถ้าเมาส์ค้างอยู่บน toast
        onTriggered: if (hover.hovered) restart(); else toast.hide()
    }
    HoverHandler { id: hover }

    Item {
        id: slide
        width: parent.width
        height: parent.height
        y: 16

        Rectangle {
            id: card
            anchors.fill: parent
            implicitWidth: row.implicitWidth + Theme.space4 * 2
            implicitHeight: row.implicitHeight + Theme.space3 * 2
            radius: Theme.radiusControl + 4
            color: Qt.rgba(20 / 255, 26 / 255, 34 / 255, 0.94)
            border.width: 1
            border.color: Theme.border

            // แถบสีสถานะด้านซ้าย
            Rectangle {
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 8 }
                width: 3
                radius: 2
                color: toast.kindColor
            }

            RowLayout {
                id: row
                anchors { fill: parent; leftMargin: Theme.space4 + 6; rightMargin: Theme.space4; topMargin: Theme.space3; bottomMargin: Theme.space3 }
                spacing: Theme.space3

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 22
                    implicitHeight: 22
                    radius: 11
                    color: Qt.rgba(toast.kindColor.r, toast.kindColor.g, toast.kindColor.b, 0.18)
                    border.width: 1
                    border.color: toast.kindColor
                    Label {
                        anchors.centerIn: parent
                        text: toast.kind === "success" ? "✓" : toast.kind === "error" ? "!" : "i"
                        font.pixelSize: 13
                        font.bold: true
                        color: toast.kindColor
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.maximumWidth: toast.maxWidth - 200
                    spacing: 1
                    Label {
                        Layout.fillWidth: true
                        text: toast.title
                        font.pixelSize: Theme.fontLabel
                        font.weight: Font.DemiBold
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: toast.detail
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textMuted
                        elide: Text.ElideMiddle
                        maximumLineCount: 2
                        wrapMode: toast.kind === "error" ? Text.Wrap : Text.NoWrap
                    }
                }

                // ปุ่ม action (เมาส์) — focusPolicy NoFocus: ไม่ดึง focus ออกจากเกม/เมนู
                Button {
                    id: actionButton
                    Layout.alignment: Qt.AlignVCenter
                    visible: toast.actionText.length > 0
                    flat: true
                    focusPolicy: Qt.NoFocus
                    text: toast.actionText
                    font.pixelSize: Theme.fontCaption
                    font.weight: Font.DemiBold
                    leftPadding: 12
                    rightPadding: 12
                    topPadding: 6
                    bottomPadding: 6
                    background: Rectangle {
                        implicitHeight: 30
                        radius: Theme.radiusChip
                        color: actionButton.down ? Theme.accentPressed : actionButton.hovered ? Theme.accent : Qt.rgba(0, 0.655, 1, 0.16)
                        border.width: 1
                        border.color: Theme.accent
                    }
                    contentItem: Label {
                        text: actionButton.text
                        font: actionButton.font
                        color: actionButton.hovered ? Theme.accentText : Theme.text
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        if (typeof toast.action === "function")
                            toast.action();
                        toast.hide();
                    }
                }

                // ปิด
                Label {
                    Layout.alignment: Qt.AlignVCenter
                    text: "✕"
                    font.pixelSize: Theme.fontCaption
                    color: closeArea.containsMouse ? Theme.text : Theme.textMuted
                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        anchors.margins: -8
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: toast.hide()
                    }
                }
            }
        }
    }
}
