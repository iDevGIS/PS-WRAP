import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaking

import "controls" as C

// PS-WRAP: หน้าทดสอบไมค์ (กดจากชิปไมค์แถบล่างหน้าแรก) — สไตล์เดียวกับ controllerPopup ใน MainView
// เลือกอุปกรณ์ (radio + ✓) → บันทึก Chiaki.settings.audioInDevice + เริ่มทดสอบสดใหม่บนอุปกรณ์นั้น
// ทดสอบสด: Chiaki.window.startMicPreview(device) + MicSpectrumOverlay ตัวเดียวกับตอนสตรีม (อ่าน Chiaki.window.micMeter)
// คีย์/จอย: keyTrap กลืนทุกปุ่ม (กันบั๊ก ✕ = Return หลุดไปสั่ง Play ที่ MainView) — ↑↓ เลือกแถว, ✕/Enter ใช้อุปกรณ์นั้น, ◯/Esc ปิด
// แถวเสียง (ใต้รายการอุปกรณ์ — ↓ ต่อลงมา): ลดเสียงรบกวน / ตัดเสียงลำโพง · ←→ หรือ ✕ เปลี่ยนระดับ · มีผลทันทีทั้งที่นี่และสตรีมที่เล่นอยู่
// ใช้ key เดิมของ upstream (speechProcessing + noise/echoSuppressLevel) — 0 dB = ปิดขั้นนั้น
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

    // ---- ลดเสียงรบกวน / ตัดเสียงลำโพง (speex) ----
    readonly property bool voiceAvailable: typeof Chiaki.settings.speechProcessing !== "undefined"
    readonly property int voiceRows: voiceAvailable ? 2 : 0
    readonly property int noiseRow: devices.length        // index ใน highlighted
    readonly property int echoRow: devices.length + 1
    readonly property var noiseSteps: [0, 12, 24, 36]
    readonly property var echoSteps: [0, 20, 40, 60]
    readonly property var stepNames: [qsTr("Off"), qsTr("Low"), qsTr("Medium"), qsTr("High")]
    readonly property int noiseIndex: voiceAvailable ? stepIndex(noiseSteps, Chiaki.settings.noiseSuppressLevel) : 0
    readonly property int echoIndex: voiceAvailable ? stepIndex(echoSteps, Chiaki.settings.echoSuppressLevel) : 0

    function stepIndex(steps, db) {
        if (!Chiaki.settings.speechProcessing || db <= 0)
            return 0;
        let best = 1;
        for (let i = 2; i < steps.length; i++)
            if (Math.abs(steps[i] - db) < Math.abs(steps[best] - db))
                best = i;
        return best;
    }
    function setVoice(isNoise, index) {
        index = Math.max(0, Math.min(stepNames.length - 1, index));
        const noise = isNoise ? noiseSteps[index] : (noiseIndex === 0 ? 0 : Chiaki.settings.noiseSuppressLevel);
        const echo = isNoise ? (echoIndex === 0 ? 0 : Chiaki.settings.echoSuppressLevel) : echoSteps[index];
        Chiaki.settings.noiseSuppressLevel = noise;
        Chiaki.settings.echoSuppressLevel = echo;
        Chiaki.settings.speechProcessing = noise > 0 || echo > 0;
    }
    function stepVoice(row, delta, wrap) {
        const isNoise = row === noiseRow;
        let i = (isNoise ? noiseIndex : echoIndex) + delta;
        if (wrap)
            i = (i + stepNames.length) % stepNames.length;
        setVoice(isNoise, i);
    }

    function deviceLabel(name) { return name && name.length ? name : qsTr("Auto (Windows default)"); }
    function restartTest(device) {
        testMuted = false;
        Chiaki.window.stopMicPreview();
        started = Chiaki.window.startMicPreview(device);
    }
    function pick(index) {
        if (index >= devices.length) {
            if (index < devices.length + voiceRows)
                stepVoice(index, 1, true);
            return;
        }
        if (index < 0)
            return;
        highlighted = index;
        const name = devices[index];
        Chiaki.settings.audioInDevice = name;   // จำไว้ (Settings → Audio และสตรีมหน้าใช้ค่าเดียวกัน)
        restartTest(name);
    }
    function move(delta) {
        highlighted = Math.max(0, Math.min(devices.length - 1 + voiceRows, highlighted + delta));
        if (highlighted < devices.length)
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
                case Qt.Key_Left: case Qt.Key_Right:
                    if (dlg.highlighted >= dlg.devices.length)
                        dlg.stepVoice(dlg.highlighted, event.key === Qt.Key_Left ? -1 : 1, false);
                    break;
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
                readonly property int rows: !dlg.voiceAvailable ? 5 : (root.height < 900 ? 3 : 4)   // มีแถวเสียง → ลดแถวให้จอ 800 ยังพอ
                height: 42 * rows + 2 * (rows - 1)   // คงที่ — สูงตาม contentHeight ทำ layout วน
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

        // ---- ลดเสียงรบกวน / ตัดเสียงลำโพง ----
        Rectangle {
            visible: dlg.voiceAvailable
            Layout.fillWidth: true
            implicitHeight: voiceCol.implicitHeight + Theme.space2 * 2
            radius: Theme.radiusControl
            color: Theme.surface
            border.width: 1
            border.color: Theme.border

            ColumnLayout {
                id: voiceCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.space2 }
                spacing: 2
                VoiceRow {
                    row: dlg.noiseRow
                    title: qsTr("Noise reduction")
                    hint: qsTr("Fans, keyboard, room hiss")
                    value: dlg.noiseIndex
                    onPicked: (index) => dlg.setVoice(true, index)
                }
                VoiceRow {
                    row: dlg.echoRow
                    title: qsTr("Speaker echo")
                    hint: Chiaki.session ? qsTr("Stops game sound from your speakers reaching the mic")
                                         : qsTr("Works on game sound during a stream — not testable here")
                    value: dlg.echoIndex
                    onPicked: (index) => dlg.setVoice(false, index)
                }
            }
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

    // แถวเลือกระดับ: ชื่อ + คำอธิบาย ซ้าย · ปุ่มแบ่งช่อง Off/Low/Medium/High ขวา · กรอบฟ้า = แถวที่จอย/คีย์บอร์ดเลือกอยู่
    component VoiceRow: Rectangle {
        id: vr
        property int row: -1
        property string title
        property string hint
        property int value: 0
        readonly property bool hot: vrMouse.containsMouse || dlg.highlighted === row
        signal picked(int index)

        Layout.fillWidth: true
        implicitHeight: 52
        radius: 9
        color: hot ? Theme.surfaceHover : "transparent"
        border.width: dlg.highlighted === row ? 2 : 0
        border.color: Theme.accent
        Behavior on color { ColorAnimation { duration: Theme.durFast } }

        MouseArea {
            id: vrMouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
        }
        RowLayout {
            anchors { fill: parent; leftMargin: Theme.space3; rightMargin: Theme.space2 }
            spacing: Theme.space3
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: vr.title
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontLabel
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                Label {
                    Layout.fillWidth: true
                    text: vr.hint
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textMuted
                }
            }
            Row {
                spacing: 4
                Repeater {
                    model: dlg.stepNames
                    delegate: Rectangle {
                        id: seg
                        required property int index
                        required property string modelData
                        readonly property bool active: index === vr.value
                        width: Math.max(68, segLabel.implicitWidth + Theme.space4)
                        height: 32
                        radius: 8
                        color: active ? Theme.accent : (segMouse.containsMouse ? Theme.surfaceHover : Theme.bg)
                        border.width: 1
                        border.color: active ? Theme.accent : Theme.border
                        Behavior on color { ColorAnimation { duration: Theme.durFast } }
                        Label {
                            id: segLabel
                            anchors.centerIn: parent
                            text: seg.modelData
                            font.pixelSize: Theme.fontCaption
                            font.weight: Font.DemiBold
                            color: seg.active ? Theme.accentText : Theme.text
                        }
                        MouseArea {
                            id: segMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                dlg.highlighted = vr.row;
                                vr.picked(seg.index);
                            }
                        }
                    }
                }
            }
        }
    }
}
