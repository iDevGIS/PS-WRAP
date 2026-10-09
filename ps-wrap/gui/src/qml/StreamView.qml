import QtQuick
import QtCore
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Window

import org.streetpea.chiaking

import "controls" as C

Item {
    id: view

    readonly property var hostWindow: view.Window.window
    property bool sessionError: false
    property bool sessionLoading: true
    property list<Item> restoreFocusItems
    readonly property bool useSeparateMenuWindow: Chiaki.window.runtimeRendererBackend === 1
    // PS-WRAP: ความสูงเมนูตามเนื้อหา (แถวตัวเลือกห่อบรรทัดเมื่อแคบ) — เดิม fix 200
    readonly property int streamMenuHeight: useSeparateMenuWindow
        ? (separateMenuWindow.contentImplicitHeight > 0 ? separateMenuWindow.contentImplicitHeight : 200)
        : (inlineMenuContent.item && inlineMenuContent.item.implicitHeight > 0 ? inlineMenuContent.item.implicitHeight : 200)
    onStreamMenuHeightChanged: if (useSeparateMenuWindow) updateSeparateMenuGeometry()
    readonly property bool streamStatsVisible: Chiaki.settings.showStreamStats && Chiaki.session && !(menuController.open || menuController.closing) && !sessionLoading && !sessionError && !(Chiaki.settings.audioVideoDisabled & 0x02)
    // PS-WRAP: overlay จอยขณะสตรีม (BudToZai DualSense tracker) — เปิด/ปิดจากเมนูสตรีม จำค่าใน QSettings กลุ่ม pswrap
    // PS-WRAP: overlay ทุกตัวอิงพื้นที่จริง — baseW = ความกว้างของกรอบ 16:9 ที่พอดีกับ view (หน้าต่างแคบ-สูงไม่ทำให้ overlay ใหญ่เกิน)
    // กรอบวิดีโอจริง: โหมด Normal คงสัดส่วน 16:9 ตรงกลาง (แถบดำรอบๆ) · Zoom/Stretch เต็ม view
    readonly property bool videoFitsView: Chiaki.window.videoMode !== ChiakiWindow.VideoMode.Normal
    readonly property real videoW: videoFitsView ? view.width : Math.min(view.width, view.height * 16 / 9)
    readonly property real videoH: videoFitsView ? view.height : Math.min(view.height, view.width * 9 / 16)
    readonly property real videoX: Math.round((view.width - videoW) / 2)
    readonly property real videoY: Math.round((view.height - videoH) / 2)
    readonly property real overlayBaseW: Math.max(320, videoW)
    readonly property real overlayScale: Math.max(0.45, Math.min(1.5, overlayBaseW / 1920))
    readonly property bool webcamVisible: Chiaki.window.camOverlay && Chiaki.session && !sessionLoading && !sessionError && !(menuController.open || menuController.closing)
    readonly property bool controllerOverlayVisible: Chiaki.window.padOverlay && Chiaki.session && !sessionLoading && !sessionError && !(menuController.open || menuController.closing) && Chiaki.controllers.length > 0
    // PS-WRAP: mic spectrum overlay (MicSpectrumOverlay.qml) — เงื่อนไขเดียวกับ facecam
    readonly property bool micOverlayVisible: Chiaki.window.micOverlay && Chiaki.session && !sessionLoading && !sessionError && !(menuController.open || menuController.closing)
    // PS-WRAP: นาฬิกา + เวลาเล่น (ClockOverlay.qml) — เงื่อนไขเดียวกับ mic · เวลาเล่นนับจากตอน session connected (sessionStartMs)
    readonly property bool clockOverlayVisible: !!Chiaki.window.clockOverlay && Chiaki.session && !sessionLoading && !sessionError && !(menuController.open || menuController.closing)
    // PS-WRAP: แชทไลฟ์ (ChatOverlay.qml) — เงื่อนไขเดียวกับนาฬิกา · ตัวดึงแชทต่อเองเมื่อเปิด overlay ระหว่างสตรีม (C++)
    readonly property bool chatOverlayVisible: !!Chiaki.window.chatOverlay && Chiaki.session && !sessionLoading && !sessionError && !(menuController.open || menuController.closing)
    property double sessionStartMs: 0
    function noteSessionStart() {
        if (!Chiaki.session || !Chiaki.session.connected)
            sessionStartMs = 0;
        else if (sessionStartMs <= 0)
            sessionStartMs = Date.now();
    }
    // PS-WRAP: อัดคลิป (Chiaki.window.recorder) — REC pill + toast อยู่ท้ายไฟล์
    readonly property QtObject recorder: Chiaki.window ? Chiaki.window.recorder : null
    property int separateMenuX: 0
    property int separateMenuY: 0
    property int separateMenuWidth: 0
    property int separateStatsX: 0
    property int separateStatsY: 0
    property int separateStatsWidth: 0
    property int separateStatsHeight: 0
    property int separateDialogX: 0
    property int separateDialogY: 0
    property bool sessionStopDialogActive: false
    property bool sessionPinDialogActive: false

    function grabInput(item) {
        Chiaki.window.grabInput();
        restoreFocusItems.push(hostWindow ? hostWindow.activeFocusItem : null);
        if (item)
            item.forceActiveFocus(Qt.TabFocusReason);
    }

    function releaseInput() {
        Chiaki.window.releaseInput();
        let item = restoreFocusItems.pop();
        if (item && item.visible)
            item.forceActiveFocus(Qt.TabFocusReason);
    }

    function updateSeparateMenuGeometry() {
        if (!hostWindow)
            return;
        const topLeft = view.mapToGlobal(0, 0);
        if (useSeparateMenuWindow) {
            separateMenuX = Math.round(topLeft.x);
            separateMenuY = Math.round(topLeft.y + view.height - streamMenuHeight);
            separateMenuWidth = Math.round(view.width);
            separateStatsX = Math.round(topLeft.x);
            separateStatsY = Math.round(topLeft.y);
            separateStatsWidth = Math.round(view.width);
            separateStatsHeight = Math.round(view.height);
        }
    }

    function updateSeparateDialogGeometry(width, height) {
        if (!useSeparateMenuWindow || !hostWindow)
            return;
        separateDialogX = Math.round(hostWindow.x + (hostWindow.width - width) / 2);
        separateDialogY = Math.round(hostWindow.y + (hostWindow.height - height) / 2);
    }

    function updateOverlayInteractionActive() {
        Chiaki.window.setOverlayInteractionActive(
            overlayEditMode ||
            camEditMode ||
            statsEditMode ||
            micEditMode ||
            clockEditMode ||
            chatEditMode ||
            menuController.open ||
            menuController.closing ||
            sessionStopDialogActive ||
            separateSessionStopWindow.visible ||
            sessionPinDialogActive ||
            separateSessionPinWindow.visible
        );
    }

    StackView.onActivating: {
        Chiaki.window.keepVideo = true;
        sessionError = false;
        errorTitleLabel.text = "";
        errorTextLabel.text = "";
        sessionLoading = !(Chiaki.window.loadingTransitionComplete || (Chiaki.settings.audioVideoDisabled & 0x02));
    }
    StackView.onDeactivated: { Chiaki.window.keepVideo = false; if (overlayEditMode) view.stopOverlayEdit(); if (camEditMode) view.stopCamEdit(); if (statsEditMode) view.stopStatsEdit(); if (micEditMode) view.stopMicEdit(); if (clockEditMode) view.stopClockEdit(); if (chatEditMode) view.stopChatEdit(); }

    Component.onCompleted: {
        noteSessionStart();
        updateSeparateMenuGeometry();
        updateOverlayInteractionActive();
        Chiaki.window.setStatsOverlayActive(streamStatsVisible && useSeparateMenuWindow);   // PS-WRAP: widget แยกเฉพาะ OpenGL — Vulkan วาด StatsOverlay inline
    }
    onStreamStatsVisibleChanged: {
        if (Chiaki.window)
            Chiaki.window.setStatsOverlayActive(streamStatsVisible && useSeparateMenuWindow);
    }
    onWidthChanged: updateSeparateMenuGeometry()
    onHeightChanged: updateSeparateMenuGeometry()
    onUseSeparateMenuWindowChanged: { updateSeparateMenuGeometry(); if (Chiaki.window) Chiaki.window.setStatsOverlayActive(streamStatsVisible && useSeparateMenuWindow); }

    Connections {
        target: view.hostWindow
        function onXChanged() { view.updateSeparateMenuGeometry() }
        function onYChanged() { view.updateSeparateMenuGeometry() }
        function onWidthChanged() { view.updateSeparateMenuGeometry() }
        function onHeightChanged() { view.updateSeparateMenuGeometry() }
        function onVisibilityChanged() { view.updateSeparateMenuGeometry() }
    }

    QtObject {
        id: menuController
        property bool closing: false
        property bool open: false

        property real lastToggleTime: 0
        function toggle() {
            var now = Date.now();
            if (now - lastToggleTime < 200)
                return;
            lastToggleTime = now;
            if (open)
                close();
            else {
                if (useSeparateMenuWindow) {
                    view.updateSeparateMenuGeometry();
                    view.grabInput(null);
                }
                open = true;
            }
            view.updateOverlayInteractionActive();
        }

        function close() {
            if (!open || closing)
                return;
            closing = true;
            open = false;
            view.releaseInput();
            view.updateOverlayInteractionActive();
        }
    }

    Rectangle {
        id: loadingView
        anchors.fill: parent
        color: "black"
        opacity: sessionError || sessionLoading || (Chiaki.settings.audioVideoDisabled & 0x02) ? 1.0 : 0.0
        visible: opacity

        Behavior on opacity { NumberAnimation { duration: 250 } }

        Item {
            anchors {
                top: parent.verticalCenter
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }

            BusyIndicator {
                id: spinner
                anchors.centerIn: parent
                width: 70
                height: width
                visible: sessionLoading
                running: sessionLoading
            }

            Label {
                anchors {
                    top: spinner.bottom
                    horizontalCenter: spinner.horizontalCenter
                    topMargin: 30
                }
                text: {
                    if(Chiaki.settings.dpadTouchEnabled)
                    {
                        if(Chiaki.settings.audioVideoDisabled == 0x01)
                            qsTr("Audio Disabled in settings\n") + qsTr("Press %1 to open stream menu").arg(Chiaki.controllers.length ? Chiaki.settings.stringForStreamMenuShortcut() : "Ctrl+O") + "\n" + qsTr("Press %1 to toggle between regular dpad and dpad touch").arg(Chiaki.settings.stringForDpadShortcut())
                        else
                            qsTr("Press %1 to open stream menu").arg(Chiaki.controllers.length ? Chiaki.settings.stringForStreamMenuShortcut() : "Ctrl+O") + "\n" + qsTr("Press %1 to toggle between regular dpad and dpad touch").arg(Chiaki.settings.stringForDpadShortcut())
                    }
                    else
                    {
                        if(Chiaki.settings.audioVideoDisabled == 0x01)
                            qsTr("Audio Disabled in settings\n") + qsTr("Press %1 to open stream menu").arg(Chiaki.controllers.length ? Chiaki.settings.stringForStreamMenuShortcut() : "Ctrl+O")
                        else
                            qsTr("Press %1 to open stream menu").arg(Chiaki.controllers.length ? Chiaki.settings.stringForStreamMenuShortcut() : "Ctrl+O")
                    }
                }
                visible: sessionLoading
            }

            Label {
                id: audioVideoDisabledTitleLabel
                anchors {
                    bottom: spinner.top
                    horizontalCenter: spinner.horizontalCenter
                }
                text: (Chiaki.settings.audioVideoDisabled & 0x01) ? qsTr("Audio and Video Disabled") : qsTr("Video Disabled")
                font.pixelSize: 24
                visible: !sessionLoading && !sessionError && (Chiaki.settings.audioVideoDisabled & 0x02)
            }

            Label {
                id: audioVideoDisabledTextLabel
                anchors {
                    top: audioVideoDisabledTitleLabel.bottom
                    horizontalCenter: audioVideoDisabledTitleLabel.horizontalCenter
                    topMargin: 10
                }
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 20
                text: (Chiaki.settings.audioVideoDisabled & 0x01) ? qsTr("You have disabled audio and video in your settings.\nTo re-enable change Audio/Video to Audio and Video Enabled in the General tab of the settings.") : qsTr("You have disabled video in your settings.\nTo re-enable change Audio/Video to Audio and Video Enabled in the General tab of the settings.")
                visible: !sessionLoading && !sessionError && (Chiaki.settings.audioVideoDisabled & 0x02)
            }

            Label {
                id: errorTitleLabel
                anchors {
                    bottom: spinner.top
                    horizontalCenter: spinner.horizontalCenter
                }
                font.pixelSize: 24
                visible: text
                onVisibleChanged: if (visible) view.grabInput(errorTitleLabel)
                Keys.onReturnPressed: root.showMainView()
                Keys.onEscapePressed: root.showMainView()
            }

            Label {
                id: errorTextLabel
                anchors {
                    top: errorTitleLabel.bottom
                    horizontalCenter: errorTitleLabel.horizontalCenter
                    topMargin: 10
                }
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 20
                visible: text
            }
        }
    }

    ColumnLayout {
        id: cantDisplayMessage
        anchors.centerIn: parent
        opacity: Chiaki.window.hasVideo && Chiaki.session && Chiaki.session.cantDisplay ? 1.0 : 0.0
        visible: opacity
        spacing: 30

        Behavior on opacity { NumberAnimation { duration: 250 } }

        onVisibleChanged: {
            if (visible) {
                menuController.close();
                view.grabInput(goToHomeButton);
            } else {
                view.releaseInput();
            }
        }

        Label {
            Layout.alignment: Qt.AlignCenter
            text: qsTr("The screen contains content that can't be displayed using Remote Play.")
        }

        Button {
            id: goToHomeButton
            Layout.alignment: Qt.AlignCenter
            Layout.preferredHeight: 60
            text: qsTr("Go to Home Screen")
            Material.background: activeFocus ? parent.Material.accent : undefined
            Material.roundedScale: Material.SmallScale
            onClicked: Chiaki.sessionGoHome()
            Keys.onReturnPressed: clicked()
            Keys.onEscapePressed: clicked()
        }
    }

    RoundButton {
        anchors {
            right: parent.right
            top: parent.top
            margins: 40
        }
        icon.source: "qrc:/icons/discover-off-24px.svg"
        icon.width: 50
        icon.height: 50
        padding: 20
        checked: true
        opacity: networkIndicatorTimer.running ? 0.7 : 0.0
        visible: opacity
        Material.background: Material.accent

        Behavior on opacity { NumberAnimation { duration: 400 } }

        Timer {
            id: networkIndicatorTimer
            running: Chiaki.session?.averagePacketLoss > (Chiaki.settings.wifiDroppedNotif * 0.01)
            interval: 400
        }
    }

    Component {
        id: streamStatsContent
        Item {
            id: streamStatsContentRoot
            anchors.fill: parent
            ColumnLayout {
                anchors {
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    rightMargin: 5
                }

                Label {
                    Layout.alignment: Qt.AlignRight
                    text: "Mbps"
                    font.pixelSize: 18
                    visible: Chiaki.session ? true : false

                    Label {
                        anchors {
                            right: parent.left
                            baseline: parent.baseline
                            rightMargin: 5
                        }
                        text: parent.visible ? Chiaki.session.measuredBitrate.toFixed(1) : ""
                        color: Material.accent
                        font.bold: true
                        font.pixelSize: 28
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("queue depth avg")
                    font.pixelSize: 15
                    opacity: Chiaki.session ? 1 : 0
                    visible: opacity > 0

                    Behavior on opacity { NumberAnimation { duration: 250 } }

                    Label {
                        anchors {
                            right: parent.left
                            baseline: parent.baseline
                            rightMargin: 5
                        }
                        text: parent.visible ? Chiaki.window.queueDepthAverage.toFixed(1) : ""
                        font.bold: true
                        color: "#90caf9"
                        font.pixelSize: 18
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("pending frame age")
                    font.pixelSize: 15
                    opacity: Chiaki.session ? 1 : 0
                    visible: opacity > 0

                    Behavior on opacity { NumberAnimation { duration: 250 } }

                    Label {
                        anchors {
                            right: parent.left
                            baseline: parent.baseline
                            rightMargin: 5
                        }
                        text: parent.visible ? qsTr("%1 ms").arg((Chiaki.window.pendingFrameAge * 1000.0).toFixed(0)) : ""
                        font.bold: true
                        color: "#90caf9"
                        font.pixelSize: 18
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignRight
                    id: statsPacketLossLabel
                    text: qsTr("packet loss")
                    font.pixelSize: 15
                    opacity: Chiaki.session ? 1 : 0
                    visible: opacity > 0

                    Behavior on opacity { NumberAnimation { duration: 250 } }

                    Label {
                        anchors {
                            right: parent.left
                            baseline: parent.baseline
                            rightMargin: 5
                        }
                        text: parent.visible ? "%1<font size=\"1\">%</font>".arg((((Chiaki.session && isFinite(Chiaki.session.averagePacketLoss)) ? Chiaki.session.averagePacketLoss : 0) * 100).toFixed(1)) : ""
                        font.bold: true
                        color: "#ef9a9a"
                        font.pixelSize: 18
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("dropped frames")
                    font.pixelSize: 15
                    opacity: Chiaki.session ? 1 : 0
                    visible: opacity > 0

                    Behavior on opacity { NumberAnimation { duration: 250 } }

                    Label {
                        id: statsDroppedFramesLabel
                        anchors {
                            right: parent.left
                            baseline: parent.baseline
                            rightMargin: 5
                        }
                        text: parent.visible ? Chiaki.window.droppedFrames : ""
                        color: "#ef9a9a"
                        font.bold: true
                        font.pixelSize: 18
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("lost frames")
                    font.pixelSize: 15
                    opacity: Chiaki.session ? 1 : 0
                    visible: opacity > 0

                    Behavior on opacity { NumberAnimation { duration: 250 } }

                    Label {
                        anchors {
                            right: parent.left
                            baseline: parent.baseline
                            rightMargin: 5
                        }
                        text: parent.visible ? ((Chiaki.session && isFinite(Chiaki.session.framesLost)) ? Chiaki.session.framesLost : 0) : ""
                        color: "#ef9a9a"
                        font.bold: true
                        font.pixelSize: 18
                    }
                }
            }
        }
    }

    // PS-WRAP: เมนู inline ใช้ StreamMenuContent ร่วมกับ StreamMenuWindow (เดิม upstream เขียนซ้ำ 2 ชุด)
    Component {
        id: menuContentComponent

        StreamMenuContent {
            anchors.fill: parent
            overlayEnabled: Chiaki.window.padOverlay
            onCloseRequested: menuController.close()
            onDisplaySettingsRequested: root.openDisplaySettings()
            onPlaceboSettingsRequested: root.openPlaceboSettings()
            onMainViewRequested: root.showMainView()
            onOverlayToggled: Chiaki.window.padOverlay = !Chiaki.window.padOverlay
            onOverlayEditRequested: { menuController.close(); view.startOverlayEdit(); }
            webcamEnabled: Chiaki.window.camOverlay
            onWebcamToggled: Chiaki.window.camOverlay = !Chiaki.window.camOverlay
            onWebcamEditRequested: { menuController.close(); view.startCamEdit(); }
            onMicEditRequested: { menuController.close(); view.startMicEdit(); }
            onClockEditRequested: { menuController.close(); view.startClockEdit(); }
            onChatEditRequested: { menuController.close(); view.startChatEdit(); }
            dockEnabled: view.dockOn
            onDockToggled: view.setDock(!view.dockOn)
            onDockEditRequested: { menuController.close(); view.startDockEdit(); }
        }
    }

    Item {
        id: menuView
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: streamMenuHeight
        y: menuController.open ? parent.height - height : parent.height
        visible: !useSeparateMenuWindow && (menuController.open || menuController.closing)
        enabled: !useSeparateMenuWindow && menuController.open
        onVisibleChanged: {
            if (visible)
                view.grabInput(inlineMenuContent.item ? inlineMenuContent.item.initialFocusItem : null);
        }

        Behavior on y {
            NumberAnimation {
                id: inlineMenuAnimation
                duration: 250
                onRunningChanged: {
                    if (!running && !menuController.open) {
                        menuController.closing = false;
                        view.updateOverlayInteractionActive();
                    }
                }
            }
        }

        Loader {
            id: inlineMenuContent
            anchors.fill: parent
            active: !useSeparateMenuWindow
            sourceComponent: menuContentComponent
        }
    }

    Settings {
        id: pswrapPrefs
        category: "pswrap"
        property real overlayX: -1      // สัดส่วนของความกว้าง view (-1 = ค่าเริ่มต้น ขวาล่าง)
        property real overlayY: -1
        property real overlayW: 0.18    // ความกว้าง overlay เป็นสัดส่วนของ view
        // facecam (webcam overlay)
        property real camX: -1          // สัดส่วนของ view (-1 = ค่าเริ่มต้น ซ้ายล่าง)
        property real camY: -1
        property real camW: 0.16
        property bool camMirror: true
        property bool camCircle: false
        property string camDevice: ""
        property real camZoom: 1.0      // ครอปดิจิทัล 1–3
        property real camPanX: 0.0      // -1..1
        property real camPanY: 0.0
        property real camKeyTol: 0.25
        property real statsX: -1        // สัดส่วนของกรอบวิดีโอ (-1 = ค่าเริ่มต้น ขวากลาง)
        property real statsY: -1
        property real statsScale: 1.0   // คูณกับ overlayScale
        // mic spectrum overlay
        property real micX: -1          // สัดส่วนของกรอบวิดีโอ (-1 = ค่าเริ่มต้น กลางล่าง)
        property real micY: -1
        property real micW: 0.17        // ความกว้างเป็นสัดส่วนของ overlayBaseW (≈320px ที่ 1920)
        // clock overlay (นาฬิกา + เวลาเล่น)
        property real clockX: -1        // สัดส่วนของกรอบวิดีโอ (-1 = ค่าเริ่มต้น ขวาบน)
        property real clockY: -1
        property real clockW: 0.13      // ความกว้างเป็นสัดส่วนของ overlayBaseW (≈250px ที่ 1920)
        // chat overlay (แชทไลฟ์)
        property real chatX: -1         // สัดส่วนของกรอบวิดีโอ (-1 = ค่าเริ่มต้น ซ้ายกลาง)
        property real chatY: -1
        property real chatW: 0.18       // ≈345px ที่ 1920
        // PS-WRAP: Stack — overlay ทุกตัวเรียงเป็นคอลัมน์เดียว กว้างเท่ากัน (ตำแหน่งอิสระด้านบนยังเก็บไว้ ปิด Stack แล้วกลับมา)
        property bool dockOn: false
        property bool dockRight: true
        property real dockW: 0.14        // ความกว้างคอลัมน์ เป็นสัดส่วนของกรอบวิดีโอ
        property real dockY: 0.03        // ขอบบนของคอลัมน์ เป็นสัดส่วนของความสูงกรอบวิดีโอ
        property string dockOrder: "clock,mic,stats,chat,cam,pad"
        property bool statusBarPinned: false   // PS-WRAP: แถบสถานะค้างบนจอ (ไม่ปัก = โผล่เมื่อเมาส์ไปขอบล่าง)
    }

    // ---- edit mode ของ overlay: ขอ input คืนจากเกมชั่วคราว ลากด้วยเมาส์/ลูกศร/จอย, มุมขวาล่างย่อขยาย, L1/R1 หรือ +/- ย่อขยาย, Esc/◯ หรือ Enter/✕ เสร็จ ----
    property bool overlayEditMode: false
    function startOverlayEdit() {
        console.log("PSWRAP startOverlayEdit sep=", useSeparateMenuWindow, "session=", !!Chiaki.session, "edit=", overlayEditMode, "pref=", Chiaki.window.padOverlay);
        if (view.dockOn) { view.startDockEdit(); return; }   // PS-WRAP: Stack
        if (useSeparateMenuWindow || !Chiaki.session || overlayEditMode)
            return;
        Chiaki.window.padOverlay = true;
        overlayEditMode = true;
        updateOverlayInteractionActive();
        view.grabInputOnce(overlayFrame);
    }
    function stopOverlayEdit() {
        console.log("PSWRAP stopOverlayEdit edit=", overlayEditMode);
        if (!overlayEditMode)
            return;
        overlayEditMode = false;
        // gotcha: ปล่อย input ทันทีทำให้ key release (Esc = ปุ่ม PS ใน keyboard map ของ upstream) หลุดไปเกม → หน่วง 250ms
        releaseAfterEdit.restart();
    }
    Connections {
        target: Chiaki.window
        function onActiveChanged() {
            if (Chiaki.window.active)
                view.windowActivatedMs = Date.now();
            // สลับไปหน้าต่างอื่นระหว่างแก้ = จบทุกตัว (คืนจอยให้เกม ไม่ค้างโหมดแก้ไว้เบื้องหลัง)
            else if (view.anyOverlayEdit)
                view.stopAllOverlayEdit();
        }
    }
    onOverlayEditModeChanged: if (overlayEditMode) overlayFrame.forceActiveFocus(Qt.TabFocusReason)
    Timer {
        id: releaseAfterEdit
        interval: 250
        onTriggered: {
            // ปล่อยเฉพาะ grab ของโหมดแก้ (ครั้งเดียว) — ไม่ไปปล่อย grab ของเมนู/dialog
            if (!view.anyOverlayEdit && view.editInputGrabbed) { view.editInputGrabbed = false; view.releaseInput(); }
            view.updateOverlayInteractionActive();
        }
    }
    // ---- stats: edit mode (ลาก/ย่อขยาย) — เหมือน pad/cam ----
    property bool statsEditMode: false
    function startStatsEdit() {
        if (view.dockOn) { view.startDockEdit(); return; }   // PS-WRAP: Stack
        if (useSeparateMenuWindow || !Chiaki.session || statsEditMode)
            return;
        if (overlayEditMode) view.stopOverlayEdit();
        if (camEditMode) view.stopCamEdit();
        if (micEditMode) view.stopMicEdit();
        if (clockEditMode) view.stopClockEdit();
        if (chatEditMode) view.stopChatEdit();
        Chiaki.window.statsOverlay = true;
        statsEditMode = true;
        updateOverlayInteractionActive();
        view.grabInputOnce(statsFrame);
    }
    function stopStatsEdit() {
        if (!statsEditMode)
            return;
        statsEditMode = false;
        statsFrame.save();
        releaseAfterEdit.restart();
    }
    onStatsEditModeChanged: if (statsEditMode) statsFrame.forceActiveFocus(Qt.TabFocusReason)
    // คลิกที่ overlay = เข้าโหมดแก้ตัวนั้น (สลับจากตัวอื่นได้ทันที) · คลิกที่ว่าง = จบโหมดแก้
    readonly property bool anyOverlayEdit: overlayEditMode || camEditMode || statsEditMode || micEditMode || clockEditMode || chatEditMode || dockEditMode
    function editOverlay(which) {
        if (view.dockOn) { view.startDockEdit(); return; }   // PS-WRAP: Stack = แก้ทั้งคอลัมน์ทีเดียว
        if (which !== "pad" && overlayEditMode) { overlayEditMode = false; }
        if (which !== "cam" && camEditMode) { camEditMode = false; camFrame.save(); }
        if (which !== "stats" && statsEditMode) { statsEditMode = false; statsFrame.save(); }
        if (which !== "mic" && micEditMode) { micEditMode = false; micFrame.save(); }
        if (which !== "clock" && clockEditMode) { clockEditMode = false; clockFrame.save(); }
        if (which !== "chat" && chatEditMode) { chatEditMode = false; chatFrame.save(); }
        if (which === "pad") { if (!overlayEditMode) { overlayEditMode = true; Chiaki.window.padOverlay = true; } view.grabInputOnce(overlayFrame); }
        else if (which === "cam") { if (!camEditMode) { camEditMode = true; Chiaki.window.camOverlay = true; } view.grabInputOnce(camFrame); }
        else if (which === "stats") { if (!statsEditMode) { statsEditMode = true; Chiaki.window.statsOverlay = true; } view.grabInputOnce(statsFrame); }
        else if (which === "mic") { if (!micEditMode) { micEditMode = true; Chiaki.window.micOverlay = true; } view.grabInputOnce(micFrame); }
        else if (which === "clock") { if (!clockEditMode) { clockEditMode = true; Chiaki.window.clockOverlay = true; } view.grabInputOnce(clockFrame); }
        else if (which === "chat") { if (!chatEditMode) { chatEditMode = true; Chiaki.window.chatOverlay = true; } view.grabInputOnce(chatFrame); }
        updateOverlayInteractionActive();
    }
    function stopAllOverlayEdit() {
        if (overlayEditMode) view.stopOverlayEdit();
        if (camEditMode) view.stopCamEdit();
        if (statsEditMode) view.stopStatsEdit();
        if (micEditMode) view.stopMicEdit();
        if (clockEditMode) view.stopClockEdit();
        if (chatEditMode) view.stopChatEdit();
        if (dockEditMode) view.stopDockEdit();
    }
    // grab input ครั้งเดียวต่อช่วงแก้ (สลับตัวที่แก้ไม่ grab ซ้อน) แล้วโฟกัสตัวใหม่
    property bool editInputGrabbed: false
    // gotcha 2026-10-05: เดิมล้าง editInputGrabbed ใน onAnyOverlayEditChanged → ตอนสลับตัว anyOverlayEdit=false ชั่วขณะ → grab ซ้ำ
    //   (นับ 2) แต่ release 1 → จอยไม่เข้าเกมจนตัดสตรีม · ตอนนี้ flag ล้างที่เดียวคือตอน release จริง (releaseAfterEdit)
    function grabInputOnce(item) {
        if (!editInputGrabbed) { editInputGrabbed = true; view.grabInput(item); }
        else item.forceActiveFocus(Qt.TabFocusReason);
    }
    // 2) คลิกที่แค่ปลุก/โฟกัสหน้าต่าง ไม่นับเป็นคลิกแก้ overlay (กันเผลอเข้าโหมดแก้ระหว่างเล่น)
    property double windowActivatedMs: 0
    property bool pressWasFocusClick: false
    // gotcha: view.Window.window คือ QQuickWindow offscreen (active=false ตลอด) — หน้าต่างจริงคือ Chiaki.window
    function notePress() { pressWasFocusClick = !Chiaki.window || !Chiaki.window.active || (Date.now() - windowActivatedMs) < 350 }
    function overlayClicked(which) { if (!pressWasFocusClick) editOverlay(which) }
    // ส่งพื้นที่ overlay ให้ C++ — เมาส์ในกรอบนี้ไป QML ไม่เข้าเกม (คลิกเข้าโหมดแก้ได้ระหว่างเล่น)
    function rectOf(item) { return item.visible ? Qt.rect(item.x, item.y, item.width, item.height) : Qt.rect(0, 0, 0, 0) }
    function pushOverlayHitRects() {
        if (!Chiaki.window) return;
        Chiaki.window.setOverlayHitRects(useSeparateMenuWindow ? [] : [rectOf(overlayFrame), rectOf(camFrame), rectOf(statsFrame), rectOf(micFrame), rectOf(clockFrame), rectOf(chatFrame), rectOf(root.recordingToast), rectOf(dockEditor), rectOf(statusBar)]);
        // PS-WRAP: กรอบ facecam ให้ภาพแนวตั้ง 9:16 (ตำแหน่งในหน้าต่าง — ตัดส่วนเดียวกันจาก overlay)
        const cam = camFrame.visible ? camFrame.mapToItem(null, 0, 0, camFrame.width, camFrame.height) : Qt.rect(0, 0, 0, 0);
        Chiaki.window.setVerticalCamRect(cam.x, cam.y, cam.width, cam.height);
    }
    // PS-WRAP: Lightbar halo — แสงเรืองด้านในขอบภาพเกมตามสีไฟจอยที่เครื่องสั่ง (อยู่ใต้ overlay อื่นทั้งหมด)
    // สีเปลี่ยน = กระพริบสว่างขึ้นแวบหนึ่ง ให้เห็นว่าเกมเปลี่ยนสถานะ (เช่น เลือดน้อย / โดนตำรวจไล่)
    Item {
        id: lightbarHalo
        readonly property color c: Chiaki.session ? Chiaki.session.lightbarColor : "transparent"
        readonly property bool hasColor: c.a > 0 && (c.r + c.g + c.b) > 0.3   // PS5 ส่ง 13,13,13 = ไฟดับ
        readonly property real thickness: Math.max(12, Math.round(view.videoH * 0.045))
        property real strength: 0.5
        x: view.videoX
        y: view.videoY
        width: view.videoW
        height: view.videoH
        visible: !!Chiaki.window.lightbarHalo && hasColor && !!Chiaki.session && !sessionLoading && !sessionError
                 && !(Chiaki.settings.audioVideoDisabled & 0x02)
        opacity: strength
        onCChanged: if (visible) flash.restart()
        SequentialAnimation {
            id: flash
            NumberAnimation { target: lightbarHalo; property: "strength"; to: 1.0; duration: 120; easing.type: Easing.OutQuad }
            NumberAnimation { target: lightbarHalo; property: "strength"; to: 0.5; duration: 700; easing.type: Easing.InOutQuad }
        }
        readonly property color edge: Qt.rgba(c.r, c.g, c.b, 0.85)
        readonly property color clear: Qt.rgba(c.r, c.g, c.b, 0.0)
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: lightbarHalo.thickness
            gradient: Gradient {
                GradientStop { position: 0.0; color: lightbarHalo.edge }
                GradientStop { position: 1.0; color: lightbarHalo.clear }
            }
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: lightbarHalo.thickness
            gradient: Gradient {
                GradientStop { position: 0.0; color: lightbarHalo.clear }
                GradientStop { position: 1.0; color: lightbarHalo.edge }
            }
        }
        Rectangle {
            anchors { top: parent.top; bottom: parent.bottom; left: parent.left }
            width: lightbarHalo.thickness
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: lightbarHalo.edge }
                GradientStop { position: 1.0; color: lightbarHalo.clear }
            }
        }
        Rectangle {
            anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
            width: lightbarHalo.thickness
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: lightbarHalo.clear }
                GradientStop { position: 1.0; color: lightbarHalo.edge }
            }
        }
    }
    Timer { interval: 200; repeat: true; running: !!Chiaki.session; onTriggered: view.pushOverlayHitRects() }
    Component.onDestruction: { if (Chiaki.window) Chiaki.window.setOverlayHitRects([]); root.toastBottomInset = 0; }
    // คลิกที่ว่างระหว่างแก้ → จบ (อยู่ใต้ overlay ทั้งหมด)
    Rectangle {
        z: 70
        visible: !useSeparateMenuWindow && view.anyOverlayEdit
        anchors { top: parent.top; topMargin: Math.max(12, videoY + 12); horizontalCenter: parent.horizontalCenter }
        radius: height / 2
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.accent
        implicitHeight: 34
        implicitWidth: editBannerLabel.implicitWidth + 36
        Label {
            id: editBannerLabel
            anchors.centerIn: parent
            font.pixelSize: Theme.fontCaption
            color: Theme.text
            text: view.dockEditMode
                  ? qsTr("Arranging overlays · drag to move · drag to the other side to switch · ◢ or L1/R1 resizes all · ▲▼ reorder · ◯ / ✕ to finish")
                  : qsTr("Editing overlay · controller paused · click empty space or press ◯ / ✕ to resume")
        }
    }
    MouseArea {
        anchors.fill: parent
        z: 50
        enabled: !useSeparateMenuWindow && view.anyOverlayEdit
        visible: enabled
        onClicked: view.stopAllOverlayEdit()
    }
    Shortcut {
        sequence: "Ctrl+Shift+O"
        autoRepeat: false
        onActivated: { console.log("PSWRAP shortcut toggle overlay"); Chiaki.window.padOverlay = !Chiaki.window.padOverlay; }
    }
    Shortcut {
        sequence: "Ctrl+Shift+E"
        autoRepeat: false
        onActivated: { console.log("PSWRAP shortcut edit"); overlayEditMode ? view.stopOverlayEdit() : view.startOverlayEdit(); }
    }
    // ---- facecam: edit mode แยกจาก overlay จอย (ใช้ grabInput เหมือนกัน) ----
    property bool camEditMode: false
    function startCamEdit() {
        if (view.dockOn) { view.startDockEdit(); return; }   // PS-WRAP: Stack
        if (useSeparateMenuWindow || !Chiaki.session || camEditMode)
            return;
        if (overlayEditMode) view.stopOverlayEdit();
        if (micEditMode) view.stopMicEdit();
        if (clockEditMode) view.stopClockEdit();
        if (chatEditMode) view.stopChatEdit();
        Chiaki.window.camOverlay = true;
        camEditMode = true;
        view.updateOverlayInteractionActive();
        view.grabInputOnce(camFrame);
    }
    function stopCamEdit() {
        if (!camEditMode)
            return;
        camEditMode = false;
        camFrame.save();
        releaseAfterEdit.restart();
    }
    onCamEditModeChanged: if (camEditMode) camFrame.forceActiveFocus(Qt.TabFocusReason)
    Shortcut {
        sequence: "Ctrl+Shift+C"
        autoRepeat: false
        onActivated: Chiaki.window.camOverlay = !Chiaki.window.camOverlay
    }
    Shortcut {
        sequence: "Ctrl+Shift+V"
        autoRepeat: false
        onActivated: camEditMode ? view.stopCamEdit() : view.startCamEdit()
    }
    Shortcut {
        sequence: "Ctrl+Shift+S"
        autoRepeat: false
        onActivated: { console.log("PSWRAP shortcut stats"); Chiaki.settings.showStreamStats = !Chiaki.settings.showStreamStats; }
    }
    // ---- mic spectrum: edit mode (ลาก/ย่อขยาย) — เหมือน pad/cam/stats ----
    property bool micEditMode: false
    function startMicEdit() {
        if (view.dockOn) { view.startDockEdit(); return; }   // PS-WRAP: Stack
        if (useSeparateMenuWindow || !Chiaki.session || micEditMode)
            return;
        if (overlayEditMode) view.stopOverlayEdit();
        if (camEditMode) view.stopCamEdit();
        if (statsEditMode) view.stopStatsEdit();
        if (clockEditMode) view.stopClockEdit();
        if (chatEditMode) view.stopChatEdit();
        Chiaki.window.micOverlay = true;
        micEditMode = true;
        view.updateOverlayInteractionActive();
        view.grabInputOnce(micFrame);
    }
    function stopMicEdit() {
        if (!micEditMode)
            return;
        micEditMode = false;
        micFrame.save();
        releaseAfterEdit.restart();
    }
    onMicEditModeChanged: if (micEditMode) micFrame.forceActiveFocus(Qt.TabFocusReason)
    Shortcut {
        sequence: "Ctrl+Shift+M"
        autoRepeat: false
        onActivated: { console.log("PSWRAP shortcut mic spectrum"); Chiaki.window.micOverlay = !Chiaki.window.micOverlay; }
    }
    // ---- อัดคลิป: Ctrl+Shift+R เริ่ม/หยุด (กดซ้ำระหว่างปิดไฟล์ = ไม่ทำอะไร) ----
    Shortcut {
        sequence: "Ctrl+Shift+R"
        autoRepeat: false
        onActivated: {
            console.log("PSWRAP shortcut record");
            if (view.recorder && !view.recorder.busy)
                Chiaki.window.toggleRecording();
        }
    }
    // ---- clock overlay: Ctrl+Shift+T เปิด/ปิด · edit mode (ลาก/ย่อขยาย) เหมือน mic ----
    property bool clockEditMode: false
    function startClockEdit() {
        if (view.dockOn) { view.startDockEdit(); return; }   // PS-WRAP: Stack
        if (useSeparateMenuWindow || !Chiaki.session || clockEditMode)
            return;
        if (overlayEditMode) view.stopOverlayEdit();
        if (camEditMode) view.stopCamEdit();
        if (statsEditMode) view.stopStatsEdit();
        if (micEditMode) view.stopMicEdit();
        Chiaki.window.clockOverlay = true;
        clockEditMode = true;
        view.updateOverlayInteractionActive();
        view.grabInputOnce(clockFrame);
    }
    function stopClockEdit() {
        if (!clockEditMode)
            return;
        clockEditMode = false;
        clockFrame.save();
        releaseAfterEdit.restart();
    }
    onClockEditModeChanged: if (clockEditMode) clockFrame.forceActiveFocus(Qt.TabFocusReason)
    Shortcut {
        sequence: "Ctrl+Shift+T"
        autoRepeat: false
        onActivated: { console.log("PSWRAP shortcut clock"); Chiaki.window.clockOverlay = !Chiaki.window.clockOverlay; }
    }
    // ---- chat overlay: Ctrl+Shift+H เปิด/ปิด · edit mode (ลาก/ย่อขยาย) เหมือนนาฬิกา ----
    property bool chatEditMode: false
    function startChatEdit() {
        if (view.dockOn) { view.startDockEdit(); return; }   // PS-WRAP: Stack
        if (useSeparateMenuWindow || !Chiaki.session || chatEditMode)
            return;
        if (overlayEditMode) view.stopOverlayEdit();
        if (camEditMode) view.stopCamEdit();
        if (statsEditMode) view.stopStatsEdit();
        if (micEditMode) view.stopMicEdit();
        if (clockEditMode) view.stopClockEdit();
        Chiaki.window.chatOverlay = true;
        chatEditMode = true;
        view.updateOverlayInteractionActive();
        view.grabInputOnce(chatFrame);
    }
    function stopChatEdit() {
        if (!chatEditMode)
            return;
        chatEditMode = false;
        chatFrame.save();
        releaseAfterEdit.restart();
    }
    onChatEditModeChanged: if (chatEditMode) chatFrame.forceActiveFocus(Qt.TabFocusReason)
    Shortcut {
        sequence: "Ctrl+Shift+H"
        autoRepeat: false
        onActivated: { console.log("PSWRAP shortcut chat"); Chiaki.window.chatOverlay = !Chiaki.window.chatOverlay; }
    }
    // ---- Instant Replay / marker / screenshot (C++: Chiaki.window.saveReplay/addMarker/takeScreenshot) ----
    //      ผลลัพธ์ (replaySaved / screenshotSaved / failed) แสดงเป็น toast ที่ Main.qml · marker แสดง pill เล็กๆ ด้านล่างนี้
    readonly property bool replayActive: !!view.recorder && !!view.recorder.replayActive
    Shortcut {
        sequence: "Ctrl+Shift+B"
        autoRepeat: false
        onActivated: {
            console.log("PSWRAP shortcut save replay");
            if (view.replayActive)
                Chiaki.window.saveReplay();
            else if (Chiaki.window.replayEnabled)
                root.recordingToast.show({ kind: "info", title: qsTr("Instant Replay is not ready yet"), timeout: 3000 });
            else
                root.recordingToast.show({
                    kind: "info",
                    title: qsTr("Instant Replay is off"),
                    detail: qsTr("Turn on Replay in the stream menu to keep the last %1 seconds.").arg(Chiaki.window.replaySeconds || 60),
                    timeout: 4000
                });
        }
    }
    Shortcut {
        sequence: "Ctrl+Shift+K"
        autoRepeat: false
        onActivated: {
            console.log("PSWRAP shortcut marker");
            if ((view.recorder && view.recorder.recording) || view.replayActive)
                Chiaki.window.addMarker();
            else
                markerPill.flash(qsTr("Markers need Record or Replay"));
        }
    }
    Shortcut {
        sequences: ["F12", "Ctrl+Shift+P"]
        autoRepeat: false
        onActivated: {
            console.log("PSWRAP shortcut screenshot");
            if (Chiaki.session)
                Chiaki.window.takeScreenshot();
        }
    }
    // ---- Go Live: Ctrl+Shift+L เริ่ม/หยุด (Chiaki.goLive — ผล/สถานะแสดงเป็น toast ที่ Main.qml + จุดสีในเมนูสตรีม) ----
    Shortcut {
        sequence: "Ctrl+Shift+L"
        autoRepeat: false
        onActivated: {
            console.log("PSWRAP shortcut go live");
            if (Chiaki.goLive)
                Chiaki.goLive.toggle();
        }
    }
    // นับ marker ต่อคลิป: เริ่มอัดใหม่ = เริ่มนับ 1 ใหม่
    property int markerCount: 0
    property bool markerWasRecording: false
    Connections {
        target: view.recorder
        ignoreUnknownSignals: true
        // recordingChanged ยิงตอน busy/description เปลี่ยนด้วย → reset เฉพาะขอบขาขึ้นของ recording
        function onRecordingChanged() {
            const r = view.recorder.recording;
            if (r && !view.markerWasRecording)
                view.markerCount = 0;
            view.markerWasRecording = r;
        }
        function onMarkerAdded(seconds) {
            view.markerCount += 1;
            markerPill.flash(qsTr("Marker %1 · %2").arg(view.markerCount).arg(view.formatElapsed(seconds)));
        }
    }

    // ---- PS-WRAP: Stack — overlay ที่เปิดอยู่เรียงเป็นคอลัมน์เดียว ชิดขอบซ้าย/ขวาของภาพ กว้างเท่ากัน ระยะห่างเท่ากัน ----
    // ปรับทีเดียวทั้งชุด (dockEditMode): ลากขึ้นลง · ลากข้ามกลางภาพ = สลับข้าง · ◢ / L1 R1 / +- = กว้างทุกตัว · ▲▼ = สลับลำดับ
    readonly property bool dockOn: pswrapPrefs.dockOn && !useSeparateMenuWindow
    // กว้างตามที่ตั้ง แต่หดเองถ้าคอลัมน์สูงเกินจอ (dockFitPx คำนวณใน dockLayout)
    readonly property real dockWpx: Math.max(120, Math.min(Math.round(videoW * pswrapPrefs.dockW), dockFitPx))
    property real dockFitPx: 100000
    readonly property real dockGap: Math.max(6, Math.round(10 * overlayScale))
    readonly property real dockMargin: Math.max(10, Math.round(20 * overlayScale))
    readonly property real dockX: pswrapPrefs.dockRight ? videoX + videoW - dockMargin - dockWpx : videoX + dockMargin
    property real dockTop: 0
    property real dockBottom: 0
    property var dockShown: []            // ชื่อ overlay ที่อยู่ในคอลัมน์ตอนนี้ (ตามลำดับ)
    property bool dockEditMode: false
    function dockFrame(name) {
        switch (name) {
        case "pad": return overlayFrame;
        case "cam": return camFrame;
        case "stats": return statsFrame;
        case "mic": return micFrame;
        case "clock": return clockFrame;
        case "chat": return chatFrame;
        }
        return null;
    }
    function dockOrderList() {
        const all = ["clock", "mic", "stats", "chat", "cam", "pad"];
        const order = pswrapPrefs.dockOrder.split(",").filter(n => all.indexOf(n) >= 0);
        for (const n of all)
            if (order.indexOf(n) < 0)
                order.push(n);
        return order;
    }
    property bool dockLayoutQueued: false
    function scheduleDockLayout() {
        if (dockLayoutQueued) return;
        dockLayoutQueued = true;
        Qt.callLater(dockLayout);
    }
    function dockLayout() {
        dockLayoutQueued = false;
        if (!dockOn) { dockShown = []; return; }
        const top = videoY + Math.round(Math.min(0.9, Math.max(0, pswrapPrefs.dockY)) * videoH);
        let y = top;
        const shown = [];
        for (const n of dockOrderList()) {
            const f = dockFrame(n);
            if (!f || !f.visible)
                continue;
            f.x = dockX;
            f.y = y;
            y += f.height + dockGap;
            shown.push(n);
        }
        dockTop = top;
        dockBottom = shown.length ? y - dockGap : top;
        dockShown = shown;
        // ความสูงแต่ละตัว ∝ ความกว้าง → ความกว้างที่ทำให้ทั้งคอลัมน์พอดีพื้นที่ใต้ขอบบนถึงล่างกรอบวิดีโอ
        let ratio = 0;
        for (const n of shown) {
            const f = dockFrame(n);
            if (f.width > 0) ratio += f.height / f.width;
        }
        const avail = videoY + videoH - dockMargin - top - dockGap * Math.max(0, shown.length - 1);
        const fit = ratio > 0 ? Math.floor(avail / ratio) : 100000;
        if (Math.abs(fit - dockFitPx) >= 2)
            dockFitPx = fit;
    }
    function relayoutAllFrames() {
        for (const n of ["pad", "cam", "stats", "mic", "clock", "chat"]) {
            const f = dockFrame(n);
            if (f) f.layout();
        }
    }
    function setDock(on) {
        if (on === pswrapPrefs.dockOn) return;
        if (!on && dockEditMode) stopDockEdit();
        pswrapPrefs.dockOn = on;
        Qt.callLater(function() { view.relayoutAllFrames(); view.dockLayout(); });
    }
    function moveInDock(name, delta) {
        const order = dockOrderList();
        const shown = dockShown;
        const i = shown.indexOf(name), j = i + delta;
        if (i < 0 || j < 0 || j >= shown.length) return;
        // สลับกับตัวที่มองเห็นถัดไป (ข้ามตัวที่ปิดอยู่)
        const a = order.indexOf(name), b = order.indexOf(shown[j]);
        order[a] = shown[j];
        order[b] = name;
        pswrapPrefs.dockOrder = order.join(",");
        dockLayout();
    }
    function dockResize(delta) {
        pswrapPrefs.dockW = Math.min(0.35, Math.max(0.07, pswrapPrefs.dockW + delta));
        scheduleDockLayout();
    }
    function dockMoveY(dy) {
        pswrapPrefs.dockY = Math.min(0.9, Math.max(0, pswrapPrefs.dockY + dy / Math.max(1, videoH)));
        scheduleDockLayout();
    }
    function startDockEdit() {
        if (useSeparateMenuWindow || !Chiaki.session || !dockOn || dockEditMode)
            return;
        if (overlayEditMode) overlayEditMode = false;
        if (camEditMode) { camEditMode = false; }
        if (statsEditMode) { statsEditMode = false; }
        if (micEditMode) { micEditMode = false; }
        if (clockEditMode) { clockEditMode = false; }
        if (chatEditMode) { chatEditMode = false; }
        dockEditMode = true;
        dockLayout();
        updateOverlayInteractionActive();
        view.grabInputOnce(dockEditor);
    }
    function stopDockEdit() {
        if (!dockEditMode)
            return;
        dockEditMode = false;
        releaseAfterEdit.restart();
    }
    onDockEditModeChanged: if (dockEditMode) dockEditor.forceActiveFocus(Qt.TabFocusReason)
    onDockXChanged: scheduleDockLayout()
    onDockGapChanged: scheduleDockLayout()
    Connections {
        target: pswrapPrefs
        function onDockOrderChanged() { view.scheduleDockLayout() }
        function onDockYChanged() { view.scheduleDockLayout() }
    }

    // กรอบแก้ทั้งคอลัมน์ (อยู่เหนือ overlay) — ลาก = ย้ายทั้งชุด · ข้ามกลางภาพ = สลับข้าง
    FocusScope {
        id: dockEditor
        z: 62
        visible: view.dockOn && view.dockEditMode && view.dockShown.length > 0
        x: view.dockX - 8
        y: view.dockTop - 8
        width: view.dockWpx + 16
        height: Math.max(40, view.dockBottom - view.dockTop) + 16
        Keys.onPressed: (event) => {
            if (!view.dockEditMode) return;
            switch (event.key) {
            case Qt.Key_Up:    view.dockMoveY(-12); break;
            case Qt.Key_Down:  view.dockMoveY(12); break;
            case Qt.Key_Left:  pswrapPrefs.dockRight = false; break;
            case Qt.Key_Right: pswrapPrefs.dockRight = true; break;
            case Qt.Key_PageUp: case Qt.Key_Minus: view.dockResize(-0.01); break;
            case Qt.Key_PageDown: case Qt.Key_Plus: case Qt.Key_Equal: view.dockResize(0.01); break;
            case Qt.Key_Escape: case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Backspace: view.stopDockEdit(); break;
            default: return;
            }
            event.accepted = true;
        }
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0.655, 1, 0.06)
            radius: Theme.radiusControl
            border.width: 2
            border.color: Theme.accent
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeAllCursor
            property real pressY: 0
            property real startDockY: 0
            onPressed: (mouse) => { pressY = mapToItem(view, mouse.x, mouse.y).y; startDockY = pswrapPrefs.dockY; }
            onPositionChanged: (mouse) => {
                if (!pressed) return;
                const p = mapToItem(view, mouse.x, mouse.y);
                pswrapPrefs.dockY = Math.min(0.9, Math.max(0, startDockY + (p.y - pressY) / Math.max(1, view.videoH)));
                const right = p.x > view.videoX + view.videoW / 2;
                if (right !== pswrapPrefs.dockRight)
                    pswrapPrefs.dockRight = right;
            }
        }
        // ปุ่ม ▲▼ ข้างละตัว (ฝั่งที่หันเข้ากลางภาพ)
        Repeater {
            model: view.dockShown
            delegate: Column {
                id: arrowCol
                required property string modelData
                required property int index
                readonly property string name: modelData
                readonly property int pos: index
                readonly property Item target: view.dockFrame(modelData)
                spacing: 4
                x: pswrapPrefs.dockRight ? -width - 6 : dockEditor.width + 6
                y: target ? target.y - dockEditor.y + Math.max(0, (target.height - height) / 2) : 0
                Repeater {
                    model: [-1, 1]
                    delegate: Rectangle {
                        id: arrowBtn
                        required property int modelData
                        readonly property bool can: modelData < 0 ? arrowCol.pos > 0 : arrowCol.pos < view.dockShown.length - 1
                        width: 30; height: 26; radius: 6
                        color: arrowMouse.containsMouse && can ? Theme.accent : Theme.surfaceRaised
                        border.width: 1
                        border.color: Theme.accent
                        opacity: can ? 1 : 0.35
                        Label {
                            anchors.centerIn: parent
                            text: arrowBtn.modelData < 0 ? "▲" : "▼"
                            color: Theme.text
                            font.pixelSize: 13
                        }
                        MouseArea {
                            id: arrowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: arrowBtn.can
                            cursorShape: Qt.PointingHandCursor
                            onClicked: view.moveInDock(arrowCol.name, arrowBtn.modelData)
                        }
                    }
                }
            }
        }
        // ◢ ย่อขยายทุกตัวพร้อมกัน (มุมล่างฝั่งที่หันเข้ากลางภาพ)
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: -10
            x: pswrapPrefs.dockRight ? -10 : parent.width - 18
            width: 28; height: 28; radius: 14
            color: Theme.accent
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeHorCursor
                property real startX: 0
                property real startW: 0
                onPressed: (mouse) => { startX = mapToItem(view, mouse.x, 0).x; startW = view.dockWpx; }
                onPositionChanged: (mouse) => {
                    if (!pressed) return;
                    const dx = mapToItem(view, mouse.x, 0).x - startX;
                    const w = pswrapPrefs.dockRight ? startW - dx : startW + dx;   // ลากออกจากขอบ = กว้างขึ้น
                    pswrapPrefs.dockW = Math.min(0.35, Math.max(0.07, w / Math.max(1, view.videoW)));
                }
            }
        }
    }

    // overlay inline (backend Vulkan: QML วาดทับวิดีโอได้) — ลาก/ย่อขยายได้ใน edit mode ตำแหน่งจำเป็นสัดส่วนของ view
    FocusScope {
        id: overlayFrame
        z: 60
        readonly property real aspect: 1138 / 765
        visible: !useSeparateMenuWindow && Chiaki.session && (controllerOverlayVisible || overlayEditMode)
        onVisibleChanged: if (visible) layout()   // คืนตำแหน่งที่จำไว้ทุกครั้งที่โผล่ (เริ่มสตรีมใหม่)
        width: view.dockOn ? view.dockWpx : Math.max(80, Math.round(overlayBaseW * pswrapPrefs.overlayW))
        height: Math.round(width / aspect)

        function layout() {
            if (view.dockOn) { view.scheduleDockLayout(); return; }   // PS-WRAP: Stack จัดตำแหน่งให้
            const maxX = Math.max(0, videoW - width);
            const maxY = Math.max(0, videoH - height);
            x = videoX + (pswrapPrefs.overlayX < 0 ? maxX - 24 : Math.round(Math.min(maxX, Math.max(0, pswrapPrefs.overlayX * videoW))));
            y = videoY + (pswrapPrefs.overlayY < 0 ? maxY - 24 : Math.round(Math.min(maxY, Math.max(0, pswrapPrefs.overlayY * videoH))));
        }
        function save() {
            if (view.dockOn) return;
            pswrapPrefs.overlayX = videoW > 0 ? (x - videoX) / videoW : -1;
            pswrapPrefs.overlayY = videoH > 0 ? (y - videoY) / videoH : -1;
        }
        function nudge(dx, dy) {
            x = Math.min(Math.max(videoX, x + dx), videoX + Math.max(0, videoW - width));
            y = Math.min(Math.max(videoY, y + dy), videoY + Math.max(0, videoH - height));
            save();
        }
        function resizeBy(delta) {
            pswrapPrefs.overlayW = Math.min(0.6, Math.max(0.08, pswrapPrefs.overlayW + delta));
            Qt.callLater(function() { layout(); save(); });
        }
        Component.onCompleted: layout()
        Connections {
            target: view
            function onWidthChanged() { overlayFrame.layout() }
            function onHeightChanged() { overlayFrame.layout() }
            function onVideoXChanged() { overlayFrame.layout() }
            function onVideoYChanged() { overlayFrame.layout() }
        }
        onWidthChanged: layout()
        onHeightChanged: layout()

        Keys.onPressed: (event) => {
            if (!overlayEditMode) return;
            switch (event.key) {
            case Qt.Key_Left:  nudge(-10, 0); break;
            case Qt.Key_Right: nudge(10, 0); break;
            case Qt.Key_Up:    nudge(0, -10); break;
            case Qt.Key_Down:  nudge(0, 10); break;
            case Qt.Key_PageUp: case Qt.Key_Minus: resizeBy(-0.01); break;
            case Qt.Key_PageDown: case Qt.Key_Plus: case Qt.Key_Equal: resizeBy(0.01); break;
            case Qt.Key_Escape: case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Backspace: view.stopOverlayEdit(); break;
            default: return;
            }
            event.accepted = true;
        }

        ControllerOverlay {
            anchors.fill: parent
            overlayOpacity: overlayEditMode ? 1.0 : 0.9
        }

        // กรอบ + hint ตอน edit
        Rectangle {
            anchors.fill: parent
            anchors.margins: -6
            visible: overlayEditMode
            color: Qt.rgba(0, 0.655, 1, 0.08)
            radius: Theme.radiusControl
            border.width: 2
            border.color: Theme.accent
        }
        MouseArea {
            anchors.fill: parent
            enabled: !overlayEditMode
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            onPressed: view.notePress()
            onClicked: view.overlayClicked("pad")
        }
        MouseArea {
            anchors.fill: parent
            enabled: overlayEditMode
            cursorShape: overlayEditMode ? Qt.SizeAllCursor : Qt.ArrowCursor
            drag.target: overlayFrame
            drag.minimumX: videoX
            drag.minimumY: videoY
            drag.maximumX: videoX + Math.max(0, videoW - overlayFrame.width)
            drag.maximumY: videoY + Math.max(0, videoH - overlayFrame.height)
            onReleased: overlayFrame.save()
            drag.onActiveChanged: if (!drag.active) overlayFrame.save()
        }
        Rectangle {
            id: resizeHandle
            visible: overlayEditMode
            anchors { right: parent.right; bottom: parent.bottom; margins: -10 }
            width: 28; height: 28; radius: 14
            color: Theme.accent
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeFDiagCursor
                property real startX: 0
                property real startW: 0
                onPressed: (mouse) => { startX = mapToItem(view, mouse.x, 0).x; startW = overlayFrame.width; }
                onPositionChanged: (mouse) => {
                    if (!pressed) return;
                    const nowX = mapToItem(view, mouse.x, 0).x;
                    const w = Math.max(120, startW + (nowX - startX));
                    pswrapPrefs.overlayW = Math.min(0.6, Math.max(0.08, w / overlayBaseW));
                }
                onReleased: overlayFrame.save()
            }
        }
    }

    // stats inline (Vulkan) — การ์ดอยู่ใน scene เดียวกับวิดีโอ ไม่ทะลุหน้าต่างอื่น
    FocusScope {
        id: statsFrame
        z: 60
        visible: !useSeparateMenuWindow && (streamStatsVisible || statsEditMode)
        readonly property real sc: view.dockOn ? view.dockWpx / Math.max(1, statsCard.implicitWidth) : overlayScale * pswrapPrefs.statsScale
        width: statsCard.implicitWidth * sc
        height: statsCard.implicitHeight * sc
        function layout() {
            if (view.dockOn) { view.scheduleDockLayout(); return; }   // PS-WRAP: Stack จัดตำแหน่งให้
            const maxX = Math.max(0, videoW - width), maxY = Math.max(0, videoH - height);
            x = videoX + (pswrapPrefs.statsX < 0 ? maxX - 12 : Math.round(Math.min(maxX, Math.max(0, pswrapPrefs.statsX * videoW))));
            y = videoY + (pswrapPrefs.statsY < 0 ? Math.round(maxY / 2) : Math.round(Math.min(maxY, Math.max(0, pswrapPrefs.statsY * videoH))));
        }
        function save() {
            if (view.dockOn) return;
            pswrapPrefs.statsX = videoW > 0 ? (x - videoX) / videoW : -1;
            pswrapPrefs.statsY = videoH > 0 ? (y - videoY) / videoH : -1;
        }
        function nudge(dx, dy) {
            x = Math.min(Math.max(videoX, x + dx), videoX + Math.max(0, videoW - width));
            y = Math.min(Math.max(videoY, y + dy), videoY + Math.max(0, videoH - height));
            save();
        }
        function resizeBy(d) { pswrapPrefs.statsScale = Math.min(2.0, Math.max(0.5, pswrapPrefs.statsScale + d)); Qt.callLater(function() { layout(); save(); }); }
        onVisibleChanged: if (visible) layout()
        onWidthChanged: layout()
        onHeightChanged: layout()
        Component.onCompleted: layout()
        Connections {
            target: view
            function onWidthChanged() { statsFrame.layout() }
            function onHeightChanged() { statsFrame.layout() }
            function onVideoXChanged() { statsFrame.layout() }
            function onVideoYChanged() { statsFrame.layout() }
        }
        Keys.onPressed: (event) => {
            if (!statsEditMode) return;
            switch (event.key) {
            case Qt.Key_Left:  nudge(-10, 0); break;
            case Qt.Key_Right: nudge(10, 0); break;
            case Qt.Key_Up:    nudge(0, -10); break;
            case Qt.Key_Down:  nudge(0, 10); break;
            case Qt.Key_PageUp: case Qt.Key_Minus: resizeBy(-0.1); break;
            case Qt.Key_PageDown: case Qt.Key_Plus: case Qt.Key_Equal: resizeBy(0.1); break;
            case Qt.Key_Escape: case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Backspace: view.stopStatsEdit(); break;
            default: return;
            }
            event.accepted = true;
        }
        StatsOverlay {
            id: statsCard
            scale: statsFrame.sc
            transformOrigin: Item.TopLeft
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -6
            visible: statsEditMode
            color: Qt.rgba(0, 0.655, 1, 0.08)
            radius: 12
            border.width: 2
            border.color: Theme.accent
        }
        MouseArea {
            anchors.fill: parent
            enabled: !statsEditMode
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            onPressed: view.notePress()
            onClicked: view.overlayClicked("stats")
        }
        MouseArea {
            anchors.fill: parent
            enabled: statsEditMode
            cursorShape: Qt.SizeAllCursor
            drag.target: statsFrame
            drag.minimumX: videoX
            drag.minimumY: videoY
            drag.maximumX: videoX + Math.max(0, videoW - statsFrame.width)
            drag.maximumY: videoY + Math.max(0, videoH - statsFrame.height)
            onReleased: statsFrame.save()
        }
        Rectangle {
            visible: statsEditMode
            anchors { right: parent.right; bottom: parent.bottom; margins: -10 }
            width: 28; height: 28; radius: 14
            color: Theme.accent
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeFDiagCursor
                property real startX: 0
                property real startS: 1
                onPressed: (mouse) => { startX = mapToItem(view, mouse.x, 0).x; startS = pswrapPrefs.statsScale; }
                onPositionChanged: (mouse) => {
                    if (!pressed) return;
                    const dx = mapToItem(view, mouse.x, 0).x - startX;
                    pswrapPrefs.statsScale = Math.min(2.0, Math.max(0.5, startS * (1 + dx / Math.max(80, statsCard.implicitWidth * statsFrame.sc))));
                }
                onReleased: statsFrame.save()
            }
        }
    }

    // facecam inline (Vulkan) — ลาก/ย่อขยายใน cam edit mode จำตำแหน่งเป็นสัดส่วนของ view · ค่าเริ่มต้นซ้ายล่าง
    FocusScope {
        id: camFrame
        z: 60
        readonly property real aspect: pswrapPrefs.camCircle ? 1.0 : 16 / 9
        visible: !useSeparateMenuWindow && Chiaki.session && (webcamVisible || camEditMode)
        onVisibleChanged: if (visible) layout()
        width: view.dockOn ? view.dockWpx : Math.max(70, Math.round(overlayBaseW * pswrapPrefs.camW))
        height: Math.round(width / aspect)

        function layout() {
            if (view.dockOn) { view.scheduleDockLayout(); return; }   // PS-WRAP: Stack จัดตำแหน่งให้
            const maxX = Math.max(0, videoW - width);
            const maxY = Math.max(0, videoH - height);
            x = videoX + (pswrapPrefs.camX < 0 ? 24 : Math.round(Math.min(maxX, Math.max(0, pswrapPrefs.camX * videoW))));
            y = videoY + (pswrapPrefs.camY < 0 ? maxY - 24 : Math.round(Math.min(maxY, Math.max(0, pswrapPrefs.camY * videoH))));
        }
        function save() {
            if (view.dockOn) return;
            pswrapPrefs.camX = videoW > 0 ? (x - videoX) / videoW : -1;
            pswrapPrefs.camY = videoH > 0 ? (y - videoY) / videoH : -1;
        }
        function nudge(dx, dy) {
            x = Math.min(Math.max(videoX, x + dx), videoX + Math.max(0, videoW - width));
            y = Math.min(Math.max(videoY, y + dy), videoY + Math.max(0, videoH - height));
            save();
        }
        function resizeBy(delta) {
            pswrapPrefs.camW = Math.min(0.5, Math.max(0.06, pswrapPrefs.camW + delta));
            Qt.callLater(function() { layout(); save(); });
        }
        Component.onCompleted: layout()
        Connections {
            target: view
            function onWidthChanged() { camFrame.layout() }
            function onHeightChanged() { camFrame.layout() }
            function onVideoXChanged() { camFrame.layout() }
            function onVideoYChanged() { camFrame.layout() }
        }
        onWidthChanged: layout()
        onHeightChanged: layout()

        Keys.onPressed: (event) => {
            if (!camEditMode) return;
            switch (event.key) {
            case Qt.Key_Left:  nudge(-10, 0); break;
            case Qt.Key_Right: nudge(10, 0); break;
            case Qt.Key_Up:    nudge(0, -10); break;
            case Qt.Key_Down:  nudge(0, 10); break;
            case Qt.Key_PageUp: case Qt.Key_Minus: resizeBy(-0.01); break;
            case Qt.Key_PageDown: case Qt.Key_Plus: case Qt.Key_Equal: resizeBy(0.01); break;
            case Qt.Key_M: pswrapPrefs.camMirror = !pswrapPrefs.camMirror; break;
            case Qt.Key_C: pswrapPrefs.camCircle = !pswrapPrefs.camCircle; break;
            case Qt.Key_N: pswrapPrefs.camDevice = webcamItem.nextCameraName(); break;
            case Qt.Key_Z: pswrapPrefs.camZoom = Math.min(3.0, pswrapPrefs.camZoom + 0.1); break;
            case Qt.Key_X: pswrapPrefs.camZoom = Math.max(1.0, pswrapPrefs.camZoom - 0.1); break;
            case Qt.Key_A: pswrapPrefs.camPanX = Math.max(-1, pswrapPrefs.camPanX - 0.1); break;
            case Qt.Key_D: pswrapPrefs.camPanX = Math.min(1, pswrapPrefs.camPanX + 0.1); break;
            case Qt.Key_W: pswrapPrefs.camPanY = Math.max(-1, pswrapPrefs.camPanY - 0.1); break;
            case Qt.Key_S: pswrapPrefs.camPanY = Math.min(1, pswrapPrefs.camPanY + 0.1); break;
            case Qt.Key_G: Chiaki.window.camBackground = (Chiaki.window.camBackground + 1) % 4; break;
            case Qt.Key_F: Chiaki.window.camFx = (Chiaki.window.camFx + 1) % 14; break;
            case Qt.Key_Escape: case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Backspace: view.stopCamEdit(); break;
            default: return;
            }
            event.accepted = true;
        }

        WebcamOverlay {
            id: webcamItem
            anchors.fill: parent
            active: camFrame.visible
            mirror: pswrapPrefs.camMirror
            circle: pswrapPrefs.camCircle
            cameraId: pswrapPrefs.camDevice
            zoom: pswrapPrefs.camZoom
            panX: pswrapPrefs.camPanX
            panY: pswrapPrefs.camPanY
            keyEnabled: Chiaki.window.camBackground === 1 || Chiaki.window.camBackground === 2
            aiEnabled: Chiaki.window.camBackground === 3
            fx: Chiaki.window.camFx
            keyColor: Chiaki.window.camBackground === 2 ? "#0000ff" : "#00ff00"
            keyTolerance: pswrapPrefs.camKeyTol
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -6
            visible: camEditMode
            color: Qt.rgba(0, 0.655, 1, 0.08)
            radius: webcamItem.cornerRadius + 6
            border.width: 2
            border.color: Theme.accent
        }
        MouseArea {
            anchors.fill: parent
            enabled: !camEditMode
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            onPressed: view.notePress()
            onClicked: view.overlayClicked("cam")
        }
        MouseArea {
            anchors.fill: parent
            enabled: camEditMode
            cursorShape: camEditMode ? Qt.SizeAllCursor : Qt.ArrowCursor
            drag.target: camFrame
            drag.minimumX: videoX
            drag.minimumY: videoY
            drag.maximumX: videoX + Math.max(0, videoW - camFrame.width)
            drag.maximumY: videoY + Math.max(0, videoH - camFrame.height)
            onReleased: camFrame.save()
            drag.onActiveChanged: if (!drag.active) camFrame.save()
        }
        Rectangle {
            visible: camEditMode
            anchors { right: parent.right; bottom: parent.bottom; margins: -10 }
            width: 28; height: 28; radius: 14
            color: Theme.accent
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeFDiagCursor
                property real startX: 0
                property real startW: 0
                onPressed: (mouse) => { startX = mapToItem(view, mouse.x, 0).x; startW = camFrame.width; }
                onPositionChanged: (mouse) => {
                    if (!pressed) return;
                    const nowX = mapToItem(view, mouse.x, 0).x;
                    const w = Math.max(100, startW + (nowX - startX));
                    pswrapPrefs.camW = Math.min(0.5, Math.max(0.06, w / overlayBaseW));
                }
                onReleased: camFrame.save()
            }
        }
    }

    // mic spectrum inline (Vulkan) — วาดที่ 320×110 แล้ว scale ตามกรอบ · จำตำแหน่งเป็นสัดส่วนของกรอบวิดีโอ · ค่าเริ่มต้นกลางล่าง
    FocusScope {
        id: micFrame
        z: 60
        readonly property real aspect: 320 / 110
        visible: !useSeparateMenuWindow && Chiaki.session && (micOverlayVisible || micEditMode)
        onVisibleChanged: if (visible) layout()
        width: view.dockOn ? view.dockWpx : Math.max(160, Math.round(overlayBaseW * pswrapPrefs.micW))
        height: Math.round(width / aspect)

        function layout() {
            if (view.dockOn) { view.scheduleDockLayout(); return; }   // PS-WRAP: Stack จัดตำแหน่งให้
            const maxX = Math.max(0, videoW - width);
            const maxY = Math.max(0, videoH - height);
            x = videoX + (pswrapPrefs.micX < 0 ? Math.round(maxX / 2) : Math.round(Math.min(maxX, Math.max(0, pswrapPrefs.micX * videoW))));
            y = videoY + (pswrapPrefs.micY < 0 ? maxY - 24 : Math.round(Math.min(maxY, Math.max(0, pswrapPrefs.micY * videoH))));
        }
        function save() {
            if (view.dockOn) return;
            pswrapPrefs.micX = videoW > 0 ? (x - videoX) / videoW : -1;
            pswrapPrefs.micY = videoH > 0 ? (y - videoY) / videoH : -1;
        }
        function nudge(dx, dy) {
            x = Math.min(Math.max(videoX, x + dx), videoX + Math.max(0, videoW - width));
            y = Math.min(Math.max(videoY, y + dy), videoY + Math.max(0, videoH - height));
            save();
        }
        function resizeBy(delta) {
            pswrapPrefs.micW = Math.min(0.5, Math.max(0.08, pswrapPrefs.micW + delta));
            Qt.callLater(function() { layout(); save(); });
        }
        Component.onCompleted: layout()
        Connections {
            target: view
            function onWidthChanged() { micFrame.layout() }
            function onHeightChanged() { micFrame.layout() }
            function onVideoXChanged() { micFrame.layout() }
            function onVideoYChanged() { micFrame.layout() }
        }
        onWidthChanged: layout()
        onHeightChanged: layout()

        Keys.onPressed: (event) => {
            if (!micEditMode) return;
            switch (event.key) {
            case Qt.Key_Left:  nudge(-10, 0); break;
            case Qt.Key_Right: nudge(10, 0); break;
            case Qt.Key_Up:    nudge(0, -10); break;
            case Qt.Key_Down:  nudge(0, 10); break;
            case Qt.Key_PageUp: case Qt.Key_Minus: resizeBy(-0.01); break;
            case Qt.Key_PageDown: case Qt.Key_Plus: case Qt.Key_Equal: resizeBy(0.01); break;
            case Qt.Key_Escape: case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Backspace: view.stopMicEdit(); break;
            default: return;
            }
            event.accepted = true;
        }

        MicSpectrumOverlay {
            id: micSpectrum
            active: micFrame.visible
            overlayOpacity: micEditMode ? 1.0 : 0.92
            scale: micFrame.width / implicitWidth
            transformOrigin: Item.TopLeft
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -6
            visible: micEditMode
            color: Qt.rgba(0, 0.655, 1, 0.08)
            radius: 16 * micSpectrum.scale + 6
            border.width: 2
            border.color: Theme.accent
        }
        MouseArea {
            anchors.fill: parent
            enabled: !micEditMode
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            // คลิกวงไมค์ = mute/unmute · ที่อื่นในการ์ด = แก้ตำแหน่ง/ขนาดเหมือนเดิม
            function onBadge(mx, my) { const p = mapToItem(micSpectrum, mx, my); return micSpectrum.hitBadge(p.x, p.y); }
            onPositionChanged: (mouse) => micSpectrum.badgeHover = !!Chiaki.session && onBadge(mouse.x, mouse.y)
            onExited: micSpectrum.badgeHover = false
            onPressed: view.notePress()
            onClicked: (mouse) => {
                if (Chiaki.session && onBadge(mouse.x, mouse.y))
                    Chiaki.session.muted = !Chiaki.session.muted;
                else
                    view.overlayClicked("mic");
            }
        }
        MouseArea {
            anchors.fill: parent
            enabled: micEditMode
            cursorShape: micEditMode ? Qt.SizeAllCursor : Qt.ArrowCursor
            drag.target: micFrame
            drag.minimumX: videoX
            drag.minimumY: videoY
            drag.maximumX: videoX + Math.max(0, videoW - micFrame.width)
            drag.maximumY: videoY + Math.max(0, videoH - micFrame.height)
            onReleased: micFrame.save()
            drag.onActiveChanged: if (!drag.active) micFrame.save()
        }
        Rectangle {
            visible: micEditMode
            anchors { right: parent.right; bottom: parent.bottom; margins: -10 }
            width: 28; height: 28; radius: 14
            color: Theme.accent
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeFDiagCursor
                property real startX: 0
                property real startW: 0
                onPressed: (mouse) => { startX = mapToItem(view, mouse.x, 0).x; startW = micFrame.width; }
                onPositionChanged: (mouse) => {
                    if (!pressed) return;
                    const nowX = mapToItem(view, mouse.x, 0).x;
                    const w = Math.max(160, startW + (nowX - startX));
                    pswrapPrefs.micW = Math.min(0.5, Math.max(0.08, w / overlayBaseW));
                }
                onReleased: micFrame.save()
            }
        }
    }

    // clock inline (Vulkan) — วาดที่ 248×72 แล้ว scale ตามกรอบ · จำตำแหน่งเป็นสัดส่วนของกรอบวิดีโอ · ค่าเริ่มต้นขวาบน
    FocusScope {
        id: clockFrame
        z: 60
        readonly property real aspect: clockCard.implicitWidth / clockCard.implicitHeight
        visible: !useSeparateMenuWindow && Chiaki.session && (clockOverlayVisible || clockEditMode)
        onVisibleChanged: if (visible) layout()
        width: view.dockOn ? view.dockWpx : Math.max(140, Math.round(overlayBaseW * pswrapPrefs.clockW))
        height: Math.round(width / aspect)

        function layout() {
            if (view.dockOn) { view.scheduleDockLayout(); return; }   // PS-WRAP: Stack จัดตำแหน่งให้
            const maxX = Math.max(0, videoW - width);
            const maxY = Math.max(0, videoH - height);
            x = videoX + (pswrapPrefs.clockX < 0 ? maxX - 24 : Math.round(Math.min(maxX, Math.max(0, pswrapPrefs.clockX * videoW))));
            y = videoY + (pswrapPrefs.clockY < 0 ? 24 : Math.round(Math.min(maxY, Math.max(0, pswrapPrefs.clockY * videoH))));
        }
        function save() {
            if (view.dockOn) return;
            pswrapPrefs.clockX = videoW > 0 ? (x - videoX) / videoW : -1;
            pswrapPrefs.clockY = videoH > 0 ? (y - videoY) / videoH : -1;
        }
        function nudge(dx, dy) {
            x = Math.min(Math.max(videoX, x + dx), videoX + Math.max(0, videoW - width));
            y = Math.min(Math.max(videoY, y + dy), videoY + Math.max(0, videoH - height));
            save();
        }
        function resizeBy(delta) {
            pswrapPrefs.clockW = Math.min(0.4, Math.max(0.07, pswrapPrefs.clockW + delta));
            Qt.callLater(function() { layout(); save(); });
        }
        Component.onCompleted: layout()
        Connections {
            target: view
            function onWidthChanged() { clockFrame.layout() }
            function onHeightChanged() { clockFrame.layout() }
            function onVideoXChanged() { clockFrame.layout() }
            function onVideoYChanged() { clockFrame.layout() }
        }
        onWidthChanged: layout()
        onHeightChanged: layout()

        Keys.onPressed: (event) => {
            if (!clockEditMode) return;
            switch (event.key) {
            case Qt.Key_Left:  nudge(-10, 0); break;
            case Qt.Key_Right: nudge(10, 0); break;
            case Qt.Key_Up:    nudge(0, -10); break;
            case Qt.Key_Down:  nudge(0, 10); break;
            case Qt.Key_PageUp: case Qt.Key_Minus: resizeBy(-0.01); break;
            case Qt.Key_PageDown: case Qt.Key_Plus: case Qt.Key_Equal: resizeBy(0.01); break;
            case Qt.Key_Escape: case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Backspace: view.stopClockEdit(); break;
            default: return;
            }
            event.accepted = true;
        }

        ClockOverlay {
            id: clockCard
            active: clockFrame.visible
            startMs: view.sessionStartMs
            overlayOpacity: clockEditMode ? 1.0 : 0.92
            scale: clockFrame.width / implicitWidth
            transformOrigin: Item.TopLeft
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -6
            visible: clockEditMode
            color: Qt.rgba(0, 0.655, 1, 0.08)
            radius: 16 * clockCard.scale + 6
            border.width: 2
            border.color: Theme.accent
        }
        MouseArea {
            anchors.fill: parent
            enabled: !clockEditMode
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            onPressed: view.notePress()
            onClicked: view.overlayClicked("clock")
        }
        MouseArea {
            anchors.fill: parent
            enabled: clockEditMode
            cursorShape: clockEditMode ? Qt.SizeAllCursor : Qt.ArrowCursor
            drag.target: clockFrame
            drag.minimumX: videoX
            drag.minimumY: videoY
            drag.maximumX: videoX + Math.max(0, videoW - clockFrame.width)
            drag.maximumY: videoY + Math.max(0, videoH - clockFrame.height)
            onReleased: clockFrame.save()
            drag.onActiveChanged: if (!drag.active) clockFrame.save()
        }
        Rectangle {
            visible: clockEditMode
            anchors { right: parent.right; bottom: parent.bottom; margins: -10 }
            width: 28; height: 28; radius: 14
            color: Theme.accent
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeFDiagCursor
                property real startX: 0
                property real startW: 0
                onPressed: (mouse) => { startX = mapToItem(view, mouse.x, 0).x; startW = clockFrame.width; }
                onPositionChanged: (mouse) => {
                    if (!pressed) return;
                    const nowX = mapToItem(view, mouse.x, 0).x;
                    const w = Math.max(140, startW + (nowX - startX));
                    pswrapPrefs.clockW = Math.min(0.4, Math.max(0.07, w / overlayBaseW));
                }
                onReleased: clockFrame.save()
            }
        }
    }

    // PS-WRAP: chat inline (Vulkan) — วาดที่ 340×420 แล้ว scale ตามกรอบ · จำตำแหน่งเป็นสัดส่วนของกรอบวิดีโอ · ค่าเริ่มต้นซ้ายกลาง
    FocusScope {
        id: chatFrame
        z: 60
        readonly property real aspect: chatCard.implicitWidth / chatCard.implicitHeight
        visible: !useSeparateMenuWindow && Chiaki.session && (chatOverlayVisible || chatEditMode)
        onVisibleChanged: if (visible) layout()
        width: view.dockOn ? view.dockWpx : Math.max(140, Math.round(overlayBaseW * pswrapPrefs.chatW))
        height: Math.round(width / aspect)

        function layout() {
            if (view.dockOn) { view.scheduleDockLayout(); return; }   // PS-WRAP: Stack จัดตำแหน่งให้
            const maxX = Math.max(0, videoW - width);
            const maxY = Math.max(0, videoH - height);
            x = videoX + (pswrapPrefs.chatX < 0 ? 24 : Math.round(Math.min(maxX, Math.max(0, pswrapPrefs.chatX * videoW))));
            y = videoY + (pswrapPrefs.chatY < 0 ? Math.round(maxY * 0.45) : Math.round(Math.min(maxY, Math.max(0, pswrapPrefs.chatY * videoH))));
        }
        function save() {
            if (view.dockOn) return;
            pswrapPrefs.chatX = videoW > 0 ? (x - videoX) / videoW : -1;
            pswrapPrefs.chatY = videoH > 0 ? (y - videoY) / videoH : -1;
        }
        function nudge(dx, dy) {
            x = Math.min(Math.max(videoX, x + dx), videoX + Math.max(0, videoW - width));
            y = Math.min(Math.max(videoY, y + dy), videoY + Math.max(0, videoH - height));
            save();
        }
        function resizeBy(delta) {
            pswrapPrefs.chatW = Math.min(0.5, Math.max(0.1, pswrapPrefs.chatW + delta));
            Qt.callLater(function() { layout(); save(); });
        }
        Component.onCompleted: layout()
        Connections {
            target: view
            function onWidthChanged() { chatFrame.layout() }
            function onHeightChanged() { chatFrame.layout() }
            function onVideoXChanged() { chatFrame.layout() }
            function onVideoYChanged() { chatFrame.layout() }
        }
        onWidthChanged: layout()
        onHeightChanged: layout()

        Keys.onPressed: (event) => {
            if (!chatEditMode) return;
            switch (event.key) {
            case Qt.Key_Left:  nudge(-10, 0); break;
            case Qt.Key_Right: nudge(10, 0); break;
            case Qt.Key_Up:    nudge(0, -10); break;
            case Qt.Key_Down:  nudge(0, 10); break;
            case Qt.Key_PageUp: case Qt.Key_Minus: resizeBy(-0.01); break;
            case Qt.Key_PageDown: case Qt.Key_Plus: case Qt.Key_Equal: resizeBy(0.01); break;
            case Qt.Key_Escape: case Qt.Key_Return: case Qt.Key_Enter: case Qt.Key_Backspace: view.stopChatEdit(); break;
            default: return;
            }
            event.accepted = true;
        }

        ChatOverlay {
            id: chatCard
            overlayOpacity: chatEditMode ? 1.0 : 0.95
            scale: chatFrame.width / implicitWidth
            transformOrigin: Item.TopLeft
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -6
            visible: chatEditMode
            color: Qt.rgba(0, 0.655, 1, 0.08)
            radius: 16 * chatCard.scale + 6
            border.width: 2
            border.color: Theme.accent
        }
        MouseArea {
            anchors.fill: parent
            enabled: !chatEditMode
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            onPressed: view.notePress()
            onClicked: view.overlayClicked("chat")
        }
        MouseArea {
            anchors.fill: parent
            enabled: chatEditMode
            cursorShape: chatEditMode ? Qt.SizeAllCursor : Qt.ArrowCursor
            drag.target: chatFrame
            drag.minimumX: videoX
            drag.minimumY: videoY
            drag.maximumX: videoX + Math.max(0, videoW - chatFrame.width)
            drag.maximumY: videoY + Math.max(0, videoH - chatFrame.height)
            onReleased: chatFrame.save()
            drag.onActiveChanged: if (!drag.active) chatFrame.save()
        }
        Rectangle {
            visible: chatEditMode
            anchors { right: parent.right; bottom: parent.bottom; margins: -10 }
            width: 28; height: 28; radius: 14
            color: Theme.accent
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeFDiagCursor
                property real startX: 0
                property real startW: 0
                onPressed: (mouse) => { startX = mapToItem(view, mouse.x, 0).x; startW = chatFrame.width; }
                onPositionChanged: (mouse) => {
                    if (!pressed) return;
                    const nowX = mapToItem(view, mouse.x, 0).x;
                    const w = Math.max(140, startW + (nowX - startX));
                    pswrapPrefs.chatW = Math.min(0.5, Math.max(0.1, w / overlayBaseW));
                }
                onReleased: chatFrame.save()
            }
        }
    }

    // ---- marker: pill เล็กๆ โผล่ ~1.6 วิ (มุมซ้ายบนของกรอบวิดีโอ ใต้ pill "Saving…") — ไม่รับ input, ไม่อยู่ใน hit rect ----
    Rectangle {
        id: markerPill
        property string text: ""
        function flash(t) {
            text = t;
            shown = true;
            markerHideTimer.restart();
        }
        property bool shown: false
        z: 65
        x: videoX + 16
        y: videoY + 16 + (recPill.visible ? recPill.height + 8 : 0)
        opacity: shown && !sessionError ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 180 } }
        enabled: false
        implicitHeight: 28
        implicitWidth: markerRow.implicitWidth + 22
        radius: height / 2
        color: Qt.rgba(0, 0, 0, 0.55)
        border.width: 1
        border.color: Qt.rgba(1, 0.71, 0.28, 0.55)
        Row {
            id: markerRow
            anchors.centerIn: parent
            spacing: 8
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 8
                rotation: 45
                color: Theme.warning
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: markerPill.text
                font.pixelSize: Theme.fontCaption
                font.weight: Font.DemiBold
                font.features: { "tnum": 1 }
                color: Theme.text
            }
        }
        Timer {
            id: markerHideTimer
            interval: 1600
            onTriggered: markerPill.shown = false
        }
    }

    // ---- อัดคลิป: pill "Saving…" ตอนปิดไฟล์ (มุมซ้ายบนของกรอบวิดีโอ) — ไม่รับ input, ไม่อยู่ใน hit rect ----
    //      ระหว่างอัด ใช้จุดแดงที่ C++ วาดลงจอโดยตรง (pswrapDecorateScreen) แทน — ทุกอย่างใน QML ติดไปในคลิป แต่จุดนั้นไม่ติด
    function formatElapsed(sec) {
        const s = Math.max(0, Math.floor(sec || 0));
        const pad = (n) => (n < 10 ? "0" : "") + n;
        return pad(Math.floor(s / 3600)) + ":" + pad(Math.floor(s / 60) % 60) + ":" + pad(s % 60);
    }
    Rectangle {
        id: recPill
        readonly property bool active: !!view.recorder && view.recorder.busy
        z: 65
        x: videoX + 16
        y: videoY + 16
        opacity: active && !sessionError ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        enabled: false
        implicitHeight: 28
        implicitWidth: recRow.implicitWidth + 22
        radius: height / 2
        color: Qt.rgba(0, 0, 0, 0.55)
        border.width: 1
        border.color: view.recorder && view.recorder.busy ? Theme.border : Qt.rgba(1, 0.36, 0.36, 0.55)
        Row {
            id: recRow
            anchors.centerIn: parent
            spacing: 8
            Rectangle {
                id: recDot
                anchors.verticalCenter: parent.verticalCenter
                width: 10
                height: 10
                radius: 5
                color: view.recorder && view.recorder.busy ? Theme.warning : "#ff3b3b"
                SequentialAnimation on opacity {
                    running: recPill.visible && !!view.recorder && view.recorder.recording
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) recDot.opacity = 1
                    NumberAnimation { from: 1; to: 0.25; duration: 700; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 0.25; to: 1; duration: 700; easing.type: Easing.InOutSine }
                }
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: view.recorder && view.recorder.busy ? qsTr("Saving…") : qsTr("REC %1").arg(view.formatElapsed(view.recorder ? view.recorder.seconds : 0))
                font.pixelSize: Theme.fontCaption
                font.weight: Font.DemiBold
                font.features: { "tnum": 1 }
                color: Theme.text
            }
        }
    }

    // ---- toast ผลการอัด อยู่ที่ Main.qml (root.recordingToast) เพราะ saved() มักมาหลังจบสตรีม (StreamView ถูกทำลายไปแล้ว)
    //      ตรงนี้แค่ดัน toast ขึ้นเหนือเมนู inline ตอนเปิด และส่งกรอบ toast เป็น hit rect (ปุ่ม "Show in folder" กดด้วยเมาส์ได้ระหว่างเล่น)
    Binding {
        target: root
        property: "toastBottomInset"
        value: (menuController.open && !useSeparateMenuWindow) ? streamMenuHeight
             : (statusBar.shown ? view.height - statusBar.y : 0)   // toast อยู่เหนือแถบสถานะ
    }

    // PS-WRAP: แถบสถานะระหว่างเล่น (StreamStatusBar.qml) — กลางล่างของภาพ · เฉพาะ backend Vulkan (QML วาดทับวิดีโอ)
    StreamStatusBar {
        id: statusBar
        z: 80
        x: Math.round(view.videoX + (view.videoW - width) / 2)
        y: Math.round(view.height - height - 16)   // ชิดขอบล่างหน้าต่าง (อยู่ในแถบ pointerAtBottom เสมอ · ภาพมีขอบดำก็ไปอยู่ในขอบดำ)
        allowed: !useSeparateMenuWindow && !!Chiaki.session && !sessionLoading && !sessionError
                 && !(menuController.open || menuController.closing) && !view.anyOverlayEdit
                 && !sessionStopDialogActive && !sessionPinDialogActive
        pinned: pswrapPrefs.statusBarPinned
        onPinToggled: pswrapPrefs.statusBarPinned = !pswrapPrefs.statusBarPinned
        onMenuRequested: { menuController.toggle(); Chiaki.window.requestOverlayUpdate(); }
        onGrabRequested: view.grabInput(null)
        onReleaseRequested: view.releaseInput()
    }

    // overlay แบบ window แยก (backend OpenGL) — โปร่งใสและไม่รับ input
    Window {
        id: separateControllerOverlayWindow
        visible: useSeparateMenuWindow && controllerOverlayVisible
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowTransparentForInput | Qt.WindowStaysOnTopHint
        color: "transparent"
        transientParent: view.hostWindow
        width: Math.max(120, Math.round(view.width * pswrapPrefs.overlayW))
        height: Math.round(width / (1138 / 765))
        x: separateStatsX + (pswrapPrefs.overlayX < 0 ? separateStatsWidth - width - 24 : Math.round(pswrapPrefs.overlayX * separateStatsWidth))
        y: separateStatsY + (pswrapPrefs.overlayY < 0 ? separateStatsHeight - height - 24 : Math.round(pswrapPrefs.overlayY * separateStatsHeight))
        onVisibleChanged: if (visible) view.updateSeparateMenuGeometry()
        ControllerOverlay { anchors.fill: parent }
    }

    // PS-WRAP: preview ภาพแนวตั้ง 9:16 — หน้าต่างแยก เปิดจากปุ่ม 9:16 ในเมนูสตรีม / tray
    VerticalPreviewWindow {
        visible: !!Chiaki.window && Chiaki.window.verticalPreview && !!Chiaki.session
    }

    StreamMenuWindow {
        id: separateMenuWindow
        transientParent: view.hostWindow
        x: separateMenuX
        y: menuController.open ? separateMenuY : separateMenuY + streamMenuHeight
        width: separateMenuWidth > 0 ? separateMenuWidth : view.width
        height: streamMenuHeight
        open: useSeparateMenuWindow && menuController.open
        closing: useSeparateMenuWindow && menuController.closing
        onCloseRequested: menuController.close()
        onDisplaySettingsRequested: {
            menuController.close();
            root.openDisplaySettings();
        }
        onPlaceboSettingsRequested: {
            menuController.close();
            root.openPlaceboSettings();
        }
        onMainViewRequested: root.showMainView()
        overlayEnabled: Chiaki.window.padOverlay
        onOverlayToggled: Chiaki.window.padOverlay = !Chiaki.window.padOverlay
        onCloseAnimationFinished: {
            if (view.hostWindow)
                view.hostWindow.requestActivate();
            menuController.closing = false;
            view.updateOverlayInteractionActive();
        }
    }

    Popup {
        id: sessionStopDialog
        property int closeAction: 0
        parent: Overlay.overlay
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        modal: true
        padding: 0
        onAboutToShow: {
            closeAction = 0;
            sessionStopDialogActive = true;
            view.updateOverlayInteractionActive();
        }
        onClosed: {
            view.releaseInput();
            sessionStopDialogActive = false;
            view.updateOverlayInteractionActive();
            if (closeAction)
                Chiaki.stopSession(closeAction == 1);
        }

        background: null
        // PS-WRAP: เนื้อหาใช้ DisconnectDialogContent (ปุ่มหลัก = Disconnect, ธีมเดียวกับแอป) — เป็น child ของ Popup เหมือนโครงเดิม upstream
        DisconnectDialogContent {
            id: sessionStopContent
            onVisibleChanged: if (visible) view.grabInput(defaultButton)
            onDisconnectRequested: { sessionStopDialog.closeAction = 2; sessionStopDialog.close(); }
            onSleepRequested: { sessionStopDialog.closeAction = 1; sessionStopDialog.close(); }
            onCancelRequested: sessionStopDialog.close()
        }
    }

    Window {
        id: separateSessionStopWindow
        property int closeAction: 0
        readonly property int dialogWidth: 560
        readonly property int dialogHeight: 280
        visible: false
        flags: Qt.Dialog | Qt.FramelessWindowHint
        color: "transparent"
        transientParent: view.hostWindow
        modality: Qt.ApplicationModal
        x: separateDialogX
        y: separateDialogY
        width: dialogWidth
        height: dialogHeight

        onVisibleChanged: {
            if (visible) {
                closeAction = 0;
                view.updateSeparateDialogGeometry(width, height);
                requestActivate();
                view.grabInput(separateSessionStopContent.defaultButton);
            } else {
                view.releaseInput();
                if (closeAction)
                    Chiaki.stopSession(closeAction === 1);
            }
            view.updateOverlayInteractionActive();
        }

        DisconnectDialogContent {
            id: separateSessionStopContent
            anchors.fill: parent
            onDisconnectRequested: { separateSessionStopWindow.closeAction = 2; separateSessionStopWindow.visible = false; }
            onSleepRequested: { separateSessionStopWindow.closeAction = 1; separateSessionStopWindow.visible = false; }
            onCancelRequested: separateSessionStopWindow.visible = false
        }
    }

    Dialog {
        id: sessionPinDialog
        parent: Overlay.overlay
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2)
        title: qsTr("Console Login PIN")
        modal: true
        closePolicy: Popup.NoAutoClose
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAboutToShow: {
            standardButton(Dialog.Ok).enabled = Qt.binding(function() {
                return pinField.acceptableInput;
            });
            view.grabInput(pinField);
            sessionPinDialogActive = true;
            view.updateOverlayInteractionActive();
        }
        onClosed: {
            view.releaseInput();
            sessionPinDialogActive = false;
            view.updateOverlayInteractionActive();
            // PS-WRAP: หลัง PIN dialog ปิด focus ค้าง (ไม่มี item ให้คืน) → คีย์ลัด/เมนูไม่ตอบ จนกว่าจะเริ่ม session ใหม่ → คืน focus ให้ view
            pinField.clear();
            Qt.callLater(function() { view.forceActiveFocus(Qt.TabFocusReason); });
        }
        onAccepted: Chiaki.enterPin(pinField.text)
        onRejected: Chiaki.stopSession(false)
        Material.roundedScale: Material.MediumScale

        // PS-WRAP: ใส่ PIN ด้วยจอยได้ (↑↓ เปลี่ยนเลข ←→ เลื่อนหลัก ✕ ตกลง ○ ยกเลิก) — เดิม TextField พิมพ์ได้แค่คีย์บอร์ด
        C.PinPad {
            id: pinField
            hidden: Chiaki.settings.streamerMode
            onAccepted: sessionPinDialog.accept()
            onCanceled: sessionPinDialog.reject()
        }
    }

    Window {
        id: separateSessionPinWindow
        readonly property int dialogWidth: 420
        readonly property int dialogHeight: 520
        visible: false
        flags: Qt.Dialog | Qt.FramelessWindowHint
        color: "transparent"
        transientParent: view.hostWindow
        modality: Qt.ApplicationModal
        x: separateDialogX
        y: separateDialogY
        width: dialogWidth
        height: dialogHeight

        onVisibleChanged: {
            if (visible) {
                separatePinField.clear();
                view.updateSeparateDialogGeometry(width, height);
                requestActivate();
                view.grabInput(separatePinField);
            } else {
                view.releaseInput();
            }
            view.updateOverlayInteractionActive();
        }

        Rectangle {
            anchors.fill: parent
            radius: 12
            color: Material.background
            border.color: "#666666"
            border.width: 1
        }

        ColumnLayout {
            anchors {
                fill: parent
                margins: 24
            }

            Label {
                Layout.alignment: Qt.AlignCenter
                text: qsTr("Console Login PIN")
                font.bold: true
                font.pixelSize: 24
            }

            C.PinPad {
                id: separatePinField
                Layout.topMargin: 12
                Layout.fillWidth: true
                hidden: Chiaki.settings.streamerMode
                onAccepted: separatePinOkButton.clicked()
                onCanceled: { separateSessionPinWindow.visible = false; Chiaki.stopSession(false); }
            }

            RowLayout {
                Layout.topMargin: 20
                Layout.alignment: Qt.AlignRight
                spacing: 16

                Button {
                    text: qsTr("Cancel")
                    Keys.onReturnPressed: clicked()
                    onClicked: {
                        separateSessionPinWindow.visible = false;
                        Chiaki.stopSession(false);
                    }
                }

                Button {
                    id: separatePinOkButton
                    text: qsTr("OK")
                    enabled: separatePinField.acceptableInput
                    Keys.onReturnPressed: clicked()
                    onClicked: {
                        const pin = separatePinField.text;
                        separateSessionPinWindow.visible = false;
                        Chiaki.enterPin(pin);
                    }
                }
            }
        }
    }

    Timer {
        id: closeTimer
        interval: 2000
        onTriggered: root.showMainView()
    }

    Connections {
        target: Chiaki

        function onSessionChanged() {
            if (!Chiaki.session) {
                if (errorTitleLabel.text)
                    closeTimer.start();
                else
                    root.showMainView();
            } else {
                sessionError = false;
                errorTitleLabel.text = "";
                errorTextLabel.text = "";
                sessionLoading = !(Chiaki.window.loadingTransitionComplete || (Chiaki.settings.audioVideoDisabled & 0x02));
            }
        }

        function onSessionError(title, text) {
            sessionError = true;
            sessionLoading = false;
            errorTitleLabel.text = title;
            errorTextLabel.text = text;
            closeTimer.start();
        }

        function onSessionPinDialogRequested() {
            if (sessionPinDialog.opened || separateSessionPinWindow.visible)
                return;
            menuController.close();
            if (useSeparateMenuWindow)
                separateSessionPinWindow.visible = true;
            else
                sessionPinDialog.open();
            Chiaki.window.requestOverlayUpdate();
        }

        function onSessionStopDialogRequested() {
            if (sessionStopDialog.opened || separateSessionStopWindow.visible)
                return;
            menuController.close();
            if (useSeparateMenuWindow)
                separateSessionStopWindow.visible = true;
            else
                sessionStopDialog.open();
            Chiaki.window.requestOverlayUpdate();
        }
    }

    Connections {
        target: Chiaki.window

        function onLoadingTransitionCompleteChanged() {
            if (Chiaki.window.loadingTransitionComplete) {
                sessionLoading = false;
                Chiaki.window.noteLoadingTransitionComplete();
                Chiaki.window.requestOverlayUpdate();
            } else if (Chiaki.session) {
                sessionLoading = !(Chiaki.settings.audioVideoDisabled & 0x02);
            }
        }

        function onMenuRequested() {
            if (sessionPinDialog.opened || sessionStopDialog.opened || separateSessionPinWindow.visible || separateSessionStopWindow.visible)
                return;
            menuController.toggle();
            Chiaki.window.requestOverlayUpdate();
        }
    }
    Connections {
        target: Chiaki

        function onSessionChanged() {
            if (!Chiaki.session)
                menuController.close();
            view.sessionStartMs = 0;   // session ใหม่ = เริ่มนับเวลาเล่นใหม่
            view.markerCount = 0;
            view.noteSessionStart();
        }
    }
    Connections {
        target: Chiaki.session

        function onConnectedChanged() {
            if (Chiaki.settings.audioVideoDisabled & 0x02)
                sessionLoading = false;
            view.noteSessionStart();
        }
    }
}
