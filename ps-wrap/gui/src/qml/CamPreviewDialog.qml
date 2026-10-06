import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtCore
import QtMultimedia

import org.streetpea.chiaking

import "controls" as C

// PS-WRAP: หน้าทดสอบ facecam (กดจากชิปกล้องแถบล่างหน้าแรก) — สไตล์เดียวกับ controllerPopup ใน MainView
// preview = WebcamOverlay ตัวเดียวกับ Settings → Facecam Preview (กล้องเปิดเฉพาะตอน dialog เปิด)
// fx / background เป็นของ C++ (Chiaki.window.camFx / camBackground — tray เปลี่ยนสดได้) · ที่เหลืออ่านจาก QSettings กลุ่ม pswrap
// คีย์/จอย: keyTrap กลืนทุกปุ่ม (กัน ✕ = Return หลุดไป Play) — ↑↓ เลือกแถว · ←→ เปลี่ยนค่า · ✕/Enter สลับ/ค่าถัดไป · ◯/Esc ปิด
Dialog {
    id: dlg

    property Item returnFocusItem: null
    signal cameraPicked(string name)     // MainView → camChip.reload()

    // ค่าจาก QSettings กลุ่ม pswrap — อ่านสดทุกครั้งที่เปิด (ใช้ value()/setValue() ไม่ประกาศ property เพื่อไม่ cache ค่าเก่าเขียนทับ)
    readonly property QtObject prefs: Settings { category: "pswrap" }
    property string camDevice: ""
    property bool camMirror: true
    property bool camCircle: false
    property real camZoom: 1.0
    property real camPanX: 0.0
    property real camPanY: 0.0
    property real camKeyTol: 0.25

    readonly property QtObject mediaDevices: MediaDevices {}
    readonly property QtObject dshowLister: DshowCamera {}
    readonly property var cameraNames: {
        let names = [];
        for (let i = 0; i < mediaDevices.videoInputs.length; ++i) names.push(mediaDevices.videoInputs[i].description);
        for (let j = 0; j < dshowLister.devices.length; ++j) if (names.indexOf(dshowLister.devices[j]) < 0) names.push(dshowLister.devices[j]);
        return names;
    }
    readonly property var cameraOptions: [""].concat(cameraNames)
    readonly property var fxNames: [qsTr("None"), qsTr("Sunglasses"), qsTr("Mustache"), qsTr("Clown nose"), qsTr("Crown"), qsTr("Bane mask"), qsTr("Party (glasses + mustache + crown)"), qsTr("Samurai mask"), qsTr("Ninja"), qsTr("Ghost (Tsushima)"), qsTr("Samurai armor (kabuto + mask)"), qsTr("Samurai armor (photo)"), qsTr("Jin mask (private)"), qsTr("Jin mask + headband (private)")]
    readonly property var backgroundNames: [qsTr("Keep"), qsTr("Remove green screen"), qsTr("Remove blue screen"), qsTr("AI remove (no green screen)")]

    property int highlighted: 0          // 0 สวิตช์ · 1 กล้อง · 2 effect · 3 background
    readonly property int rowCount: 4

    function toBool(v, def) {
        if (v === undefined || v === null || v === "") return def;
        return v === true || v === "true" || v === 1 || v === "1";
    }
    function toNum(v, def) {
        const n = Number(v);
        return (v === undefined || v === null || v === "" || isNaN(n)) ? def : n;
    }
    function reload() {
        prefs.sync();
        camDevice = String(prefs.value("camDevice", "") || "");
        camMirror = toBool(prefs.value("camMirror", true), true);
        camCircle = toBool(prefs.value("camCircle", false), false);
        camZoom = toNum(prefs.value("camZoom", 1.0), 1.0);
        camPanX = toNum(prefs.value("camPanX", 0.0), 0.0);
        camPanY = toNum(prefs.value("camPanY", 0.0), 0.0);
        camKeyTol = toNum(prefs.value("camKeyTol", 0.25), 0.25);
        dshowLister.refreshDevices();
    }
    function setCamera(name) {
        camDevice = name;
        prefs.setValue("camDevice", name);
        prefs.sync();
        cameraPicked(name);
    }
    function cycle(list, cur, delta) { return (cur + delta + list.length) % list.length; }
    // ←→ / ✕ บนแถวที่เลือก
    function adjust(row, delta) {
        switch (row) {
        case 0: Chiaki.window.camOverlay = !Chiaki.window.camOverlay; break;
        case 1: setCamera(cameraOptions[cycle(cameraOptions, Math.max(0, cameraOptions.indexOf(camDevice)), delta)]); break;
        case 2: Chiaki.window.camFx = cycle(fxNames, Chiaki.window.camFx, delta); break;
        case 3: Chiaki.window.camBackground = cycle(backgroundNames, Chiaki.window.camBackground, delta); break;
        }
    }

    parent: Overlay.overlay
    x: Math.round((root.width - width) / 2)   // ตามแบบ controllerPopup (root = ApplicationWindow) — parent.* ทำ polish loop
    y: Math.round((root.height - height) / 2)
    width: Math.min(root.width - Theme.screenMargin * 2, 900)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: Theme.space6

    onAboutToShow: { reload(); highlighted = 0; }
    onOpened: Qt.callLater(function() { keyTrap.forceActiveFocus(); })
    onClosed: if (returnFocusItem) returnFocusItem.forceActiveFocus(Qt.TabFocusReason)

    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusCard
        border.width: 1
        border.color: Theme.border
    }
    Overlay.modal: Rectangle { color: Theme.overlay }

    // แถวตั้งค่า: ชื่อ + ค่า + ‹ › (เมาส์คลิกลูกศร/ค่า, จอยใช้ ←→) — toggle = สวิตช์
    component SettingRow: Rectangle {
        id: sr
        property int row: 0
        property string label: ""
        property string value: ""
        property bool isSwitch: false
        property bool checked: false
        readonly property bool hot: srMouse.containsMouse || dlg.highlighted === row
        Layout.fillWidth: true
        implicitHeight: 52
        radius: 9
        color: hot ? Theme.surfaceHover : "transparent"
        border.width: dlg.highlighted === row ? 2 : 0
        border.color: Theme.accent
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
        MouseArea {
            id: srMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: { dlg.highlighted = sr.row; dlg.adjust(sr.row, 1); }
        }
        ColumnLayout {
            anchors { left: parent.left; leftMargin: Theme.space3; right: ctrl.left; rightMargin: Theme.space3; verticalCenter: parent.verticalCenter }
            spacing: 0
            Label {
                Layout.fillWidth: true
                text: sr.label
                font.pixelSize: Theme.fontCaption
                color: Theme.textMuted
                elide: Text.ElideRight
            }
            Label {
                Layout.fillWidth: true
                visible: !sr.isSwitch
                text: sr.value
                font.pixelSize: Theme.fontLabel
                font.weight: Font.DemiBold
                color: Theme.text
                elide: Text.ElideRight
            }
        }
        Item {
            id: ctrl
            anchors { right: parent.right; rightMargin: Theme.space3; verticalCenter: parent.verticalCenter }
            width: sr.isSwitch ? 40 : 64
            height: 28
            // สวิตช์
            Rectangle {
                visible: sr.isSwitch
                anchors.centerIn: parent
                width: 40; height: 22; radius: 11
                color: sr.checked ? Theme.accent : Theme.border
                Behavior on color { ColorAnimation { duration: Theme.durFast } }
                Rectangle {
                    width: 16; height: 16; radius: 8
                    anchors.verticalCenter: parent.verticalCenter
                    x: sr.checked ? parent.width - width - 3 : 3
                    color: "white"
                    Behavior on x { NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic } }
                }
            }
            // ‹ ›
            Row {
                visible: !sr.isSwitch
                anchors.centerIn: parent
                spacing: 4
                Repeater {
                    model: [-1, 1]
                    delegate: Rectangle {
                        required property int modelData
                        width: 30; height: 28; radius: 8
                        color: arrowMouse.containsMouse ? Theme.accent : Qt.rgba(1, 1, 1, 0.06)
                        Label {
                            anchors.centerIn: parent
                            text: modelData < 0 ? "‹" : "›"
                            font.pixelSize: Theme.fontBody
                            font.bold: true
                            color: arrowMouse.containsMouse ? Theme.accentText : Theme.text
                        }
                        MouseArea {
                            id: arrowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: { dlg.highlighted = sr.row; dlg.adjust(sr.row, modelData); }
                        }
                    }
                }
            }
        }
    }

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
                case Qt.Key_Up: dlg.highlighted = Math.max(0, dlg.highlighted - 1); break;
                case Qt.Key_Down: dlg.highlighted = Math.min(dlg.rowCount - 1, dlg.highlighted + 1); break;
                case Qt.Key_Left: if (dlg.highlighted > 0) dlg.adjust(dlg.highlighted, -1); break;
                case Qt.Key_Right: if (dlg.highlighted > 0) dlg.adjust(dlg.highlighted, 1); break;
                case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Space:
                    if (!event.isAutoRepeat) dlg.adjust(dlg.highlighted, 1);
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
                source: "qrc:/icons/menu/cam.svg"
            }
            Label {
                text: qsTr("Facecam")
                font.pixelSize: Theme.fontTitle
                font.weight: Font.DemiBold
                color: Theme.text
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                id: camStatusPill
                // "Live" เฉพาะตอนมีเฟรมเข้าจริง (camPreview.status) — เปิดไม่ได้/ไม่มีเฟรมใน ~3 s = แดง "No video from camera"
                readonly property color c: !camPreview.hasCamera || camPreview.noVideo ? Theme.danger
                                           : camPreview.status !== "live" ? Theme.warning
                                           : (Chiaki.window.camOverlay ? Theme.success : Theme.textMuted)
                radius: Theme.radiusChip
                color: Qt.rgba(c.r, c.g, c.b, 0.15)
                implicitHeight: 28
                implicitWidth: camStatusRow.implicitWidth + Theme.space3 * 2
                Layout.maximumWidth: 360
                RowLayout {
                    id: camStatusRow
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, 360 - Theme.space3 * 2)
                    spacing: Theme.space2
                    Rectangle { implicitWidth: 8; implicitHeight: 8; radius: 4; color: camStatusPill.c }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: !camPreview.hasCamera ? qsTr("No camera")
                              : camPreview.noVideo ? qsTr("No video from camera")
                              : camPreview.status !== "live" ? qsTr("Starting camera…")
                              : (Chiaki.window.camOverlay ? qsTr("Live · %1").arg(camPreview.cameraName) : qsTr("Live · hidden in stream"))
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.DemiBold
                        color: camStatusPill.c
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: dlg.availableWidth >= 760 ? 2 : 1
            columnSpacing: Theme.space4
            rowSpacing: Theme.space4

            // ---- preview สด ----
            Rectangle {
                id: previewBox
                Layout.preferredWidth: dlg.availableWidth >= 760 ? Math.round(dlg.availableWidth * 0.48) : dlg.availableWidth
                Layout.preferredHeight: previewArea.height + Theme.space4 * 2 + 18
                Layout.alignment: Qt.AlignTop
                radius: Theme.radiusControl
                color: Theme.bg
                border.width: 1
                border.color: Theme.border
                Item {
                    id: previewArea
                    anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: Theme.space4 + 18 }
                    width: dlg.camCircle ? Math.min(parent.width - Theme.space4 * 2, 260) : parent.width - Theme.space4 * 2
                    height: dlg.camCircle ? width : Math.round(width * 9 / 16)
                    // พื้นลาย checker ให้เห็นส่วนโปร่งใสตอนตัดพื้นหลัง
                    Rectangle {
                        anchors.fill: parent
                        radius: camPreview.cornerRadius
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.border
                    }
                    WebcamOverlay {
                        id: camPreview
                        anchors.fill: parent
                        active: dlg.visible   // กล้องเปิดเฉพาะตอน dialog เปิด (ปิด = ปล่อย device)
                        cameraId: dlg.camDevice
                        mirror: dlg.camMirror
                        circle: dlg.camCircle
                        zoom: dlg.camZoom
                        panX: dlg.camPanX
                        panY: dlg.camPanY
                        keyEnabled: Chiaki.window.camBackground === 1 || Chiaki.window.camBackground === 2
                        aiEnabled: Chiaki.window.camBackground === 3
                        fx: Chiaki.window.camFx
                        keyColor: Chiaki.window.camBackground === 2 ? "#0000ff" : "#00ff00"
                        keyTolerance: dlg.camKeyTol
                    }
                    Label {
                        visible: !camPreview.hasCamera
                        anchors.centerIn: parent
                        text: qsTr("No camera connected")
                        font.pixelSize: Theme.fontLabel
                        color: Theme.textMuted
                    }
                }
                Label {
                    anchors { right: parent.right; top: parent.top; margins: Theme.space3 }
                    text: qsTr("Live preview · ◯ / Esc closes")
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textMuted
                }
            }

            // ---- ตั้งค่า ----
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: Theme.space1

                SettingRow {
                    row: 0
                    isSwitch: true
                    label: qsTr("Show facecam during stream")
                    checked: Chiaki.window.camOverlay
                }
                SettingRow {
                    row: 1
                    label: qsTr("Camera")
                    value: dlg.camDevice === "" ? qsTr("System default") : dlg.camDevice
                }
                SettingRow {
                    row: 2
                    label: qsTr("Effect")
                    value: dlg.fxNames[Math.max(0, Math.min(dlg.fxNames.length - 1, Chiaki.window.camFx))]
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space3
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontCaption
                    color: Chiaki.window.camFx > 0 && camPreview.fxError.length ? Theme.danger : Theme.textMuted
                    text: Chiaki.window.camFx > 0
                          ? (camPreview.fxError.length ? camPreview.fxError
                             : (camPreview.fxActive ? (camPreview.fxMesh ? qsTr("Face mesh 478 pts · %1 ms").arg(camPreview.fxMs) : qsTr("Basic tracking (face_mesh.onnx missing)"))
                                : qsTr("Looking for a face…")))
                          : qsTr("AI face tracking · effects follow your face")
                }
                SettingRow {
                    row: 3
                    label: qsTr("Background")
                    value: dlg.backgroundNames[Math.max(0, Math.min(dlg.backgroundNames.length - 1, Chiaki.window.camBackground))]
                }
                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.space3
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontCaption
                    color: Chiaki.window.camBackground === 3 && camPreview.aiError.length ? Theme.danger : Theme.textMuted
                    text: Chiaki.window.camBackground === 3
                          ? (camPreview.aiError.length ? camPreview.aiError : (camPreview.aiActive ? qsTr("AI on CPU · %1 ms/frame").arg(camPreview.aiMs) : qsTr("Loading AI model…")))
                          : qsTr("Chroma key needs a plain green/blue backdrop · AI works anywhere")
                }
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
                text: qsTr("↑↓ choose · ←→ change. Size, mirror, zoom and shape are in Settings → General. During a stream: Ctrl+Shift+C shows/hides the facecam, Ctrl+Shift+V moves it; the tray menu can change effect and background live.")
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
