import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

// PS-WRAP Link: การ์ดที่เปิดจากไทล์ Link ในเมนูสตรีม (Chiaki.link — pswraplink.h)
//  ลิงก์เชิญ (QR / คัดลอก / สร้างใหม่) · รายชื่อเพื่อน (ให้จอย / ดึงคืน / เตะ) · ปิด Link
//  จอย/คีย์บอร์ด: ↑↓←→ ย้ายโฟกัสตามลำดับ · ✕/Enter = กด · ◯/Esc = ปิดการ์ด
Popup {
    id: panel
    readonly property var link: Chiaki.link !== undefined ? Chiaki.link : null
    readonly property bool ready: !!panel.link && panel.link.state === "ready"
    property Item returnFocusTo: null
    // QR ปิดไว้ก่อนทุกครั้ง — กันคนดูไลฟ์ (YouTube ฯลฯ) สแกนจากจอแล้วเข้ามาเอง · กดเพื่อโชว์
    property bool qrShown: false
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 16
    width: 440

    function stateText() {
        const l = panel.link;
        if (!l || !l.active)
            return qsTr("Off");
        if (l.state === "connecting")
            return qsTr("Connecting…");
        if (l.state === "error")
            return qsTr("Error");
        return l.friendCount > 0 ? qsTr("%n friend(s) connected", "", l.friendCount) : qsTr("Waiting for friends");
    }
    function stageText() {
        const l = panel.link;
        if (!l || l.stageState === "off")
            return "";
        if (l.stageState === "live")
            return qsTr("Stage Broadcast is live · %1 of %2 viewers").arg(l.stageViewers).arg(l.stageMax);
        if (l.stageState === "error")
            return qsTr("Stage Broadcast couldn't connect — retrying…");
        return qsTr("Starting Stage Broadcast…");
    }
    function modeHint(m) {
        if (m === "stage")
            return qsTr("Sent once through the PS-WRAP server, so many friends can watch, chat and react. A friend you hand the controller to is moved to a direct connection.");
        if (m === "both")
            return qsTr("The first 4 friends connect directly; everyone after them watches through Stage Broadcast.");
        return qsTr("Up to 4 friends, each sent straight from this PC: the lowest delay, everyone can talk and play.");
    }
    function stateColor() {
        const l = panel.link;
        if (!l || !l.active) return Theme.textMuted;
        return l.state === "ready" ? Theme.success : l.state === "error" ? Theme.danger : Theme.warning;
    }
    function friendLine(f) {
        let parts = [];
        parts.push(f.stage ? qsTr("Watching on Stage") : f.status === "watching" ? qsTr("Watching") : qsTr("Connecting…"));
        if (!f.stage)
            parts.push(f.quality);
        if (f.latency >= 0)
            parts.push(qsTr("ping %1 ms").arg(f.latency));
        if (f.micMuted)
            parts.push(qsTr("mic muted"));
        else if (f.speaking)
            parts.push(qsTr("talking"));
        else if (f.mic)
            parts.push(qsTr("mic on"));
        return parts.join(" · ");
    }

    // ---------- โฟกัส: เรียงตามลำดับในการ์ด (ลูกศรทุกทิศ = ก่อนหน้า/ถัดไป) ----------
    function collect(item, out) {
        if (!item || item.visible === false)
            return;
        if (item.navigable === true && item.enabled)
            out.push(item);
        for (let i = 0; i < item.children.length; ++i)
            collect(item.children[i], out);
    }
    function move(from, delta) {
        let items = [];
        collect(body, items);
        if (!items.length)
            return;
        const i = items.indexOf(from);
        items[(i < 0 ? 0 : (i + delta + items.length) % items.length)].forceActiveFocus(Qt.TabFocusReason);
    }
    function focusFirst() {
        let items = [];
        collect(body, items);
        if (items.length)
            items[0].forceActiveFocus(Qt.TabFocusReason);
    }
    function navKey(item, event) {
        switch (event.key) {
        case Qt.Key_Up:
        case Qt.Key_Left:  move(item, -1); break;
        case Qt.Key_Down:
        case Qt.Key_Right: move(item, 1); break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Space: item.clicked(); break;
        case Qt.Key_Escape: panel.close(); break;
        default: return;
        }
        event.accepted = true;
    }

    onAboutToShow: qrShown = false
    onOpened: focusFirst()
    onClosed: if (returnFocusTo && returnFocusTo.visible) returnFocusTo.forceActiveFocus(Qt.TabFocusReason)
    // รายชื่อเพื่อนเปลี่ยน → ปุ่มในแถวถูกสร้างใหม่ โฟกัสหลุด → กลับไปปุ่มแรก
    Connections {
        target: panel.link
        ignoreUnknownSignals: true
        function onChanged() {
            Qt.callLater(function() {
                if (panel.visible && !panel.contentItem.activeFocus)
                    panel.focusFirst();
            });
        }
    }

    background: Rectangle {
        radius: 16
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.accent
    }

    component PanelButton: Button {
        id: pb
        property bool navigable: true
        property bool primary: false
        property bool danger: false
        flat: true
        focusPolicy: Qt.StrongFocus
        font.pixelSize: 13
        font.weight: Font.DemiBold
        leftPadding: 14
        rightPadding: 14
        Keys.onPressed: (event) => panel.navKey(pb, event)
        background: Rectangle {
            implicitHeight: 36
            radius: 10
            color: pb.danger ? (pb.down ? "#b33a3a" : Qt.rgba(0.95, 0.35, 0.35, 0.16))
                 : pb.primary ? (pb.down ? Qt.darker(Theme.accent, 1.2) : Theme.accent)
                 : pb.down ? Theme.surfaceHover
                 : pb.hovered ? Qt.rgba(1, 1, 1, 0.1) : Qt.rgba(1, 1, 1, 0.05)
            border.width: pb.activeFocus ? Theme.focusWidth : 1
            border.color: pb.activeFocus ? Theme.accent : Qt.rgba(1, 1, 1, 0.08)
            opacity: pb.enabled ? 1.0 : 0.4
        }
        contentItem: Label {
            text: pb.text
            font: pb.font
            color: pb.primary ? Theme.accentText : pb.danger ? "#ff9a9a" : Theme.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    contentItem: ColumnLayout {
        id: body
        spacing: 12

        // หัว: ชื่อ + สถานะ
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Image {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                sourceSize: Qt.size(20, 20)
                source: "qrc:/icons/menu/link.svg"
            }
            Label {
                text: qsTr("Link")
                font.pixelSize: 18
                font.weight: Font.Bold
                color: Theme.text
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: panel.stateColor()
            }
            Label {
                text: panel.stateText()
                font.pixelSize: Theme.fontCaption
                color: Theme.textMuted
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontCaption
            color: Theme.textMuted
            text: qsTr("Friends open the invite link in a web browser to watch and hear the game. One friend at a time can hold the controller.")
        }

        // แบบห้อง: Direct P2P / Stage Broadcast / ทั้งคู่ — เปลี่ยนได้ระหว่างเปิด (ห้องและลิงก์เดิม)
        Label {
            text: qsTr("ROOM TYPE")
            font.pixelSize: 11
            font.letterSpacing: 1.6
            font.weight: Font.DemiBold
            color: Theme.textMuted
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: [
                    { key: "direct", label: qsTr("Direct P2P") },
                    { key: "stage", label: qsTr("Stage Broadcast") },
                    { key: "both", label: qsTr("Both") }
                ]
                delegate: PanelButton {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.label
                    primary: !!panel.link && panel.link.mode === modelData.key
                    enabled: !!panel.link && (!panel.link.active || panel.ready)
                    onClicked: if (panel.link) panel.link.mode = modelData.key
                    Accessible.name: modelData.label
                }
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontCaption
            color: Theme.textMuted
            text: panel.link ? panel.modeHint(panel.link.mode) : ""
        }
        Label {
            Layout.fillWidth: true
            visible: text !== ""
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontCaption
            font.weight: Font.DemiBold
            color: panel.link && panel.link.stageState === "live" ? Theme.success
                 : panel.link && panel.link.stageState === "error" ? Theme.danger : Theme.warning
            text: panel.stageText()
        }

        // ลิงก์เชิญ: QR + ปุ่ม
        RowLayout {
            Layout.fillWidth: true
            spacing: 14
            visible: panel.ready
            PanelButton {
                id: qrBtn
                Layout.preferredWidth: 132
                Layout.preferredHeight: 132
                padding: 0
                onClicked: panel.qrShown = !panel.qrShown
                Accessible.name: panel.qrShown ? qsTr("Hide QR code") : qsTr("Show QR code")
                background: Rectangle {
                    radius: 8
                    color: panel.qrShown ? "white" : (qrBtn.down ? Theme.surfaceHover : qrBtn.hovered ? Qt.rgba(1, 1, 1, 0.1) : Qt.rgba(1, 1, 1, 0.05))
                    border.width: qrBtn.activeFocus ? Theme.focusWidth : 1
                    border.color: qrBtn.activeFocus ? Theme.accent : Qt.rgba(1, 1, 1, 0.08)
                }
                contentItem: Item {
                    Image {
                        anchors.fill: parent
                        anchors.margins: 4
                        visible: panel.qrShown
                        source: panel.qrShown && panel.link ? panel.link.inviteQr : ""
                        smooth: false
                        fillMode: Image.PreserveAspectFit
                    }
                    Column {
                        anchors.centerIn: parent
                        width: parent.width - 16
                        spacing: 6
                        visible: !panel.qrShown
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 28
                            height: 28
                            sourceSize: Qt.size(28, 28)
                            source: "qrc:/icons/menu/link.svg"
                            opacity: 0.7
                        }
                        Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: qsTr("Show QR code")
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textMuted
                    text: qsTr("Copy the link and send it to a friend, or show the QR code to scan. Hidden so stream viewers can't scan it.")
                }
                PanelButton {
                    Layout.fillWidth: true
                    primary: true
                    text: qsTr("Copy invite link")
                    onClicked: panel.link.copyInvite()
                }
                PanelButton {
                    Layout.fillWidth: true
                    text: qsTr("New link")
                    onClicked: { panel.qrShown = false; panel.link.resetInvite(); }
                    ToolTip.visible: hovered || activeFocus
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("The old link stops working. Friends already here stay.")
                }
            }
        }
        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            visible: !!panel.link && panel.link.active && !panel.ready
            running: visible
        }

        // ทีม PS-WRAP ขอเข้าดู — เจ้าของห้องต้องกดอนุญาตเอง (ไม่ตอบ 60 วิ = ไม่อนุญาต)
        Rectangle {
            visible: !!panel.link && panel.link.viewRequest
            Layout.fillWidth: true
            implicitHeight: viewReqCol.implicitHeight + 20
            radius: 12
            color: Qt.rgba(0.77, 0.71, 0.99, 0.12)
            border.width: 1
            border.color: Qt.rgba(0.77, 0.71, 0.99, 0.45)
            ColumnLayout {
                id: viewReqCol
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("PS-WRAP team asks to watch this stream")
                    font.weight: Font.DemiBold
                    color: "#c4b5fd"
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("View only: no controller, no mic. You can remove them any time.")
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textMuted
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    PanelButton {
                        Layout.fillWidth: true
                        text: qsTr("Decline")
                        onClicked: panel.link.answerViewRequest(false)
                    }
                    PanelButton {
                        Layout.fillWidth: true
                        primary: true
                        text: qsTr("Allow")
                        onClicked: panel.link.answerViewRequest(true)
                    }
                }
            }
        }

        // เพื่อน
        Label {
            visible: panel.ready
            text: !panel.link ? "" : panel.link.stageViewers > 0
                  ? qsTr("FRIENDS · %1/4 DIRECT · %2 ON STAGE").arg(panel.link.friendCount - panel.link.stageViewers).arg(panel.link.stageViewers)
                  : qsTr("FRIENDS · %1/4").arg(panel.link.friendCount)
            font.pixelSize: 11
            font.letterSpacing: 1.6
            font.weight: Font.DemiBold
            color: Theme.textMuted
        }
        Label {
            visible: panel.ready && panel.link.friendCount === 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            text: qsTr("No one yet — send the invite link.")
        }
        Repeater {
            model: panel.link && panel.ready ? panel.link.friends : []
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: friendRow.implicitHeight + 16
                radius: 12
                color: modelData.controller ? Qt.rgba(0, 0.65, 1, 0.14) : Qt.rgba(1, 1, 1, 0.04)
                border.width: modelData.wantsController ? 1 : 0
                border.color: Theme.warning
                RowLayout {
                    id: friendRow
                    anchors.fill: parent
                    anchors.margins: 8
                    anchors.leftMargin: 12
                    spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Label {
                                Layout.maximumWidth: 210
                                elide: Text.ElideRight
                                text: modelData.staff ? qsTr("PS-WRAP team · view only")
                                    : modelData.controller ? qsTr("%1 · has the controller").arg(modelData.name)
                                    : modelData.wantsController ? qsTr("%1 · wants the controller").arg(modelData.name)
                                    : modelData.name
                                font.weight: Font.DemiBold
                                color: modelData.staff ? "#c4b5fd" : modelData.wantsController ? Theme.warning : Theme.text
                            }
                            // ต่อแบบไหน: P2P / STAGE
                            Rectangle {
                                Layout.preferredHeight: 18
                                Layout.preferredWidth: badgeRow.implicitWidth + 12
                                radius: 6
                                color: modelData.stage ? Qt.rgba(0.49, 0.36, 1, 0.28) : Qt.rgba(0, 0.65, 1, 0.22)
                                border.width: 1
                                border.color: modelData.stage ? Qt.rgba(0.77, 0.71, 0.99, 0.55) : Qt.rgba(0.36, 0.78, 1, 0.5)
                                Row {
                                    id: badgeRow
                                    anchors.centerIn: parent
                                    spacing: 4
                                    Image {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 11
                                        height: 11
                                        sourceSize: Qt.size(22, 22)
                                        source: modelData.stage ? "qrc:/icons/menu/stage.svg" : "qrc:/icons/menu/p2p.svg"
                                    }
                                    Label {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData.stage ? qsTr("STAGE") : qsTr("P2P")
                                        font.pixelSize: 10
                                        font.weight: Font.Black
                                        font.letterSpacing: 0.6
                                        color: modelData.stage ? "#ddd6fe" : "#bae6fd"
                                    }
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }
                        Label {
                            text: panel.friendLine(modelData)
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                        }
                    }
                    PanelButton {
                        visible: !modelData.staff
                        text: modelData.controller ? qsTr("Take back") : qsTr("Give controller")
                        primary: !modelData.controller && modelData.wantsController
                        onClicked: modelData.controller ? panel.link.takeControllerFrom(modelData.peer) : panel.link.giveController(modelData.peer)
                    }
                    // ไมค์เพื่อน: โผล่เมื่อเพื่อนเปิดไมค์ (หรือถูกปิดเสียงไว้)
                    PanelButton {
                        visible: modelData.mic || modelData.micMuted
                        text: modelData.micMuted ? qsTr("Unmute") : qsTr("Mute")
                        onClicked: panel.link.setMicMuted(modelData.peer, !modelData.micMuted)
                    }
                    // กล้องเพื่อน: ปิด/อนุญาต (โผล่เมื่อเปิดอยู่หรือถูกปิดไว้)
                    PanelButton {
                        visible: (!!panel.link.cams && panel.link.cams[modelData.peer] !== undefined) || !!modelData.camBlocked
                        text: modelData.camBlocked ? qsTr("Allow camera") : qsTr("Camera off")
                        onClicked: panel.link.setCamBlocked(modelData.peer, !modelData.camBlocked)
                    }
                    // ดาว = ขอจอยแล้วได้เลย (จำไว้แม้ออกแล้วเข้าใหม่)
                    PanelButton {
                        visible: !!modelData.canStar
                        text: modelData.starred ? qsTr("★ Starred") : qsTr("☆ Star")
                        onClicked: panel.link.setStarred(modelData.peer, !modelData.starred)
                    }
                    PanelButton {
                        text: qsTr("Remove")
                        danger: true
                        onClicked: panel.link.kick(modelData.peer)
                    }
                }
            }
        }

        PanelButton {
            Layout.fillWidth: true
            danger: true
            visible: !!panel.link && panel.link.active
            text: qsTr("Turn Link off")
            onClicked: { panel.link.stop(); panel.close(); }
        }
    }
}
