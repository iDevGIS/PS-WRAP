import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

// PS-WRAP: หน้า Settings › Go Live (SettingsDialog index 10 · C++ PsWrapGoLive = Chiaki.goLive — pswraplive.h)
// - จัดการปลายทางไลฟ์หลายแพลตฟอร์ม: เปิด/ปิด, server, stream key (ซ่อน/เผย), bitrate, codec ต่อปลายทาง
// - stream key ไม่ผ่าน QSettings/registry — C++ เก็บใน Windows Credential Manager (pswrapsecrets) ทันทีที่พิมพ์เสร็จ
//   แล้วล้างข้อความในช่องทิ้ง (QML ไม่ถือ key ค้างไว้) · ปุ่ม Show อ่านกลับมาแสดงชั่วคราว 15 วิ
// - ห้ามเผย key ตอน streamer mode หรือกำลังไลฟ์ (จออาจถูกจับภาพออกไลฟ์อยู่)
// - นำทางด้วยจอย/คีย์: ↑↓ ไล่ทุก control ตามลำดับ (focus chain ของ controls/), ←→ ปรับ slider, Enter = แก้/กด
// - destinations ต้องเป็น QAbstractListModel (role ด้านล่าง) ไม่ใช่ QVariantList — ถ้า reset ทั้งลิสต์
//   ทุกครั้งที่แก้ค่า delegate จะถูกสร้างใหม่ → focus หลุด (จอยใช้ไม่ได้) และ handler ที่กำลังรันจะอ้าง object ที่ตายแล้ว
// - API: docs/06-go-live.md › "API ของหน้า Settings" · เริ่ม/หยุดไลฟ์อยู่ที่เมนูสตรีม (Live) / Ctrl+Shift+L / tray — หน้านี้แค่ตั้งค่า + ดูสถานะ
Item {
    id: page

    property real uiScale: 1.0
    readonly property alias flick: liveFlick
    readonly property alias firstControl: addPlatformBox

    // build ที่ไม่มี Go Live → null → หน้าแสดงข้อความแทน ไม่ throw
    readonly property var api: Chiaki.goLive !== undefined && Chiaki.goLive !== null ? Chiaki.goLive : null
    readonly property var platforms: api ? api.platforms : []
    readonly property int destinationCount: api ? api.destinations.count : 0
    readonly property bool revealAllowed: api !== null && !api.live && !Chiaki.settings.streamerMode

    function platformInfo(id) {
        for (let i = 0; i < platforms.length; ++i)
            if (platforms[i].id === id)
                return platforms[i];
        return null;
    }

    function ensureVisible(item) { liveFlick.ensureItemVisible(item) }

    Flickable {
        id: liveFlick
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
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AlwaysOn
            visible: liveFlick.contentHeight > liveFlick.height
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

            // ยังไม่มี backend
            Label {
                Layout.fillWidth: true
                visible: page.api === null
                wrapMode: Text.WordWrap
                color: Theme.warning
                font.pixelSize: Math.round(Theme.fontBody * page.uiScale)
                text: qsTr("Go Live is not available in this build yet.")
            }

            // สถานะตอนไลฟ์ (แก้ค่าไม่ได้ระหว่างไลฟ์)
            Label {
                Layout.fillWidth: true
                visible: page.api !== null && page.api.live
                wrapMode: Text.WordWrap
                font.pixelSize: Math.round(Theme.fontBody * page.uiScale)
                font.weight: Font.DemiBold
                color: page.api && page.api.state === "live" ? Theme.success : Theme.warning
                text: page.api && page.api.live ? "●  " + page.api.summary + "  ·  " + qsTr("stop from the stream menu to change settings") : ""
            }

            // แถบเพิ่มปลายทาง
            Flow {
                Layout.fillWidth: true
                spacing: Theme.space6
                enabled: page.api !== null && !page.api.live

                C.ComboBox {
                    id: addPlatformBox
                    firstInFocusChain: true
                    model: page.platforms
                    textRole: "name"
                    valueRole: "id"
                    onActiveFocusChanged: if (activeFocus) page.ensureVisible(this)
                }
                C.Button {
                    text: qsTr("Add destination")
                    highlighted: true
                    enabled: addPlatformBox.currentIndex >= 0
                    onActiveFocusChanged: if (activeFocus) page.ensureVisible(this)
                    onClicked: page.api.addDestination(addPlatformBox.currentValue)
                }
            }

            Label {
                Layout.fillWidth: true
                font.pixelSize: Theme.fontCaption
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                text: page.api && !page.api.secureStoreAvailable
                      ? qsTr("Stream keys cannot be saved on this system — you will be asked for them each time you go live.")
                      : qsTr("Every enabled destination gets the 16:9 H.264 game picture with your overlays, at the Output Resolution set in Settings › General (YouTube and Custom up to 4K; other platforms are scaled to 1080p). Destinations marked Vertical 9:16 get the 1080 × 1920 picture from the 9:16 window instead. Start and stop from the stream menu (Live) or Ctrl+Shift+L. Stream keys are stored in Windows Credential Manager, never in the settings file.")
            }

            // สรุปแบนด์วิดท์ + คำเตือน (คำนวณฝั่ง C++ — engine ใช้ค่าเดียวกัน)
            Rectangle {
                Layout.fillWidth: true
                visible: page.destinationCount > 0
                implicitHeight: summaryCol.implicitHeight + Theme.space3 * 2
                radius: Theme.radiusControl
                color: Theme.surfaceRaised
                border.width: 1
                border.color: page.api && page.api.twitchParityWarning ? Theme.warning : Theme.border

                ColumnLayout {
                    id: summaryCol
                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        margins: Theme.space3
                    }
                    spacing: Theme.space1
                    Label {
                        Layout.fillWidth: true
                        font.pixelSize: Math.round(Theme.fontLabel * page.uiScale)
                        color: Theme.text
                        wrapMode: Text.WordWrap
                        //: %1 = total upload in Mbps, %2 = recommended minimum upload in Mbps
                        text: qsTr("Total upload: %1 Mbps · recommended connection: at least %2 Mbps up")
                              .arg(((page.api ? page.api.totalKbps : 0) / 1000).toFixed(1))
                              .arg(((page.api ? page.api.totalKbps : 0) * 1.5 / 1000).toFixed(1))
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: page.api !== null && page.api.twitchParityWarning
                        font.pixelSize: Theme.fontCaption
                        color: Theme.warning
                        wrapMode: Text.WordWrap
                        text: qsTr("Twitch simulcasting rules: the Twitch stream must be at least the same quality as your other destinations.")
                    }
                }
            }

            // การ์ดต่อปลายทาง
            Repeater {
                model: page.api ? page.api.destinations : null

                Rectangle {
                    id: card
                    required property int index
                    // role ของ destinations model
                    required property string destId
                    required property string platform
                    required property bool destEnabled
                    required property string server
                    required property bool keySaved
                    required property int videoBitrate
                    required property string codec
                    required property bool vertical
                    required property string status
                    required property string statusText

                    readonly property var info: page.platformInfo(platform)
                    readonly property bool hasPresets: info !== null && info.servers.length > 0
                    property bool customServer: !hasPresets || server === "" || info.servers.findIndex(s => s.url === server) < 0
                    property bool revealed: false

                    Layout.fillWidth: true
                    implicitHeight: cardCol.implicitHeight + Theme.space4 * 2
                    radius: Theme.radiusCard
                    color: Theme.surface
                    border.width: 1
                    border.color: destEnabled ? Theme.accent : Theme.border

                    function set(key, value) { page.api.setDestinationValue(destId, key, value) }

                    // ซ่อน key กลับเองหลัง 15 วิ / เริ่มไลฟ์ / เปิด streamer mode
                    onRevealedChanged: if (revealed) revealTimer.restart()
                    Timer {
                        id: revealTimer
                        interval: 15000
                        onTriggered: card.hideKey()
                    }
                    function hideKey() {
                        keyField.text = "";
                        keyField.dirty = false;
                        revealed = false;
                    }
                    Connections {
                        target: page
                        function onRevealAllowedChanged() { if (!page.revealAllowed && card.revealed) card.hideKey() }
                    }

                    ColumnLayout {
                        id: cardCol
                        anchors {
                            left: parent.left
                            right: parent.right
                            top: parent.top
                            margins: Theme.space4
                        }
                        spacing: Theme.space3

                        // หัวการ์ด: แถบสี + ชื่อแพลตฟอร์ม + เปิด/ปิด + ลบ
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space3
                            Rectangle {
                                Layout.preferredWidth: 4
                                Layout.preferredHeight: Math.round(Theme.fontLabel * page.uiScale)
                                radius: 2
                                color: card.destEnabled ? Theme.accent : Theme.textMuted
                            }
                            Label {
                                Layout.fillWidth: true
                                text: (card.info ? card.info.name : card.platform).toUpperCase()
                                      + (card.vertical ? "  ·  9:16" : "")
                                font.pixelSize: Theme.fontCaption
                                font.letterSpacing: 1.2
                                font.weight: Font.DemiBold
                                color: Theme.textMuted
                                elide: Text.ElideRight
                            }
                            C.CheckBox {
                                text: qsTr("Enabled")
                                checked: card.destEnabled
                                enabled: !page.api.live
                                // onClicked ไม่ใช่ onToggled: Enter บน C.CheckBox เรียก toggle()+clicked() (ไม่ emit toggled)
                                onClicked: card.set("enabled", checked)
                                onActiveFocusChanged: if (activeFocus) page.ensureVisible(this)
                            }
                            C.Button {
                                text: qsTr("Remove")
                                flat: true
                                enabled: !page.api.live
                                onActiveFocusChanged: if (activeFocus) page.ensureVisible(this)
                                onClicked: page.api.removeDestination(card.destId)
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: Theme.space4
                            rowSpacing: Theme.space3

                            // ---- Server ----
                            Label {
                                text: qsTr("Server")
                                font.pixelSize: Math.round(Theme.fontBody * page.uiScale)
                                color: Theme.text
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.space2
                                C.ComboBox {
                                    id: serverBox
                                    Layout.fillWidth: true
                                    visible: card.hasPresets
                                    enabled: !page.api.live
                                    // รายการ preset + "Custom…" ท้ายสุด
                                    model: card.hasPresets ? card.info.servers.map(s => s.label).concat([qsTr("Custom…")]) : []
                                    currentIndex: {
                                        if (!card.hasPresets)
                                            return -1;
                                        const i = card.info.servers.findIndex(s => s.url === card.server);
                                        return i >= 0 && !card.customServer ? i : card.info.servers.length;
                                    }
                                    onActivated: (i) => {
                                        if (i < card.info.servers.length) {
                                            card.customServer = false;
                                            card.set("server", card.info.servers[i].url);
                                        } else {
                                            card.customServer = true;
                                        }
                                    }
                                    onActiveFocusChanged: if (activeFocus) page.ensureVisible(this)
                                }
                                C.TextField {
                                    id: customServerField
                                    Layout.fillWidth: true
                                    visible: card.customServer
                                    enabled: !page.api.live
                                    text: card.server
                                    placeholderText: "rtmps://host:443/app"
                                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                                    onEditingFinished: if (text.trim() !== card.server) card.set("server", text.trim())
                                    onActiveFocusChanged: if (activeFocus) page.ensureVisible(this)
                                }
                            }

                            // ---- Stream key ----
                            Label {
                                text: qsTr("Stream key")
                                font.pixelSize: Math.round(Theme.fontBody * page.uiScale)
                                color: Theme.text
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.space2
                                C.TextField {
                                    id: keyField
                                    Layout.fillWidth: true
                                    enabled: !page.api.live
                                    echoMode: card.revealed ? TextInput.Normal : TextInput.Password
                                    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                                    placeholderText: card.keySaved ? qsTr("Saved — type to replace") : qsTr("Paste stream key")
                                    property bool dirty: false   // ผู้ใช้พิมพ์/วางเอง (ไม่ใช่ค่าที่เผยมา)
                                    onTextEdited: dirty = true
                                    onEditingFinished: {
                                        // ล้างช่องก่อน แล้วค่อยบันทึกลง Credential Manager (ไม่ถือ key ค้างใน QML)
                                        if (!dirty) {
                                            if (!card.revealed)
                                                card.hideKey();
                                            return;
                                        }
                                        const key = text.trim();
                                        const id = card.destId;
                                        card.hideKey();
                                        if (key.length > 0)
                                            page.api.setStreamKey(id, key);
                                    }
                                    onActiveFocusChanged: if (activeFocus) page.ensureVisible(this)
                                }
                                C.Button {
                                    text: card.revealed ? qsTr("Hide") : qsTr("Show")
                                    visible: card.keySaved
                                    enabled: card.revealed || page.revealAllowed
                                    onActiveFocusChanged: if (activeFocus) page.ensureVisible(this)
                                    onClicked: {
                                        if (card.revealed) {
                                            card.hideKey();
                                        } else {
                                            keyField.text = page.api.revealStreamKey(card.destId);
                                            keyField.dirty = false;
                                            card.revealed = true;
                                        }
                                    }
                                }
                                C.Button {
                                    text: qsTr("Clear")
                                    flat: true
                                    visible: card.keySaved
                                    enabled: !page.api.live
                                    onActiveFocusChanged: if (activeFocus) page.ensureVisible(this)
                                    onClicked: {
                                        const id = card.destId;
                                        card.hideKey();
                                        page.api.clearStreamKey(id);
                                    }
                                }
                            }

                            // ---- Codec ---- (ซ่อนเมื่อมีตัวเลือกเดียว — ตอนนี้ H.264 อย่างเดียวทุกเจ้า)
                            Label {
                                visible: codecBox.visible
                                text: qsTr("Video codec")
                                font.pixelSize: Math.round(Theme.fontBody * page.uiScale)
                                color: Theme.text
                            }
                            C.ComboBox {
                                id: codecBox
                                Layout.fillWidth: true
                                visible: model.length > 1
                                enabled: !page.api.live
                                model: card.info ? card.info.codecs : ["h264"]
                                displayText: ({ h264: "H.264", hevc: "HEVC (H.265)", av1: "AV1" })[currentText] || currentText
                                currentIndex: Math.max(0, model.indexOf(card.codec))
                                onActivated: (i) => card.set("codec", model[i])
                                onActiveFocusChanged: if (activeFocus) page.ensureVisible(this)
                            }

                            // ---- Bitrate ----
                            Label {
                                text: qsTr("Video bitrate")
                                font.pixelSize: Math.round(Theme.fontBody * page.uiScale)
                                color: Theme.text
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.space3
                                C.Slider {
                                    id: bitrateSlider
                                    Layout.fillWidth: true
                                    enabled: !page.api.live
                                    from: 1000
                                    to: card.info ? card.info.maxVideoKbps : 6000
                                    stepSize: 500
                                    snapMode: Slider.SnapAlways
                                    value: card.videoBitrate
                                    onMoved: card.set("videoBitrate", Math.round(value))
                                    onActiveFocusChanged: if (activeFocus) page.ensureVisible(this)
                                }
                                Label {
                                    Layout.minimumWidth: Math.round(110 * page.uiScale)
                                    horizontalAlignment: Text.AlignRight
                                    font.pixelSize: Math.round(Theme.fontLabel * page.uiScale)
                                    font.weight: Font.DemiBold
                                    color: Theme.text
                                    text: qsTr("%1 Mbps").arg((bitrateSlider.value / 1000).toFixed(1))
                                }
                            }

                            // ---- PS-WRAP: ภาพแนวตั้ง 9:16 (เลย์เอาต์จากหน้าต่าง 9:16 ในเมนูสตรีม) ----
                            Label {
                                text: qsTr("Picture")
                                font.pixelSize: Math.round(Theme.fontBody * page.uiScale)
                                color: Theme.text
                            }
                            C.CheckBox {
                                text: qsTr("Vertical 9:16 (1080 × 1920, layout from the 9:16 window)")
                                checked: card.vertical
                                enabled: !page.api.live
                                onClicked: card.set("vertical", checked)
                                onActiveFocusChanged: if (activeFocus) page.ensureVisible(this)
                            }
                        }

                        // หมายเหตุเฉพาะแพลตฟอร์ม (key ต่อรอบ, คุณสมบัติบัญชี ฯลฯ) + สถานะล่าสุดจาก engine
                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            wrapMode: Text.WordWrap
                            text: card.info && card.info.note ? card.info.note : ""
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            font.pixelSize: Theme.fontCaption
                            color: card.status === "error" ? Theme.danger
                                 : card.status === "live" ? Theme.success
                                 : card.status === "reconnecting" ? Theme.warning : Theme.textMuted
                            wrapMode: Text.WordWrap
                            text: card.statusText
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: page.api !== null && page.destinationCount === 0
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Math.round(Theme.fontBody * page.uiScale)
                color: Theme.textMuted
                wrapMode: Text.WordWrap
                text: qsTr("No destinations yet. Pick a platform above and press Add destination.")
            }

            // error ล่าสุดจาก backend (เช่น Credential Manager เขียนไม่ได้)
            Label {
                Layout.fillWidth: true
                visible: page.api !== null && page.api.lastError.length > 0
                font.pixelSize: Theme.fontCaption
                color: Theme.danger
                wrapMode: Text.WordWrap
                text: page.api ? page.api.lastError : ""
            }

            // ---------- PS-WRAP: แชทไลฟ์บนจอ (Chiaki.window.liveChat — pswrapchat.cpp) ----------
            Rectangle {
                id: chatCardBox
                readonly property QtObject chat: Chiaki.window ? Chiaki.window.liveChat : null
                Layout.fillWidth: true
                Layout.topMargin: Theme.space4
                implicitHeight: chatCol.implicitHeight + Theme.space4 * 2
                radius: Theme.radiusCard
                color: Theme.surface
                border.width: 1
                border.color: Theme.border

                GridLayout {
                    id: chatCol
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.space4 }
                    columns: 2
                    columnSpacing: Theme.space4
                    rowSpacing: Theme.space3

                    Label {
                        Layout.columnSpan: 2
                        text: qsTr("CHAT ON SCREEN")
                        font.pixelSize: Theme.fontCaption
                        font.letterSpacing: 1.5
                        font.weight: Font.DemiBold
                        color: Theme.textMuted
                    }
                    Label {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textMuted
                        text: qsTr("Shows YouTube and Twitch chat as an overlay while you stream (Chat in the stream menu, Ctrl+Shift+H or the tray). It connects only while the overlay is on. Twitch needs no login. YouTube needs a YouTube Data API v3 key from Google Cloud Console; each check costs quota, so chat refreshes every 5 seconds or more.")
                    }

                    Label {
                        text: qsTr("YouTube")
                        font.pixelSize: Math.round(Theme.fontBody * page.uiScale)
                        color: Theme.text
                    }
                    C.TextField {
                        Layout.fillWidth: true
                        text: chatCardBox.chat ? chatCardBox.chat.youtubeSource : ""
                        placeholderText: qsTr("@handle, channel ID (UC…) or live video link")
                        onEditingFinished: if (chatCardBox.chat) chatCardBox.chat.youtubeSource = text
                        onActiveFocusChanged: if (activeFocus) page.ensureVisible(this)
                    }

                    Label {
                        text: qsTr("API key")
                        font.pixelSize: Math.round(Theme.fontBody * page.uiScale)
                        color: Theme.text
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        C.TextField {
                            id: ytKeyField
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                            inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                            placeholderText: chatCardBox.chat && chatCardBox.chat.youtubeKeySaved ? qsTr("Saved — type to replace") : qsTr("Paste YouTube Data API v3 key")
                            onEditingFinished: {
                                // ล้างช่องก่อน แล้วค่อยบันทึกลง Credential Manager (ไม่ถือ key ค้างใน QML)
                                const key = text.trim();
                                text = "";
                                if (key.length > 0 && chatCardBox.chat)
                                    chatCardBox.chat.setYoutubeApiKey(key);
                            }
                            onActiveFocusChanged: if (activeFocus) page.ensureVisible(this)
                        }
                        C.Button {
                            text: qsTr("Clear")
                            flat: true
                            visible: !!chatCardBox.chat && chatCardBox.chat.youtubeKeySaved
                            onClicked: chatCardBox.chat.setYoutubeApiKey("")
                        }
                    }

                    Label {
                        text: qsTr("Twitch")
                        font.pixelSize: Math.round(Theme.fontBody * page.uiScale)
                        color: Theme.text
                    }
                    C.TextField {
                        Layout.fillWidth: true
                        text: chatCardBox.chat ? chatCardBox.chat.twitchChannel : ""
                        placeholderText: qsTr("channel name (no login needed)")
                        onEditingFinished: if (chatCardBox.chat) chatCardBox.chat.twitchChannel = text
                        onActiveFocusChanged: if (activeFocus) page.ensureVisible(this)
                    }

                    Label {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        visible: text.length > 0
                        wrapMode: Text.WordWrap
                        font.pixelSize: Theme.fontCaption
                        color: Theme.textMuted
                        text: chatCardBox.chat && chatCardBox.chat.running ? chatCardBox.chat.status : ""
                    }
                }
            }
        }
    }
}
