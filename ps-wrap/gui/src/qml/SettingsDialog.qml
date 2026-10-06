import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtMultimedia

import org.streetpea.chiaking

import "controls" as C

DialogView {
    enum Console {
        PS4,
        PS5
    }
    property int selectedConsole: SettingsDialog.Console.PS5
    property bool quitControllerMapping: true
    readonly property int labelWidth: Math.round(270 * dialog.uiScale)   // PS-WRAP: คอลัมน์ชื่อ setting
    // PS-WRAP: facecam prefs (ชุดเดียวกับ StreamView: QSettings กลุ่ม pswrap) — อ่านตอนเปิด Settings, StreamView อ่านตอนเริ่มสตรีม
    // (DialogView รับ child เฉพาะ Item → ประกาศ object ที่ไม่ใช่ Item ผ่าน property)
    readonly property QtObject camPrefsObject: Settings {
        id: camPrefs
        category: "pswrap"
        property string camDevice: ""
        property bool camMirror: true
        property bool camCircle: false
        property real camZoom: 1.0
        property real camKeyTol: 0.25
        property real camPanX: 0.0
        property real camPanY: 0.0
    }
    readonly property QtObject mediaDevicesObject: MediaDevices { id: mediaDevices }
    // PS-WRAP: เลือกโฟลเดอร์เก็บคลิป (Chiaki.window.recordingFolder) — dialog native ของระบบ
    readonly property QtObject recFolderDialogObject: FolderDialog {
        id: recFolderDialog
        title: qsTr("Choose recording folder")
        currentFolder: dialog.localPathToUrl(Chiaki.window.recordingFolder)
        onAccepted: {
            const path = dialog.urlToLocalPath(selectedFolder);
            if (path.length) {
                Chiaki.window.recordingFolder = path;
                recFolderField.text = path;
            }
            recBrowseButton.forceActiveFocus(Qt.TabFocusReason);
        }
        onRejected: recBrowseButton.forceActiveFocus(Qt.TabFocusReason)
    }
    function urlToLocalPath(url) {
        let path = decodeURIComponent(String(url).replace(/^file:\/\/\/?/, ""));
        if (Qt.platform.os === "windows")
            path = path.replace(/\//g, "\\");
        else if (!path.startsWith("/"))
            path = "/" + path;
        return path;
    }
    function localPathToUrl(path) {
        if (!path)
            return "";
        const p = String(path).replace(/\\/g, "/");
        return "file://" + (p.startsWith("/") ? "" : "/") + p;
    }
    readonly property QtObject dshowListerObject: DshowCamera { id: dshowLister }   // PS-WRAP: รายชื่อกล้อง DirectShow/virtual
    function allCameraNames() {
        let names = [];
        for (let i = 0; i < mediaDevices.videoInputs.length; ++i) names.push(mediaDevices.videoInputs[i].description);
        for (let j = 0; j < dshowLister.devices.length; ++j) if (names.indexOf(dshowLister.devices[j]) < 0) names.push(dshowLister.devices[j]);
        return names;
    }
    id: dialog
    title: qsTr("Settings")
    header: qsTr("* Defaults in () to right of value or marked with (Default)")
    buttonVisible: false
    function flickContainsItem(flick, item) {
        let current = item;
        while (current) {
            if (current === flick || current === flick.contentItem)
                return true;
            current = current.parent;
        }
        return false;
    }
    function flickVisibilityTarget(flick, item) {
        let current = item;
        while (current && current !== flick && current !== flick.contentItem) {
            if (current.parent === flick.contentItem)
                return current;
            if (current.parent && current.parent.parent === flick.contentItem)
                return current;
            current = current.parent;
        }
        return item;
    }
    function ensureItemVisibleInFlick(flick, item) {
        if (!flick || !item || !flickContainsItem(flick, item))
            return;
        let target = flickVisibilityTarget(flick, item);
        if (target.height > flick.height)   // PS-WRAP: container (GridLayout) สูงกว่าจอ → เลื่อนไป "ก้น" container ไม่ใช่ item → ใช้ item เอง
            target = item;
        const top = target.mapToItem(flick.contentItem, 0, 0).y;
        const bottom = top + target.height;
        if (top < flick.contentY)
            flick.contentY = top;
        else if (bottom > flick.contentY + flick.height)
            flick.contentY = bottom - flick.height;
    }
    function itemIsInsideNestedScrollable(flick, item) {
        let current = item ? item.parent : null;
        while (current && current !== flick && current !== flick.contentItem) {
            if (current.contentY !== undefined && current.contentHeight !== undefined && current !== flick)
                return true;
            current = current.parent;
        }
        return false;
    }
    function nestedScrollableTarget(flick, item) {
        let current = item;
        while (current && current !== flick && current !== flick.contentItem) {
            if (current.contentY !== undefined && current.contentHeight !== undefined && current !== flick)
                return current;
            current = current.parent;
        }
        return item;
    }
    function ensureActiveFocusVisible() {
        const flick = activeSettingsFlick();
        const window = dialog.Window.window;
        if (!flick || !window || !window.activeFocusItem)
            return;
        const target = itemIsInsideNestedScrollable(flick, window.activeFocusItem)
            ? nestedScrollableTarget(flick, window.activeFocusItem)
            : window.activeFocusItem;
        ensureItemVisibleInFlick(flick, target);
    }
    function scrollFlickKeepingItemVisible(flick, item, delta) {
        if (!flick || !item)
            return;
        const maxContentY = Math.max(0, flick.contentHeight - flick.height);
        if (maxContentY <= 0)
            return;
        flick.contentY = Math.max(0, Math.min(maxContentY, flick.contentY + delta));
    }
    function focusCurrentTabFirstItem() {
        let item = null;
        switch (bar.currentIndex) {
        case 0: item = hideToTrayCheck; break;   // PS-WRAP: item บนสุดของหน้า (เดิม disconnectAction อยู่กลางหน้า → เปิดมาเลื่อนข้าม preview)
        case 1: item = hwDecoderCombo; break;
        case 2: item = consoleSelection; break;
        case 3: item = audioOutDevice; break;
        case 4: item = registerNewButton; break;
        case 5: item = keysPage.resetButton; break;
        case 6: item = controllerMappingChange; break;
        case 7: item = firstRemoteFocusableItem(); break;
        case 8: item = profile; break;
        }
        if (item)
            item.forceActiveFocus(Qt.TabFocusReason);
    }
    function activeSettingsFlick() {
        switch (bar.currentIndex) {
        case 0: return generalFlick;
        case 1: return videoFlick;
        case 2: return streamFlick;
        case 3: return audiowifiFlick;
        case 4: return consolesFlick;
        case 5: return keysPage.flick;
        case 6: return controllersFlick;
        case 7: return remoteFlick;
        case 8: return configFlick;
        default: return null;
        }
    }

    function firstRemoteFocusableItem() {
        if (openPsnLogin.visible)
            return openPsnLogin;
        if (resetPsnTokens.visible)
            return resetPsnTokens;
        if (holePunchGuessingCheckbox.visible)
            return holePunchGuessingCheckbox;
        if (portGuessCountSlider.visible)
            return portGuessCountSlider;
        if (portGuessSocketSlider.visible)
            return portGuessSocketSlider;
        return remoteFlick;
    }
    Keys.onPressed: (event) => {
        if (event.modifiers)
            return;
        switch (event.key) {
        case Qt.Key_PageUp:
            bar.decrementCurrentIndex();
            event.accepted = true;
            break;
        case Qt.Key_PageDown:
            bar.incrementCurrentIndex();
            event.accepted = true;
            break;
        case Qt.Key_Up:
        {
            const flick = activeSettingsFlick();
            if (!flick || flick.contentHeight <= flick.height || flick.contentY <= 0.001)
                return;
            flick.flick(0, 500);
            event.accepted = true;
            break;
        }
        case Qt.Key_Down:
        {
            const flick = activeSettingsFlick();
            if (!flick || flick.contentHeight <= flick.height || flick.contentY >= flick.contentHeight - flick.height - 0.001)
                return;
            flick.flick(0, -500);
            event.accepted = true;
            break;
        }
        }
    }

    Item {
        // PS-WRAP Phase 3: sidebar แทน TabBar แนวนอน — คง id `bar` + currentIndex/incrementCurrentIndex/decrementCurrentIndex (ListView มี API เดียวกับ TabBar)
        // index ต้องตรงกับ StackLayout ด้านล่าง: 0 General, 1 Video, 2 Stream, 3 Audio/Wifi, 4 Consoles, 5 Keys, 6 Controllers, 7 Remote, 8 Config
        Rectangle {
            id: sidebar
            width: dialog.width < 1500 ? 220 : 280
            anchors {
                top: parent.top
                left: parent.left
                bottom: parent.bottom
            }
            color: Theme.surface
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }

            ListView {
                id: bar
                anchors {
                    fill: parent
                    topMargin: Theme.space4
                    leftMargin: Theme.space3
                    rightMargin: Theme.space3 + 1
                    bottomMargin: 64
                }
                clip: true
                interactive: false
                currentIndex: 0
                spacing: 2
                model: [
                    { name: qsTr("General"),     group: qsTr("Basics") },
                    { name: qsTr("Video"),       group: qsTr("Basics") },
                    { name: qsTr("Stream"),      group: qsTr("Basics") },
                    { name: qsTr("Audio & Wi-Fi"), group: qsTr("Basics") },
                    { name: qsTr("Consoles"),    group: qsTr("Basics") },
                    { name: qsTr("Keys"),        group: qsTr("Advanced") },
                    { name: qsTr("Controllers"), group: qsTr("Advanced") },
                    { name: qsTr("Remote"),      group: qsTr("Advanced") },
                    { name: qsTr("Config"),      group: qsTr("Advanced") }
                ]
                section.property: "group"
                section.delegate: Label {
                    width: ListView.view.width
                    topPadding: Theme.space4
                    bottomPadding: Theme.space2
                    leftPadding: Theme.space3
                    text: section
                    font.pixelSize: Theme.fontCaption
                    font.letterSpacing: 2
                    font.weight: Font.DemiBold
                    color: Theme.textMuted
                }
                delegate: Rectangle {
                    required property int index
                    required property var modelData
                    readonly property bool current: ListView.isCurrentItem
                    width: ListView.view.width
                    height: 48
                    radius: Theme.radiusControl
                    color: current ? Theme.accent : (navMouse.containsMouse ? Theme.surfaceRaised : "transparent")
                    Behavior on color { ColorAnimation { duration: Theme.durFast } }
                    Label {
                        anchors {
                            left: parent.left
                            right: parent.right
                            leftMargin: Theme.space4
                            verticalCenter: parent.verticalCenter
                        }
                        text: modelData.name
                        font.pixelSize: Theme.fontBody
                        font.weight: current ? Font.DemiBold : Font.Normal
                        color: current ? Theme.accentText : Theme.text
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        id: navMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: bar.currentIndex = index
                    }
                }
            }

            // hint: L1 / R1 เปลี่ยนหมวด
            RowLayout {
                anchors {
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                    margins: Theme.space4
                }
                spacing: Theme.space2
                Image { Layout.preferredWidth: 26; Layout.preferredHeight: 26; sourceSize: Qt.size(26, 26); source: "qrc:/icons/l1.svg" }
                Image { Layout.preferredWidth: 26; Layout.preferredHeight: 26; sourceSize: Qt.size(26, 26); source: "qrc:/icons/r1.svg" }
                Label { text: qsTr("Switch section"); font.pixelSize: Theme.fontCaption; color: Theme.textMuted; Layout.fillWidth: true; elide: Text.ElideRight }
            }
        }

        StackLayout {
            anchors {
                top: parent.top
                left: sidebar.right
                right: parent.right
                bottom: parent.bottom
            }
            currentIndex: bar.currentIndex
            onCurrentIndexChanged: {
                const flick = dialog.activeSettingsFlick();
                if (flick) flick.contentY = 0;   // PS-WRAP: เปิดหน้าใหม่ให้เริ่มที่บนสุดเสมอ
                dialog.focusCurrentTabFirstItem()
                Qt.callLater(dialog.ensureActiveFocusVisible)
            }

            Item {
                // General
                Flickable {
                    id: generalFlick
                    anchors {
                        fill: parent
                        topMargin: 32
                        bottomMargin: 20
                    }
                    clip: true
                    contentWidth: Math.max(width, generalLayout.width)
                    contentHeight: generalLayout.height + Theme.space12
                    flickableDirection: Flickable.AutoFlickIfNeeded
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AlwaysOn
                        visible: generalFlick.contentHeight > generalFlick.height
                    }
                    // PS-WRAP: การ์ดพื้นหลังของหน้า
                    Rectangle {
                        anchors { left: generalLayout.left; right: parent.right; top: generalLayout.top; bottom: generalLayout.bottom; margins: -Theme.space6; rightMargin: Theme.space6 }
                        z: -1
                        radius: Theme.radiusCard
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.border
                    }
                    ColumnLayout {
                        id: generalLayout
                        anchors {
                            top: parent.top
                            topMargin: Theme.space6
                            left: parent.left
                            leftMargin: Theme.space8
                        }
                        spacing: 10
                        GridLayout {
                                                        columns: 3
                            rowSpacing: 14
                            columnSpacing: 24

                            // PS-WRAP: window behavior
                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Hide To Tray On Close")
                            }
                            C.CheckBox {
                                id: hideToTrayCheck
                                firstInFocusChain: true
                                checked: Chiaki.window.hideToTray
                                onToggled: Chiaki.window.hideToTray = !Chiaki.window.hideToTray
                                text: qsTr("Close button hides PS-WRAP to the system tray (Quit from tray menu)")
                            }
                            Label { font.pixelSize: Theme.fontCaption; color: Theme.textMuted; text: qsTr("(Off)") }

                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Always On Top")
                            }
                            C.CheckBox {
                                checked: Chiaki.window.alwaysOnTop
                                onToggled: Chiaki.window.alwaysOnTop = !Chiaki.window.alwaysOnTop
                                text: qsTr("Keep the window above other windows (main menu and stream)")
                            }
                            Label { font.pixelSize: Theme.fontCaption; color: Theme.textMuted; text: qsTr("(Off)") }

                            // PS-WRAP: facecam — preview สด (WebcamOverlay ตัวเดียวกับตอนสตรีม: mirror/shape/zoom/key ตามจริง)
                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                                Layout.topMargin: Theme.space1
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Facecam Preview")
                            }
                            Item {
                                id: camPreviewBox
                                readonly property bool onGeneralPage: bar.currentIndex === 0 && dialog.visible
                                Layout.preferredWidth: dialog.controlWidth
                                Layout.preferredHeight: camPrefs.camCircle ? dialog.controlWidth : Math.round(dialog.controlWidth * 9 / 16)
                                // พื้นลาย checker ให้เห็นส่วนโปร่งใสตอน chroma key
                                Rectangle {
                                    anchors.fill: parent
                                    radius: camPreview.cornerRadius
                                    color: Theme.bg
                                    border.width: 1
                                    border.color: Theme.border
                                }
                                WebcamOverlay {
                                    id: camPreview
                                    anchors.fill: parent
                                    active: camPreviewBox.onGeneralPage   // เปิดกล้องเฉพาะตอนอยู่หน้า General (ปิดหน้า = ปล่อย device)
                                    cameraId: camPrefs.camDevice
                                    mirror: camPrefs.camMirror
                                    circle: camPrefs.camCircle
                                    zoom: camPrefs.camZoom
                                    panX: camPrefs.camPanX
                                    panY: camPrefs.camPanY
                                    keyEnabled: Chiaki.window.camBackground === 1 || Chiaki.window.camBackground === 2
                                    aiEnabled: Chiaki.window.camBackground === 3
                                    fx: Chiaki.window.camFx
                                    keyColor: Chiaki.window.camBackground === 2 ? "#0000ff" : "#00ff00"
                                    keyTolerance: camPrefs.camKeyTol
                                }
                            }
                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                                Layout.topMargin: Theme.space1
                                font.pixelSize: Theme.fontCaption
                                color: Theme.textMuted
                                wrapMode: Text.WordWrap
                                Layout.maximumWidth: 260
                                text: camPreview.hasCamera ? qsTr("live · %1").arg(camPreview.cameraName) : qsTr("no camera connected")
                            }

                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Facecam Camera")
                            }
                            C.ComboBox {
                                id: camDeviceCombo
                                Layout.preferredWidth: dialog.controlWidth
                                readonly property var names: dialog.allCameraNames()
                                model: [qsTr("System default")].concat(names)
                                currentIndex: Math.max(0, names.indexOf(camPrefs.camDevice) + 1)
                                onActivated: index => camPrefs.camDevice = index > 0 ? names[index - 1] : ""
                                onVisibleChanged: if (visible) dshowLister.refreshDevices()
                            }
                            Label {
                                font.pixelSize: Theme.fontCaption
                                color: Theme.textMuted
                                text: camDeviceCombo.names.length ? qsTr("(System default) · %1 found · virtual cams (NVIDIA Broadcast / OBS) supported").arg(camDeviceCombo.names.length) : qsTr("(System default) · no camera connected")
                            }

                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Facecam Mirror")
                            }
                            C.CheckBox {
                                checked: camPrefs.camMirror
                                onToggled: camPrefs.camMirror = !camPrefs.camMirror
                                text: qsTr("Flip horizontally like a mirror")
                            }
                            Label { font.pixelSize: Theme.fontCaption; color: Theme.textMuted; text: qsTr("(On)") }

                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Facecam Shape")
                            }
                            C.ComboBox {
                                Layout.preferredWidth: dialog.controlWidth
                                model: [qsTr("Rounded"), qsTr("Circle")]
                                currentIndex: camPrefs.camCircle ? 1 : 0
                                onActivated: index => camPrefs.camCircle = index === 1
                            }
                            Label { font.pixelSize: Theme.fontCaption; color: Theme.textMuted; text: qsTr("(Rounded) · toggle in stream: Ctrl+Shift+C cam, Ctrl+Shift+V move") }

                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Facecam Zoom")
                            }
                            C.Slider {
                                id: camZoomSlider
                                Layout.preferredWidth: dialog.controlWidth
                                from: 1.0
                                to: 3.0
                                stepSize: 0.1
                                value: camPrefs.camZoom
                                onMoved: camPrefs.camZoom = value
                                Label {
                                    anchors.left: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    leftPadding: 10
                                    text: camZoomSlider.value.toFixed(1) + "x"
                                }
                            }
                            Label { font.pixelSize: Theme.fontCaption; color: Theme.textMuted; text: qsTr("(1.0x) · crop into the face; pan with WASD in move mode") }

                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Facecam Pan")
                            }
                            RowLayout {
                                Layout.preferredWidth: dialog.controlWidth
                                spacing: Theme.space3
                                enabled: camPrefs.camZoom > 1.0
                                Label { text: "X"; color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
                                C.Slider { Layout.fillWidth: true; from: -1; to: 1; stepSize: 0.05; value: camPrefs.camPanX; onMoved: camPrefs.camPanX = value }
                                Label { text: "Y"; color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
                                C.Slider { Layout.fillWidth: true; from: -1; to: 1; stepSize: 0.05; value: camPrefs.camPanY; onMoved: camPrefs.camPanY = value }
                            }
                            Label { font.pixelSize: Theme.fontCaption; color: Theme.textMuted; text: qsTr("(center) · only when zoomed") }

                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Facecam Background")
                            }
                            C.ComboBox {
                                Layout.preferredWidth: dialog.controlWidth
                                model: [qsTr("Keep"), qsTr("Remove green screen"), qsTr("Remove blue screen"), qsTr("AI remove (no green screen)")]
                                currentIndex: Chiaki.window.camBackground
                                onActivated: index => Chiaki.window.camBackground = index
                            }
                            Label {
                                font.pixelSize: Theme.fontCaption
                                color: Chiaki.window.camBackground === 3 && camPreview.aiError.length ? Theme.danger : Theme.textMuted
                                text: Chiaki.window.camBackground === 3
                                      ? (camPreview.aiError.length ? camPreview.aiError : (camPreview.aiActive ? qsTr("(Keep) · AI on CPU · %1 ms/frame").arg(camPreview.aiMs) : qsTr("(Keep) · loading AI model…")))
                                      : qsTr("(Keep) · chroma key needs a plain green/blue backdrop · AI works anywhere")
                            }

                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Facecam Key Tolerance")
                            }
                            C.Slider {
                                id: camKeyTolSlider
                                Layout.preferredWidth: dialog.controlWidth
                                enabled: Chiaki.window.camBackground > 0
                                from: 0.05
                                to: 0.6
                                stepSize: 0.01
                                value: camPrefs.camKeyTol
                                onMoved: camPrefs.camKeyTol = value
                                Label {
                                    anchors.left: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    leftPadding: 10
                                    text: camKeyTolSlider.value.toFixed(2)
                                }
                            }
                            Label { font.pixelSize: Theme.fontCaption; color: Theme.textMuted; text: Chiaki.window.camBackground === 3 ? qsTr("(0.25) · AI edge softness") : qsTr("(0.25) · higher removes more; lower keeps edges") }

                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Facecam Effect")
                            }
                            C.ComboBox {
                                Layout.preferredWidth: dialog.controlWidth
                                model: [qsTr("None"), qsTr("Sunglasses"), qsTr("Mustache"), qsTr("Clown nose"), qsTr("Crown"), qsTr("Bane mask"), qsTr("Party (glasses + mustache + crown)"), qsTr("Samurai mask"), qsTr("Ninja"), qsTr("Ghost (Tsushima)"), qsTr("Samurai armor (kabuto + mask)"), qsTr("Samurai armor (photo)"), qsTr("Jin mask (private)"), qsTr("Jin mask + headband (private)")]
                                currentIndex: Chiaki.window.camFx
                                onActivated: index => Chiaki.window.camFx = index
                            }
                            Label {
                                font.pixelSize: Theme.fontCaption
                                color: Chiaki.window.camFx > 0 && camPreview.fxError.length ? Theme.danger : Theme.textMuted
                                text: Chiaki.window.camFx > 0 ? (camPreview.fxError.length ? camPreview.fxError : (camPreview.fxActive ? (camPreview.fxMesh ? qsTr("(None) · face mesh 478 pts · %1 ms").arg(camPreview.fxMs) : qsTr("(None) · basic tracking (face_mesh.onnx missing)")) : qsTr("(None) · looking for a face…"))) : qsTr("(None) · AI face tracking · F in move mode")
                            }

                            // PS-WRAP: อัดคลิป — โฟลเดอร์ปลายทาง (Ctrl+Shift+R หรือปุ่ม Record ในเมนูสตรีม)
                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Recording Folder")
                            }
                            C.TextField {
                                id: recFolderField
                                Layout.preferredWidth: dialog.controlWidth
                                text: Chiaki.window.recordingFolder
                                placeholderText: qsTr("Videos\\PS-WRAP")
                                onEditingFinished: {
                                    const path = text.trim();
                                    if (path.length && path !== Chiaki.window.recordingFolder)
                                        Chiaki.window.recordingFolder = path;
                                    else if (!path.length)
                                        text = Chiaki.window.recordingFolder;
                                }
                            }
                            RowLayout {
                                spacing: Theme.space2
                                C.Button {
                                    id: recBrowseButton
                                    text: qsTr("Browse…")
                                    onClicked: recFolderDialog.open()
                                }
                                C.Button {
                                    text: qsTr("Open folder")
                                    onClicked: Chiaki.window.openRecordingsFolder()
                                }
                            }

                            Item { Layout.preferredWidth: dialog.labelWidth; Layout.preferredHeight: 1 }
                            Label {
                                Layout.columnSpan: 2
                                Layout.preferredWidth: dialog.controlWidth + 260
                                Layout.maximumWidth: dialog.controlWidth + 260
                                Layout.topMargin: -Theme.space2
                                wrapMode: Text.WordWrap
                                font.pixelSize: Theme.fontCaption
                                color: Theme.textMuted
                                text: qsTr("Recordings capture exactly what is on screen, including every visible overlay (pad, facecam, stats, mic spectrum), plus game audio and your microphone while it is unmuted. HDR streams are recorded as HDR (HEVC 10-bit). Toggle in stream: Ctrl+Shift+R or Record in the stream menu.")
                            }

                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Action On Disconnect")
                            }

                        C.ComboBox {
                            id: disconnectAction
                            Layout.preferredWidth: dialog.controlWidth
                            model: [qsTr("Do Nothing"), qsTr("Enter Sleep Mode"), qsTr("Ask")]
                            currentIndex: Chiaki.settings.disconnectAction
                            onActivated: index => Chiaki.settings.disconnectAction = index
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(Ask)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Action On Suspend")
                        }

                        C.ComboBox {
                            Layout.preferredWidth: dialog.controlWidth
                            model: [qsTr("Do Nothing"), qsTr("Enter Sleep Mode")]
                            currentIndex: Chiaki.settings.suspendAction
                            onActivated: index => Chiaki.settings.suspendAction = index
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(Do Nothing)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Steam Deck Haptics")
                            visible: (typeof Chiaki.settings.steamDeckHaptics !== "undefined")
                        }

                        C.CheckBox {
                            text: qsTr("True haptics for SteamDeck, better quality but noisier")
                            checked: {
                                if(typeof Chiaki.settings.steamDeckHaptics !== "undefined")
                                    Chiaki.settings.steamDeckHaptics
                                else
                                    false
                            }
                            onToggled: Chiaki.settings.steamDeckHaptics = checked
                            visible: (typeof Chiaki.settings.steamDeckHaptics !== "undefined")
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(Unchecked)")
                            visible: (typeof Chiaki.settings.steamDeckHaptics !== "undefined")
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Steam Deck Vertical")
                            visible: typeof Chiaki.settings.verticalDeck !== "undefined"
                        }

                        C.CheckBox {
                            text: qsTr("Use Steam Deck in vertical orientation (motion controls)")
                            checked: {
                                if(typeof Chiaki.settings.verticalDeck !== "undefined")
                                    Chiaki.settings.verticalDeck
                                else
                                    false
                            }
                            onToggled: Chiaki.settings.verticalDeck = checked
                            visible: typeof Chiaki.settings.verticalDeck !== "undefined"
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(Unchecked)")
                            visible: typeof Chiaki.settings.verticalDeck !== "undefined"
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Audio/Video")
                        }

                        C.ComboBox {
                            Layout.preferredWidth: dialog.controlWidth
                            model: [qsTr("Audio and Video Enabled"), qsTr("Audio Disabled"), qsTr("Video Disabled"), qsTr("Audio and Video Disabled")]
                            currentIndex: Chiaki.settings.audioVideoDisabled
                            onActivated: index => Chiaki.settings.audioVideoDisabled = index
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(Audio and Video Enabled)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Log Directory")
                        }

                        Label {
                            // PS-WRAP: กว้างเท่า control อื่น (เดิม 600 ดันทั้งคอลัมน์ล้นจอเล็ก) — ฟอนต์ย่อให้พอดีด้วย HorizontalFit
                            Layout.preferredWidth: dialog.controlWidth
                            Layout.maximumWidth: dialog.controlWidth
                            text: Chiaki.settings.logDirectory
                            verticalAlignment: Text.AlignVCenter
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: 10

                            C.Button {
                                id: openButton
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: parent.paintedWidth + 20
                                }
                                text: qsTr("Open")
                                onClicked: Qt.openUrlExternally("file://" + parent.text);
                                Material.roundedScale: Material.SmallScale
                            }
                        }
                        Label {

                        }
                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Streamer Mode (Hides Info)")
                        }
                        C.CheckBox {
                            id: streamerMode
                            checked: Chiaki.settings.streamerMode
                            onToggled: Chiaki.settings.streamerMode = !Chiaki.settings.streamerMode
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(Unchecked)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Stream Menu Shortcut Enabled")
                        }
                        C.CheckBox {
                            id: streamMenu
                            checked: Chiaki.settings.streamMenuEnabled
                            onToggled: Chiaki.settings.streamMenuEnabled = !Chiaki.settings.streamMenuEnabled
                            KeyNavigation.priority: KeyNavigation.BeforeItem
                            KeyNavigation.up: streamerMode
                            KeyNavigation.left: streamMenu
                            KeyNavigation.right: streamMenu
                            KeyNavigation.down: {
                                if(streamMenuShortcut1.visible)
                                    streamMenuShortcut1
                                else
                                    streamMenu
                            }
                        }

                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                font.pixelSize: Theme.fontCaption
                                color: Theme.textMuted
                                text: qsTr("(Checked)")
                            }
                        }
                        RowLayout {
                            spacing: 10
                            visible: Chiaki.settings.streamMenuEnabled
                            Layout.alignment: Qt.AlignHCenter
                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Stream Menu Combo")
                            }

                        C.ComboBox {
                            id: streamMenuShortcut1
                            implicitContentWidthPolicy: ComboBox.WidestText
                            firstInFocusChain: false
                            model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                            currentIndex: Chiaki.settings.streamMenuShortcut1
                            onActivated: index => Chiaki.settings.streamMenuShortcut1 = index
                            KeyNavigation.priority: {
                                if(!popup.visible)
                                    KeyNavigation.BeforeItem
                                else
                                    KeyNavigation.AfterItem
                            }
                            KeyNavigation.up: streamMenu
                            KeyNavigation.down: streamMenuShortcut1
                            KeyNavigation.left: streamMenuShortcut1
                            KeyNavigation.right: streamMenuShortcut2
                        }

                        C.ComboBox {
                            id: streamMenuShortcut2
                            implicitContentWidthPolicy: ComboBox.WidestText
                            firstInFocusChain: false
                            model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                            currentIndex: Chiaki.settings.streamMenuShortcut2
                            onActivated: index => Chiaki.settings.streamMenuShortcut2 = index
                            KeyNavigation.priority: {
                                if(!popup.visible)
                                    KeyNavigation.BeforeItem
                                else
                                    KeyNavigation.AfterItem
                            }
                            KeyNavigation.up: streamMenu
                            KeyNavigation.down: streamMenuShortcut2
                            KeyNavigation.left: streamMenuShortcut1
                            KeyNavigation.right: streamMenuShortcut3
                        }

                        C.ComboBox {
                            id: streamMenuShortcut3
                            implicitContentWidthPolicy: ComboBox.WidestText
                            firstInFocusChain: false
                            model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                            currentIndex: Chiaki.settings.streamMenuShortcut3
                            onActivated: index => Chiaki.settings.streamMenuShortcut3 = index
                            KeyNavigation.priority: {
                                if(!popup.visible)
                                    KeyNavigation.BeforeItem
                                else
                                    KeyNavigation.AfterItem
                            }
                            KeyNavigation.up: streamMenu
                            KeyNavigation.down: streamMenuShortcut3
                            KeyNavigation.left: streamMenuShortcut2
                            KeyNavigation.right: streamMenuShortcut4
                        }

                        C.ComboBox {
                            id: streamMenuShortcut4
                            implicitContentWidthPolicy: ComboBox.WidestText
                            firstInFocusChain: false
                            model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                            currentIndex: Chiaki.settings.streamMenuShortcut4
                            onActivated: index => Chiaki.settings.streamMenuShortcut4 = index
                            KeyNavigation.priority: {
                                if(!popup.visible)
                                    KeyNavigation.BeforeItem
                                else
                                    KeyNavigation.AfterItem
                            }
                            KeyNavigation.up: streamMenu
                            KeyNavigation.down: streamMenuShortcut4
                            KeyNavigation.left: streamMenuShortcut3
                            KeyNavigation.right: streamMenuShortcut4
                        }

                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                font.pixelSize: Theme.fontCaption
                                color: Theme.textMuted
                                text: qsTr("(L1+R1+L3+R3)")
                            }
                        }
                    }
                }
            }

            Item {
                // Video
                Flickable {
                    id: videoFlick
                    anchors {
                        fill: parent
                        topMargin: 32
                        bottomMargin: 20
                    }
                    clip: true
                    contentWidth: Math.max(width, videoGrid.width)
                    contentHeight: videoGrid.height + Theme.space12
                    flickableDirection: Flickable.AutoFlickIfNeeded
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AlwaysOn
                        visible: videoFlick.contentHeight > videoFlick.height
                    }
                    // PS-WRAP: การ์ดพื้นหลังของหน้า
                    Rectangle {
                        anchors { left: videoGrid.left; right: parent.right; top: videoGrid.top; bottom: videoGrid.bottom; margins: -Theme.space6; rightMargin: Theme.space6 }
                        z: -1
                        radius: Theme.radiusCard
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.border
                    }
                    GridLayout {
                        id: videoGrid
                        anchors {
                            top: parent.top
                            topMargin: Theme.space6
                            left: parent.left
                            leftMargin: Theme.space8
                        }
                        columns: 3
                        rowSpacing: 14
                        columnSpacing: 24

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Hardware Decoder")
                        }

                    RowLayout {
                        Layout.preferredWidth: dialog.controlWidth
                        spacing: 12

                        C.ComboBox {
                            id: hwDecoderCombo
                            Layout.preferredWidth: Math.round(dialog.controlWidth * 0.55)
                            model: Chiaki.settings.availableDecoders
                            currentIndex: Math.max(0, model.indexOf(Chiaki.settings.decoder))
                            KeyNavigation.priority: KeyNavigation.BeforeItem
                            KeyNavigation.up: hwDecoderCombo
                            KeyNavigation.right: zeroCopyCheck
                            KeyNavigation.down: windowTypeCombo
                            onActivated: (index) => Chiaki.settings.decoder = index ? model[index] : ""
                        }

                        Label {
                            text: qsTr("Zero-Copy")
                        }

                        C.CheckBox {
                            id: zeroCopyCheck
                            KeyNavigation.priority: KeyNavigation.BeforeItem
                            KeyNavigation.up: zeroCopyCheck
                            KeyNavigation.left: hwDecoderCombo
                            KeyNavigation.down: windowTypeCombo
                            checked: Chiaki.settings.useZeroCopy
                            onToggled: Chiaki.settings.useZeroCopy = checked
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textMuted
                        text: qsTr("(Auto)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.preferredWidth: dialog.labelWidth
                        Layout.maximumWidth: dialog.labelWidth
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        text: qsTr("Window Type")
                    }

                    C.ComboBox {
                        id: windowTypeCombo
                        Layout.preferredWidth: dialog.controlWidth
                        popup.width: 500
                        model: [qsTr("Stream Resolution"), qsTr("Custom Resolution"), qsTr("Adjust Resolution Manually"), qsTr("Fullscreen"), qsTr("Zoom [adjust zoom using slider in stream menu]"), qsTr("Stretch")]
                        currentIndex: Chiaki.settings.windowType
                        onActivated: (index) => Chiaki.settings.windowType = index;
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textMuted
                        text: qsTr("(Fullscreen)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.preferredWidth: dialog.labelWidth
                        Layout.maximumWidth: dialog.labelWidth
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        text: qsTr("Custom Resolution Width")
                        visible: Chiaki.settings.windowType == 1
                    }

                    C.TextField {
                        id: customResolutionWidth
                        Layout.preferredWidth: dialog.controlWidth
                        visible: Chiaki.settings.windowType == 1
                        text: Chiaki.settings.customResolutionWidth
                        Material.accent: text && !validate() ? Material.Red : undefined
                        onEditingFinished: {
                            if (validate()) {
                                Chiaki.settings.customResolutionWidth = parseInt(text);
                            } else {
                                Chiaki.settings.customResolutionWidth = 0;
                                text = "";
                            }
                        }
                        function validate() {
                            var num = parseInt(text);
                            return num >= 0 && num <= 9999;
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textMuted
                        text: qsTr("(1920)")
                        visible: Chiaki.settings.windowType == 1
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.preferredWidth: dialog.labelWidth
                        Layout.maximumWidth: dialog.labelWidth
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        text: qsTr("Custom Resolution Height")
                        visible: Chiaki.settings.windowType == 1
                    }

                    C.TextField {
                        id: customResolutionHeight
                        Layout.preferredWidth: dialog.controlWidth
                        visible: Chiaki.settings.windowType == 1
                        text: Chiaki.settings.customResolutionHeight
                        Material.accent: text && !validate() ? Material.Red : undefined
                        onEditingFinished: {
                            if (validate()) {
                                Chiaki.settings.customResolutionHeight = parseInt(text);
                            } else {
                                Chiaki.settings.customResolutionHeight = 0;
                                text = "";
                            }
                        }
                        function validate() {
                            var num = parseInt(text);
                            return num >= 0 && num <= 9999;
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textMuted
                        text: qsTr("(1080)")
                        visible: Chiaki.settings.windowType == 1
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.preferredWidth: dialog.labelWidth
                        Layout.maximumWidth: dialog.labelWidth
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        text: qsTr("Toggle Fullscreen on Double-click")
                    }

                    C.CheckBox {
                        checked: Chiaki.settings.fullscreenDoubleClick
                        onToggled: Chiaki.settings.fullscreenDoubleClick = checked
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textMuted
                        text: qsTr("(Unchecked)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.preferredWidth: dialog.labelWidth
                        Layout.maximumWidth: dialog.labelWidth
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        text: qsTr("Hide Cursor during Stream")
                    }
                    C.CheckBox {
                        checked: Chiaki.settings.hideCursor
                        onToggled: Chiaki.settings.hideCursor = !Chiaki.settings.hideCursor
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textMuted
                        text: qsTr("(Checked)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.preferredWidth: dialog.labelWidth
                        Layout.maximumWidth: dialog.labelWidth
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        text: qsTr("Vertical Sync")
                    }

                    C.CheckBox {
                        checked: Chiaki.settings.vSyncEnabled
                        onClicked: {
                            Chiaki.settings.vSyncEnabled = checked
                            if (Chiaki.window.runtimeRendererBackend === 1 && Chiaki.settings.restartApplication())
                                Qt.quit()
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textMuted
                        text: qsTr("(Unchecked)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.preferredWidth: dialog.labelWidth
                        Layout.maximumWidth: dialog.labelWidth
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        text: qsTr("Render Preset")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: Math.round(dialog.controlWidth * 1.3)
                        model: [qsTr("Fast"), qsTr("Default"), qsTr("High Quality"), qsTr("High Quality + Spatial Upscaling"), qsTr("High Quality + Advanced Spatial Upscaling"), qsTr("Custom")]
                        currentIndex: Chiaki.settings.videoPreset
                        onActivated: (index) => {
                            Chiaki.settings.videoPreset = index;
                            switch (index) {
                            case 0: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.Fast; break;
                            case 1: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.Default; break;
                            case 2: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.HighQuality; break;
                            case 3: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.HighQualitySpatial; break;
                            case 4: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.HighQualityAdvancedSpatial; break;
                            case 5: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.Custom; break;
                            }
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textMuted
                        text: qsTr("(High Quality)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.preferredWidth: dialog.labelWidth
                        Layout.maximumWidth: dialog.labelWidth
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        text: qsTr("Frame Delivery")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: dialog.controlWidth
                        model: [qsTr("Direct Mapping"), qsTr("Frame Queue")]
                        currentIndex: Chiaki.settings.directFrameMapping ? 0 : 1
                        onActivated: (index) => {
                            const direct = index === 0;
                            if (direct === Chiaki.settings.directFrameMapping)
                                return;
                            Chiaki.settings.directFrameMapping = direct;
                            if (Chiaki.settings.restartApplication())
                                Qt.quit();
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textMuted
                        text: qsTr("(Direct Mapping)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.preferredWidth: dialog.labelWidth
                        Layout.maximumWidth: dialog.labelWidth
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        text: qsTr("Frame Mixer")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: dialog.controlWidth
                        model: [qsTr("None"), qsTr("Oversample"), qsTr("Hermite"), qsTr("Linear"), qsTr("Cubic")]
                        enabled: !Chiaki.settings.directFrameMapping
                        currentIndex: Chiaki.settings.directFrameMapping ? 0 : Chiaki.settings.placeboFrameMixer
                        onActivated: index => Chiaki.settings.placeboFrameMixer = index
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textMuted
                        text: qsTr("(None)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.preferredWidth: dialog.labelWidth
                        Layout.maximumWidth: dialog.labelWidth
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        text: qsTr("Renderer Backend")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: dialog.controlWidth
                        model: [qsTr("Vulkan"), qsTr("OpenGL")]
                        currentIndex: Chiaki.settings.rendererBackend
                        onActivated: (index) => {
                            if (index === Chiaki.settings.rendererBackend)
                                return;
                            Chiaki.settings.rendererBackend = index;
                            if (Chiaki.settings.restartApplication())
                                Qt.quit();
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textMuted
                        text: qsTr("(Vulkan)")
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.preferredWidth: dialog.labelWidth
                        Layout.maximumWidth: dialog.labelWidth
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        text: qsTr("Vulkan Deferred Swap")
                        visible: Chiaki.settings.rendererBackend == 0
                    }

                    C.CheckBox {
                        checked: Chiaki.settings.vulkanDeferredSwap
                        onToggled: Chiaki.settings.vulkanDeferredSwap = checked
                        visible: Chiaki.settings.rendererBackend == 0
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textMuted
                        text: qsTr("(Unchecked)")
                        visible: Chiaki.settings.rendererBackend == 0
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.preferredWidth: dialog.labelWidth
                        Layout.maximumWidth: dialog.labelWidth
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        text: qsTr("Custom Renderer Settings")
                        visible: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom
                    }

                    C.Button {
                        id: customRendererSettings
                        text: qsTr("Open")
                        onClicked: root.showPlaceboSettingsDialog()
                        Material.roundedScale: Material.SmallScale
                        visible: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom
                    }

                    Label { visible: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.preferredWidth: dialog.labelWidth
                        Layout.maximumWidth: dialog.labelWidth
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        text: qsTr("Display Settings")
                    }

                    C.Button {
                        id: displaySettings
                        text: qsTr("Open")
                        onClicked: root.showDisplaySettingsDialog()
                        Material.roundedScale: Material.SmallScale
                        lastInFocusChain: true
                    }

                    Label {}
                    }
                }
            }

            Item {
                // Stream
                Flickable {
                    id: streamFlick
                    anchors {
                        fill: parent
                        topMargin: 32
                        bottomMargin: 20
                    }
                    clip: true
                    contentWidth: Math.max(width, streamGrid.width)
                    contentHeight: streamGrid.height + Theme.space12
                    flickableDirection: Flickable.AutoFlickIfNeeded
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AlwaysOn
                        visible: streamFlick.contentHeight > streamFlick.height
                    }
                    // PS-WRAP: การ์ดพื้นหลังของหน้า
                    Rectangle {
                        anchors { left: streamGrid.left; right: parent.right; top: streamGrid.top; bottom: streamGrid.bottom; margins: -Theme.space6; rightMargin: Theme.space6 }
                        z: -1
                        radius: Theme.radiusCard
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.border
                    }
                    GridLayout {
                        id: streamGrid
                        anchors {
                            top: parent.top
                            topMargin: Theme.space6
                            left: parent.left
                            leftMargin: Theme.space8
                        }
                        columns: 3
                        rowSpacing: 14
                        columnSpacing: 24

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Settings for")
                        }


                    C.ComboBox {
                        id: consoleSelection
                        Layout.preferredWidth: dialog.controlWidth
                        Layout.alignment: Qt.AlignLeft
                        model: [qsTr("PS4"), qsTr("PS5")]
                        currentIndex: selectedConsole
                        onActivated: (index) => selectedConsole = index
                        firstInFocusChain: true
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                        KeyNavigation.down: {
                            if(selectedConsole == SettingsDialog.Console.PS4)
                                resolutionLocalPS4
                            else
                                resolutionLocalPS5

                        }

                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("")
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("")
                    }

                    Label {
                        Layout.alignment: Qt.AlignCenter
                        text: qsTr("Local")
                    }

                    Label {
                        Layout.alignment: Qt.AlignCenter
                        text: qsTr("Remote")
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.preferredWidth: dialog.labelWidth
                        Layout.maximumWidth: dialog.labelWidth
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        text: qsTr("Resolution")
                    }

                    C.ComboBox {
                        id: resolutionLocalPS4
                        Layout.preferredWidth: dialog.controlWidth
                        model: [qsTr("360p"), qsTr("540p"), qsTr("720p (Default)"), qsTr("1080p (PS5 and PS4 Pro)")]
                        currentIndex: Chiaki.settings.resolutionLocalPS4 - 1
                        onActivated: (index) => {
                            Chiaki.settings.resolutionLocalPS4 = index + 1
                            Chiaki.settings.bitrateLocalPS4 = 0
                        }
                        visible: selectedConsole == SettingsDialog.Console.PS4
                        KeyNavigation.right: resolutionRemotePS4
                        KeyNavigation.down: fpsLocalPS4
                        KeyNavigation.up: consoleSelection
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                    C.ComboBox {
                        id: resolutionRemotePS4
                        Layout.preferredWidth: dialog.controlWidth
                        model: [qsTr("360p"), qsTr("540p"), qsTr("720p (Default)"), qsTr("1080p (PS5 and PS4 Pro)")]
                        currentIndex: Chiaki.settings.resolutionRemotePS4 - 1
                        onActivated: (index) => {
                            Chiaki.settings.resolutionRemotePS4 = index + 1
                            Chiaki.settings.bitrateRemotePS4 = 0
                        }
                        visible: selectedConsole == SettingsDialog.Console.PS4
                        KeyNavigation.left: resolutionLocalPS4
                        KeyNavigation.down: fpsRemotePS4
                        KeyNavigation.up: consoleSelection
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                    C.ComboBox {
                        id: resolutionLocalPS5
                        Layout.preferredWidth: dialog.controlWidth
                        model: [qsTr("360p"), qsTr("540p"), qsTr("720p"), qsTr("1080p (Default)")]
                        currentIndex: Chiaki.settings.resolutionLocalPS5 - 1
                        onActivated: (index) => {
                            Chiaki.settings.resolutionLocalPS5 = index + 1
                            Chiaki.settings.bitrateLocalPS5 = 0
                        }
                        visible: selectedConsole == SettingsDialog.Console.PS5
                        KeyNavigation.right: resolutionRemotePS5
                        KeyNavigation.up: consoleSelection
                        KeyNavigation.down: fpsLocalPS5
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                    C.ComboBox {
                        id: resolutionRemotePS5
                        Layout.preferredWidth: dialog.controlWidth
                        model: [qsTr("360p"), qsTr("540p"), qsTr("720p (Default)"), qsTr("1080p")]
                        currentIndex: Chiaki.settings.resolutionRemotePS5 - 1
                        onActivated: (index) => {
                            Chiaki.settings.resolutionRemotePS5 = index + 1
                            Chiaki.settings.bitrateRemotePS5 = 0
                        }
                        visible: selectedConsole == SettingsDialog.Console.PS5
                        KeyNavigation.left: resolutionLocalPS5
                        KeyNavigation.up: consoleSelection
                        KeyNavigation.down: fpsRemotePS5
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.preferredWidth: dialog.labelWidth
                        Layout.maximumWidth: dialog.labelWidth
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        text: qsTr("FPS")
                    }

                    C.ComboBox {
                        id: fpsLocalPS4
                        Layout.preferredWidth: dialog.controlWidth
                        model: [qsTr("30 fps"), qsTr("60 fps (Default)")]
                        currentIndex: (Chiaki.settings.fpsLocalPS4 / 30) - 1
                        onActivated: (index) => Chiaki.settings.fpsLocalPS4 = (index + 1) * 30
                        visible: selectedConsole == SettingsDialog.Console.PS4
                        KeyNavigation.up: resolutionLocalPS4
                        KeyNavigation.right: fpsRemotePS4
                        KeyNavigation.down: bitrateLocalPS4
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                    C.ComboBox {
                        id: fpsRemotePS4
                        Layout.preferredWidth: dialog.controlWidth
                        model: [qsTr("30 fps"), qsTr("60 fps (Default)")]
                        currentIndex: (Chiaki.settings.fpsRemotePS4 / 30) - 1
                        onActivated: (index) => Chiaki.settings.fpsRemotePS4 = (index + 1) * 30
                        visible: selectedConsole == SettingsDialog.Console.PS4
                        KeyNavigation.up: resolutionRemotePS4
                        KeyNavigation.left: fpsLocalPS4
                        KeyNavigation.down: bitrateRemotePS4
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                    C.ComboBox {
                        id: fpsLocalPS5
                        Layout.preferredWidth: dialog.controlWidth
                        model: [qsTr("30 fps"), qsTr("60 fps (Default)")]
                        currentIndex: (Chiaki.settings.fpsLocalPS5 / 30) - 1
                        onActivated: (index) => Chiaki.settings.fpsLocalPS5 = (index + 1) * 30
                        visible: selectedConsole == SettingsDialog.Console.PS5
                        KeyNavigation.up: resolutionLocalPS5
                        KeyNavigation.right: fpsRemotePS5
                        KeyNavigation.down: bitrateLocalPS5
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                    C.ComboBox {
                        id: fpsRemotePS5
                        Layout.preferredWidth: dialog.controlWidth
                        model: [qsTr("30 fps"), qsTr("60 fps (Default)")]
                        currentIndex: (Chiaki.settings.fpsRemotePS5 / 30) - 1
                        onActivated: (index) => Chiaki.settings.fpsRemotePS5 = (index + 1) * 30
                        visible: selectedConsole == SettingsDialog.Console.PS5
                        KeyNavigation.up: resolutionRemotePS5
                        KeyNavigation.left: fpsLocalPS5
                        KeyNavigation.down: bitrateRemotePS5
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.preferredWidth: dialog.labelWidth
                        Layout.maximumWidth: dialog.labelWidth
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        text: qsTr("Bitrate")
                    }

                    C.Slider {
                        id: bitrateLocalPS4
                        visible: selectedConsole == SettingsDialog.Console.PS4
                        property var bitrate: {
                            var rate = 0;
                            switch (Chiaki.settings.resolutionLocalPS4) {
                            case 1: rate = 2; break; // 360p
                            case 2: rate = 6; break; // 540p
                            case 3: rate = 10; break; // 720p
                            case 4: rate = 15; break; // 1080p
                            }
                            return rate;
                        }
                        Layout.preferredWidth: Math.round(dialog.controlWidth * 0.5)
                        from: 2
                        to: 100
                        stepSize: 1
                        value: Chiaki.settings.bitrateLocalPS4 / 1000 ? (Chiaki.settings.bitrateLocalPS4 / 1000) : bitrate
                        onMoved: Chiaki.settings.bitrateLocalPS4 = value * 1000;
                        KeyNavigation.up: fpsLocalPS4
                        KeyNavigation.down: bitrateLocalPS4
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: (parent.value) + qsTr(" Mbps") + qsTr(" (%1 Mbps)").arg(parent.bitrate.toFixed(0))
                        }
                    }

                    C.Slider {
                        id: bitrateRemotePS4
                        visible: selectedConsole == SettingsDialog.Console.PS4
                        property var bitrate: {
                            var rate = 0;
                            switch (Chiaki.settings.resolutionRemotePS4) {
                            case 1: rate = 2; break; // 360p
                            case 2: rate = 6; break; // 540p
                            case 3: rate = 10; break; // 720p
                            case 4: rate = 15; break; // 1080p
                            }
                            return rate;
                        }
                        Layout.preferredWidth: Math.round(dialog.controlWidth * 0.5)
                        from: 2
                        to: 100
                        stepSize: 1
                        value: Chiaki.settings.bitrateRemotePS4 / 1000 ? (Chiaki.settings.bitrateRemotePS4 / 1000) : bitrate
                        onMoved: Chiaki.settings.bitrateRemotePS4 = value * 1000;
                        KeyNavigation.up: fpsRemotePS4
                        KeyNavigation.down: bitrateRemotePS4
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        lastInFocusChain: true

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: (parent.value) + qsTr(" Mbps") + qsTr(" (%1 Mbps)").arg(parent.bitrate.toFixed(0))
                        }
                    }

                    C.Slider {
                        id: bitrateLocalPS5
                        visible: selectedConsole == SettingsDialog.Console.PS5
                        property var bitrate: {
                            var rate = 0;
                            switch (Chiaki.settings.resolutionLocalPS5) {
                            case 1: rate = 2; break; // 360p
                            case 2: rate = 6; break; // 540p
                            case 3: rate = 10; break; // 720p
                            case 4: rate = 15; break; // 1080p
                            }
                            return rate;
                        }
                        Layout.preferredWidth: Math.round(dialog.controlWidth * 0.5)
                        from: 2
                        to: 100
                        stepSize: 1
                        value: Chiaki.settings.bitrateLocalPS5 / 1000 ? (Chiaki.settings.bitrateLocalPS5 / 1000) : bitrate
                        onMoved: Chiaki.settings.bitrateLocalPS5 = value * 1000;
                        KeyNavigation.up: fpsLocalPS5
                        KeyNavigation.down: codecLocalPS5
                        KeyNavigation.priority: KeyNavigation.BeforeItem

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: (parent.value) + qsTr(" Mbps") + qsTr(" (%1 Mbps)").arg(parent.bitrate.toFixed(0))
                        }
                    }

                    C.Slider {
                        id: bitrateRemotePS5
                        visible: selectedConsole == SettingsDialog.Console.PS5
                        property var bitrate: {
                            var rate = 0;
                            switch (Chiaki.settings.resolutionRemotePS5) {
                            case 1: rate = 2; break; // 360p
                            case 2: rate = 6; break; // 540p
                            case 3: rate = 10; break; // 720p
                            case 4: rate = 15; break; // 1080p
                            }
                            return rate;
                        }
                        Layout.preferredWidth: Math.round(dialog.controlWidth * 0.5)
                        from: 2
                        to: 100
                        stepSize: 1
                        value: Chiaki.settings.bitrateRemotePS5 / 1000 ? (Chiaki.settings.bitrateRemotePS5 / 1000) : bitrate
                        onMoved: Chiaki.settings.bitrateRemotePS5 = value * 1000;
                        KeyNavigation.up: fpsRemotePS5
                        KeyNavigation.down: codecRemotePS5
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        lastInFocusChain: true

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: (parent.value) + qsTr(" Mbps") + qsTr(" (%1 Mbps)").arg(parent.bitrate.toFixed(0))
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                        Layout.preferredWidth: dialog.labelWidth
                        Layout.maximumWidth: dialog.labelWidth
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        text: qsTr("Codec")
                        visible: selectedConsole == SettingsDialog.Console.PS5
                    }

                    C.ComboBox {
                        id: codecLocalPS5
                        Layout.preferredWidth: dialog.controlWidth
                        property bool openGlBackend: Chiaki.settings.rendererBackend === 1
						model: openGlBackend ? [qsTr("H264"), qsTr("H265 (Default)")] : [qsTr("H264"), qsTr("H265 (Default)"), qsTr("H265 HDR")]
                        currentIndex: Chiaki.settings.codecLocalPS5
                        onActivated: (index) => Chiaki.settings.codecLocalPS5 = index
                        visible: selectedConsole == SettingsDialog.Console.PS5
                        Keys.onReturnPressed: {
                            if (popup.visible) {
                                activated(highlightedIndex);
                                popup.close();
                            } else
                                popup.open();
                        }
                        KeyNavigation.up: bitrateLocalPS5
                        KeyNavigation.right: codecRemotePS5
                        KeyNavigation.down: codecLocalPS5
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }

                        C.ComboBox {
                            id: codecRemotePS5
                            Layout.preferredWidth: dialog.controlWidth
                        property bool openGlBackend: Chiaki.settings.rendererBackend === 1
                        model: openGlBackend ? [qsTr("H264"), qsTr("H265 (Default)")] : [qsTr("H264"), qsTr("H265 (Default)"), qsTr("H265 HDR")]
                            currentIndex: Chiaki.settings.codecRemotePS5
                            onActivated: (index) => Chiaki.settings.codecRemotePS5 = index
                            visible: selectedConsole == SettingsDialog.Console.PS5
                            lastInFocusChain: true
                            Keys.onReturnPressed: {
                                if (popup.visible) {
                                    activated(highlightedIndex);
                                    popup.close();
                                } else
                                    popup.open();
                            }
                            KeyNavigation.up: bitrateRemotePS5
                            KeyNavigation.left: codecLocalPS5
                            KeyNavigation.priority: {
                                if(!popup.visible)
                                    KeyNavigation.BeforeItem
                                else
                                    KeyNavigation.AfterItem
                            }
                        }
                    }
                }
            }

            Item {
                // Audio and Wifi
                Flickable {
                    id: audiowifiFlick
                    anchors {
                        fill: parent
                        topMargin: 32
                        bottomMargin: 20
                    }
                    clip: true
                    contentWidth: Math.max(width, audiowifigrid.width)
                    contentHeight: audiowifigrid.height + Theme.space12
                    flickableDirection: Flickable.AutoFlickIfNeeded
                    ScrollBar.vertical: ScrollBar {
                        id: audiowifiScrollbar
                        policy: ScrollBar.AlwaysOn
                        visible: audiowifiFlick.contentHeight > audiowifiFlick.height
                    }
                    // PS-WRAP: การ์ดพื้นหลังของหน้า
                    Rectangle {
                        anchors { left: audiowifigrid.left; right: parent.right; top: audiowifigrid.top; bottom: audiowifigrid.bottom; margins: -Theme.space6; rightMargin: Theme.space6 }
                        z: -1
                        radius: Theme.radiusCard
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.border
                    }
                    GridLayout {
                        id: audiowifigrid
                        columns: 3
                        rowSpacing: 14
                        columnSpacing: 24
                        onVisibleChanged: if (visible) Chiaki.settings.refreshAudioDevices()

                        anchors {
                            top: parent.top
                            topMargin: Theme.space6
                            left: parent.left
                            leftMargin: Theme.space8
                        }
                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Output Device")
                        }

                        C.ComboBox {
                            id: audioOutDevice
                            Layout.preferredWidth: dialog.controlWidth
                            popup.x: (width - popup.width) / 2
                            popup.width: 700
                            popup.font.pixelSize: 16
                            firstInFocusChain: true
                            model: [qsTr("Auto")].concat(Chiaki.settings.availableAudioOutDevices)
                            currentIndex: Math.max(0, model.indexOf(Chiaki.settings.audioOutDevice))
                            onActivated: (index) => Chiaki.settings.audioOutDevice = index ? model[index] : ""
                            KeyNavigation.down: audioInDevice
                            KeyNavigation.priority: popup.visible ? KeyNavigation.AfterItem : KeyNavigation.BeforeItem
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(Auto)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Input Device")
                        }

                        C.ComboBox {
                            id: audioInDevice
                            Layout.preferredWidth: dialog.controlWidth
                            popup.x: (width - popup.width) / 2
                            popup.width: 700
                            popup.font.pixelSize: 16
                            model: [qsTr("Auto")].concat(Chiaki.settings.availableAudioInDevices)
                            currentIndex: Math.max(0, model.indexOf(Chiaki.settings.audioInDevice))
                            onActivated: (index) => Chiaki.settings.audioInDevice = index ? model[index] : ""
                            KeyNavigation.up: audioOutDevice
                            KeyNavigation.down: audioBufferSizeSlider
                            KeyNavigation.priority: popup.visible ? KeyNavigation.AfterItem : KeyNavigation.BeforeItem
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(Auto)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Audio Buffer Size")
                        }

                        C.Slider {
                            id: audioBufferSizeSlider
                            Layout.preferredWidth: Math.round(dialog.controlWidth * 0.625)
                            from: 1
                            to: 10
                            stepSize: 1
                            value: Chiaki.settings.audioBufferSize / 1920 ? (Chiaki.settings.audioBufferSize / 1920) : 5
                            onMoved: Chiaki.settings.audioBufferSize = value * 1920;
                            KeyNavigation.up: audioInDevice

                            Label {
                                anchors {
                                    left: parent.right
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 10
                                }
                                text: {
                                    (parent.value * 10).toFixed(0) + qsTr(" ms")
                                }
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(50 ms)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Audio Volume")
                        }

                        C.Slider {
                            Layout.preferredWidth: Math.round(dialog.controlWidth * 0.625)
                            from: 0
                            to: 128
                            stepSize: 1
                            value: Chiaki.settings.audioVolume
                            onMoved: Chiaki.settings.audioVolume = value

                            Label {
                                anchors {
                                    left: parent.right
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 10
                                }
                                text: {
                                    ((parent.value / 128.0) * 100).toFixed(0) + qsTr("% volume")
                                }
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(100%)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Start Mic Unmuted")
                        }

                        C.CheckBox {
                            checked: Chiaki.settings.startMicUnmuted
                            onToggled: Chiaki.settings.startMicUnmuted = checked
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(Unchecked)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Speech Processing")
                            visible: typeof Chiaki.settings.speechProcessing !== "undefined"
                        }

                        C.CheckBox {
                            text: qsTr("Noise suppression + echo cancellation")
                            checked: Chiaki.settings.speechProcessing
                            onToggled: Chiaki.settings.speechProcessing = !Chiaki.settings.speechProcessing
                            visible: typeof Chiaki.settings.speechProcessing !== "undefined"
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(Unchecked)")
                            visible: typeof Chiaki.settings.speechProcessing !== "undefined"
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Noise To Suppress")
                            visible: if (typeof Chiaki.settings.speechProcessing !== "undefined") {Chiaki.settings.speechProcessing} else {false}
                        }

                        C.Slider {
                            Layout.preferredWidth: Math.round(dialog.controlWidth * 0.625)
                            from: 0
                            to: 60
                            stepSize: 1
                            visible: if (typeof Chiaki.settings.speechProcessing !== "undefined") {Chiaki.settings.speechProcessing} else {false}
                            value: Chiaki.settings.noiseSuppressLevel
                            onMoved: Chiaki.settings.noiseSuppressLevel = value

                            Label {
                                anchors {
                                    left: parent.right
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 10
                                }
                                text: qsTr("%1 dB").arg(parent.value)
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(6 dB)")
                            visible: if (typeof Chiaki.settings.speechProcessing !== "undefined") {Chiaki.settings.speechProcessing} else {false}
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Echo To Suppress")
                            visible: if (typeof Chiaki.settings.speechProcessing !== "undefined") {Chiaki.settings.speechProcessing} else {false}
                        }

                        C.Slider {
                            Layout.preferredWidth: Math.round(dialog.controlWidth * 0.625)
                            from: 0
                            to: 60
                            stepSize: 1
                            value: Chiaki.settings.echoSuppressLevel
                            visible: if (typeof Chiaki.settings.speechProcessing !== "undefined") {Chiaki.settings.speechProcessing} else {false}
                            onMoved: Chiaki.settings.echoSuppressLevel = value

                            Label {
                                anchors {
                                    left: parent.right
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 10
                                }
                                text: qsTr("%1 dB").arg(parent.value)
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(30 dB)")
                            visible: if (typeof Chiaki.settings.speechProcessing !== "undefined") {Chiaki.settings.speechProcessing} else {false}
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Weak Wifi Notification")
                        }

                        C.Slider {
                            Layout.preferredWidth: Math.round(dialog.controlWidth * 0.625)
                            from: 0
                            to: 100
                            stepSize: 1
                            value: Chiaki.settings.wifiDroppedNotif
                            onMoved: Chiaki.settings.wifiDroppedNotif = value

                            Label {
                                anchors {
                                    left: parent.right
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 10
                                }
                                text: qsTr(">= %1% dropped packets").arg(parent.value)
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(3%)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Packet Loss Reported Max")
                        }

                        C.Slider {
                            Layout.preferredWidth: Math.round(dialog.controlWidth * 0.625)
                            from: 0
                            to: 100
                            stepSize: 1
                            value: Chiaki.settings.packetLossReportedMax
                            onMoved: Chiaki.settings.packetLossReportedMax = value

                            Label {
                                anchors {
                                    left: parent.right
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 10
                                }
                                text: qsTr("%1% packet loss").arg(parent.value)
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(5%)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Request IDR Frame on FEC Failure")
                        }

                        C.CheckBox {
                            checked: Chiaki.settings.iDROnFECFailureEnabled
                            onToggled: Chiaki.settings.iDROnFECFailureEnabled = !Chiaki.settings.iDROnFECFailureEnabled
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(Unchecked)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Show Stream Stats During Gameplay")
                        }

                        C.CheckBox {
                            lastInFocusChain: true
                            checked: Chiaki.settings.showStreamStats
                            onToggled: Chiaki.settings.showStreamStats = !Chiaki.settings.showStreamStats
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(Unchecked)")
                        }
                    }
                }
            }

            Item {
                // Consoles
                Flickable {
                    id: consolesFlick
                    anchors {
                        fill: parent
                        topMargin: 32
                        bottomMargin: 20
                    }
                    clip: true
                    contentWidth: Math.max(width, consolesLayout.width)
                    contentHeight: consolesLayout.height + Theme.space12
                    flickableDirection: Flickable.AutoFlickIfNeeded
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AlwaysOn
                        visible: consolesFlick.contentHeight > consolesFlick.height
                    }
                    // PS-WRAP: การ์ดพื้นหลังของหน้า
                    Rectangle {
                        anchors { left: consolesLayout.left; right: parent.right; top: consolesLayout.top; bottom: consolesLayout.bottom; margins: -Theme.space6; rightMargin: Theme.space6 }
                        z: -1
                        radius: Theme.radiusCard
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.border
                    }
                    ColumnLayout {
                        id: consolesLayout
                        anchors {
                            top: parent.top
                            topMargin: Theme.space6
                            left: parent.left
                            leftMargin: Theme.space8
                        }
                        spacing: 10

                        Label {
                            text: ""
                        }

                        Label {
                            text: ""
                        }

                        C.Button {
                            id: registerNewButton
                            Layout.alignment: Qt.AlignHCenter
                            Layout.topMargin: 10
                            topPadding: 26
                            leftPadding: 30
                            rightPadding: 30
                            bottomPadding: 26
                            firstInFocusChain: true
                            text: qsTr("Register New")
                            onClicked: root.showRegistDialog("255.255.255.255", true)
                            Material.roundedScale: Material.SmallScale
                        }

                        Label {
                            id: consolesLabel
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Registered Consoles")
                            font.bold: true
                        }

                        ListView {
                            id: consolesView
                            Layout.alignment: Qt.AlignHCenter
                            height: 170
                            onCountChanged: {
                                consolesView.contentHeight = consolesView.count * 80 + consolesView.anchors.topMargin;
                            }
                            keyNavigationEnabled: false
                            width: 700
                            ScrollBar.vertical: ScrollBar {
                                id: consolesScrollbar
                                policy: ScrollBar.AlwaysOn
                                visible: consolesView.contentHeight > consolesView.height
                            }
                            clip: true
                            model: Chiaki.settings.registeredHosts
                            delegate: ItemDelegate {
                                text: "%1 (%2, %3)".arg(Chiaki.settings.streamerMode ? "hidden" : modelData.mac).arg(modelData.ps5 ? "PS5" : "PS4").arg(modelData.name)
                                height: 80
                                width: parent ? parent.width : 0
                                leftPadding: autoConnectButton.width + 40

                                CheckBox {
                                    property bool firstInFocusChain: false
                                    property bool lastInFocusChain: false
                                    property bool lastDownInFocusChain: index > consolesView.count + hiddenConsolesView.count - 2

                                    id: autoConnectButton
                                    anchors {
                                        left: parent.left
                                        verticalCenter: parent.verticalCenter
                                        leftMargin: 20
                                    }
                                    text: qsTr("Auto-Connect")
                                    checked: Chiaki.settings.autoConnectMac == modelData.mac
                                    onToggled: Chiaki.settings.autoConnectMac = checked ? modelData.mac : "";

                                    Keys.onPressed: (event) => {
                                        switch (event.key) {
                                        case Qt.Key_Right:
                                            if (!lastInFocusChain) {
                                                let item = nextItemInFocusChain();
                                                if (item)
                                                    item.forceActiveFocus(Qt.TabFocusReason);
                                                event.accepted = true;
                                            }
                                            break;
                                        case Qt.Key_Up:
                                            if (!firstInFocusChain) {
                                                let item = nextItemInFocusChain(false);
                                                if (item) {
                                                    item.forceActiveFocus(Qt.TabFocusReason);
                                                    dialog.ensureItemVisibleInFlick(consolesView, item);
                                                }
                                                let count = index > 0 ? 2: 0;
                                                for(var i = 0; i < count; i++)
                                                {
                                                    let item2 = item.nextItemInFocusChain(false);
                                                    if (item)
                                                    {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        dialog.ensureItemVisibleInFlick(consolesView, item);
                                                        item = item2;
                                                    }
                                                }
                                                event.accepted = true;
                                            }
                                            break;
                                        case Qt.Key_Down:
                                            if (!lastDownInFocusChain) {
                                                let item = nextItemInFocusChain();
                                                if (item) {
                                                    item.forceActiveFocus(Qt.TabFocusReason);
                                                    dialog.ensureItemVisibleInFlick(consolesView, item);
                                                }
                                                let count = 2;
                                                for(var i = 0; i < count; i++)
                                                {
                                                    let item2 = item.nextItemInFocusChain();
                                                    if (item)
                                                    {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        dialog.ensureItemVisibleInFlick(consolesView, item);
                                                        item = item2;
                                                    }
                                                }
                                                event.accepted = true;
                                            }
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

                                Button {
                                    property bool firstInFocusChain: false
                                    property bool lastInFocusChain: index > consolesView.count + hiddenConsolesView.count - 2
                                    Material.background: visualFocus ? Material.accent : undefined

                                    Component.onDestruction: {
                                        if (visualFocus) {
                                            let item = nextItemInFocusChain();
                                            if (item)
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                        }
                                    }
                                    Keys.onPressed: (event) => {
                                        switch (event.key) {
                                            case Qt.Key_Left:
                                                if (!firstInFocusChain) {
                                                    let item = nextItemInFocusChain(false);
                                                    if (item)
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                    event.accepted = true;
                                                }
                                                break;
                                            case Qt.Key_Up:
                                                if (!firstInFocusChain)
                                                {
                                                    let item = nextItemInFocusChain(false);
                                                    if (item) {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        dialog.ensureItemVisibleInFlick(consolesView, item);
                                                    }
                                                    let count = 2;
                                                    for(var i = 0; i < count; i++)
                                                    {
                                                        let item2 = item.nextItemInFocusChain(false);
                                                        if (item)
                                                        {
                                                            item.forceActiveFocus(Qt.TabFocusReason);
                                                            dialog.ensureItemVisibleInFlick(consolesView, item);
                                                            item = item2;
                                                        }
                                                    }
                                                    event.accepted = true;
                                                }
                                                break;
                                            case Qt.Key_Down:
                                                if (!lastInFocusChain) {
                                                    let item = nextItemInFocusChain();
                                                    if (item) {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        dialog.ensureItemVisibleInFlick(consolesView, item);
                                                    }
                                                    let count = index < consolesView.count - 1 ? 2: 0;
                                                    for(var i = 0; i < count; i++)
                                                    {
                                                        let item2 = item.nextItemInFocusChain();
                                                        if (item)
                                                        {
                                                            item.forceActiveFocus(Qt.TabFocusReason);
                                                            dialog.ensureItemVisibleInFlick(consolesView, item);
                                                            item = item2;
                                                        }
                                                    }
                                                    event.accepted = true;
                                                }
                                                break;
                                            case Qt.Key_Return:
                                                if (visualFocus) {
                                                    clicked();
                                                }
                                                event.accepted = true;
                                                break;
                                        }
                                    }
                                    anchors {
                                        right: parent.right
                                        verticalCenter: parent.verticalCenter
                                        rightMargin: 20
                                    }
                                    text: qsTr("Delete")
                                    onClicked: root.showConfirmDialog(qsTr("Delete Console"), qsTr("Are you sure you want to delete this console?"), () => Chiaki.settings.deleteRegisteredHost(index));
                                    Material.roundedScale: Material.SmallScale
                                    Material.accent: Material.Red
                                }
                            }
                        }

                        Label {
                            id: hiddenConsolesLabel
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("Hidden Consoles")
                            font.bold: true
                        }
                        ListView {
                            id: hiddenConsolesView
                            Layout.alignment: Qt.AlignHCenter
                            keyNavigationEnabled: false
                            height: 170
                            onCountChanged: {
                                hiddenConsolesView.contentHeight = hiddenConsolesView.count * 80 + hiddenConsolesView.anchors.topMargin;
                            }
                            clip: true
                            width: 500
                            ScrollBar.vertical: ScrollBar {
                                id: hiddenConsolesScrollbar
                                policy: ScrollBar.AlwaysOn
                                visible: hiddenConsolesView.contentHeight > hiddenConsolesView.height
                            }
                            model: Chiaki.hiddenHosts
                            delegate: ItemDelegate {
                                text: "%1 (%2)".arg(Chiaki.settings.streamerMode ? "hidden" : modelData.mac).arg(modelData.name)
                                height: 80
                                width: parent ? parent.width : 0

                                Button {
                                    property bool firstInFocusChain: false
                                    property bool lastInFocusChain: index > hiddenConsolesView.count - 2
                                    Material.background: visualFocus ? Material.accent : undefined

                                    Component.onDestruction: {
                                        if (visualFocus) {
                                            let item = nextItemInFocusChain();
                                            if (item)
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                        }
                                    }
                                    Keys.onPressed: (event) => {
                                        switch (event.key) {
                                            case Qt.Key_Up:
                                                if (!firstInFocusChain)
                                                {
                                                    let item = nextItemInFocusChain(false);
                                                    if (item) {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        dialog.ensureItemVisibleInFlick(hiddenConsolesView, item);
                                                    }
                                                    event.accepted = true;
                                                }
                                                break;
                                            case Qt.Key_Down:
                                                if (!lastInFocusChain) {
                                                    let item = nextItemInFocusChain();
                                                    if (item) {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        dialog.ensureItemVisibleInFlick(hiddenConsolesView, item);
                                                    }
                                                    event.accepted = true;
                                                }
                                                break;
                                            case Qt.Key_Return:
                                                if (visualFocus) {
                                                    clicked();
                                                }
                                                event.accepted = true;
                                                break;
                                        }
                                    }
                                    anchors {
                                        right: parent.right
                                        verticalCenter: parent.verticalCenter
                                        rightMargin: 20
                                    }
                                    text: qsTr("Unhide")
                                    onClicked: root.showConfirmDialog(qsTr("Unhide Console"), qsTr("Are you sure you want to unhide this console?"), () => Chiaki.unhideHost(modelData.mac));
                                    Material.roundedScale: Material.SmallScale
                                    Material.accent: Material.Red
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Keys — PS-WRAP: หน้าใหม่แยกไฟล์ (SettingsKeysPage.qml)
                id: controllerMapping
                SettingsKeysPage {
                    id: keysPage
                    anchors.fill: parent
                    uiScale: dialog.uiScale
                    keyDialog: keyDialog
                }
            }

            Item {
                // Controllers
                Flickable {
                    id: controllersFlick
                    anchors {
                        fill: parent
                        topMargin: 32
                        bottomMargin: 20
                    }
                    clip: true
                    contentWidth: Math.max(width, controllersLayout.width)
                    contentHeight: controllersLayout.height + Theme.space12
                    flickableDirection: Flickable.AutoFlickIfNeeded
                    ScrollBar.vertical: ScrollBar {
                        id: controllersScrollbar
                        policy: ScrollBar.AlwaysOn
                        visible: controllersFlick.contentHeight > controllersFlick.height
                    }
                    // PS-WRAP: การ์ดพื้นหลังของหน้า
                    Rectangle {
                        anchors { left: controllersLayout.left; right: parent.right; top: controllersLayout.top; bottom: controllersLayout.bottom; margins: -Theme.space6; rightMargin: Theme.space6 }
                        z: -1
                        radius: Theme.radiusCard
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.border
                    }
                    ColumnLayout {
                        id: controllersLayout
                        anchors {
                            top: parent.top
                            topMargin: Theme.space6
                            left: parent.left
                            leftMargin: Theme.space8
                        }
                        spacing: 10
                        C.Button {
                            Layout.alignment: Qt.AlignHCenter
                            id: controllerMappingChange
                            firstInFocusChain: true
                            text: "Change Controller Mapping"
                            onClicked: controllerMappingDialog.show({
                                reset: false
                            });
                        }
                        C.Button {
                            Layout.alignment: Qt.AlignHCenter
                            id: controllerMappingReset
                            text: "Reset Controller Mapping"
                            onClicked: controllerMappingDialog.show({
                                reset: true
                            });
                        }

                        RowLayout {
                            spacing: 10
                            Layout.alignment: Qt.AlignHCenter
                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Background Controller Events")
                            }
                            C.CheckBox {
                                id: backgroundController
                                text: qsTr("Process controller input when application is in background")
                                checked: {
                                    Chiaki.settings.allowJoystickBackgroundEvents
                                }
                                onToggled: Chiaki.settings.allowJoystickBackgroundEvents = checked
                            }

                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                font.pixelSize: Theme.fontCaption
                                color: Theme.textMuted
                                text: qsTr("(Checked)")
                            }
                        }
                        RowLayout {
                            spacing: 10
                            Layout.alignment: Qt.AlignHCenter
                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Dpad Touchpad Emulation")
                            }
                            C.CheckBox {
                                id: dpadTouch
                                checked: Chiaki.settings.dpadTouchEnabled
                                onToggled: Chiaki.settings.dpadTouchEnabled = !Chiaki.settings.dpadTouchEnabled
                                KeyNavigation.priority: KeyNavigation.BeforeItem
                                KeyNavigation.up: backgroundController
                                KeyNavigation.left: dpadTouch
                                KeyNavigation.right: dpadTouch
                                KeyNavigation.down: {
                                    if(touchIncrement.visible)
                                        touchIncrement
                                    else
                                        posButtons
                                }
                            }

                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                font.pixelSize: Theme.fontCaption
                                color: Theme.textMuted
                                text: qsTr("(Checked)")
                            }
                        }

                        RowLayout {
                            id: touchIncrementLayout
                            spacing: 10
                            Layout.alignment: Qt.AlignHCenter
                            visible: Chiaki.settings.dpadTouchEnabled
                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Dpad Touch Increment")
                            }

                            C.Slider {
                                id: touchIncrement
                                Layout.preferredWidth: Math.round(dialog.controlWidth * 0.625)
                                from: 1
                                to: 1079
                                stepSize: 1
                                value: Chiaki.settings.dpadTouchIncrement
                                onMoved: Chiaki.settings.dpadTouchIncrement = value

                                Label {
                                    anchors {
                                        left: parent.right
                                        verticalCenter: parent.verticalCenter
                                        leftMargin: 10
                                    }
                                    text: qsTr("%1 mm").arg(parent.value / 100)
                                }
                                KeyNavigation.priority: KeyNavigation.BeforeItem
                                KeyNavigation.up: dpadTouch
                                KeyNavigation.down: {
                                    if(dpadShortcut1.visible)
                                        dpadShortcut1
                                    else
                                        posButtons;
                                }
                            }

                            Label {
                                Layout.alignment: Qt.AlignRight
                                Layout.leftMargin: 100
                                text: qsTr("(0.3 mm)")
                            }
                        }
                        RowLayout {
                            spacing: 10
                            visible: Chiaki.settings.dpadTouchEnabled
                            Layout.alignment: Qt.AlignHCenter
                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Dpad Regular/Touch Combo")
                            }

                            C.ComboBox {
                                id: dpadShortcut1
                                implicitContentWidthPolicy: ComboBox.WidestText
                                firstInFocusChain: false
                                model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                                currentIndex: Chiaki.settings.dpadTouchShortcut1
                                onActivated: index => Chiaki.settings.dpadTouchShortcut1 = index
                                KeyNavigation.priority: {
                                    if(!popup.visible)
                                        KeyNavigation.BeforeItem
                                    else
                                        KeyNavigation.AfterItem
                                }
                                KeyNavigation.up: touchIncrement
                                KeyNavigation.down: posButtons
                                KeyNavigation.left: dpadShortcut1
                                KeyNavigation.right: dpadShortcut2
                            }

                            C.ComboBox {
                                id: dpadShortcut2
                                implicitContentWidthPolicy: ComboBox.WidestText
                                firstInFocusChain: false
                                model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                                currentIndex: Chiaki.settings.dpadTouchShortcut2
                                onActivated: index => Chiaki.settings.dpadTouchShortcut2 = index
                                KeyNavigation.priority: {
                                    if(!popup.visible)
                                        KeyNavigation.BeforeItem
                                    else
                                        KeyNavigation.AfterItem
                                }
                                KeyNavigation.up: touchIncrement
                                KeyNavigation.down: posButtons
                                KeyNavigation.left: dpadShortcut1
                                KeyNavigation.right: dpadShortcut3
                            }

                            C.ComboBox {
                                id: dpadShortcut3
                                implicitContentWidthPolicy: ComboBox.WidestText
                                firstInFocusChain: false
                                model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                                currentIndex: Chiaki.settings.dpadTouchShortcut3
                                onActivated: index => Chiaki.settings.dpadTouchShortcut3 = index
                                KeyNavigation.priority: {
                                    if(!popup.visible)
                                        KeyNavigation.BeforeItem
                                    else
                                        KeyNavigation.AfterItem
                                }
                                KeyNavigation.up: touchIncrement
                                KeyNavigation.down: posButtons
                                KeyNavigation.left: dpadShortcut2
                                KeyNavigation.right: dpadShortcut4
                            }

                            C.ComboBox {
                                id: dpadShortcut4
                                implicitContentWidthPolicy: ComboBox.WidestText
                                firstInFocusChain: false
                                model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                                currentIndex: Chiaki.settings.dpadTouchShortcut4
                                onActivated: index => Chiaki.settings.dpadTouchShortcut4 = index
                                KeyNavigation.priority: {
                                    if(!popup.visible)
                                        KeyNavigation.BeforeItem
                                    else
                                        KeyNavigation.AfterItem
                                }
                                KeyNavigation.up: touchIncrement
                                KeyNavigation.down: posButtons
                                KeyNavigation.left: dpadShortcut3
                                KeyNavigation.right: dpadShortcut4
                            }

                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                font.pixelSize: Theme.fontCaption
                                color: Theme.textMuted
                                text: qsTr("(L1+R1+dpad Up)")
                            }
                        }
                        RowLayout {
                            spacing: 10
                            Layout.alignment: Qt.AlignHCenter
                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Buttons By Position")
                            }

                            C.CheckBox {
                                id: posButtons
                                text: qsTr("Use buttons by position instead of by label")
                                checked: Chiaki.settings.buttonsByPosition
                                onToggled: Chiaki.settings.buttonsByPosition = checked
                                KeyNavigation.priority: KeyNavigation.BeforeItem
                                KeyNavigation.up: {
                                    if(dpadShortcut1.visible)
                                        dpadShortcut1
                                    else
                                        dpadTouch;
                                }
                                KeyNavigation.down: rumbleHaptics
                                KeyNavigation.left: posButtons
                                KeyNavigation.right: posButtons
                            }

                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                font.pixelSize: Theme.fontCaption
                                color: Theme.textMuted
                                text: qsTr("(Unchecked)")
                            }
                        }
                        RowLayout {
                            spacing: 10
                            Layout.alignment: Qt.AlignHCenter
                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("Rumble Haptics")
                            }

                            C.ComboBox {
                                id: rumbleHaptics
                                Layout.preferredWidth: dialog.controlWidth
                                model: [qsTr("Off"), qsTr("Very Weak"), qsTr("Weak"), qsTr("Normal"), qsTr("Strong"), qsTr("Very Strong")]
                                currentIndex: Chiaki.settings.rumbleHapticsIntensity
                                onActivated: (index) => Chiaki.settings.rumbleHapticsIntensity = index;
                            }

                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                font.pixelSize: Theme.fontCaption
                                color: Theme.textMuted
                                text: qsTr("(Normal)")
                            }
                        }
                        RowLayout {
                            spacing: 10
                            Layout.alignment: Qt.AlignHCenter
                            Label {
                                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                                Layout.preferredWidth: dialog.labelWidth
                                Layout.maximumWidth: dialog.labelWidth
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                text: qsTr("True Haptics Intensity")
                            }

                            C.Slider {
                                id: hapticOverride
                                Layout.preferredWidth: Math.round(dialog.controlWidth * 0.625)
                                from: 0
                                to: 2
                                stepSize: 0.1
                                value: Chiaki.settings.hapticOverride
                                onMoved: Chiaki.settings.hapticOverride = value;
                                lastInFocusChain: true
                                Label {
                                    anchors {
                                        left: parent.right
                                        verticalCenter: parent.verticalCenter
                                        leftMargin: 10
                                    }
                                    text: {
                                        if(parent.value > 0.99 && parent.value < 1.01)
                                            qsTr("console setting")
                                        else
                                            (parent.value * 100).toFixed(0) + qsTr(" % console setting")
                                    }
                                }
                            }

                            Label {
                                Layout.alignment: Qt.AlignRight
                                Layout.leftMargin: 250
                                text: qsTr("(console setting)")
                            }
                        }
                    }
                }
            }

            Item {
                // Remote (PSN and Hole Punching)
                Flickable {
                    id: remoteFlick
                    anchors {
                        fill: parent
                        topMargin: 32
                        bottomMargin: 20
                    }
                    clip: true
                    contentWidth: Math.max(width, remoteGrid.width)
                    contentHeight: remoteGrid.y + remoteGrid.height + Theme.space12
                    flickableDirection: Flickable.AutoFlickIfNeeded
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AlwaysOn
                        visible: remoteFlick.contentHeight > remoteFlick.height
                    }
                    // PS-WRAP: การ์ดพื้นหลังของหน้า
                    Rectangle {
                        anchors { left: remoteGrid.left; right: parent.right; top: remoteGrid.top; bottom: remoteGrid.bottom; margins: -Theme.space6; rightMargin: Theme.space6 }
                        z: -1
                        radius: Theme.radiusCard
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.border
                    }
                    GridLayout {
                        id: remoteGrid
                        anchors {
                            top: parent.top
                            horizontalCenter: parent.horizontalCenter
                            topMargin: 30
                        }
                        columns: 3
                        rowSpacing: 20
                        columnSpacing: 10

                        C.Button {
                            id: openPsnLogin
                            firstInFocusChain: visible
                            Layout.alignment: Qt.AlignHCenter
                            Layout.columnSpan: 3

                            text: qsTr("Login to PSN")
                            onClicked: {
                                root.showPSNTokenDialog(false)
                            }
                            Material.roundedScale: Material.SmallScale
                            visible: !Chiaki.settings.psnRefreshToken || !Chiaki.settings.psnAuthToken || !Chiaki.settings.psnAuthTokenExpiry || !Chiaki.settings.psnAccountId
                        }

                        C.Button {
                            id: resetPsnTokens
                            topPadding: 26
                            leftPadding: 30
                            rightPadding: 30
                            bottomPadding: 26
                            text: qsTr("Clear PSN Token")
                            firstInFocusChain: !openPsnLogin.visible
                            Layout.alignment: Qt.AlignHCenter
                            Layout.columnSpan: 3
                            onClicked: {
                                Chiaki.settings.psnRefreshToken = ""
                                Chiaki.settings.psnAuthToken = ""
                                Chiaki.settings.psnAuthTokenExpiry = ""
                                Chiaki.settings.psnAccountId = ""
                                openPsnLogin.forceActiveFocus(Qt.TabFocusReason);
                            }
                            Material.roundedScale: Material.SmallScale
                            visible: Chiaki.settings.psnRefreshToken && Chiaki.settings.psnAuthToken && Chiaki.settings.psnAuthTokenExpiry && Chiaki.settings.psnAccountId
                        }


                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Hole Punching Port Guessing")
                        }

                        C.CheckBox {
                            id: holePunchGuessingCheckbox
                            text: qsTr("Force STUN port guessing")
                            checked: Chiaki.settings.portGuessingEnabled
                            onToggled: Chiaki.settings.portGuessingEnabled = checked
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(Unchecked)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Port Guess Count")
                        }

                        C.Slider {
                            id: portGuessCountSlider
                            Layout.preferredWidth: Math.round(dialog.controlWidth * 0.625)
                            from: 0
                            to: 75
                            stepSize: 1
                            value: Chiaki.settings.portGuessCount
                            onMoved: Chiaki.settings.portGuessCount = value

                            Label {
                                anchors {
                                    left: parent.right
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 10
                                }
                                text: parent.value + qsTr(" guesses")
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            Layout.leftMargin: 100
                            text: qsTr("(75)")
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            Layout.preferredWidth: dialog.labelWidth
                            Layout.maximumWidth: dialog.labelWidth
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            text: qsTr("Port Guess Socket Count")
                        }

                        C.Slider {
                            id: portGuessSocketSlider
                            Layout.preferredWidth: Math.round(dialog.controlWidth * 0.625)
                            from: 0
                            to: 500
                            stepSize: 1
                            value: Chiaki.settings.portGuessSocketCount
                            onMoved: Chiaki.settings.portGuessSocketCount = value
                            lastInFocusChain: true

                            Label {
                                anchors {
                                    left: parent.right
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 10
                                }
                                text: parent.value + qsTr(" sockets")
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            text: qsTr("(250)")
                            Layout.leftMargin: 100
                        }
                    }
                }
            }

            Item {
                // Config (Options such as Import/Export and Logging)
                Flickable {
                    id: configFlick
                    anchors {
                        fill: parent
                        topMargin: 32
                        bottomMargin: 20
                    }
                    clip: true
                    contentWidth: Math.max(width, configGrid.width)
                    contentHeight: configGrid.y + configGrid.height + Theme.space12
                    flickableDirection: Flickable.AutoFlickIfNeeded
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AlwaysOn
                        visible: configFlick.contentHeight > configFlick.height
                    }
                    // PS-WRAP: การ์ดพื้นหลังของหน้า
                    Rectangle {
                        anchors { left: configGrid.left; right: parent.right; top: configGrid.top; bottom: configGrid.bottom; margins: -Theme.space6; rightMargin: Theme.space6 }
                        z: -1
                        radius: Theme.radiusCard
                        color: Theme.surface
                        border.width: 1
                        border.color: Theme.border
                    }
                    GridLayout {
                        id: configGrid
                        anchors {
                            top: parent.top
                            horizontalCenter: parent.horizontalCenter
                            topMargin: 30
                        }
                        columns: 1
                        rowSpacing: 20
                        columnSpacing: 10

                        Label {
                            text: {
                                if(Chiaki.settings.currentProfile)
                                    qsTr("Current Profile: ") + Chiaki.settings.currentProfile
                                else
                                    qsTr("Current Profile: default")
                            }
                        }

                        C.Button {
                            id: profile
                            firstInFocusChain: true
                            text: qsTr("Manage Profiles")
                            onClicked: {
                                root.showProfileDialog()
                            }
                            Material.roundedScale: Material.SmallScale
                        }

                    C.Button {
                        id: exportButton
                        text: qsTr("Export settings to file")
                        onClicked: {
                            Chiaki.settings.exportSettings();
                        }
                        Material.roundedScale: Material.SmallScale
                    }

                    C.Button {
                        id: importButton
                        text: qsTr("Import settings from file")
                        onClicked: {
                            Chiaki.settings.importSettings();
                        }
                        Material.roundedScale: Material.SmallScale
                    }

                    C.Button {
                        id: aboutButton
                        text: qsTr("About %1-ng").arg(Qt.application.name)
                        onClicked: aboutDialog.open()
                        Material.roundedScale: Material.SmallScale
                    }

                    C.CheckBox {
                        text: qsTr("Sanitize Logs (checked)")
                        checked: Chiaki.settings.logSanitize
                        onToggled: Chiaki.settings.logSanitize = checked
                    }

                        C.CheckBox {
                            text: qsTr("Verbose Logging (unchecked)")
                            checked: Chiaki.settings.logVerbose
                            lastInFocusChain: true
                            onToggled: Chiaki.settings.logVerbose = checked
                        }
                    }
                }
            }
        }

        Connections {
            target: dialog.Window.window
            function onActiveFocusItemChanged() {
                dialog.ensureActiveFocusVisible()
            }
        }

        Item {
            Timer {
                id: openTimer
                interval: 100
                running: false
                onTriggered: {
                    if(controllerMappingDialog.resetMapping)
                    {
                        if(!Chiaki.controllerMappingDefaultMapping)
                        {
                            quitControllerMapping = false;
                            Chiaki.controllerMappingReset();
                        }
                        controllerMappingDialog.close();
                    }
                    else
                    {
                        controllerMappingChange.forceActiveFocus(Qt.TabFocusReason);
                        root.showControllerMappingDialog();
                        quitControllerMapping = false;
                        controllerMappingDialog.resetFocus = false;
                        controllerMappingDialog.close();
                    }
                }
            }
        }

        Dialog {
            id: aboutDialog
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("About %1-ng").arg(Qt.application.name)
            modal: true
            standardButtons: Dialog.Ok
            Material.roundedScale: Material.MediumScale
            onAboutToHide: aboutButton.forceActiveFocus(Qt.TabFocusReason)

            RowLayout {
                spacing: 50
                onVisibleChanged: if (visible) forceActiveFocus(Qt.TabFocusReason)
                Keys.onReturnPressed: aboutDialog.close()
                Keys.onEscapePressed: aboutDialog.close()

                Image {
                    Layout.preferredWidth: Math.round(dialog.controlWidth * 0.5)
                    fillMode: Image.PreserveAspectFit
                    verticalAlignment: Image.AlignTop
                    source: "qrc:icons/chiaking-logo.svg"
                }

                Label {
                    Layout.preferredWidth: dialog.controlWidth
                    verticalAlignment: Text.AlignTop
                    wrapMode: Text.Wrap
                    text: "<h1>chiaki-ng</h1> by Street Pea, version %1
                        <h2>Fork of Chiaki</h2> by Florian Markl at version 2.1.1

                        <p>This program is free software: you can redistribute it and/or modify
                        it under the terms of the GNU Affero General Public License version 3
                        as published by the Free Software Foundation.</p>

                        <p>This program is distributed in the hope that it will be useful,
                        but WITHOUT ANY WARRANTY; without even the implied warranty of
                        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
                        GNU General Public License for more details.</p>".arg(Qt.application.version)
                }
            }
        }

        Dialog {
            id: keyDialog
            focus: false
            property int buttonValue
            property var buttonCallback
            property var keysIndex
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("Key Capture")
            modal: true
            standardButtons: Dialog.Close
            closePolicy: Popup.CloseOnPressOutside
            onOpened: keyLabel.forceActiveFocus(Qt.TabFocusReason)
            onClosed: {
                keysPage.focusMappingIndex(keysIndex);
                keyLabel.focus = false;
                focus = false;
            }
            Material.roundedScale: Material.MediumScale

            function show(opts) {
                buttonValue = opts.value;
                buttonCallback = opts.callback;
                keysIndex = opts.mappingIndex;
                open();
            }

            Label {
                id: keyLabel
                focus: true
                text: qsTr("Press any key to configure button or click close")
                Keys.onReleased: (event) => {
                    var name = Chiaki.settings.changeControllerKey(keyDialog.buttonValue, event.key);
                    keyDialog.buttonCallback(name);
                    keyDialog.close();
                }
            }
        }

        Dialog {
            id: controllerMappingDialog
            property bool resetFocus: true
            property bool resetMapping: false
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("Controller Capture")
            modal: true
            standardButtons: Dialog.Close
            closePolicy: Popup.CloseOnPressOutside
            onOpened: {
                controllerLabel.forceActiveFocus(Qt.TabFocusReason);
                Chiaki.beginControllerMapping(resetMapping);
            }
            onClosed: {
                if(resetFocus)
                {
                    if(resetMapping)
                        controllerMappingReset.forceActiveFocus(Qt.TabFocusReason);
                    else
                        controllerMappingChange.forceActiveFocus(Qt.TabFocusReason);
                    focus = false;
                }
                else
                {
                    resetFocus = true;
                    focus = false;
                }
                if(quitControllerMapping)
                    Chiaki.controllerMappingQuit();
                else
                    quitControllerMapping = true;
            }
            Material.roundedScale: Material.MediumScale

            function show(opts) {
                resetMapping = opts.reset;
                open();
            }
            Label {
                id: controllerLabel
                text: qsTr("Choose the controller by pressing any button on the controller")
            }
        }

        Dialog {
            id: steamControllerMappingDialog
            property bool resetMapping: false
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("Controller Managed by Steam")
            modal: true
            standardButtons: Dialog.Close
            closePolicy: Popup.NoAutoClose
            onOpened: {
                steamLabel.forceActiveFocus(Qt.TabFocusReason);
            }
            onClosed: {
                if(resetMapping)
                    controllerMappingReset.forceActiveFocus(Qt.TabFocusReason);
                else
                    controllerMappingChange.forceActiveFocus(Qt.TabFocusReason);
                focus = false;
            }
            Material.roundedScale: Material.MediumScale

            Label {
                id: steamLabel
                wrapMode: TextEdit.Wrap
                text: qsTr("This controller is managed by Steam.\nPlease use Steam to map controller or disable Steam Input for the controller before mapping here.")
                Keys.onReturnPressed: steamControllerMappingDialog.close();
                Keys.onEscapePressed: steamControllerMappingDialog.close();
            }
        }

        Connections {
            target: Chiaki

            function onControllerMappingInProgressChanged()
            {
                if(Chiaki.controllerMappingInProgress)
                    openTimer.start();
            }

            function onControllerMappingSteamControllerSelected()
            {
                controllerMappingDialog.resetFocus = false;
                quitControllerMapping = false;
                steamControllerMappingDialog.resetMapping = controllerMappingDialog.resetMapping;
                controllerMappingDialog.close();
                steamControllerMappingDialog.open();
            }
        }
    }
}
