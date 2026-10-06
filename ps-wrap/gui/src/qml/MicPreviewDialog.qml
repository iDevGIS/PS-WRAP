import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaking

import "controls" as C

// PS-WRAP: หน้าทดสอบไมค์ (กดจากชิปไมค์แถบล่างหน้าแรก) — สไตล์เดียวกับ controllerPopup ใน MainView
// เลือกอุปกรณ์ (radio + ✓) → บันทึก Chiaki.settings.audioInDevice + เริ่มทดสอบสดใหม่บนอุปกรณ์นั้น
// ทดสอบสด: Chiaki.window.startMicPreview(device) + MicSpectrumOverlay ตัวเดียวกับตอนสตรีม (อ่าน Chiaki.window.micMeter)
// คีย์/จอย: keyTrap กลืนทุกปุ่ม (กันบั๊ก ✕ = Return หลุดไปสั่ง Play ที่ MainView) — ↑↓ เลือกแถว, ✕/Enter ใช้อุปกรณ์นั้น, ◯/Esc ปิด
Dialog {
    id: dlg

    property Item returnFocusItem: null
    readonly property QtObject meter: Chiaki.window ? Chiaki.window.micMeter : null
    readonly property var devices: [""].concat(Chiaki.settings.availableAudioInDevices)
    readonly property string current: Chiaki.settings.audioInDevice
    readonly property bool currentMissing: current !== "" && Chiaki.settings.availableAudioInDevices.indexOf(current) < 0
    property int highlighted: 0
    property bool testing: false        // true ระหว่าง dialog เปิด → MicSpectrumOverlay เปิด FFT
    property bool started: false        // ผลของ startMicPreview
    property bool prevMeterEnabled: false
    property bool testMuted: false      // คลิกวงไมค์ใน preview

    function deviceLabel(name) { return name && name.length ? name : qsTr("Auto (Windows default)"); }
    function restartTest(device) {
        testMuted = false;
        Chiaki.window.stopMicPreview();
        started = Chiaki.window.startMicPreview(device);
    }
    function pick(index) {
        if (index < 0 || index >= devices.length)
            return;
        highlighted = index;
        const name = devices[index];
        Chiaki.settings.audioInDevice = name;   // จำไว้ (Settings → Audio และสตรีมหน้าใช้ค่าเดียวกัน)
        restartTest(name);
    }
    function move(delta) {
        highlighted = Math.max(0, Math.min(devices.length - 1, highlighted + delta));
        deviceList.positionViewAtIndex(highlighted, ListView.Contain);
    }

    parent: Overlay.overlay
    x: Math.round((root.width - width) / 2)   // ตามแบบ controllerPopup (root = ApplicationWindow) — parent.* ทำ polish loop
    y: Math.round((root.height - height) / 2)
    width: Math.min(root.width - Theme.screenMargin * 2, 760)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: Theme.space6

    onAboutToShow: {
        Chiaki.settings.refreshAudioDevices();
        highlighted = Math.max(0, devices.indexOf(current));
    }
    onOpened: {
        prevMeterEnabled = meter ? meter.enabled : false;
        testing = true;
        restartTest(current);
        if (meter)
            meter.enabled = true;
        Qt.callLater(function() { keyTrap.forceActiveFocus(); });
    }
    onClosed: {
        Chiaki.window.stopMicPreview();
        started = false;
        testing = false;                     // MicSpectrumOverlay ปิด meter เอง → คืนค่าเดิมทับ (หน้าแรก = false)
        if (meter)
            meter.enabled = prevMeterEnabled;
        if (returnFocusItem)
            returnFocusItem.forceActiveFocus(Qt.TabFocusReason);
    }
    // รายการอุปกรณ์มาแบบ async หลัง refresh — คง highlight ไว้ที่อุปกรณ์ปัจจุบัน
    onDevicesChanged: if (opened) highlighted = Math.max(0, devices.indexOf(current))

    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusCard
        border.width: 1
        border.color: Theme.border
    }
    Overlay.modal: Rectangle { color: Theme.overlay }

    ColumnLayout {
        anchors {
            left: parent.left
            right: parent.right
        }
        spacing: Theme.space4

        FocusScope {
            id: keyTrap
            focus: true
            onActiveFocusChanged: if (!activeFocus && dlg.opened) Qt.callLater(function() { keyTrap.forceActiveFocus(); })
            Layout.preferredHeight: 0
            Keys.onPressed: (event) => {
                switch (event.key) {
                case Qt.Key_Escape: case Qt.Key_Back: dlg.close(); break;
                case Qt.Key_Up: dlg.move(-1); break;
                case Qt.Key_Down: dlg.move(1); break;
                case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Space:
                    if (!event.isAutoRepeat) dlg.pick(dlg.highlighted);
                    break;
                }
                event.accepted = true;
            }
            Keys.onReleased: (event) => { event.accepted = true; }
        }

        // ---- หัว ----
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space3
            Image {
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                sourceSize: Qt.size(28, 28)
                source: "qrc:/icons/menu/mic.svg"
            }
            Label {
                text: qsTr("Microphone")
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                color: Theme.text
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                id: micStatusPill
                readonly property color c: dlg.testMuted ? Theme.textMuted : !dlg.started ? Theme.danger : (dlg.meter && dlg.meter.live ? Theme.success : Theme.warning)
                radius: Theme.radiusChip
                color: Qt.rgba(c.r, c.g, c.b, 0.15)
                implicitHeight: 28
                implicitWidth: statusRow.implicitWidth + Theme.space3 * 2
                RowLayout {
                    id: statusRow
                    anchors.centerIn: parent
                    spacing: Theme.space2
                    Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: micStatusPill.c }
                    Label {
                        text: dlg.testMuted ? qsTr("Muted") : !dlg.started ? qsTr("Not detected") : (dlg.meter && dlg.meter.live ? qsTr("Live") : qsTr("Testing…"))
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.DemiBold
                        color: micStatusPill.c
                    }
                }
            }
        }

        // ---- รายการอุปกรณ์ ----
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: deviceList.height + Theme.space2 * 2
            radius: Theme.radiusControl
            color: Theme.surface
            border.width: 1
            border.color: Theme.border

            ListView {
                id: deviceList
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.space2 }
                height: 42 * 5 + 2 * 4   // คงที่ — สูงตาม contentHeight ทำ layout วน
                clip: true
                spacing: 2
                boundsBehavior: Flickable.StopAtBounds
                model: dlg.devices
                ScrollBar.vertical: ScrollBar { policy: deviceList.contentHeight > deviceList.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    required property int index
                    readonly property bool selected: modelData === dlg.current
                    readonly property bool hot: rowMouse.containsMouse || dlg.highlighted === index
                    width: ListView.view.width - 8
                    height: 42
                    radius: 9
                    color: hot ? Theme.surfaceHover : (selected ? Qt.rgba(0, 0.655, 1, 0.10) : "transparent")
                    border.width: dlg.highlighted === index ? 2 : 0
                    border.color: Theme.accent
                    Behavior on color { ColorAnimation { duration: Theme.durFast } }

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
                        text: dlg.deviceLabel(row.modelData)
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
                        onClicked: dlg.pick(row.index)
                    }
                }
            }
        }

        Label {
            visible: dlg.currentMissing
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontCaption
            color: Theme.warning
            text: qsTr("\"%1\" is not connected — testing the Windows default microphone instead.").arg(dlg.current)
        }

        // ---- ทดสอบสด ----
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: spectrumBox.height + Theme.space4 * 2 + 18
            radius: Theme.radiusControl
            color: Theme.bg
            border.width: 1
            border.color: Theme.border

            Item {
                id: spectrumBox
                anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: Theme.space4 + 18 }
                width: Math.min(parent.width - Theme.space4 * 2, 640)
                height: Math.round(width * 110 / 320)
                MicSpectrumOverlay {
                    active: dlg.testing
                    overlayOpacity: 1.0
                    muted: dlg.testMuted
                    muteClickable: true
                    // คลิกวงไมค์ = หยุด/เริ่มทดสอบ (ไม่มี session ให้ mute)
                    onToggleMuteRequested: {
                        dlg.testMuted = !dlg.testMuted;
                        if (dlg.testMuted) { Chiaki.window.stopMicPreview(); dlg.started = false; }
                        else dlg.restartTest(dlg.current);
                    }
                    idleText: dlg.testMuted ? "" : (dlg.started ? qsTr("Listening…") : qsTr("No microphone"))
                    scale: spectrumBox.width / implicitWidth
                    transformOrigin: Item.TopLeft
                }
            }
            Label {
                anchors { right: parent.right; top: parent.top; margins: Theme.space3 }
                text: qsTr("Speak to test · ◯ / Esc closes")
                font.pixelSize: Theme.fontCaption
                color: Theme.textMuted
            }
        }

        // ---- ท้าย ----
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space2
            spacing: Theme.space3
            Label {
                Layout.fillWidth: true
                font.pixelSize: Theme.fontCaption
                color: Theme.textMuted
                text: qsTr("Used for PS5 voice chat and recordings. You can also switch it during a stream from the stream menu or the tray.")
                wrapMode: Text.WordWrap
            }
            C.Button {
                highlighted: true
                focusPolicy: Qt.NoFocus
                text: qsTr("Close")
                icon.source: typeof root !== "undefined" && root.controllerButton ? root.controllerButton("moon") : ""
                icon.width: 22
                icon.height: 22
                icon.color: "transparent"
                onClicked: dlg.close()
            }
        }
    }
}
