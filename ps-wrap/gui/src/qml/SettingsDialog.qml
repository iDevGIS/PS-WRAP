import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtMultimedia

import org.streetpea.chiaking

import "controls" as C

// PS-WRAP: Settings ยกชุดใหม่ (2026-10-10) — sidebar มีไอคอน จัดกลุ่ม Play / Controls / Connection / Create / App
// แต่ละหน้า = C.SettingsPage (หัวหน้า + การ์ด C.SettingsSection) · การเดินด้วยจอยใช้ focus chain ตามลำดับในไฟล์
// (firstInFocusChain/lastInFocusChain ต้นท้ายหน้า) + KeyNavigation เฉพาะตาราง Stream / dpad / stream menu combo
DialogView {
    enum Console {
        PS4,
        PS5
    }
    property int selectedConsole: SettingsDialog.Console.PS5
    property bool quitControllerMapping: true
    readonly property int labelWidth: Math.round(270 * dialog.uiScale)   // PS-WRAP: คอลัมน์ชื่อ setting (หน้า Game presets ใช้)

    // ลำดับหน้า = ลำดับใน sidebar และ StackLayout
    readonly property int pageGeneral: 0
    readonly property int pageStream: 1
    readonly property int pageVideo: 2
    readonly property int pageAudio: 3
    readonly property int pageGames: 4
    readonly property int pageControllers: 5
    readonly property int pageKeys: 6
    readonly property int pageConsoles: 7
    readonly property int pageRemote: 8
    readonly property int pageFacecam: 9
    readonly property int pageRecording: 10
    readonly property int pageGoLive: 11
    readonly property int pageSystem: 12

    readonly property var padButtonNames: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]

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
    header: qsTr("Values in ( ) are the defaults")
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
                return current;   // PS-WRAP: ใน C.SettingsPage = การ์ด SettingsSection ทั้งใบ (เลื่อนให้เห็นหัวการ์ดด้วย)
            current = current.parent;
        }
        return item;
    }
    function ensureItemVisibleInFlick(flick, item) {
        if (!flick || !item || !flickContainsItem(flick, item))
            return;
        let target = flickVisibilityTarget(flick, item);
        if (target.height > flick.height)   // PS-WRAP: container (การ์ด/GridLayout) สูงกว่าจอ → ใช้ item เอง
            target = item;
        const top = target.mapToItem(flick.contentItem, 0, 0).y;
        const bottom = top + target.height;
        if (top < flick.contentY)
            flick.contentY = Math.max(0, top - Theme.space2);
        else if (bottom > flick.contentY + flick.height)
            flick.contentY = Math.min(Math.max(0, flick.contentHeight - flick.height), bottom - flick.height + Theme.space2);
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
        case pageGeneral: item = hideToTrayCheck; break;
        case pageStream: item = consoleSelection; break;
        case pageVideo: item = renderPresetCombo; break;
        case pageAudio: item = audioOutDevice; break;
        case pageGames: item = gamePresetsPage.firstItem; break;
        case pageControllers: item = controllerMappingChange; break;
        case pageKeys: item = keysPage.resetButton; break;
        case pageConsoles: item = registerNewButton; break;
        case pageRemote: item = firstRemoteFocusableItem(); break;
        case pageFacecam: item = camDeviceCombo; break;
        case pageRecording: item = recFolderField; break;
        case pageGoLive: item = goLivePage.firstControl; break;
        case pageSystem: item = profile; break;
        }
        if (item)
            item.forceActiveFocus(Qt.TabFocusReason);
    }
    function activeSettingsFlick() {
        switch (bar.currentIndex) {
        case pageGeneral: return generalPage.flick;
        case pageStream: return streamPage.flick;
        case pageVideo: return videoPage.flick;
        case pageAudio: return audioPage.flick;
        case pageGames: return gamePresetsPage.flick;
        case pageControllers: return controllersPage.flick;
        case pageKeys: return keysPage.flick;
        case pageConsoles: return consolesPage.flick;
        case pageRemote: return remotePage.flick;
        case pageFacecam: return facecamPage.flick;
        case pageRecording: return recordingPage.flick;
        case pageGoLive: return goLivePage.flick;
        case pageSystem: return systemPage.flick;
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
        return remotePage.flick;
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
        // PS-WRAP: sidebar แทน TabBar แนวนอน — คง id `bar` + currentIndex/incrementCurrentIndex/decrementCurrentIndex (ListView มี API เดียวกับ TabBar)
        // index ต้องตรงกับ StackLayout ด้านล่าง (pageGeneral … pageSystem)
        Rectangle {
            id: sidebar
            width: dialog.width < 1500 ? 240 : 290
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
                    topMargin: Theme.space2
                    leftMargin: Theme.space3
                    rightMargin: Theme.space3 + 1
                    bottomMargin: 64
                }
                clip: true
                // PS-WRAP: จอเตี้ยรายการล้น — เลื่อนได้ (wheel/ลาก) + เลื่อนตามหมวดที่เลือกตอน L1/R1
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: bar.contentHeight > bar.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
                onCurrentIndexChanged: {
                    if (currentIndex <= 0) positionViewAtBeginning();   // ให้หัวกลุ่มแรกโผล่ด้วย
                    else positionViewAtIndex(currentIndex, ListView.Contain);
                }
                currentIndex: 0
                spacing: 2
                model: [
                    { name: qsTr("General"),       group: qsTr("Play"),       icon: "qrc:/icons/settings/general.svg" },
                    { name: qsTr("Stream"),        group: qsTr("Play"),       icon: "qrc:/icons/menu/quality.svg" },
                    { name: qsTr("Video"),         group: qsTr("Play"),       icon: "qrc:/icons/menu/display.svg" },
                    { name: qsTr("Audio"),         group: qsTr("Play"),       icon: "qrc:/icons/menu/volume.svg" },
                    { name: qsTr("Game presets"),  group: qsTr("Play"),       icon: "qrc:/icons/settings/games.svg" },
                    { name: qsTr("Controllers"),   group: qsTr("Controls"),   icon: "qrc:/icons/controller.svg" },
                    { name: qsTr("Keyboard"),      group: qsTr("Controls"),   icon: "qrc:/icons/settings/keyboard.svg" },
                    { name: qsTr("Consoles"),      group: qsTr("Connection"), icon: "qrc:/icons/settings/consoles.svg" },
                    { name: qsTr("Remote Play"),   group: qsTr("Connection"), icon: "qrc:/icons/settings/remote.svg" },
                    { name: qsTr("Facecam"),       group: qsTr("Create"),     icon: "qrc:/icons/menu/cam.svg" },
                    { name: qsTr("Recording"),     group: qsTr("Create"),     icon: "qrc:/icons/menu/record.svg" },
                    { name: qsTr("Go Live"),       group: qsTr("Create"),     icon: "qrc:/icons/menu/live.svg" },
                    { name: qsTr("System"),        group: qsTr("App"),        icon: "qrc:/icons/menu/settings.svg" }
                ]
                section.property: "group"
                section.delegate: Label {
                    width: ListView.view.width
                    topPadding: Theme.space4
                    bottomPadding: Theme.space1
                    leftPadding: Theme.space3
                    text: section.toUpperCase()
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
                    height: 44
                    radius: Theme.radiusControl
                    color: current ? Theme.accent : (navMouse.containsMouse ? Theme.surfaceRaised : "transparent")
                    Behavior on color { ColorAnimation { duration: Theme.durFast } }
                    RowLayout {
                        anchors {
                            fill: parent
                            leftMargin: Theme.space3
                            rightMargin: Theme.space3
                        }
                        spacing: Theme.space3
                        Image {
                            Layout.preferredWidth: 22
                            Layout.preferredHeight: 22
                            sourceSize: Qt.size(22, 22)
                            source: modelData.icon
                            opacity: current ? 1.0 : 0.7
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.name
                            font.weight: current ? Font.DemiBold : Font.Normal
                            color: current ? Theme.accentText : Theme.text
                            elide: Text.ElideRight
                        }
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

            // ───────────────────────── 0 General ─────────────────────────
            C.SettingsPage {
                id: generalPage
                title: qsTr("General")
                subtitle: qsTr("How the PS-WRAP window behaves, Discord status and what happens when a stream ends")
                icon: "qrc:/icons/settings/general.svg"

                C.SettingsSection {
                    title: qsTr("Window")
                    icon: "qrc:/icons/settings/window.svg"

                    C.RowLabel { text: qsTr("Hide To Tray On Close") }
                    C.CheckBox {
                        id: hideToTrayCheck
                        firstInFocusChain: true
                        checked: Chiaki.window.hideToTray
                        onToggled: Chiaki.window.hideToTray = !Chiaki.window.hideToTray
                        text: qsTr("Close button hides PS-WRAP to the system tray (Quit from tray menu)")
                    }
                    C.Hint { text: qsTr("(Off)") }

                    C.RowLabel { text: qsTr("Always On Top") }
                    C.CheckBox {
                        checked: Chiaki.window.alwaysOnTop
                        onToggled: Chiaki.window.alwaysOnTop = !Chiaki.window.alwaysOnTop
                        text: qsTr("Keep the window above other windows (main menu and stream)")
                    }
                    C.Hint { text: qsTr("(Off)") }
                }

                // PS-WRAP: Discord Rich Presence (สถานะ "Playing PS-WRAP · <เกม>" บนโปรไฟล์ Discord)
                C.SettingsSection {
                    title: qsTr("Discord")
                    description: qsTr("Needs the Discord app running on this PC")
                    icon: "qrc:/icons/menu/chat.svg"

                    C.RowLabel { text: qsTr("Discord Status") }
                    C.CheckBox {
                        checked: Chiaki.window.discordPresence
                        onToggled: Chiaki.window.discordPresence = !Chiaki.window.discordPresence
                        text: qsTr("Show \"Playing PS-WRAP\" and play time on your Discord profile (needs the Discord app)")
                    }
                    C.Hint { text: qsTr("(On)") }

                    C.RowLabel { text: qsTr("Show Game On Discord") }
                    C.CheckBox {
                        enabled: Chiaki.window.discordPresence
                        checked: Chiaki.window.discordShowGame
                        onToggled: Chiaki.window.discordShowGame = !Chiaki.window.discordShowGame
                        text: qsTr("Include the name of the game running on the console")
                    }
                    C.Hint { text: qsTr("(On)") }
                }

                C.SettingsSection {
                    title: qsTr("When A Stream Ends")
                    description: qsTr("What to do with the console when you disconnect or the stream is suspended")
                    icon: "qrc:/icons/menu/power.svg"

                    C.RowLabel { text: qsTr("Action On Disconnect") }
                    C.ComboBox {
                        id: disconnectAction
                        Layout.preferredWidth: dialog.controlWidth
                        model: [qsTr("Do Nothing"), qsTr("Enter Sleep Mode"), qsTr("Ask")]
                        currentIndex: Chiaki.settings.disconnectAction
                        onActivated: index => Chiaki.settings.disconnectAction = index
                    }
                    C.Hint { text: qsTr("(Ask)") }

                    C.RowLabel { text: qsTr("Action On Suspend") }
                    C.ComboBox {
                        Layout.preferredWidth: dialog.controlWidth
                        model: [qsTr("Do Nothing"), qsTr("Enter Sleep Mode")]
                        currentIndex: Chiaki.settings.suspendAction
                        onActivated: index => Chiaki.settings.suspendAction = index
                    }
                    C.Hint { text: qsTr("(Do Nothing)") }
                }

                C.SettingsSection {
                    title: qsTr("Privacy")
                    icon: "qrc:/icons/settings/privacy.svg"

                    C.RowLabel { text: qsTr("Streamer Mode (Hides Info)") }
                    C.CheckBox {
                        id: streamerMode
                        lastInFocusChain: true
                        text: qsTr("Hide console addresses and the PIN on screen while you share it")
                        checked: Chiaki.settings.streamerMode
                        onToggled: Chiaki.settings.streamerMode = !Chiaki.settings.streamerMode
                    }
                    C.Hint { text: qsTr("(Unchecked)") }
                }
            }

            // ───────────────────────── 1 Stream ─────────────────────────
            C.SettingsPage {
                id: streamPage
                title: qsTr("Stream")
                subtitle: qsTr("Picture size, frame rate and bitrate the console sends, and network warnings")
                icon: "qrc:/icons/menu/quality.svg"

                C.SettingsSection {
                    title: qsTr("Resolution, Frame Rate And Bitrate")
                    description: qsTr("Local = on the same network as the console · Remote = over the internet. Not sure? Check my connection measures your network and recommends values.")
                    icon: "qrc:/icons/menu/quality.svg"

                    C.RowLabel { text: qsTr("Settings for") }
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
                        KeyNavigation.right: checkConnectionButton
                    }

                    // PS-WRAP: วัดการเชื่อมต่อไปเครื่อง แล้วแนะนำ/ตั้งค่า Local ให้ (ConnectionCheckDialog)
                    C.Button {
                        id: checkConnectionButton
                        Layout.alignment: Qt.AlignLeft
                        text: qsTr("Check my connection")
                        onClicked: root.showConnectionCheck(null, checkConnectionButton)
                        KeyNavigation.left: consoleSelection
                        KeyNavigation.down: selectedConsole == SettingsDialog.Console.PS4 ? resolutionRemotePS4 : resolutionRemotePS5
                    }

                    Item { Layout.preferredHeight: 1 }
                    Label {
                        Layout.alignment: Qt.AlignCenter
                        text: qsTr("Local")
                        font.weight: Font.DemiBold
                        color: Theme.textMuted
                    }
                    Label {
                        Layout.alignment: Qt.AlignCenter
                        text: qsTr("Remote")
                        font.weight: Font.DemiBold
                        color: Theme.textMuted
                    }

                    C.RowLabel { text: qsTr("Resolution") }

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

                    C.RowLabel { text: qsTr("FPS") }

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

                    C.RowLabel { text: qsTr("Bitrate") }

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
                        KeyNavigation.down: wifiNotifSlider
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
                        KeyNavigation.down: wifiNotifSlider
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

                        Label {
                            anchors {
                                left: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 10
                            }
                            text: (parent.value) + qsTr(" Mbps") + qsTr(" (%1 Mbps)").arg(parent.bitrate.toFixed(0))
                        }
                    }

                    C.RowLabel {
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
                        KeyNavigation.down: wifiNotifSlider
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
                        Keys.onReturnPressed: {
                            if (popup.visible) {
                                activated(highlightedIndex);
                                popup.close();
                            } else
                                popup.open();
                        }
                        KeyNavigation.up: bitrateRemotePS5
                        KeyNavigation.left: codecLocalPS5
                        KeyNavigation.down: wifiNotifSlider
                        KeyNavigation.priority: {
                            if(!popup.visible)
                                KeyNavigation.BeforeItem
                            else
                                KeyNavigation.AfterItem
                        }
                    }
                }

                C.SettingsSection {
                    title: qsTr("Network")
                    description: qsTr("Warnings and recovery when the connection drops packets")
                    icon: "qrc:/icons/settings/network.svg"

                    C.RowLabel { text: qsTr("Weak Wifi Notification") }
                    C.Slider {
                        id: wifiNotifSlider
                        Layout.preferredWidth: Math.round(dialog.controlWidth * 0.625)
                        Layout.rightMargin: Math.round(dialog.controlWidth * 0.75)   // เผื่อป้ายค่าที่ห้อยขวา slider
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
                    C.Hint { text: qsTr("(3%)") }

                    C.RowLabel { text: qsTr("Packet Loss Reported Max") }
                    C.Slider {
                        Layout.preferredWidth: Math.round(dialog.controlWidth * 0.625)
                        from: 0
                        to: 100
                        stepSize: 1
                        Layout.rightMargin: Math.round(dialog.controlWidth * 0.75)
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
                    C.Hint { text: qsTr("(5%)") }

                    C.RowLabel { text: qsTr("Request IDR Frame on FEC Failure") }
                    C.CheckBox {
                        checked: Chiaki.settings.iDROnFECFailureEnabled
                        onToggled: Chiaki.settings.iDROnFECFailureEnabled = !Chiaki.settings.iDROnFECFailureEnabled
                    }
                    C.Hint { text: qsTr("(Unchecked)") }

                    C.RowLabel { text: qsTr("Show Stream Stats During Gameplay") }
                    C.CheckBox {
                        checked: Chiaki.settings.showStreamStats
                        onToggled: Chiaki.settings.showStreamStats = !Chiaki.settings.showStreamStats
                    }
                    C.Hint { text: qsTr("(Unchecked)") }
                }

                C.SettingsSection {
                    title: qsTr("Troubleshooting")
                    icon: "qrc:/icons/settings/general.svg"

                    C.RowLabel { text: qsTr("Audio/Video") }
                    C.ComboBox {
                        Layout.preferredWidth: dialog.controlWidth
                        lastInFocusChain: true
                        model: [qsTr("Audio and Video Enabled"), qsTr("Audio Disabled"), qsTr("Video Disabled"), qsTr("Audio and Video Disabled")]
                        currentIndex: Chiaki.settings.audioVideoDisabled
                        onActivated: index => Chiaki.settings.audioVideoDisabled = index
                    }
                    C.Hint { text: qsTr("(Audio and Video Enabled)") }
                }
            }

            // ───────────────────────── 2 Video ─────────────────────────
            C.SettingsPage {
                id: videoPage
                title: qsTr("Video")
                subtitle: qsTr("Picture quality, the stream window and the renderer")
                icon: "qrc:/icons/menu/display.svg"

                C.SettingsSection {
                    title: qsTr("Picture Quality")
                    description: qsTr("Upscaling makes the picture sharper but needs a stronger graphics card. Also in the stream menu under QUALITY.")
                    icon: "qrc:/icons/settings/effects.svg"

                    C.RowLabel { text: qsTr("Render Preset") }
                    C.ComboBox {
                        id: renderPresetCombo
                        firstInFocusChain: true
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
                    C.Hint { text: qsTr("(High Quality)") }

                    C.RowLabel {
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
                    Item { Layout.preferredHeight: 1; visible: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom }

                    C.RowLabel { text: qsTr("Display Settings") }
                    C.Button {
                        id: displaySettings
                        text: qsTr("Open")
                        onClicked: root.showDisplaySettingsDialog()
                        Material.roundedScale: Material.SmallScale
                    }
                    C.Hint { text: qsTr("HDR, brightness and colors of your screen") }
                }

                C.SettingsSection {
                    title: qsTr("Stream Window")
                    icon: "qrc:/icons/settings/window.svg"

                    C.RowLabel { text: qsTr("Window Type") }
                    C.ComboBox {
                        id: windowTypeCombo
                        Layout.preferredWidth: dialog.controlWidth
                        popup.width: 500
                        model: [qsTr("Stream Resolution"), qsTr("Custom Resolution"), qsTr("Adjust Resolution Manually"), qsTr("Fullscreen"), qsTr("Zoom [adjust zoom using slider in stream menu]"), qsTr("Stretch")]
                        currentIndex: Chiaki.settings.windowType
                        onActivated: (index) => Chiaki.settings.windowType = index;
                    }
                    C.Hint { text: qsTr("(Fullscreen)") }

                    C.RowLabel {
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
                    C.Hint {
                        text: qsTr("(1920)")
                        visible: Chiaki.settings.windowType == 1
                    }

                    C.RowLabel {
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
                    C.Hint {
                        text: qsTr("(1080)")
                        visible: Chiaki.settings.windowType == 1
                    }

                    C.RowLabel { text: qsTr("Toggle Fullscreen on Double-click") }
                    C.CheckBox {
                        checked: Chiaki.settings.fullscreenDoubleClick
                        onToggled: Chiaki.settings.fullscreenDoubleClick = checked
                    }
                    C.Hint { text: qsTr("(Unchecked)") }

                    C.RowLabel { text: qsTr("Hide Cursor during Stream") }
                    C.CheckBox {
                        checked: Chiaki.settings.hideCursor
                        onToggled: Chiaki.settings.hideCursor = !Chiaki.settings.hideCursor
                    }
                    C.Hint { text: qsTr("(Checked)") }
                }

                C.SettingsSection {
                    title: qsTr("Renderer (Advanced)")
                    description: qsTr("Change these only if the picture stutters, tears or PS-WRAP freezes. Some of them restart PS-WRAP.")
                    icon: "qrc:/icons/menu/renderer.svg"

                    C.RowLabel { text: qsTr("Hardware Decoder") }
                    RowLayout {
                        Layout.preferredWidth: dialog.controlWidth
                        spacing: 12

                        C.ComboBox {
                            id: hwDecoderCombo
                            Layout.preferredWidth: Math.round(dialog.controlWidth * 0.55)
                            model: Chiaki.settings.availableDecoders
                            currentIndex: Math.max(0, model.indexOf(Chiaki.settings.decoder))
                            KeyNavigation.priority: KeyNavigation.BeforeItem
                            KeyNavigation.right: zeroCopyCheck
                            onActivated: (index) => Chiaki.settings.decoder = index ? model[index] : ""
                        }

                        Label {
                            text: qsTr("Zero-Copy")
                        }

                        C.CheckBox {
                            id: zeroCopyCheck
                            KeyNavigation.priority: KeyNavigation.BeforeItem
                            KeyNavigation.left: hwDecoderCombo
                            checked: Chiaki.settings.useZeroCopy
                            onToggled: Chiaki.settings.useZeroCopy = checked
                        }
                    }
                    C.Hint { text: qsTr("(Auto)") }

                    C.RowLabel { text: qsTr("Vertical Sync") }
                    C.CheckBox {
                        checked: Chiaki.settings.vSyncEnabled
                        onClicked: {
                            Chiaki.settings.vSyncEnabled = checked
                            if (Chiaki.window.runtimeRendererBackend === 1 && Chiaki.settings.restartApplication())
                                Qt.quit()
                        }
                    }
                    C.Hint { text: qsTr("(Unchecked)") }

                    C.RowLabel { text: qsTr("Frame Delivery") }
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
                    C.Hint { text: qsTr("(Direct Mapping)") }

                    C.RowLabel { text: qsTr("Frame Mixer") }
                    C.ComboBox {
                        Layout.preferredWidth: dialog.controlWidth
                        model: [qsTr("None"), qsTr("Oversample"), qsTr("Hermite"), qsTr("Linear"), qsTr("Cubic")]
                        enabled: !Chiaki.settings.directFrameMapping
                        currentIndex: Chiaki.settings.directFrameMapping ? 0 : Chiaki.settings.placeboFrameMixer
                        onActivated: index => Chiaki.settings.placeboFrameMixer = index
                    }
                    C.Hint { text: qsTr("(None)") }

                    C.RowLabel { text: qsTr("Renderer Backend") }
                    C.ComboBox {
                        Layout.preferredWidth: dialog.controlWidth
                        lastInFocusChain: Chiaki.settings.rendererBackend != 0
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
                    C.Hint { text: qsTr("(Vulkan)") }

                    C.RowLabel {
                        text: qsTr("Vulkan Deferred Swap")
                        visible: Chiaki.settings.rendererBackend == 0
                    }
                    C.CheckBox {
                        lastInFocusChain: true
                        checked: Chiaki.settings.vulkanDeferredSwap
                        onToggled: Chiaki.settings.vulkanDeferredSwap = checked
                        visible: Chiaki.settings.rendererBackend == 0
                    }
                    C.Hint {
                        text: qsTr("(Unchecked)")
                        visible: Chiaki.settings.rendererBackend == 0
                    }
                }
            }

            // ───────────────────────── 3 Audio ─────────────────────────
            C.SettingsPage {
                id: audioPage
                title: qsTr("Audio")
                subtitle: qsTr("Where the game sound plays and which microphone goes to the console")
                icon: "qrc:/icons/menu/volume.svg"
                onVisibleChanged: if (visible) Chiaki.settings.refreshAudioDevices()

                C.SettingsSection {
                    title: qsTr("Speaker")
                    description: qsTr("Also switchable while playing: the speaker button in the stream menu or on the status bar")
                    icon: "qrc:/icons/menu/volume.svg"

                    C.RowLabel { text: qsTr("Output Device") }
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
                    }
                    C.Hint { text: qsTr("(Auto)") }

                    C.RowLabel { text: qsTr("Audio Volume") }
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
                    C.Hint { text: qsTr("(100%)") }

                    C.RowLabel { text: qsTr("Audio Buffer Size") }
                    C.Slider {
                        id: audioBufferSizeSlider
                        Layout.preferredWidth: Math.round(dialog.controlWidth * 0.625)
                        from: 1
                        to: 10
                        stepSize: 1
                        value: Chiaki.settings.audioBufferSize / 1920 ? (Chiaki.settings.audioBufferSize / 1920) : 5
                        onMoved: Chiaki.settings.audioBufferSize = value * 1920;

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
                    C.Hint { text: qsTr("(50 ms) · raise it if the sound crackles") }
                }

                C.SettingsSection {
                    title: qsTr("Microphone")
                    description: qsTr("Your voice goes to the console (party chat and games)")
                    icon: "qrc:/icons/menu/mic.svg"

                    C.RowLabel { text: qsTr("Input Device") }
                    C.ComboBox {
                        id: audioInDevice
                        Layout.preferredWidth: dialog.controlWidth
                        popup.x: (width - popup.width) / 2
                        popup.width: 700
                        popup.font.pixelSize: 16
                        model: [qsTr("Auto")].concat(Chiaki.settings.availableAudioInDevices)
                        currentIndex: Math.max(0, model.indexOf(Chiaki.settings.audioInDevice))
                        onActivated: (index) => Chiaki.settings.audioInDevice = index ? model[index] : ""
                    }
                    C.Hint { text: qsTr("(Auto)") }

                    C.RowLabel { text: qsTr("Start Mic Unmuted") }
                    C.CheckBox {
                        lastInFocusChain: typeof Chiaki.settings.speechProcessing === "undefined"
                        checked: Chiaki.settings.startMicUnmuted
                        onToggled: Chiaki.settings.startMicUnmuted = checked
                    }
                    C.Hint { text: qsTr("(Unchecked)") }

                    C.RowLabel {
                        text: qsTr("Speech Processing")
                        visible: typeof Chiaki.settings.speechProcessing !== "undefined"
                    }
                    C.CheckBox {
                        text: qsTr("Noise suppression + echo cancellation")
                        lastInFocusChain: !Chiaki.settings.speechProcessing
                        checked: Chiaki.settings.speechProcessing
                        onToggled: Chiaki.settings.speechProcessing = !Chiaki.settings.speechProcessing
                        visible: typeof Chiaki.settings.speechProcessing !== "undefined"
                    }
                    C.Hint {
                        text: qsTr("(Unchecked) · turn off when you send music through the mic")
                        visible: typeof Chiaki.settings.speechProcessing !== "undefined"
                    }

                    C.RowLabel {
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
                    C.Hint {
                        text: qsTr("(6 dB)")
                        visible: if (typeof Chiaki.settings.speechProcessing !== "undefined") {Chiaki.settings.speechProcessing} else {false}
                    }

                    C.RowLabel {
                        text: qsTr("Echo To Suppress")
                        visible: if (typeof Chiaki.settings.speechProcessing !== "undefined") {Chiaki.settings.speechProcessing} else {false}
                    }
                    C.Slider {
                        Layout.preferredWidth: Math.round(dialog.controlWidth * 0.625)
                        from: 0
                        to: 60
                        stepSize: 1
                        lastInFocusChain: true
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
                    C.Hint {
                        text: qsTr("(30 dB)")
                        visible: if (typeof Chiaki.settings.speechProcessing !== "undefined") {Chiaki.settings.speechProcessing} else {false}
                    }
                }
            }

            // ───────────────────────── 4 Game presets ─────────────────────────
            Item {
                // PS-WRAP: preset รายเกม แยกไฟล์ (SettingsGamePresetsPage.qml)
                C.SettingsPageHeader {
                    id: gamePresetsHeader
                    anchors { top: parent.top; left: parent.left; right: parent.right; topMargin: Theme.space6; leftMargin: Theme.space8; rightMargin: Theme.space8 }
                    title: qsTr("Game presets")
                    subtitle: qsTr("Settings that switch on by themselves when a particular game is running")
                    icon: "qrc:/icons/settings/games.svg"
                }
                SettingsGamePresetsPage {
                    id: gamePresetsPage
                    anchors { top: gamePresetsHeader.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
                    uiScale: dialog.uiScale
                    labelWidth: dialog.labelWidth
                    controlWidth: dialog.controlWidth
                    confirm: (title, text, callback) => root.showConfirmDialog(title, text, callback)
                }
            }

            // ───────────────────────── 5 Controllers ─────────────────────────
            C.SettingsPage {
                id: controllersPage
                title: qsTr("Controllers")
                subtitle: qsTr("Button mapping, vibration and controller shortcuts")
                icon: "qrc:/icons/controller.svg"

                C.SettingsSection {
                    title: qsTr("Buttons")
                    icon: "qrc:/icons/settings/mapping.svg"

                    C.RowLabel { text: qsTr("Controller Mapping") }
                    RowLayout {
                        spacing: Theme.space3
                        C.Button {
                            id: controllerMappingChange
                            firstInFocusChain: true
                            text: qsTr("Change Controller Mapping")
                            onClicked: controllerMappingDialog.show({
                                reset: false
                            });
                        }
                        C.Button {
                            id: controllerMappingReset
                            text: qsTr("Reset Controller Mapping")
                            onClicked: controllerMappingDialog.show({
                                reset: true
                            });
                        }
                    }
                    Item { Layout.preferredHeight: 1 }

                    C.RowLabel { text: qsTr("Buttons By Position") }
                    C.CheckBox {
                        id: posButtons
                        text: qsTr("Use buttons by position instead of by label")
                        checked: Chiaki.settings.buttonsByPosition
                        onToggled: Chiaki.settings.buttonsByPosition = checked
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        KeyNavigation.left: posButtons
                        KeyNavigation.right: posButtons
                    }
                    C.Hint { text: qsTr("(Unchecked)") }

                    C.RowLabel { text: qsTr("Background Controller Events") }
                    C.CheckBox {
                        id: backgroundController
                        text: qsTr("Process controller input when application is in background")
                        checked: {
                            Chiaki.settings.allowJoystickBackgroundEvents
                        }
                        onToggled: Chiaki.settings.allowJoystickBackgroundEvents = checked
                    }
                    C.Hint { text: qsTr("(Checked)") }
                }

                C.SettingsSection {
                    title: qsTr("Vibration")
                    icon: "qrc:/icons/settings/vibration.svg"

                    C.RowLabel { text: qsTr("Rumble Haptics") }
                    C.ComboBox {
                        id: rumbleHaptics
                        Layout.preferredWidth: dialog.controlWidth
                        model: [qsTr("Off"), qsTr("Very Weak"), qsTr("Weak"), qsTr("Normal"), qsTr("Strong"), qsTr("Very Strong")]
                        currentIndex: Chiaki.settings.rumbleHapticsIntensity
                        onActivated: (index) => Chiaki.settings.rumbleHapticsIntensity = index;
                    }
                    C.Hint { text: qsTr("(Normal)") }

                    C.RowLabel { text: qsTr("True Haptics Intensity") }
                    C.Slider {
                        id: hapticOverride
                        Layout.preferredWidth: Math.round(dialog.controlWidth * 0.625)
                        from: 0
                        to: 2
                        stepSize: 0.1
                        value: Chiaki.settings.hapticOverride
                        onMoved: Chiaki.settings.hapticOverride = value;
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
                    C.Hint { text: qsTr("(console setting)") }

                    C.RowLabel {
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
                    C.Hint {
                        text: qsTr("(Unchecked)")
                        visible: (typeof Chiaki.settings.steamDeckHaptics !== "undefined")
                    }

                    C.RowLabel {
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
                    C.Hint {
                        text: qsTr("(Unchecked)")
                        visible: typeof Chiaki.settings.verticalDeck !== "undefined"
                    }
                }

                C.SettingsSection {
                    title: qsTr("D-pad As Touchpad")
                    description: qsTr("Lets the D-pad act as swipes on the touchpad, for games that need touchpad gestures")
                    icon: "qrc:/icons/settings/touch.svg"

                    C.RowLabel { text: qsTr("Dpad Touchpad Emulation") }
                    C.CheckBox {
                        id: dpadTouch
                        checked: Chiaki.settings.dpadTouchEnabled
                        onToggled: Chiaki.settings.dpadTouchEnabled = !Chiaki.settings.dpadTouchEnabled
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        KeyNavigation.left: dpadTouch
                        KeyNavigation.right: dpadTouch
                        KeyNavigation.down: {
                            if(touchIncrement.visible)
                                touchIncrement
                            else
                                streamMenu
                        }
                    }
                    C.Hint { text: qsTr("(Checked)") }

                    C.RowLabel {
                        text: qsTr("Dpad Touch Increment")
                        visible: Chiaki.settings.dpadTouchEnabled
                    }
                    C.Slider {
                        id: touchIncrement
                        visible: Chiaki.settings.dpadTouchEnabled
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
                        KeyNavigation.down: dpadShortcut1
                    }
                    C.Hint {
                        text: qsTr("(0.3 mm)")
                        visible: Chiaki.settings.dpadTouchEnabled
                    }

                    C.RowLabel {
                        text: qsTr("Dpad Regular/Touch Combo")
                        visible: Chiaki.settings.dpadTouchEnabled
                    }
                    GridLayout {
                        // PS-WRAP: 2x2 ให้กว้างเท่า control อื่น (4 ช่องเรียงแถวเดียวดันหน้าล้นจอเล็ก)
                        columns: 2
                        rowSpacing: Theme.space2
                        columnSpacing: Theme.space2
                        visible: Chiaki.settings.dpadTouchEnabled

                        C.ComboBox {
                            id: dpadShortcut1
                            Layout.preferredWidth: Math.round((dialog.controlWidth - Theme.space2) / 2)
                            firstInFocusChain: false
                            model: dialog.padButtonNames
                            currentIndex: Chiaki.settings.dpadTouchShortcut1
                            onActivated: index => Chiaki.settings.dpadTouchShortcut1 = index
                            KeyNavigation.priority: {
                                if(!popup.visible)
                                    KeyNavigation.BeforeItem
                                else
                                    KeyNavigation.AfterItem
                            }
                            KeyNavigation.up: touchIncrement
                            KeyNavigation.down: dpadShortcut3
                            KeyNavigation.left: dpadShortcut1
                            KeyNavigation.right: dpadShortcut2
                        }

                        C.ComboBox {
                            id: dpadShortcut2
                            Layout.preferredWidth: Math.round((dialog.controlWidth - Theme.space2) / 2)
                            firstInFocusChain: false
                            model: dialog.padButtonNames
                            currentIndex: Chiaki.settings.dpadTouchShortcut2
                            onActivated: index => Chiaki.settings.dpadTouchShortcut2 = index
                            KeyNavigation.priority: {
                                if(!popup.visible)
                                    KeyNavigation.BeforeItem
                                else
                                    KeyNavigation.AfterItem
                            }
                            KeyNavigation.up: touchIncrement
                            KeyNavigation.down: dpadShortcut4
                            KeyNavigation.left: dpadShortcut1
                            KeyNavigation.right: dpadShortcut3
                        }

                        C.ComboBox {
                            id: dpadShortcut3
                            Layout.preferredWidth: Math.round((dialog.controlWidth - Theme.space2) / 2)
                            firstInFocusChain: false
                            model: dialog.padButtonNames
                            currentIndex: Chiaki.settings.dpadTouchShortcut3
                            onActivated: index => Chiaki.settings.dpadTouchShortcut3 = index
                            KeyNavigation.priority: {
                                if(!popup.visible)
                                    KeyNavigation.BeforeItem
                                else
                                    KeyNavigation.AfterItem
                            }
                            KeyNavigation.up: dpadShortcut1
                            KeyNavigation.down: streamMenu
                            KeyNavigation.left: dpadShortcut2
                            KeyNavigation.right: dpadShortcut4
                        }

                        C.ComboBox {
                            id: dpadShortcut4
                            Layout.preferredWidth: Math.round((dialog.controlWidth - Theme.space2) / 2)
                            firstInFocusChain: false
                            model: dialog.padButtonNames
                            currentIndex: Chiaki.settings.dpadTouchShortcut4
                            onActivated: index => Chiaki.settings.dpadTouchShortcut4 = index
                            KeyNavigation.priority: {
                                if(!popup.visible)
                                    KeyNavigation.BeforeItem
                                else
                                    KeyNavigation.AfterItem
                            }
                            KeyNavigation.up: dpadShortcut2
                            KeyNavigation.down: streamMenu
                            KeyNavigation.left: dpadShortcut3
                            KeyNavigation.right: dpadShortcut4
                        }
                    }
                    C.Hint {
                        text: qsTr("(L1+R1+dpad Up)")
                        visible: Chiaki.settings.dpadTouchEnabled
                    }
                }

                C.SettingsSection {
                    title: qsTr("Stream Menu Shortcut")
                    description: qsTr("Hold these buttons together during a stream to open the stream menu")
                    icon: "qrc:/icons/menu/menu.svg"

                    C.RowLabel { text: qsTr("Stream Menu Shortcut Enabled") }
                    C.CheckBox {
                        id: streamMenu
                        checked: Chiaki.settings.streamMenuEnabled
                        onToggled: Chiaki.settings.streamMenuEnabled = !Chiaki.settings.streamMenuEnabled
                        KeyNavigation.priority: KeyNavigation.BeforeItem
                        KeyNavigation.left: streamMenu
                        KeyNavigation.right: streamMenu
                        KeyNavigation.down: {
                            if(streamMenuShortcut1.visible)
                                streamMenuShortcut1
                            else
                                streamMenu
                        }
                    }
                    C.Hint { text: qsTr("(Checked)") }

                    C.RowLabel {
                        text: qsTr("Stream Menu Combo")
                        visible: Chiaki.settings.streamMenuEnabled
                    }
                    GridLayout {
                        // PS-WRAP: 2x2 ให้กว้างเท่า control อื่น (4 ช่องเรียงแถวเดียวดันหน้าล้นจอเล็ก)
                        columns: 2
                        rowSpacing: Theme.space2
                        columnSpacing: Theme.space2
                        visible: Chiaki.settings.streamMenuEnabled

                        C.ComboBox {
                            id: streamMenuShortcut1
                            Layout.preferredWidth: Math.round((dialog.controlWidth - Theme.space2) / 2)
                            firstInFocusChain: false
                            model: dialog.padButtonNames
                            currentIndex: Chiaki.settings.streamMenuShortcut1
                            onActivated: index => Chiaki.settings.streamMenuShortcut1 = index
                            KeyNavigation.priority: {
                                if(!popup.visible)
                                    KeyNavigation.BeforeItem
                                else
                                    KeyNavigation.AfterItem
                            }
                            KeyNavigation.up: streamMenu
                            KeyNavigation.down: streamMenuShortcut3
                            KeyNavigation.left: streamMenuShortcut1
                            KeyNavigation.right: streamMenuShortcut2
                        }

                        C.ComboBox {
                            id: streamMenuShortcut2
                            Layout.preferredWidth: Math.round((dialog.controlWidth - Theme.space2) / 2)
                            firstInFocusChain: false
                            model: dialog.padButtonNames
                            currentIndex: Chiaki.settings.streamMenuShortcut2
                            onActivated: index => Chiaki.settings.streamMenuShortcut2 = index
                            KeyNavigation.priority: {
                                if(!popup.visible)
                                    KeyNavigation.BeforeItem
                                else
                                    KeyNavigation.AfterItem
                            }
                            KeyNavigation.up: streamMenu
                            KeyNavigation.down: streamMenuShortcut4
                            KeyNavigation.left: streamMenuShortcut1
                            KeyNavigation.right: streamMenuShortcut3
                        }

                        C.ComboBox {
                            id: streamMenuShortcut3
                            Layout.preferredWidth: Math.round((dialog.controlWidth - Theme.space2) / 2)
                            firstInFocusChain: false
                            model: dialog.padButtonNames
                            currentIndex: Chiaki.settings.streamMenuShortcut3
                            onActivated: index => Chiaki.settings.streamMenuShortcut3 = index
                            KeyNavigation.priority: {
                                if(!popup.visible)
                                    KeyNavigation.BeforeItem
                                else
                                    KeyNavigation.AfterItem
                            }
                            KeyNavigation.up: streamMenuShortcut1
                            KeyNavigation.down: streamMenuShortcut3
                            KeyNavigation.left: streamMenuShortcut2
                            KeyNavigation.right: streamMenuShortcut4
                        }

                        C.ComboBox {
                            id: streamMenuShortcut4
                            Layout.preferredWidth: Math.round((dialog.controlWidth - Theme.space2) / 2)
                            firstInFocusChain: false
                            model: dialog.padButtonNames
                            currentIndex: Chiaki.settings.streamMenuShortcut4
                            onActivated: index => Chiaki.settings.streamMenuShortcut4 = index
                            KeyNavigation.priority: {
                                if(!popup.visible)
                                    KeyNavigation.BeforeItem
                                else
                                    KeyNavigation.AfterItem
                            }
                            KeyNavigation.up: streamMenuShortcut2
                            KeyNavigation.down: streamMenuShortcut4
                            KeyNavigation.left: streamMenuShortcut3
                            KeyNavigation.right: streamMenuShortcut4
                        }
                    }
                    C.Hint {
                        text: qsTr("(L1+R1+L3+R3)")
                        visible: Chiaki.settings.streamMenuEnabled
                    }
                }
            }

            // ───────────────────────── 6 Keyboard ─────────────────────────
            Item {
                // PS-WRAP: หน้าแยกไฟล์ (SettingsKeysPage.qml)
                id: controllerMapping
                C.SettingsPageHeader {
                    id: keysHeader
                    anchors { top: parent.top; left: parent.left; right: parent.right; topMargin: Theme.space6; leftMargin: Theme.space8; rightMargin: Theme.space8 }
                    title: qsTr("Keyboard")
                    subtitle: qsTr("Which keyboard keys press which controller buttons while you play")
                    icon: "qrc:/icons/settings/keyboard.svg"
                }
                SettingsKeysPage {
                    id: keysPage
                    anchors { top: keysHeader.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
                    uiScale: dialog.uiScale
                    keyDialog: keyDialog
                }
            }

            // ───────────────────────── 7 Consoles ─────────────────────────
            C.SettingsPage {
                id: consolesPage
                title: qsTr("Consoles")
                subtitle: qsTr("Pair a PS5 or PS4 with PS-WRAP, pick one to connect to automatically, or bring back hidden ones")
                icon: "qrc:/icons/settings/consoles.svg"

                C.SettingsSection {
                    title: qsTr("Add A Console")
                    description: qsTr("Finds PlayStation consoles on your network. Have the console on, with Remote Play enabled in its settings.")
                    icon: "qrc:/icons/settings/add.svg"

                    C.Button {
                        id: registerNewButton
                        firstInFocusChain: true
                        text: qsTr("Register New")
                        onClicked: root.showRegistDialog("255.255.255.255", true)
                        Material.roundedScale: Material.SmallScale
                    }
                }

                C.SettingsSection {
                    id: consolesLabel
                    title: qsTr("Registered Consoles")
                    description: qsTr("Auto-Connect starts a stream to that console as soon as PS-WRAP opens")
                    icon: "qrc:/icons/settings/consoles.svg"
                    columns: 1
                    stretch: true

                    Label {
                        visible: consolesView.count === 0
                        text: qsTr("No consoles registered yet")
                        color: Theme.textMuted
                    }

                    ListView {
                        id: consolesView
                        visible: count > 0
                        Layout.fillWidth: true
                        Layout.minimumWidth: 560
                        Layout.preferredHeight: Math.max(1, Math.min(count, 4)) * 80
                        keyNavigationEnabled: false
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
                }

                C.SettingsSection {
                    id: hiddenConsolesLabel
                    visible: hiddenConsolesView.count > 0
                    title: qsTr("Hidden Consoles")
                    description: qsTr("Consoles you hid from the home screen")
                    icon: "qrc:/icons/settings/privacy.svg"
                    columns: 1
                    stretch: true

                    ListView {
                        id: hiddenConsolesView
                        Layout.fillWidth: true
                        Layout.minimumWidth: 560
                        Layout.preferredHeight: Math.max(1, Math.min(count, 4)) * 80
                        keyNavigationEnabled: false
                        clip: true
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

            // ───────────────────────── 8 Remote Play ─────────────────────────
            C.SettingsPage {
                id: remotePage
                title: qsTr("Remote Play")
                subtitle: qsTr("Play away from home over the internet with your PlayStation Network account")
                icon: "qrc:/icons/settings/remote.svg"

                C.SettingsSection {
                    title: qsTr("PlayStation Network")
                    description: resetPsnTokens.visible
                                 ? qsTr("Signed in. PS-WRAP can find your consoles and connect to them over the internet.")
                                 : qsTr("Sign in to find your consoles and connect to them over the internet.")
                    icon: "qrc:/icons/settings/psn.svg"
                    columns: 1

                    C.Button {
                        id: openPsnLogin
                        firstInFocusChain: visible
                        text: qsTr("Login to PSN")
                        onClicked: {
                            root.showPSNTokenDialog(false)
                        }
                        Material.roundedScale: Material.SmallScale
                        visible: !Chiaki.settings.psnRefreshToken || !Chiaki.settings.psnAuthToken || !Chiaki.settings.psnAuthTokenExpiry || !Chiaki.settings.psnAccountId
                    }

                    C.Button {
                        id: resetPsnTokens
                        text: qsTr("Clear PSN Token")
                        firstInFocusChain: !openPsnLogin.visible
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
                }

                C.SettingsSection {
                    title: qsTr("Internet Connection (Advanced)")
                    description: qsTr("Only change these if connecting from outside your home keeps failing")
                    icon: "qrc:/icons/settings/network.svg"

                    C.RowLabel { text: qsTr("Hole Punching Port Guessing") }
                    C.CheckBox {
                        id: holePunchGuessingCheckbox
                        text: qsTr("Force STUN port guessing")
                        checked: Chiaki.settings.portGuessingEnabled
                        onToggled: Chiaki.settings.portGuessingEnabled = checked
                    }
                    C.Hint { text: qsTr("(Unchecked)") }

                    C.RowLabel { text: qsTr("Port Guess Count") }
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
                    C.Hint { text: qsTr("(75)"); Layout.leftMargin: 100 }

                    C.RowLabel { text: qsTr("Port Guess Socket Count") }
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
                    C.Hint { text: qsTr("(250)"); Layout.leftMargin: 100 }
                }
            }

            // ───────────────────────── 9 Facecam ─────────────────────────
            C.SettingsPage {
                id: facecamPage
                title: qsTr("Facecam")
                subtitle: qsTr("Your webcam on top of the game — for recordings, live streams and screenshots")
                icon: "qrc:/icons/menu/cam.svg"

                C.SettingsSection {
                    title: qsTr("Camera")
                    description: qsTr("Toggle in stream: Ctrl+Shift+C cam, Ctrl+Shift+V move")
                    icon: "qrc:/icons/menu/cam.svg"

                    // PS-WRAP: facecam — preview สด (WebcamOverlay ตัวเดียวกับตอนสตรีม: mirror/shape/zoom/key ตามจริง)
                    C.RowLabel {
                        text: qsTr("Facecam Preview")
                        Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                        Layout.topMargin: Theme.space1
                    }
                    Item {
                        id: camPreviewBox
                        readonly property bool onFacecamPage: bar.currentIndex === dialog.pageFacecam && dialog.visible
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
                            active: camPreviewBox.onFacecamPage   // เปิดกล้องเฉพาะตอนอยู่หน้า Facecam (ออกจากหน้า = ปล่อย device)
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
                    C.Hint {
                        Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                        Layout.topMargin: Theme.space1
                        color: camPreview.noVideo ? Theme.danger : Theme.textMuted
                        text: camPreview.statusText
                    }

                    C.RowLabel { text: qsTr("Facecam Camera") }
                    C.ComboBox {
                        id: camDeviceCombo
                        firstInFocusChain: true
                        Layout.preferredWidth: dialog.controlWidth
                        readonly property var names: dialog.allCameraNames()
                        model: [qsTr("System default")].concat(names)
                        currentIndex: Math.max(0, names.indexOf(camPrefs.camDevice) + 1)
                        onActivated: index => camPrefs.camDevice = index > 0 ? names[index - 1] : ""
                        onVisibleChanged: if (visible) dshowLister.refreshDevices()
                    }
                    C.Hint {
                        text: camDeviceCombo.names.length ? qsTr("(System default) · %1 found · virtual cams (NVIDIA Broadcast / OBS) supported").arg(camDeviceCombo.names.length) : qsTr("(System default) · no camera connected")
                    }

                    C.RowLabel { text: qsTr("Facecam Mirror") }
                    C.CheckBox {
                        checked: camPrefs.camMirror
                        onToggled: camPrefs.camMirror = !camPrefs.camMirror
                        text: qsTr("Flip horizontally like a mirror")
                    }
                    C.Hint { text: qsTr("(On)") }

                    C.RowLabel { text: qsTr("Facecam Shape") }
                    C.ComboBox {
                        Layout.preferredWidth: dialog.controlWidth
                        model: [qsTr("Rounded"), qsTr("Circle")]
                        currentIndex: camPrefs.camCircle ? 1 : 0
                        onActivated: index => camPrefs.camCircle = index === 1
                    }
                    C.Hint { text: qsTr("(Rounded)") }
                }

                C.SettingsSection {
                    title: qsTr("Framing")
                    description: qsTr("Crop into your face. In move mode you can also pan with WASD.")
                    icon: "qrc:/icons/menu/zoom.svg"

                    C.RowLabel { text: qsTr("Facecam Zoom") }
                    C.Slider {
                        id: camZoomSlider
                        Layout.preferredWidth: dialog.controlWidth
                        from: 1.0
                        to: 3.0
                        stepSize: 0.1
                        Layout.rightMargin: 48
                        value: camPrefs.camZoom
                        onMoved: camPrefs.camZoom = value
                        Label {
                            anchors.left: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            leftPadding: 10
                            text: camZoomSlider.value.toFixed(1) + "x"
                        }
                    }
                    C.Hint { text: qsTr("(1.0x)") }

                    C.RowLabel { text: qsTr("Facecam Pan") }
                    RowLayout {
                        Layout.preferredWidth: dialog.controlWidth
                        spacing: Theme.space3
                        enabled: camPrefs.camZoom > 1.0
                        Label { text: "X"; color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
                        C.Slider { Layout.fillWidth: true; from: -1; to: 1; stepSize: 0.05; value: camPrefs.camPanX; onMoved: camPrefs.camPanX = value }
                        Label { text: "Y"; color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
                        C.Slider { Layout.fillWidth: true; from: -1; to: 1; stepSize: 0.05; value: camPrefs.camPanY; onMoved: camPrefs.camPanY = value }
                    }
                    C.Hint { text: qsTr("(center) · only when zoomed") }
                }

                C.SettingsSection {
                    title: qsTr("Background And Effects")
                    icon: "qrc:/icons/settings/effects.svg"

                    C.RowLabel { text: qsTr("Facecam Background") }
                    C.ComboBox {
                        Layout.preferredWidth: dialog.controlWidth
                        model: [qsTr("Keep"), qsTr("Remove green screen"), qsTr("Remove blue screen"), qsTr("AI remove (no green screen)")]
                        currentIndex: Chiaki.window.camBackground
                        onActivated: index => Chiaki.window.camBackground = index
                    }
                    C.Hint {
                        color: Chiaki.window.camBackground === 3 && camPreview.aiError.length ? Theme.danger : Theme.textMuted
                        text: Chiaki.window.camBackground === 3
                              ? (camPreview.aiError.length ? camPreview.aiError : (camPreview.aiActive ? qsTr("(Keep) · AI on CPU · %1 ms/frame").arg(camPreview.aiMs) : qsTr("(Keep) · loading AI model…")))
                              : qsTr("(Keep) · chroma key needs a plain green/blue backdrop · AI works anywhere")
                    }

                    C.RowLabel { text: qsTr("Facecam Key Tolerance") }
                    C.Slider {
                        id: camKeyTolSlider
                        Layout.preferredWidth: dialog.controlWidth
                        enabled: Chiaki.window.camBackground > 0
                        from: 0.05
                        to: 0.6
                        stepSize: 0.01
                        Layout.rightMargin: 48
                        value: camPrefs.camKeyTol
                        onMoved: camPrefs.camKeyTol = value
                        Label {
                            anchors.left: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            leftPadding: 10
                            text: camKeyTolSlider.value.toFixed(2)
                        }
                    }
                    C.Hint { text: Chiaki.window.camBackground === 3 ? qsTr("(0.25) · AI edge softness") : qsTr("(0.25) · higher removes more; lower keeps edges") }

                    C.RowLabel { text: qsTr("Facecam Effect") }
                    C.ComboBox {
                        Layout.preferredWidth: dialog.controlWidth
                        lastInFocusChain: true
                        model: [qsTr("None"), qsTr("Sunglasses"), qsTr("Mustache"), qsTr("Clown nose"), qsTr("Crown"), qsTr("Bane mask"), qsTr("Party (glasses + mustache + crown)"), qsTr("Samurai mask"), qsTr("Ninja"), qsTr("Ghost (Tsushima)"), qsTr("Samurai armor (kabuto + mask)"), qsTr("Samurai armor (photo)"), qsTr("Jin mask (private)"), qsTr("Jin mask + headband (private)")]
                        currentIndex: Chiaki.window.camFx
                        onActivated: index => Chiaki.window.camFx = index
                    }
                    C.Hint {
                        color: Chiaki.window.camFx > 0 && camPreview.fxError.length ? Theme.danger : Theme.textMuted
                        text: Chiaki.window.camFx > 0 ? (camPreview.fxError.length ? camPreview.fxError : (camPreview.fxActive ? (camPreview.fxMesh ? qsTr("(None) · face mesh 478 pts · %1 ms").arg(camPreview.fxMs) : qsTr("(None) · basic tracking (face_mesh.onnx missing)")) : qsTr("(None) · looking for a face…"))) : qsTr("(None) · AI face tracking · F in move mode")
                    }
                }
            }

            // ───────────────────────── 10 Recording ─────────────────────────
            C.SettingsPage {
                id: recordingPage
                title: qsTr("Recording")
                subtitle: qsTr("Clips, Instant Replay and screenshots")
                icon: "qrc:/icons/menu/record.svg"

                // PS-WRAP: อัดคลิป — โฟลเดอร์ปลายทาง + ขนาดภาพของคลิป / Instant Replay / Go Live (16:9)
                C.SettingsSection {
                    title: qsTr("Clips And Replays")
                    description: qsTr("Recordings capture the 16:9 game picture with every visible overlay (pad, facecam, stats, mic spectrum), plus game audio and your microphone while it is unmuted. The window size and black bars do not matter. 1440p and 4K are upscaled from the stream with the upscaler picked under QUALITY in the stream menu (HQ + Spatial / HQ + Adv) — YouTube gives 4K uploads a much higher bitrate. HDR streams are recorded as HDR (HEVC 10-bit). Toggle in stream: Ctrl+Shift+R or Record in the stream menu.")
                    icon: "qrc:/icons/menu/record.svg"

                    C.RowLabel { text: qsTr("Recording Folder") }
                    C.TextField {
                        id: recFolderField
                        firstInFocusChain: true
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

                    C.RowLabel { text: qsTr("Output Resolution") }
                    C.ComboBox {
                        readonly property var heights: [0, 1440, 2160]
                        Layout.preferredWidth: dialog.controlWidth
                        lastInFocusChain: true
                        model: [qsTr("Same as stream"), qsTr("1440p (upscaled)"), qsTr("4K (upscaled)")]
                        currentIndex: Math.max(0, heights.indexOf(Chiaki.window.captureHeight))
                        onActivated: index => Chiaki.window.captureHeight = heights[index]
                    }
                    C.Hint { text: qsTr("(Same as stream) · clips, replay, live") }
                }
            }

            // ───────────────────────── 11 Go Live ─────────────────────────
            Item {
                // PS-WRAP: ปลายทางไลฟ์ + stream key แยกไฟล์ (GoLiveSettingsPage.qml · C++ Chiaki.goLive)
                C.SettingsPageHeader {
                    id: goLiveHeader
                    anchors { top: parent.top; left: parent.left; right: parent.right; topMargin: Theme.space6; leftMargin: Theme.space8; rightMargin: Theme.space8 }
                    title: qsTr("Go Live")
                    subtitle: qsTr("Stream your game to YouTube, Twitch, Facebook and more at the same time")
                    icon: "qrc:/icons/menu/live.svg"
                }
                GoLiveSettingsPage {
                    id: goLivePage
                    anchors { top: goLiveHeader.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
                    uiScale: dialog.uiScale
                }
            }

            // ───────────────────────── 12 System ─────────────────────────
            C.SettingsPage {
                id: systemPage
                title: qsTr("System")
                subtitle: qsTr("Settings profiles, backup, logs and information about PS-WRAP")
                icon: "qrc:/icons/menu/settings.svg"

                C.SettingsSection {
                    title: qsTr("Profiles And Backup")
                    description: qsTr("A profile is a complete set of settings you can switch between (for example one for each TV)")
                    icon: "qrc:/icons/settings/profile.svg"

                    C.RowLabel { text: qsTr("Current Profile") }
                    Label {
                        text: Chiaki.settings.currentProfile ? Chiaki.settings.currentProfile : qsTr("default")
                        font.weight: Font.DemiBold
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

                    C.RowLabel { text: qsTr("Settings File") }
                    RowLayout {
                        spacing: Theme.space2
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
                    }
                    Item { Layout.preferredHeight: 1 }
                }

                C.SettingsSection {
                    title: qsTr("Logs")
                    description: qsTr("Logs help find problems. PS-WRAP-Diagnostics.exe collects them for you.")
                    icon: "qrc:/icons/settings/logs.svg"

                    C.RowLabel { text: qsTr("Log Directory") }
                    Label {
                        // PS-WRAP: กว้างเท่า control อื่น — ฟอนต์ย่อให้พอดีด้วย HorizontalFit
                        Layout.preferredWidth: dialog.controlWidth
                        Layout.maximumWidth: dialog.controlWidth
                        text: Chiaki.settings.logDirectory
                        verticalAlignment: Text.AlignVCenter
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 10
                        elide: Text.ElideMiddle
                        color: Theme.textMuted
                    }
                    C.Button {
                        id: openButton
                        text: qsTr("Open")
                        onClicked: Qt.openUrlExternally("file://" + Chiaki.settings.logDirectory);
                        Material.roundedScale: Material.SmallScale
                    }

                    C.RowLabel { text: qsTr("Sanitize Logs") }
                    C.CheckBox {
                        text: qsTr("Remove addresses and account info from the log")
                        checked: Chiaki.settings.logSanitize
                        onToggled: Chiaki.settings.logSanitize = checked
                    }
                    C.Hint { text: qsTr("(Checked)") }

                    C.RowLabel { text: qsTr("Verbose Logging") }
                    C.CheckBox {
                        text: qsTr("Write much more detail (only while hunting a problem)")
                        checked: Chiaki.settings.logVerbose
                        onToggled: Chiaki.settings.logVerbose = checked
                    }
                    C.Hint { text: qsTr("(Unchecked)") }
                }

                C.SettingsSection {
                    title: qsTr("About")
                    icon: "qrc:/icons/menu/info.svg"

                    C.RowLabel { text: qsTr("Version, credits and licenses") }
                    C.Button {
                        id: aboutButton
                        lastInFocusChain: true
                        text: qsTr("About PS-WRAP")
                        onClicked: aboutDialog.open()
                        Material.roundedScale: Material.SmallScale
                    }
                    Item { Layout.preferredHeight: 1 }
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

        // PS-WRAP: หน้า About ใหม่ (AboutDialog.qml) — เครดิต chiaki-ng / Chiaki + license · เดิมขึ้น "About PS-WRAP-ng" และบอกว่าเป็น chiaki-ng
        AboutDialog {
            id: aboutDialog
            returnFocusTo: aboutButton
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
