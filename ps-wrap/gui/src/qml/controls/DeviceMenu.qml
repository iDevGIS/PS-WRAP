import QtQuick
import QtQuick.Controls
import QtQuick.Effects

import org.streetpea.chiaking

// PS-WRAP: เมนูเลือกอุปกรณ์ (ไมค์ / กล้อง) สำหรับชิปแถบล่างหน้าแรก — การ์ดตามธีม, radio + ✓, สวิตช์ด้านบน (ถ้ามี)
// ไม่ใช้ MenuItem.checkable: คลิกแล้ว Qt สลับ checked เองทำให้ binding หลุด (ติ๊กหายหลังเลือก)
Popup {
    id: menu
    property string title: ""
    property var items: []          // [{ label: "...", value: ... }]
    property var current
    property bool showToggle: false
    property string toggleText: ""
    property bool toggleChecked: false
    property int highlighted: -1    // -1 = สวิตช์ (ถ้ามี) · 0.. = รายการ
    signal picked(var value)
    signal toggleClicked()

    width: 400
    padding: 6
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // เปิดเหนือ anchor ชิดซ้าย ไม่ล้นขวาจอ
    function openAbove(anchor) {
        parent = anchor;
        const gx = anchor.mapToItem(null, 0, 0).x;
        const maxX = (Overlay.overlay ? Overlay.overlay.width : 1920) - gx - width - 12;
        x = Math.min(0, maxX);
        y = -implicitHeight - 10;
        highlighted = Math.max(0, indexOfCurrent());
        open();
    }
    function indexOfCurrent() {
        for (let i = 0; i < items.length; ++i)
            if (items[i].value === current) return i;
        return -1;
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durFast }
        NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.durFast; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 90 }
    }

    background: Item {
        Rectangle {
            id: card
            anchors.fill: parent
            radius: 14
            color: Theme.surface
            border.width: 1
            border.color: Theme.border
            visible: false
        }
        MultiEffect {
            source: card
            anchors.fill: card
            shadowEnabled: true
            shadowColor: Qt.rgba(0, 0, 0, 0.6)
            shadowBlur: 0.8
            shadowVerticalOffset: 6
            autoPaddingEnabled: true
        }
    }

    contentItem: Column {
        spacing: 2
        focus: true

        Keys.onUpPressed: menu.highlighted = Math.max(menu.showToggle ? -1 : 0, menu.highlighted - 1)
        Keys.onDownPressed: menu.highlighted = Math.min(menu.items.length - 1, menu.highlighted + 1)
        Keys.onReturnPressed: activate()
        Keys.onEnterPressed: activate()
        Keys.onSpacePressed: activate()
        function activate() {
            if (menu.highlighted < 0) {
                menu.toggleClicked();
            } else if (menu.highlighted < menu.items.length) {
                menu.picked(menu.items[menu.highlighted].value);
                menu.close();
            }
        }

        Label {
            visible: menu.title.length > 0
            leftPadding: Theme.space3
            topPadding: Theme.space2
            bottomPadding: Theme.space1
            text: menu.title.toUpperCase()
            font.pixelSize: Theme.fontCaption - 1
            font.weight: Font.DemiBold
            font.letterSpacing: 1.4
            color: Theme.textMuted
        }

        // สวิตช์ (เช่น "Show facecam during stream")
        Rectangle {
            visible: menu.showToggle
            width: parent.width
            height: 46
            radius: 9
            color: toggleMouse.containsMouse || menu.highlighted === -1 ? Theme.surfaceHover : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durFast } }
            Label {
                anchors { left: parent.left; leftMargin: Theme.space3; verticalCenter: parent.verticalCenter; right: sw.left; rightMargin: Theme.space3 }
                text: menu.toggleText
                elide: Text.ElideRight
                font.pixelSize: Theme.fontLabel
                color: Theme.text
            }
            Rectangle {
                id: sw
                anchors { right: parent.right; rightMargin: Theme.space3; verticalCenter: parent.verticalCenter }
                width: 40; height: 22; radius: 11
                color: menu.toggleChecked ? Theme.accent : Theme.border
                Behavior on color { ColorAnimation { duration: Theme.durFast } }
                Rectangle {
                    width: 16; height: 16; radius: 8
                    anchors.verticalCenter: parent.verticalCenter
                    x: menu.toggleChecked ? parent.width - width - 3 : 3
                    color: "white"
                    Behavior on x { NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic } }
                }
            }
            MouseArea {
                id: toggleMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onEntered: menu.highlighted = -1
                onClicked: menu.toggleClicked()
            }
        }
        Rectangle {
            visible: menu.showToggle
            width: parent.width - Theme.space3 * 2
            x: Theme.space3
            height: 1
            color: Theme.border
        }
        Item { visible: menu.showToggle; width: 1; height: 2 }

        Repeater {
            model: menu.items
            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                readonly property bool selected: modelData.value === menu.current
                readonly property bool hot: rowMouse.containsMouse || menu.highlighted === index
                width: menu.availableWidth
                height: 42
                radius: 9
                color: hot ? Theme.surfaceHover : (selected ? Qt.rgba(0, 0.655, 1, 0.10) : "transparent")
                Behavior on color { ColorAnimation { duration: Theme.durFast } }

                // radio
                Rectangle {
                    id: radio
                    anchors { left: parent.left; leftMargin: Theme.space3; verticalCenter: parent.verticalCenter }
                    width: 16; height: 16; radius: 8
                    color: "transparent"
                    border.width: 2
                    border.color: row.selected ? Theme.accent : Theme.textMuted
                    Rectangle {
                        anchors.centerIn: parent
                        width: 8; height: 8; radius: 4
                        color: Theme.accent
                        visible: row.selected
                    }
                }
                Label {
                    anchors { left: radio.right; leftMargin: Theme.space3; right: check.left; rightMargin: Theme.space2; verticalCenter: parent.verticalCenter }
                    text: row.modelData.label
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontLabel
                    font.weight: row.selected ? Font.DemiBold : Font.Normal
                    color: row.selected ? Theme.accent : Theme.text
                }
                Label {
                    id: check
                    anchors { right: parent.right; rightMargin: Theme.space3; verticalCenter: parent.verticalCenter }
                    text: "✓"
                    font.pixelSize: Theme.fontLabel
                    font.weight: Font.Bold
                    color: Theme.accent
                    opacity: row.selected ? 1 : 0
                }
                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: menu.highlighted = row.index
                    onClicked: { menu.picked(row.modelData.value); menu.close(); }
                }
            }
        }
    }
}
