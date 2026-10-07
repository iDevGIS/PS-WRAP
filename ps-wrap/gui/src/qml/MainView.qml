import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtCore
import QtMultimedia
import "controls" as C

import org.streetpea.chiaking

// PS-WRAP Phase 2: หน้าหลักใหม่ — console card, สถานะสี, Play เด่น, hint จอยรวมแถบล่าง, onboarding เป็น banner
// คงของ upstream: Keys ทั้งหมด, hostsView (id/currentItem/visible semantics), functions ใน delegate, settingsButton id
Pane {
    padding: 0
    id: consolePane

    // ---- onboarding: แทน RemindDialog ซ้อนกัน (Steam → PSN) ด้วย banner ไม่บล็อก ----
    readonly property bool canSteamShortcut: typeof Chiaki.createSteamShortcut === "function"
    readonly property bool psnLinked: Chiaki.settings.psnRefreshToken && Chiaki.settings.psnAuthToken && Chiaki.settings.psnAuthTokenExpiry && Chiaki.settings.psnAccountId
    readonly property bool showSteamSetup: Chiaki.settings.addSteamShortcutAsk && canSteamShortcut
    readonly property bool showPsnSetup: Chiaki.settings.remotePlayAsk && !psnLinked
    readonly property bool showSetupBanner: (showSteamSetup || showPsnSetup) && !Chiaki.autoConnect && !Chiaki.window.directStream

    function controllerKind(c) {
        if (!c) return "";
        if (c.dualSenseEdge) return "DualSense Edge";
        if (c.dualSense) return "DualSense";
        if (c.playStation) return qsTr("PlayStation controller");
        if (c.steamVirtual) return qsTr("Steam Input");
        if (c.handheld) return qsTr("Handheld");
        return c.name ? c.name : qsTr("Controller");
    }
    // ไอคอนตามรุ่น: DualSense/Edge → pad-dualsense, PS อื่น (DualShock 4) → pad-dualshock, handheld → pad-handheld, อื่นๆ → generic
    // PS4/PS5 ใช้ Kenney Input Prompts (CC0) — ดู gui/res/LICENSE-kenney-input-prompts.txt · Edge ใช้รูป PS5 + badge
    function controllerIcon(c) {
        if (!c) return "qrc:/icons/controller.svg";
        if (c.dualSense || c.dualSenseEdge) return "qrc:/icons/pad-ps5.svg";
        if (c.playStation) return "qrc:/icons/pad-ps4.svg";
        if (c.handheld) return "qrc:/icons/pad-handheld.svg";
        return "qrc:/icons/controller.svg";
    }
    function powerText(level) {
        switch (level) {
        case "wired": return qsTr("USB / wired");
        case "full": return qsTr("Battery full");
        case "medium": return qsTr("Battery medium");
        case "low": return qsTr("Battery low");
        case "empty": return qsTr("Battery empty");
        default: return qsTr("Battery unknown");
        }
    }
    function powerColor(level) {
        if (level === "low" || level === "empty") return Theme.danger;
        if (level === "medium") return Theme.warning;
        if (level === "unknown") return Theme.textMuted;
        return Theme.success;
    }

    // ---- หน้าสถานะ controller (กดจาก chip ในแถบล่าง) ----
    // แพตเทิร์นเดียวกับ ConfirmDialog ของ upstream (Dialog + x/y จาก root + เนื้อหาเป็น child) — พิสูจน์แล้วว่ากลางจอ
    // PS-WRAP: เครดิต / license (เปิดจากชิป About ที่แถบล่าง)
    AboutDialog {
        id: aboutDialog
        returnFocusTo: consolePane
    }

    Dialog {
        id: controllerPopup
        parent: Overlay.overlay
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        width: Math.min(root.width - Theme.screenMargin * 2, 760)
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: Theme.space6
        onOpened: { popupTick.restart(); Qt.callLater(function() { keyTrap.forceActiveFocus(); }); }
        onClosed: { popupTick.stop(); consolePane.forceActiveFocus(Qt.TabFocusReason); }
        background: Rectangle {
            color: Theme.surfaceRaised
            radius: Theme.radiusCard
            border.width: 1
            border.color: Theme.border
        }
        Overlay.modal: Rectangle { color: Theme.overlay }

        // รีเฟรชแบตทุก 3 วิ ระหว่างเปิด (powerLevel เป็น invokable ไม่มี notify)
        property int tick: 0
        Timer { id: popupTick; interval: 3000; repeat: true; onTriggered: controllerPopup.tick++ }

        ColumnLayout {
            anchors {
                left: parent.left
                right: parent.right
            }
            spacing: Theme.space4
            // กลืนคีย์/ปุ่มจอยทั้งหมดขณะ modal เปิด (โหมดทดสอบ: ✕ △ □ d-pad แค่ส่องใน preview) — ◯/Esc เท่านั้นที่ปิด
            // (บั๊กที่ลูกพี่เจอ: ✕ = Return หลุดไปสั่ง Play ที่ MainView)
            FocusScope {
                id: keyTrap
                focus: true
                // ถ้า focus หลุดระหว่าง modal เปิด (เช่นคลิกเมาส์) ดึงกลับ
                onActiveFocusChanged: if (!activeFocus && controllerPopup.opened) Qt.callLater(function() { keyTrap.forceActiveFocus(); })
                Layout.preferredHeight: 0
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Escape)
                        controllerPopup.close();
                    event.accepted = true;
                }
                Keys.onReleased: (event) => { event.accepted = true; }
            }

            RowLayout {
                Layout.fillWidth: true
                Image {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    sourceSize: Qt.size(28, 28)
                    source: "qrc:/icons/controller.svg"
                }
                Label {
                    text: qsTr("Controllers")
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: Chiaki.controllers.length > 0 ? qsTr("%n connected", "", Chiaki.controllers.length) : qsTr("none connected")
                    font.pixelSize: Theme.fontLabel
                    color: Theme.textMuted
                }
            }

            Label {
                visible: Chiaki.controllers.length === 0
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontBody
                color: Theme.textMuted
                text: qsTr("No controller detected. Connect a DualSense, DualShock or any SDL-compatible gamepad via USB or Bluetooth.")
            }

            Repeater {
                model: Chiaki.controllers
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    readonly property string level: { controllerPopup.tick; return modelData.powerLevel(); }
                    Layout.fillWidth: true
                    implicitHeight: padInfo.implicitHeight + Theme.space4 * 2
                    radius: Theme.radiusControl
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.border

                    RowLayout {
                        id: padInfo
                        anchors {
                            fill: parent
                            margins: Theme.space4
                        }
                        spacing: Theme.space4

                        Rectangle {
                            Layout.preferredWidth: 72
                            Layout.preferredHeight: 52
                            radius: Theme.radiusControl
                            color: Qt.rgba(0, 0.655, 1, 0.12)
                            Image {
                                anchors.centerIn: parent
                                width: 48; height: 48
                                sourceSize: Qt.size(48, 48)
                                fillMode: Image.PreserveAspectFit
                                source: consolePane.controllerIcon(modelData)
                            }
                            // badge EDGE (ไม่มีไอคอนทั้งตัวของ Edge ใน pack ที่ใช้ได้ถูกลิขสิทธิ์)
                            Rectangle {
                                visible: modelData.dualSenseEdge
                                anchors { right: parent.right; bottom: parent.bottom; margins: 3 }
                                radius: 4
                                color: Theme.accent
                                implicitWidth: edgeTag.implicitWidth + 6
                                implicitHeight: 14
                                Label { id: edgeTag; anchors.centerIn: parent; text: "EDGE"; font.pixelSize: 9; font.weight: Font.Bold; color: Theme.accentText }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space1
                            RowLayout {
                                spacing: Theme.space2
                                Label {
                                    text: (index + 1) + ".  " + consolePane.controllerKind(modelData)
                                    font.pixelSize: Theme.fontBody
                                    font.weight: Font.DemiBold
                                    color: Theme.text
                                }
                                Rectangle {
                                    visible: modelData.playStation
                                    radius: Theme.radiusChip
                                    color: Qt.rgba(0, 0.655, 1, 0.15)
                                    implicitHeight: 22
                                    implicitWidth: psTag.implicitWidth + Theme.space3 * 2
                                    Label { id: psTag; anchors.centerIn: parent; text: "PlayStation"; font.pixelSize: Theme.fontCaption; font.weight: Font.DemiBold; color: Theme.accent }
                                }
                                Rectangle {
                                    visible: modelData.steamVirtual
                                    radius: Theme.radiusChip
                                    color: Qt.rgba(1, 0.71, 0.28, 0.15)
                                    implicitHeight: 22
                                    implicitWidth: steamTag.implicitWidth + Theme.space3 * 2
                                    Label { id: steamTag; anchors.centerIn: parent; text: "Steam Input"; font.pixelSize: Theme.fontCaption; font.weight: Font.DemiBold; color: Theme.warning }
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                font.pixelSize: Theme.fontLabel
                                color: Theme.textMuted
                                text: {
                                    let parts = [];
                                    if (modelData.name) parts.push(modelData.name);
                                    if (modelData.vidpid) parts.push(modelData.vidpid);
                                    if (modelData.guid) parts.push(Chiaki.settings.streamerMode ? qsTr("GUID hidden") : modelData.guid);
                                    return parts.join("  ·  ");
                                }
                            }
                        }

                        // แบต / USB
                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            radius: Theme.radiusChip
                            color: Qt.rgba(consolePane.powerColor(level).r, consolePane.powerColor(level).g, consolePane.powerColor(level).b, 0.15)
                            implicitHeight: 28
                            implicitWidth: powerRow.implicitWidth + Theme.space3 * 2
                            RowLayout {
                                id: powerRow
                                anchors.centerIn: parent
                                spacing: Theme.space2
                                Rectangle { width: 8; height: 8; radius: 4; color: consolePane.powerColor(level) }
                                Label {
                                    text: consolePane.powerText(level)
                                    font.pixelSize: Theme.fontCaption
                                    font.weight: Font.DemiBold
                                    color: consolePane.powerColor(level)
                                }
                            }
                        }
                    }
                }
            }

            // ทดสอบสด: overlay เดียวกับที่โชว์ขณะสตรีม — กดปุ่ม/ขยับสติ๊กแล้วเห็นผลทันที
            Rectangle {
                visible: Chiaki.controllers.length > 0
                Layout.fillWidth: true
                implicitHeight: livePad.height + Theme.space4 * 2
                radius: Theme.radiusControl
                color: Theme.bg
                border.width: 1
                border.color: Theme.border
                ControllerOverlay {
                    id: livePad
                    anchors {
                        horizontalCenter: parent.horizontalCenter
                        top: parent.top
                        topMargin: Theme.space4
                    }
                    width: Math.min(parent.width - Theme.space4 * 2, 420)
                    overlayOpacity: 1.0
                }
                Label {
                    anchors { right: parent.right; top: parent.top; margins: Theme.space3 }
                    text: qsTr("Press buttons to test · ◯ / Esc closes")
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textMuted
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space2
                spacing: Theme.space3
                Label {
                    Layout.fillWidth: true
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textMuted
                    text: qsTr("Hot-plug supported: this list updates when you connect or disconnect a controller. The overlay above is what you see while streaming (toggle it from the stream menu).")
                    wrapMode: Text.WordWrap
                }
                C.Button {
                    text: qsTr("Map Buttons")
                    focusPolicy: Qt.NoFocus
                    visible: Chiaki.controllers.length > 0
                    onClicked: { controllerPopup.close(); root.showControllerMappingDialog(); }
                }
                C.Button {
                    id: closePopupButton
                    highlighted: true
                    focusPolicy: Qt.NoFocus
                    text: qsTr("Close")
                    icon.source: root.controllerButton("moon")
                    icon.width: 22
                    icon.height: 22
                    icon.color: "transparent"
                    onClicked: controllerPopup.close()
                }
            }
        }
    }

    // PS-WRAP: หน้าทดสอบไมค์ / facecam (กดจากชิปแถบล่าง) — สไตล์เดียวกับ controllerPopup
    MicPreviewDialog {
        id: micPreviewDialog
        returnFocusItem: consolePane
    }
    CamPreviewDialog {
        id: camPreviewDialog
        returnFocusItem: consolePane
        onCameraPicked: camChip.reload()
    }

    readonly property int visibleHostCount: {
        let n = 0;
        for (let i = 0; i < Chiaki.hosts.length; ++i)
            if (Chiaki.hosts[i].display)
                ++n;
        return n;
    }

    StackView.onActivated: {
        forceActiveFocus(Qt.TabFocusReason);
        // upstream เคลียร์ remotePlayAsk อัตโนมัติถ้าเชื่อม PSN แล้ว — คงไว้
        if (Chiaki.settings.remotePlayAsk && psnLinked)
            Chiaki.settings.remotePlayAsk = false;
    }
    Keys.onUpPressed: {
        if(hostsView.currentItem && hostsView.currentItem.visible)
        {
            hostsView.decrementCurrentIndex()
            while(!hostsView.currentItem.visible)
                hostsView.decrementCurrentIndex()
        }
    }
    Keys.onDownPressed: {
        if(hostsView.currentItem && hostsView.currentItem.visible)
        {
            hostsView.incrementCurrentIndex()
            while(!hostsView.currentItem.visible)
                 hostsView.incrementCurrentIndex()
        }
    }
    Keys.onMenuPressed: settingsButton.clicked()
    Keys.onReturnPressed: if (hostsView.currentItem) hostsView.currentItem.connectToHost()
    Keys.onYesPressed: if (hostsView.currentItem) hostsView.currentItem.wakeUpHost()
    Keys.onNoPressed: if (hostsView.currentItem) hostsView.currentItem.deleteHost()
    Keys.onEscapePressed: root.showConfirmDialog(qsTr("Quit"), qsTr("Are you sure you want to quit?"), () => Qt.quit())
    Keys.onPressed: (event) => {
        if (event.modifiers)
            return;
        switch (event.key) {
        case Qt.Key_PageUp:
            if (hostsView.currentItem) hostsView.currentItem.setConsolePin();
            event.accepted = true;
            break;
        case Qt.Key_PageDown:
            if (Chiaki.settings.psnAuthToken) Chiaki.refreshPsnToken();
            event.accepted = true;
            break;
        case Qt.Key_F1:
            if (canSteamShortcut) root.showSteamShortcutDialog(false);
            event.accepted = true;
            break;
        case Qt.Key_F2:
            root.showManualHostDialog();
            event.accepted = true;
            break;
        }
    }

    // ---- header ----
    RowLayout {
        id: header
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            leftMargin: Theme.screenMargin
            rightMargin: Theme.screenMargin
            topMargin: Theme.space6
        }
        height: 64
        spacing: Theme.space3

        Image {
            Layout.preferredWidth: 44
            Layout.preferredHeight: 44
            source: "qrc:/icons/pswrap.svg"
            sourceSize: Qt.size(44, 44)
            fillMode: Image.PreserveAspectFit
        }
        Label {
            text: "PS-WRAP"
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Bold
            font.letterSpacing: 1
            color: Theme.text
        }
        Item { Layout.fillWidth: true }

        C.Button {
            flat: true
            focusPolicy: Qt.NoFocus
            text: qsTr("Refresh PSN")
            icon.source: "qrc:/icons/r1.svg"
            icon.width: 24
            icon.height: 24
            icon.color: "transparent"   // PS-WRAP: glyph มีสีในตัว ห้าม tint
            visible: Chiaki.settings.psnAuthToken
            onClicked: Chiaki.refreshPsnToken()
        }
        // PS-WRAP: always on top (toggle chip) + hide to tray — ไม่ใช้ highlighted ของ flat button (กลายเป็นก้อนทึบ ตัวหนังสือหาย)
        component HeaderChip: Rectangle {
            id: chip
            property string iconSource
            property string iconActiveSource: iconSource
            property string label
            property bool checked: false
            property string tip
            signal clicked()
            radius: Theme.radiusChip
            implicitHeight: 40
            implicitWidth: chipRow.implicitWidth + Theme.space4 * 2
            color: checked ? Qt.rgba(0, 0.655, 1, 0.14) : (chipMouse.containsMouse ? Theme.surfaceRaised : "transparent")
            border.width: 1
            border.color: checked ? Theme.accent : (chipMouse.containsMouse ? Theme.border : "transparent")
            Behavior on color { ColorAnimation { duration: Theme.durFast } }
            Behavior on border.color { ColorAnimation { duration: Theme.durFast } }
            RowLayout {
                id: chipRow
                anchors.centerIn: parent
                spacing: Theme.space2
                Image {
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    sourceSize: Qt.size(20, 20)
                    source: chip.checked ? chip.iconActiveSource : chip.iconSource
                }
                Label {
                    visible: chip.label.length > 0
                    text: chip.label
                    font.pixelSize: Theme.fontLabel
                    font.weight: Font.DemiBold
                    color: chip.checked ? Theme.accent : Theme.text
                }
            }
            MouseArea {
                id: chipMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: chip.clicked()
            }
            ToolTip.visible: chipMouse.containsMouse && chip.tip.length > 0
            ToolTip.delay: 600
            ToolTip.text: chip.tip
        }

        HeaderChip {
            iconSource: "qrc:/icons/menu/pin.svg"
            iconActiveSource: "qrc:/icons/menu/pin-accent.svg"
            label: consolePane.width >= 1500 ? qsTr("On top") : ""
            checked: Chiaki.window.alwaysOnTop
            tip: qsTr("Always on top")
            onClicked: Chiaki.window.alwaysOnTop = !Chiaki.window.alwaysOnTop
        }
        HeaderChip {
            iconSource: "qrc:/icons/menu/tray.svg"
            label: consolePane.width >= 1500 ? qsTr("To tray") : ""
            tip: qsTr("Hide to system tray (click the tray icon to bring it back)")
            onClicked: Chiaki.window.hideToTrayNow()
        }
        C.Button {
            flat: true
            focusPolicy: Qt.NoFocus
            text: qsTr("Add Console")
            icon.source: "qrc:/icons/add-24px.svg"
            icon.width: 24
            icon.height: 24
            onClicked: root.showManualHostDialog()
        }
        C.Button {
            id: settingsButton
            flat: true
            focusPolicy: Qt.NoFocus
            text: qsTr("Settings")
            icon.source: "qrc:/icons/settings-20px.svg"
            icon.width: 24
            icon.height: 24
            onClicked: root.showSettingsDialog()
        }
    }

    // ---- setup banner (onboarding) ----
    Rectangle {
        id: setupBanner
        anchors {
            top: header.bottom
            left: parent.left
            right: parent.right
            leftMargin: Theme.screenMargin
            rightMargin: Theme.screenMargin
            topMargin: Theme.space6
        }
        visible: consolePane.showSetupBanner
        height: visible ? bannerCol.implicitHeight + Theme.space4 * 2 : 0
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: 1
        border.color: Theme.border

        // จอสูง: ชื่อ / คำอธิบาย / ปุ่ม · จอเตี้ย (<900 เช่น Deck 800): แถวเดียว ชื่อ + ปุ่ม ไม่มีคำอธิบาย
        readonly property bool compact: consolePane.height < 900

        ColumnLayout {
            id: bannerCol
            anchors {
                fill: parent
                margins: setupBanner.compact ? Theme.space3 : Theme.space4
                leftMargin: Theme.space6
                rightMargin: Theme.space6
            }
            spacing: Theme.space2

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space3
                Label {
                    text: qsTr("Finish setting up")
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                Item { Layout.fillWidth: true }
                C.Button {
                    visible: setupBanner.compact && consolePane.showPsnSetup
                    focusPolicy: Qt.NoFocus
                    text: qsTr("Connect PSN")
                    highlighted: true
                    onClicked: root.showPSNTokenDialog(false)
                }
                C.Button {
                    visible: setupBanner.compact && consolePane.showSteamSetup
                    focusPolicy: Qt.NoFocus
                    text: qsTr("Steam Shortcut")
                    onClicked: root.showSteamShortcutDialog(true)
                }
                C.Button {
                    flat: true
                    focusPolicy: Qt.NoFocus
                    font.pixelSize: Theme.fontLabel
                    text: qsTr("Don't show again")
                    onClicked: {
                        Chiaki.settings.addSteamShortcutAsk = false;
                        Chiaki.settings.remotePlayAsk = false;
                    }
                }
            }
            Label {
                visible: !setupBanner.compact
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontLabel
                color: Theme.textMuted
                text: {
                    let parts = [];
                    if (consolePane.showSteamSetup)
                        parts.push(qsTr("Add PS-WRAP to Steam with official artwork and controller layout"));
                    if (consolePane.showPsnSetup)
                        parts.push(qsTr("Connect PSN for automatic registration and play outside your home network"));
                    return parts.join("  ·  ");
                }
            }
            RowLayout {
                visible: !setupBanner.compact
                spacing: Theme.space3
                C.Button {
                    visible: consolePane.showPsnSetup
                    focusPolicy: Qt.NoFocus
                    text: qsTr("Connect PSN")
                    highlighted: true
                    onClicked: root.showPSNTokenDialog(false)
                }
                C.Button {
                    visible: consolePane.showSteamSetup
                    focusPolicy: Qt.NoFocus
                    text: qsTr("Steam Shortcut")
                    onClicked: root.showSteamShortcutDialog(true)
                }
            }
        }
    }

    Label {
        id: sectionLabel
        anchors {
            top: setupBanner.bottom
            left: parent.left
            leftMargin: Theme.screenMargin
            topMargin: Theme.space8
        }
        text: qsTr("CONSOLES")
        font.pixelSize: Theme.fontCaption
        font.letterSpacing: 2
        font.weight: Font.DemiBold
        color: Theme.textMuted
    }

    // ---- console list ----
    ListView {
        id: hostsView
        keyNavigationWraps: true
        anchors {
            top: sectionLabel.bottom
            left: parent.left
            right: parent.right
            bottom: hintBar.top
            leftMargin: Theme.screenMargin
            rightMargin: Theme.screenMargin
            topMargin: Theme.space3
            bottomMargin: Theme.space4
        }
        clip: true
        spacing: Theme.cardGap
        model: Chiaki.hosts
        onCountChanged: {
            if(!hostsView.currentItem)
                hostsView.incrementCurrentIndex();
            if(!hostsView.currentItem)
                return;
            if(!hostsView.currentItem.visible)
            {
                for(var i = 0; i < hostsView.count; i++)
                {
                    hostsView.incrementCurrentIndex()
                    if(hostsView.currentItem.visible)
                    {
                        break;
                    }
                }
            }
        }

        delegate: Item {
            id: delegate
            visible: modelData.display
            width: ListView.view ? ListView.view.width : 0
            height: modelData.display ? 168 : 0
            readonly property bool highlighted: ListView.isCurrentItem
            readonly property bool remoteOnly: modelData.duid && !modelData.discovered
            readonly property string stateKey: remoteOnly ? "remote" : modelData.state
            readonly property color stateColor: remoteOnly ? Theme.accent : Theme.stateColor(modelData.state)
            readonly property string stateText: {
                if (remoteOnly) return qsTr("Remote via PSN");
                if (modelData.state === "ready") return qsTr("Ready");
                if (modelData.state === "standby") return qsTr("Standby");
                return qsTr("Unknown");
            }

            function connectToHost() {
                if(modelData.discovered)
                    Chiaki.connectToHost(index, modelData.name);
                else
                    Chiaki.connectToHost(index);
            }

            function wakeUpHost() {
                if(!modelData.discovered && !modelData.duid)
                    Chiaki.wakeUpHost(index);
            }

            function deleteHost() {
                if (modelData.manual)
                    root.showConfirmDialog(qsTr("Delete Console"), qsTr("Are you sure you want to delete this console?"), () => {Chiaki.deleteHost(index)});
                else if (modelData.discovered && !modelData.registered)
                    root.showConfirmDialog(qsTr("Hide Console"), qsTr("Are you sure you want to hide this console?") + "\n\n" + qsTr("Note: You can unhide from the Consoles section of the Settings under Hidden Consoles"), () => Chiaki.hideHost(modelData.mac, modelData.name));
            }

            function setConsolePin() {
                root.showConsolePinDialog(index);
            }

            Rectangle {
                id: card
                anchors.fill: parent
                radius: Theme.radiusCard
                color: cardMouse.containsMouse ? Theme.surfaceRaised : Theme.surface
                border.width: delegate.highlighted ? 2 : 1
                border.color: delegate.highlighted ? Theme.accent : Theme.border
                Behavior on color { ColorAnimation { duration: Theme.durFast } }
                Behavior on border.color { ColorAnimation { duration: Theme.durFast } }

                MouseArea {
                    id: cardMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        hostsView.currentIndex = index;
                        delegate.connectToHost();
                    }
                }

                RowLayout {
                    anchors {
                        fill: parent
                        leftMargin: Theme.space6
                        rightMargin: Theme.space6
                        topMargin: Theme.space4
                        bottomMargin: Theme.space4
                    }
                    // PS-WRAP: จอแคบ (หน้าต่างเล็ก/Deck) — ย่อรูป ช่องไฟ ปุ่ม Play และซ่อนปุ่มรอง ไม่ให้ Play หลุดขอบการ์ด
                    readonly property bool compact: card.width < 1100
                    readonly property bool tight: card.width < 900
                    spacing: compact ? Theme.space4 : Theme.space6

                    // PS-WRAP: รูปเครื่องตามรุ่น + ไฟสถานะ animation (ConsoleArt.qml)
                    ConsoleArt {
                        Layout.preferredWidth: parent.compact ? 120 : 190
                        Layout.preferredHeight: parent.compact ? 84 : 133
                        ps5: modelData.ps5
                        state: delegate.remoteOnly ? "unknown" : modelData.state
                        highlighted: delegate.highlighted || cardMouse.containsMouse
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: Theme.space1

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space3
                            Label {
                                Layout.fillWidth: true
                                Layout.maximumWidth: implicitWidth   // หดได้ (elide) แต่ไม่ยืดดันชิปสถานะ
                                text: modelData.name ? modelData.name : (modelData.ps5 ? "PlayStation 5" : "PlayStation 4")
                                font.pixelSize: Theme.fontTitle
                                font.weight: Font.DemiBold
                                color: Theme.text
                                elide: Text.ElideRight
                            }
                            // status chip
                            Rectangle {
                                radius: Theme.radiusChip
                                color: Qt.rgba(delegate.stateColor.r, delegate.stateColor.g, delegate.stateColor.b, 0.15)
                                implicitHeight: 28
                                implicitWidth: chipRow.implicitWidth + Theme.space3 * 2
                                RowLayout {
                                    id: chipRow
                                    anchors.centerIn: parent
                                    spacing: Theme.space2
                                    Rectangle { width: 10; height: 10; radius: 5; color: delegate.stateColor }
                                    Label {
                                        text: delegate.stateText
                                        font.pixelSize: Theme.fontCaption
                                        font.weight: Font.DemiBold
                                        color: delegate.stateColor
                                    }
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }

                        Label {
                            font.pixelSize: Theme.fontLabel
                            color: Theme.textMuted
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            text: {
                                let parts = [];
                                if (modelData.address)
                                    parts.push(Chiaki.settings.streamerMode ? qsTr("address hidden") : modelData.address);
                                if (modelData.mac)
                                    parts.push(modelData.registered ? qsTr("registered") : qsTr("not registered"));
                                if (modelData.duid && modelData.discovered)
                                    parts.push(qsTr("automatic registration available"));
                                else if (!modelData.duid) {
                                    if (modelData.discovered && modelData.manual) parts.push(qsTr("discovered + manual"));
                                    else if (modelData.manual) parts.push(qsTr("manual"));
                                }
                                return parts.join("  ·  ");
                            }
                        }

                        Label {
                            visible: text.length > 0
                            font.pixelSize: Theme.fontLabel
                            color: Theme.text
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            text: {
                                if (delegate.remoteOnly || !modelData.discovered) return "";
                                let t = "";
                                if (modelData.app) t = qsTr("Playing: %1").arg(modelData.app);
                                if (modelData.titleId) t += (t ? "  ·  " : "") + modelData.titleId;
                                return t;
                            }
                        }
                    }

                    // secondary actions (เล็ก, flat) — เฉพาะที่ใช้ได้กับ console นี้
                    RowLayout {
                        Layout.alignment: Qt.AlignVCenter
                        spacing: Theme.space1
                        visible: !parent.tight   // จอแคบ: ใช้ปุ่มลัดจอยในแถบล่างแทน (L1 = PIN, △ = Wake Up)

                        C.Button {
                            flat: true
                            focusPolicy: Qt.NoFocus
                            text: qsTr("Wake Up")
                            visible: modelData.registered && !modelData.duid && !modelData.discovered
                            onClicked: delegate.wakeUpHost()
                        }
                        C.Button {
                            flat: true
                            focusPolicy: Qt.NoFocus
                            text: qsTr("PIN")
                            visible: modelData.registered
                            onClicked: delegate.setConsolePin()
                        }
                        C.Button {
                            flat: true
                            focusPolicy: Qt.NoFocus
                            text: modelData.manual ? qsTr("Delete") : qsTr("Hide")
                            visible: modelData.manual || (modelData.discovered && !modelData.registered)
                            onClicked: delegate.deleteHost()
                        }
                    }

                    // primary action
                    C.Button {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.preferredWidth: parent.compact ? 140 : 180
                        Layout.preferredHeight: 56
                        highlighted: true
                        focusPolicy: Qt.NoFocus
                        font.pixelSize: Theme.fontTitle
                        text: modelData.registered || modelData.duid ? qsTr("Play") : qsTr("Register")
                        onClicked: delegate.connectToHost()
                    }
                }
            }
        }

        // empty state
        Column {
            anchors.centerIn: parent
            visible: consolePane.visibleHostCount === 0
            spacing: Theme.space3
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Chiaki.discoveryEnabled ? qsTr("Looking for consoles…") : qsTr("Discovery is off")
                font.pixelSize: Theme.fontTitle
                color: Theme.text
            }
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Turn on your PlayStation and keep it on the same network,\nor add a console manually.")
                font.pixelSize: Theme.fontLabel
                color: Theme.textMuted
            }
            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: Chiaki.discoveryEnabled
                visible: running
            }
        }
    }

    // ---- bottom hint bar: ปุ่มจอย + discovery toggle ----
    Rectangle {
        id: hintBar
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: 64
        color: Theme.surface
        border.width: 0
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: Theme.border }

        RowLayout {
            anchors {
                fill: parent
                leftMargin: Theme.screenMargin
                rightMargin: Theme.screenMargin
            }
            spacing: consolePane.width >= 1600 ? Theme.space6 : Theme.space3

            Repeater {
                model: [
                    { glyph: root.controllerButton("cross"),   label: qsTr("Play") },
                    { glyph: root.controllerButton("pyramid"), label: qsTr("Wake Up") },
                    { glyph: root.controllerButton("box"),     label: qsTr("Hide") },
                    { glyph: "qrc:/icons/l1.svg",              label: qsTr("Console PIN") },
                    { glyph: "qrc:/icons/r3.svg",              label: qsTr("Add Console") },
                    { glyph: "qrc:/icons/options.svg",         label: qsTr("Settings") }
                ]
                delegate: RowLayout {
                    spacing: Theme.space2
                    Image {
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 26
                        sourceSize: Qt.size(26, 26)
                        source: modelData.glyph
                    }
                    Label {
                        visible: consolePane.width >= 1400   // จอแคบ (Deck 1280): เหลือแค่ไอคอนปุ่ม
                        text: modelData.label
                        font.pixelSize: Theme.fontLabel
                        color: Theme.textMuted
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // PS-WRAP: ไมค์ / facecam แบบเดียวกับชิปจอย — กดเพื่อเลือกอุปกรณ์
            Rectangle {
                id: micChip
                readonly property string device: Chiaki.settings.audioInDevice
                readonly property bool present: device === "" || Chiaki.settings.availableAudioInDevices.indexOf(device) >= 0
                radius: Theme.radiusChip
                color: micMouse.containsMouse ? Theme.surfaceRaised : Qt.rgba(1, 1, 1, 0.04)
                border.width: 1
                border.color: micMouse.containsMouse ? Theme.accent : Theme.border
                implicitHeight: 36
                implicitWidth: micRow.implicitWidth + Theme.space4 * 2
                Behavior on color { ColorAnimation { duration: Theme.durFast } }
                Component.onCompleted: Chiaki.settings.refreshAudioDevices()
                RowLayout {
                    id: micRow
                    anchors.centerIn: parent
                    spacing: Theme.space2
                    Image {
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        sourceSize: Qt.size(20, 20)
                        source: "qrc:/icons/menu/mic.svg"
                        opacity: micChip.present ? 1.0 : 0.4
                    }
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: micChip.present ? Theme.success : Theme.textMuted
                    }
                    Label {
                        Layout.maximumWidth: 220
                        visible: consolePane.width >= 1800   // จอแคบ: ชิปเหลือไอคอน + จุดสถานะ (ไม่ล้นแถบล่าง)
                        elide: Text.ElideRight
                        text: micChip.device === "" ? qsTr("Default mic") : micChip.device
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.DemiBold
                        color: micChip.present ? Theme.text : Theme.textMuted
                    }
                }
                MouseArea {
                    id: micMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: micPreviewDialog.open()
                }
            }

            Rectangle {
                id: camChip
                // ชื่อกล้องเก็บใน QSettings กลุ่ม pswrap (StreamView/SettingsDialog ใช้ key เดียวกัน) — อ่านสดทุกครั้งที่หน้านี้กลับมา
                property string device: ""
                readonly property var names: {
                    let n = [];
                    for (let i = 0; i < camDevices.videoInputs.length; ++i) n.push(camDevices.videoInputs[i].description);
                    for (let j = 0; j < camLister.devices.length; ++j) if (n.indexOf(camLister.devices[j]) < 0) n.push(camLister.devices[j]);
                    return n;
                }
                readonly property string shown: device !== "" ? device : (camDevices.defaultVideoInput.description || "")
                readonly property bool present: shown !== "" && names.indexOf(shown) >= 0
                readonly property bool on: Chiaki.window.camOverlay
                function reload() { camPrefs.sync(); device = camPrefs.value("camDevice", ""); camLister.refreshDevices(); }
                Component.onCompleted: reload()
                Connections {
                    target: consolePane
                    function onVisibleChanged() { if (consolePane.visible) camChip.reload(); }
                }
                MediaDevices { id: camDevices }
                DshowCamera { id: camLister }
                Settings { id: camPrefs; category: "pswrap" }
                radius: Theme.radiusChip
                color: camMouse.containsMouse ? Theme.surfaceRaised : Qt.rgba(1, 1, 1, 0.04)
                border.width: 1
                border.color: camMouse.containsMouse ? Theme.accent : Theme.border
                implicitHeight: 36
                implicitWidth: camRow.implicitWidth + Theme.space4 * 2
                Behavior on color { ColorAnimation { duration: Theme.durFast } }
                RowLayout {
                    id: camRow
                    anchors.centerIn: parent
                    spacing: Theme.space2
                    Image {
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                        sourceSize: Qt.size(20, 20)
                        source: "qrc:/icons/menu/cam.svg"
                        opacity: camChip.present && camChip.on ? 1.0 : 0.4
                    }
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: camChip.present && camChip.on ? Theme.success : Theme.textMuted
                    }
                    Label {
                        Layout.maximumWidth: 220
                        visible: consolePane.width >= 1800   // จอแคบ: ชิปเหลือไอคอน + จุดสถานะ (ไม่ล้นแถบล่าง)
                        elide: Text.ElideRight
                        text: !camChip.present ? qsTr("No camera") : (camChip.on ? camChip.shown : qsTr("%1 · off").arg(camChip.shown))
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.DemiBold
                        color: camChip.present && camChip.on ? Theme.text : Theme.textMuted
                    }
                }
                MouseArea {
                    id: camMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { camChip.reload(); camPreviewDialog.open(); }
                }
            }

            // สถานะจอยที่ต่ออยู่ (Chiaki.controllers จาก C++) — กดเพื่อเปิดหน้าสถานะ
            Rectangle {
                id: padChip
                readonly property int count: Chiaki.controllers.length
                readonly property var first: count > 0 ? Chiaki.controllers[0] : null
                readonly property string label: {
                    if (!first) return qsTr("No controller");
                    let n = consolePane.controllerKind(first);
                    return count > 1 ? n + " +" + (count - 1) : n;
                }
                radius: Theme.radiusChip
                color: padMouse.containsMouse ? Theme.surfaceRaised : Qt.rgba(1, 1, 1, 0.04)
                border.width: 1
                border.color: padMouse.containsMouse ? Theme.accent : Theme.border
                implicitHeight: 36
                implicitWidth: padRow.implicitWidth + Theme.space4 * 2
                Behavior on color { ColorAnimation { duration: Theme.durFast } }
                RowLayout {
                    id: padRow
                    anchors.centerIn: parent
                    spacing: Theme.space2
                    Image {
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 26
                        sourceSize: Qt.size(26, 26)
                        fillMode: Image.PreserveAspectFit
                        source: consolePane.controllerIcon(padChip.first)
                        opacity: padChip.count > 0 ? 1.0 : 0.4
                    }
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: padChip.count > 0 ? Theme.success : Theme.textMuted
                    }
                    Label {
                        text: padChip.label
                        visible: consolePane.width >= 1800
                        font.pixelSize: Theme.fontCaption
                        font.weight: Font.DemiBold
                        color: padChip.count > 0 ? Theme.text : Theme.textMuted
                    }
                }
                MouseArea {
                    id: padMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: controllerPopup.open()
                }
            }

            // PS-WRAP: เปิดโฟลเดอร์คลิปที่อัด (Chiaki.window.recordingFolder)
            BarChip {
                iconSource: "qrc:/icons/menu/folder.svg"
                text: qsTr("Recordings")
                onClicked: Chiaki.window.openRecordingsFolder()
            }

            // PS-WRAP: เปิดโฟลเดอร์ภาพหน้าจอ (เลือกภาพล่าสุดให้ใน Explorer)
            BarChip {
                iconSource: "qrc:/icons/menu/screenshot.svg"
                text: qsTr("Screenshots")
                onClicked: Chiaki.window.openScreenshotsFolder()
            }

            // PS-WRAP: Discovery เป็นชิปแบบเดียวกับ mic/cam/จอย (เดิมเป็น flat button ไอคอนฟ้าลอยเดี่ยว)
            BarChip {
                iconSource: "qrc:/icons/menu/discover.svg"
                active: Chiaki.discoveryEnabled
                showDot: true
                text: active ? qsTr("Discovery on") : qsTr("Discovery off")
                onClicked: Chiaki.discoveryEnabled = !Chiaki.discoveryEnabled
            }

            // PS-WRAP: เวอร์ชัน + เครดิต (AboutDialog.qml) — แทนเลขเวอร์ชันเปล่าๆ (เดิมเป็นเลขของ upstream)
            BarChip {
                iconSource: "qrc:/icons/menu/info.svg"
                text: qsTr("PS-WRAP %1 · About").arg(Qt.application.version)
                onClicked: aboutDialog.open()
            }
        }
    }

    // PS-WRAP: ชิปแถบล่าง (หน้าตาเดียวกับ micChip/camChip/padChip)
    component BarChip: Rectangle {
        id: chip
        property string iconSource
        property string text
        property bool active: true
        property bool showDot: false
        signal clicked()
        radius: Theme.radiusChip
        color: chipMouse.containsMouse ? Theme.surfaceRaised : Qt.rgba(1, 1, 1, 0.04)
        border.width: 1
        border.color: chipMouse.containsMouse ? Theme.accent : Theme.border
        implicitHeight: 36
        implicitWidth: chipRow.implicitWidth + Theme.space4 * 2
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
        RowLayout {
            id: chipRow
            anchors.centerIn: parent
            spacing: Theme.space2
            Image {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                sourceSize: Qt.size(20, 20)
                source: chip.iconSource
                opacity: chip.active ? 1.0 : 0.4
            }
            Rectangle {
                visible: chip.showDot
                width: 8; height: 8; radius: 4
                color: chip.active ? Theme.success : Theme.textMuted
            }
            Label {
                visible: consolePane.width >= 1800   // จอแคบ: เหลือแค่ไอคอน (+ จุดสถานะ)
                text: chip.text
                font.pixelSize: Theme.fontCaption
                font.weight: Font.DemiBold
                color: chip.active ? Theme.text : Theme.textMuted
            }
        }
        MouseArea {
            id: chipMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.clicked()
        }
        ToolTip.visible: chipMouse.containsMouse && consolePane.width < 1800
        ToolTip.text: chip.text
        ToolTip.delay: 500
    }
}
