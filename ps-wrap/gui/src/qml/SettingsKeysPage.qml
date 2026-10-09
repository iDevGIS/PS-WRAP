import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

// PS-WRAP: หน้า Settings › Keys ใหม่
// - จัด 26 ปุ่มเป็น 6 การ์ดตามหมวด (Face / D-Pad / Shoulders & triggers / System / Left stick / Right stick)
// - จำนวนคอลัมน์การ์ดย่อ/ขยายตามความกว้าง (1–3) → ไม่ล้นจอเล็ก
// - นำทางด้วยจอย/คีย์: ↑↓ ไล่ในการ์ด → การ์ดบน/ล่าง → แถบเครื่องมือ, ←→ ข้ามการ์ดแถวเดียวกัน
// - ข้อมูล/การเซฟใช้ของ upstream ทั้งหมด (Chiaki.settings.controllerMapping, changeControllerKey, clearKeyMapping)
Item {
    id: page

    property real uiScale: 1.0
    property var keyDialog: null                 // Dialog "Key Capture" ของ SettingsDialog
    readonly property alias flick: keysFlick
    readonly property alias resetButton: resetAllKeys

    // mapping สดจาก C++ (เปลี่ยนเมื่อ reset/แก้คีย์ → ปุ่มอัปเดตข้อความเองโดยไม่ถูกสร้างใหม่)
    readonly property var mapping: Chiaki.settings.controllerMapping

    // ค่าปุ่ม = ChiakiControllerButton / ChiakiControllerAnalogButton / ControllerButtonExt (gui/include/settings.h)
    readonly property var groups: [
        { title: qsTr("Face buttons"),         values: [1 << 0, 1 << 1, 1 << 2, 1 << 3] },                 // Cross Moon Box Pyramid
        { title: qsTr("D-Pad"),                values: [1 << 6, 1 << 7, 1 << 4, 1 << 5] },                 // Up Down Left Right
        { title: qsTr("Shoulders & triggers"), values: [1 << 8, 1 << 9, 1 << 16, 1 << 17] },               // L1 R1 L2 R2
        { title: qsTr("System"),               values: [1 << 15, 1 << 12, 1 << 13, 1 << 14] },             // PS Options Share Touchpad
        { title: qsTr("Left stick"),           values: [1 << 10, 1 << 20, 1 << 21, 1 << 19, 1 << 18] },    // L3 Up Down Left Right
        { title: qsTr("Right stick"),          values: [1 << 11, 1 << 24, 1 << 25, 1 << 23, 1 << 22] }     // R3 Up Down Left Right
    ]

    function mappingIndexOf(value) {
        for (let i = 0; i < mapping.length; ++i)
            if (mapping[i].buttonValue === value)
                return i;
        return -1;
    }

    function chipAt(g, r) {
        const card = cardRep.itemAt(g);
        return card ? card.chipAt(r) : null;
    }

    // keyDialog ปิดแล้วให้ focus กลับมาที่ปุ่มเดิม
    function focusMappingIndex(idx) {
        if (idx < 0 || idx >= mapping.length)
            return;
        const value = mapping[idx].buttonValue;
        for (let g = 0; g < groups.length; ++g) {
            const r = groups[g].values.indexOf(value);
            if (r >= 0) {
                const chip = chipAt(g, r);
                if (chip)
                    chip.forceActiveFocus(Qt.TabFocusReason);
                return;
            }
        }
    }

    function focusChip(g, r) {
        const chip = chipAt(g, r);
        if (!chip)
            return false;
        chip.forceActiveFocus(Qt.TabFocusReason);
        keysFlick.ensureItemVisible(chip);
        return true;
    }

    // ปุ่มแสดงคีย์ที่ผูกไว้ (กดเพื่อจับคีย์ใหม่)
    component KeyChip: Button {
        id: chip
        property int group: 0
        property int row: 0
        property int buttonValue: 0
        readonly property int mappingIndex: page.mappingIndexOf(buttonValue)
        readonly property string keyName: mappingIndex >= 0 ? page.mapping[mappingIndex].keyName : ""

        text: keyName.length ? keyName : qsTr("Unset")
        font.weight: Font.DemiBold
        font.pixelSize: Math.round(Theme.fontLabel * page.uiScale)
        implicitWidth: Math.round(140 * page.uiScale)
        implicitHeight: Math.round(40 * page.uiScale)
        leftPadding: Theme.space3
        rightPadding: Theme.space3
        topPadding: 0
        bottomPadding: 0

        contentItem: Label {
            text: chip.text
            font: chip.font
            color: chip.keyName.length ? Theme.text : Theme.textMuted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: Theme.radiusControl
            color: chip.down ? Theme.surfaceHover : chip.hovered ? Theme.surfaceRaised : Theme.bg
            border.width: 1
            border.color: chip.visualFocus ? Theme.accent : Theme.border
            Behavior on color { ColorAnimation { duration: Theme.durFast } }
            Rectangle {
                anchors.fill: parent
                anchors.margins: -Theme.focusMargin
                radius: parent.radius + Theme.focusMargin
                color: "transparent"
                border.width: Theme.focusWidth
                border.color: Theme.accent
                visible: chip.visualFocus
            }
        }

        onActiveFocusChanged: if (activeFocus) keysFlick.ensureItemVisible(this)
        onClicked: {
            if (!page.keyDialog || mappingIndex < 0)
                return;
            page.keyDialog.show({
                value: buttonValue,
                mappingIndex: mappingIndex,
                callback: (name) => {}            // ข้อความอัปเดตเองจาก mapping ที่เปลี่ยน
            });
        }

        Keys.onPressed: (event) => {
            const cols = cardsGrid.columns;
            const rows = page.groups[group].values.length;
            switch (event.key) {
            case Qt.Key_Up:
                if (row > 0)
                    page.focusChip(group, row - 1);
                else if (group - cols >= 0)
                    page.focusChip(group - cols, page.groups[group - cols].values.length - 1);
                else {
                    resetAllKeys.forceActiveFocus(Qt.TabFocusReason);
                    keysFlick.ensureItemVisible(resetAllKeys);
                }
                event.accepted = true;
                break;
            case Qt.Key_Down:
                if (row < rows - 1)
                    page.focusChip(group, row + 1);
                else if (group + cols < page.groups.length)
                    page.focusChip(group + cols, 0);
                event.accepted = true;
                break;
            case Qt.Key_Left:
                if (group % cols > 0)
                    page.focusChip(group - 1, Math.min(row, page.groups[group - 1].values.length - 1));
                event.accepted = true;
                break;
            case Qt.Key_Right:
                if (group % cols < cols - 1 && group + 1 < page.groups.length)
                    page.focusChip(group + 1, Math.min(row, page.groups[group + 1].values.length - 1));
                event.accepted = true;
                break;
            case Qt.Key_Return:
                if (visualFocus)
                    clicked();
                event.accepted = true;
                break;
            }
        }
    }

    Flickable {
        id: keysFlick
        anchors {
            fill: parent
            topMargin: 32
            bottomMargin: 20
        }
        clip: true
        contentWidth: width
        contentHeight: content.height + Theme.space12
        flickableDirection: Flickable.AutoFlickIfNeeded
        function ensureItemVisible(item) {
            if (!item)
                return;
            const top = item.mapToItem(content, 0, 0).y - Theme.space4;
            const bottom = top + item.height + Theme.space8;
            if (top < contentY)
                contentY = Math.max(0, top);
            else if (bottom > contentY + height)
                contentY = bottom - height;
        }
        ScrollBar.vertical: C.SlimScrollBar {
            policy: ScrollBar.AlwaysOn
            visible: keysFlick.contentHeight > keysFlick.height
        }

        ColumnLayout {
            id: content
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                leftMargin: Theme.space8
                rightMargin: Theme.space8
            }
            spacing: Theme.space4

            // แถบเครื่องมือ: Reset + สวิตช์ 2 ตัว (ห่อบรรทัดเมื่อแคบ)
            Flow {
                id: toolbar
                Layout.fillWidth: true
                spacing: Theme.space6

                C.Button {
                    id: resetAllKeys
                    text: qsTr("Reset All Keys")
                    firstInFocusChain: true
                    onClicked: Chiaki.settings.clearKeyMapping()
                    onActiveFocusChanged: if (activeFocus) keysFlick.ensureItemVisible(this)
                    Keys.onPressed: (event) => {
                        switch (event.key) {
                        case Qt.Key_Right:
                            keyboardCheck.forceActiveFocus(Qt.TabFocusReason);
                            event.accepted = true;
                            break;
                        case Qt.Key_Down:
                            page.focusChip(0, 0);
                            event.accepted = true;
                            break;
                        case Qt.Key_Return:
                            if (visualFocus)
                                clicked();
                            event.accepted = true;
                            break;
                        }
                    }
                }
                C.CheckBox {
                    id: keyboardCheck
                    text: qsTr("Keyboard as controller")
                    checked: Chiaki.settings.keyboardEnabled
                    onToggled: Chiaki.settings.keyboardEnabled = checked
                    onActiveFocusChanged: if (activeFocus) keysFlick.ensureItemVisible(this)
                    Keys.onPressed: (event) => {
                        switch (event.key) {
                        case Qt.Key_Left:
                            resetAllKeys.forceActiveFocus(Qt.TabFocusReason);
                            event.accepted = true;
                            break;
                        case Qt.Key_Right:
                            mouseTouchCheck.forceActiveFocus(Qt.TabFocusReason);
                            event.accepted = true;
                            break;
                        case Qt.Key_Down:
                            page.focusChip(Math.min(1, cardsGrid.columns - 1), 0);
                            event.accepted = true;
                            break;
                        case Qt.Key_Return:
                            if (visualFocus) {
                                toggle();
                                toggled();
                            }
                            event.accepted = true;
                            break;
                        }
                    }
                }
                C.CheckBox {
                    id: mouseTouchCheck
                    text: qsTr("Enable Mouse Touchpad")
                    checked: Chiaki.settings.mouseTouchEnabled
                    onToggled: Chiaki.settings.mouseTouchEnabled = checked
                    onActiveFocusChanged: if (activeFocus) keysFlick.ensureItemVisible(this)
                    Keys.onPressed: (event) => {
                        switch (event.key) {
                        case Qt.Key_Left:
                            keyboardCheck.forceActiveFocus(Qt.TabFocusReason);
                            event.accepted = true;
                            break;
                        case Qt.Key_Down:
                            page.focusChip(Math.min(2, cardsGrid.columns - 1), 0);
                            event.accepted = true;
                            break;
                        case Qt.Key_Return:
                            if (visualFocus) {
                                toggle();
                                toggled();
                            }
                            event.accepted = true;
                            break;
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                font.pixelSize: Theme.fontCaption
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                text: qsTr("Click a key to rebind it. Keys only apply when \"Keyboard as controller\" is on.")
            }

            // การ์ดตามหมวด — คอลัมน์ 1–3 ตามความกว้าง
            GridLayout {
                id: cardsGrid
                Layout.fillWidth: true
                readonly property int cardMin: Math.round(360 * page.uiScale)
                columns: Math.max(1, Math.min(3, Math.floor((width + columnSpacing) / (cardMin + columnSpacing))))
                columnSpacing: Theme.cardGap
                rowSpacing: Theme.cardGap

                Repeater {
                    id: cardRep
                    model: page.groups

                    Rectangle {
                        id: card
                        required property int index
                        required property var modelData
                        function chipAt(r) { const row = chipRep.itemAt(r); return row ? row.chip : null }

                        Layout.fillWidth: true
                        Layout.preferredWidth: cardsGrid.cardMin
                        Layout.alignment: Qt.AlignTop
                        implicitHeight: cardCol.implicitHeight + Theme.space4 * 2
                        radius: Theme.radiusCard
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.border

                        ColumnLayout {
                            id: cardCol
                            anchors {
                                left: parent.left
                                right: parent.right
                                top: parent.top
                                margins: Theme.space4
                            }
                            spacing: Theme.space2

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.bottomMargin: Theme.space1
                                spacing: Theme.space2
                                Rectangle {
                                    Layout.preferredWidth: 4
                                    Layout.preferredHeight: Math.round(Theme.fontLabel * page.uiScale)
                                    radius: 2
                                    color: Theme.accent
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: card.modelData.title.toUpperCase()
                                    font.pixelSize: Theme.fontCaption
                                    font.letterSpacing: 1.2
                                    font.weight: Font.DemiBold
                                    color: Theme.textMuted
                                    elide: Text.ElideRight
                                }
                            }

                            Repeater {
                                id: chipRep
                                model: card.modelData.values

                                RowLayout {
                                    required property int index
                                    required property var modelData
                                    property alias chip: chipItem
                                    Layout.fillWidth: true
                                    spacing: Theme.space3

                                    Label {
                                        Layout.fillWidth: true
                                        readonly property int mi: page.mappingIndexOf(parent.modelData)
                                        text: mi >= 0 ? page.mapping[mi].buttonName : ""
                                        font.pixelSize: Math.round(Theme.fontBody * page.uiScale)
                                        color: Theme.text
                                        elide: Text.ElideRight
                                    }
                                    KeyChip {
                                        id: chipItem
                                        group: card.index
                                        row: parent.index
                                        buttonValue: parent.modelData
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
