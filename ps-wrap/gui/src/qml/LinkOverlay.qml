import QtQuick
import QtQuick.Layouts

import org.streetpea.chiaking

// PS-WRAP Link: ป้ายมุมขวาบนระหว่างสตรีม — โผล่เองเมื่อเปิด Link (Chiaki.link · pswraplink.h)
//  ┌──────────────────────────────┐
//  │ ● LINK      2 watching        │
//  │ (M) Mint     🎮 controller    │
//  │ (J) Jay      watching · 24 ms │
//  │ ┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄ │
//  │ Jay wants the controller      │  ← มีคนขอจอย = แถบสีเหลือง + ปุ่ม/คีย์ลัด
//  │ [ Give  Ctrl+Shift+G ]        │
//  └──────────────────────────────┘
// ไม่มี QR/ลิงก์ในนี้ — overlay ติดไปในภาพที่ส่งให้เพื่อนและในคลิปที่อัด (ลิงก์เชิญอยู่ในเมนู › Link)
Item {
    id: root

    readonly property var link: Chiaki.link !== undefined ? Chiaki.link : null
    readonly property var friends: link ? link.friends : []
    readonly property var requester: {
        for (let i = 0; i < friends.length; ++i)
            if (friends[i].wantsController)
                return friends[i];
        return null;
    }
    readonly property var holder: {
        for (let i = 0; i < friends.length; ++i)
            if (friends[i].controller)
                return friends[i];
        return null;
    }
    // คีย์ลัดใน StreamView (Ctrl+Shift+G / Ctrl+Shift+U) เรียกผ่านสองตัวนี้
    function giveToRequester() {
        if (link && requester)
            link.giveController(requester.peer);
    }
    function takeBack() {
        if (link && holder)
            link.takeController();
    }

    readonly property color ice: "#7fe8ff"
    readonly property color amber: "#ffc857"
    readonly property color violet: "#c4b5fd"
    readonly property var avatarColors: ["#00a7ff", "#a78bfa", "#34d399", "#f472b6", "#fb923c"]
    function avatarColor(name) {
        let h = 0;
        for (let i = 0; i < name.length; ++i)
            h = (h * 31 + name.charCodeAt(i)) >>> 0;
        return avatarColors[h % avatarColors.length];
    }

    implicitWidth: 340

    // ปุ่มเล็กในแถวเพื่อน (คลิกได้ — การ์ดอยู่ใน overlay hit rects ของ StreamView)
    component Pill: Rectangle {
        id: pill
        property string label
        property color tint: Qt.rgba(1, 1, 1, 0.12)
        property color ink: "white"
        signal clicked()
        implicitHeight: 20
        implicitWidth: pillText.implicitWidth + 14
        radius: 10
        color: pillArea.pressed ? Qt.darker(tint, 1.4) : pillArea.containsMouse ? Qt.lighter(tint, 1.35) : tint
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, pillArea.containsMouse ? 0.35 : 0.14)
        Text {
            id: pillText
            anchors.centerIn: parent
            text: pill.label
            font.pixelSize: 11
            font.weight: Font.Bold
            color: pill.ink
        }
        MouseArea {
            id: pillArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pill.clicked()
        }
    }
    // ย้าย/ย่อได้ (StreamView เก็บตำแหน่ง+สถานะย่อใน pswrapPrefs): ลากที่หัวการ์ด · ดับเบิลคลิกหัว = คืนที่เดิม · ปุ่ม – / + = ย่อเหลือหัว
    property bool mini: false
    property real dragMinX: 0
    property real dragMaxX: 0
    property real dragMinY: 0
    property real dragMaxY: 0
    signal dragFinished()
    signal resetPosition()
    signal toggleMini()

    implicitHeight: col.implicitHeight + 24
    width: mini ? headerRow.implicitWidth + 24 : implicitWidth
    height: implicitHeight

    // ---------- การ์ดกระจก (แบบเดียวกับ ClockOverlay) ----------
    Rectangle {
        anchors.fill: parent
        radius: 16
        color: Qt.rgba(8 / 255, 12 / 255, 18 / 255, 0.72)
        border.width: 1
        border.color: root.requester ? Qt.rgba(1, 0.78, 0.34, 0.55) : Qt.rgba(1, 1, 1, 0.09)
        Behavior on border.color { ColorAnimation { duration: 200 } }
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 }
            height: parent.height * 0.45
            radius: parent.radius - 1
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.07) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
            }
        }
    }

    // ลากที่แถบหัว (อยู่ใต้ปุ่มในหัวการ์ด — ปุ่มย่อยังกดได้)
    MouseArea {
        x: 0; y: 0
        width: root.width
        height: headerRow.height + 18
        hoverEnabled: true
        cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        drag.target: root
        drag.minimumX: root.dragMinX
        drag.maximumX: root.dragMaxX
        drag.minimumY: root.dragMinY
        drag.maximumY: root.dragMaxY
        drag.threshold: 4
        onReleased: root.dragFinished()
        onDoubleClicked: root.resetPosition()
    }

    ColumnLayout {
        id: col
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12 }
        spacing: 8

        // หัว: จุดเต้น + LINK + จำนวนคน + ปุ่มย่อ
        RowLayout {
            id: headerRow
            Layout.fillWidth: true
            spacing: 8
            Item {
                Layout.preferredWidth: 12
                Layout.preferredHeight: 12
                Rectangle {
                    id: pulse
                    anchors.centerIn: parent
                    width: 12; height: 12; radius: 6
                    color: root.link && root.link.state === "ready" ? "#3fb950" : "#d29922"
                    opacity: 0.35
                    SequentialAnimation on scale {
                        running: root.visible
                        loops: Animation.Infinite
                        NumberAnimation { from: 0.6; to: 1.6; duration: 1100; easing.type: Easing.OutCubic }
                        PauseAnimation { duration: 300 }
                    }
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 8; height: 8; radius: 4
                    color: pulse.color
                }
            }
            Text {
                text: qsTr("LINK")
                font.pixelSize: 13
                font.weight: Font.Black
                font.letterSpacing: 2
                color: root.ice
            }
            Item { Layout.fillWidth: true }
            Text {
                text: !root.link || root.link.state !== "ready" ? qsTr("Connecting…")
                    : root.friends.length === 0 ? qsTr("Waiting for friends")
                    : qsTr("%n watching", "", root.friends.length)
                font.pixelSize: 12
                font.weight: Font.DemiBold
                color: Qt.rgba(1, 1, 1, 0.75)
            }
            // ย่อ/ขยาย (ย่ออยู่ก็ยังเห็นแถบคำขอจอยถ้ามีคนขอ)
            Rectangle {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                radius: 6
                color: miniArea.containsMouse ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.06)
                Text {
                    anchors.centerIn: parent
                    text: root.mini ? "+" : "–"
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    color: Qt.rgba(1, 1, 1, 0.8)
                }
                MouseArea {
                    id: miniArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.toggleMini()
                }
            }
        }

        // ยังไม่มีใคร: บอกทางเอาลิงก์
        Text {
            visible: !root.mini && !!root.link && root.link.state === "ready" && root.friends.length === 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            color: Qt.rgba(1, 1, 1, 0.6)
            text: qsTr("Invite link is on the clipboard · QR code in the menu › Link")
        }

        // เพื่อนแต่ละคน
        Repeater {
            model: root.mini ? [] : root.friends
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                spacing: 8
                // รูปโปรไฟล์ (PNG ที่ pswraplink encode เอง) · ไม่มีรูป = ตัวอักษรแรกบนสีประจำชื่อ
                Rectangle {
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 26
                    radius: 8
                    color: modelData.color || root.avatarColor(modelData.name)
                    // ขอบเขียว = ถือจอย · ขอบฟ้าเต้น = กำลังพูด
                    border.width: modelData.speaking || modelData.controller ? 2 : 0
                    border.color: modelData.speaking ? "#00a7ff" : "#3fb950"
                    Text {
                        anchors.centerIn: parent
                        visible: !modelData.avatar
                        text: modelData.name.length ? modelData.name.charAt(0).toUpperCase() : "?"
                        font.pixelSize: 13
                        font.weight: Font.Bold
                        color: "white"
                    }
                    Image {
                        anchors.fill: parent
                        anchors.margins: modelData.speaking || modelData.controller ? 2 : 0
                        visible: !!modelData.avatar
                        source: modelData.avatar || ""
                        fillMode: Image.PreserveAspectCrop
                        sourceSize: Qt.size(52, 52)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: modelData.staff ? qsTr("PS-WRAP team · watching")
                        : modelData.micMuted ? qsTr("%1 · muted").arg(modelData.name)
                        : modelData.speaking ? qsTr("%1 · talking").arg(modelData.name)
                        : modelData.name
                    elide: Text.ElideRight
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: modelData.staff ? root.violet : "white"
                }
                // สถานะ: ถือจอย (เขียว) · ขอจอย (เหลือง) · ดูอยู่ + ping
                Rectangle {
                    Layout.preferredHeight: 20
                    Layout.preferredWidth: chip.implicitWidth + 14
                    radius: 10
                    color: modelData.controller ? Qt.rgba(0.25, 0.73, 0.31, 0.25)
                         : modelData.wantsController ? Qt.rgba(1, 0.78, 0.34, 0.22)
                         : Qt.rgba(1, 1, 1, 0.07)
                    Text {
                        id: chip
                        anchors.centerIn: parent
                        text: modelData.controller ? qsTr("Controller")
                            : modelData.wantsController ? qsTr("Wants to play")
                            : modelData.status !== "watching" ? qsTr("Joining…")
                            : modelData.latency >= 0 ? qsTr("%1 ms").arg(modelData.latency) : qsTr("Watching")
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        font.features: { "tnum": 1 }
                        color: modelData.controller ? "#7ee787" : modelData.wantsController ? root.amber : Qt.rgba(1, 1, 1, 0.7)
                    }
                }
                // ปิด/เปิดเสียงไมค์คนนี้ (โผล่เมื่อเพื่อนเปิดไมค์)
                Pill {
                    visible: modelData.mic || modelData.micMuted
                    label: modelData.micMuted ? qsTr("Unmute") : qsTr("Mute")
                    tint: modelData.micMuted ? Qt.rgba(0.95, 0.35, 0.4, 0.45) : Qt.rgba(1, 1, 1, 0.12)
                    onClicked: root.link.setMicMuted(modelData.peer, !modelData.micMuted)
                }
                // ดึงจอยคืนจากคนที่ถืออยู่
                Pill {
                    visible: modelData.controller
                    label: qsTr("Take back")
                    tint: Qt.rgba(0.25, 0.73, 0.31, 0.35)
                    onClicked: root.takeBack()
                }
            }
        }

        // มีคนขอจอย: แถบคำขอ + ปุ่ม (คลิกได้) + คีย์ลัด
        Rectangle {
            visible: !!root.requester
            Layout.fillWidth: true
            Layout.preferredHeight: reqCol.implicitHeight + 16
            radius: 12
            color: Qt.rgba(1, 0.78, 0.34, 0.12)
            border.width: 1
            border.color: Qt.rgba(1, 0.78, 0.34, 0.4)
            ColumnLayout {
                id: reqCol
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: root.requester ? qsTr("%1 wants the controller").arg(root.requester.name) : ""
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: root.amber
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    radius: 8
                    color: giveArea.pressed ? Qt.darker(root.amber, 1.25) : giveArea.containsMouse ? Qt.lighter(root.amber, 1.08) : root.amber
                    Text {
                        anchors.centerIn: parent
                        text: root.holder ? qsTr("Hand over · Ctrl+Shift+G") : qsTr("Give controller · Ctrl+Shift+G")
                        font.pixelSize: 12
                        font.weight: Font.Bold
                        color: "#1a1200"
                    }
                    MouseArea {
                        id: giveArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.giveToRequester()
                    }
                }
            }
        }

        // ทีม PS-WRAP ขอเข้าดู (จากหน้า Monitor) — ต้องกดอนุญาตเอง · ย่ออยู่ก็เห็น · ไม่ตอบ 60 วิ = ไม่อนุญาต
        Rectangle {
            visible: !!root.link && root.link.viewRequest
            Layout.fillWidth: true
            Layout.preferredHeight: viewCol.implicitHeight + 16
            radius: 12
            color: Qt.rgba(0.77, 0.71, 0.99, 0.12)
            border.width: 1
            border.color: Qt.rgba(0.77, 0.71, 0.99, 0.45)
            ColumnLayout {
                id: viewCol
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 10 }
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("PS-WRAP team asks to watch this stream")
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: root.violet
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("View only: no controller, no mic. You can remove them any time.")
                    font.pixelSize: 11
                    color: Qt.rgba(1, 1, 1, 0.6)
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Item { Layout.fillWidth: true }
                    Pill {
                        label: qsTr("Decline")
                        onClicked: root.link.answerViewRequest(false)
                    }
                    Pill {
                        label: qsTr("Allow")
                        tint: Qt.rgba(0.77, 0.71, 0.99, 0.45)
                        onClicked: root.link.answerViewRequest(true)
                    }
                }
            }
        }

        // มีคนถือจอยอยู่: คีย์ลัดดึงคืน (ปุ่มอยู่ในแถวของคนนั้น)
        Text {
            visible: !root.mini && !!root.holder && !root.requester
            Layout.fillWidth: true
            text: qsTr("Take back: Ctrl+Shift+U")
            font.pixelSize: 11
            color: Qt.rgba(1, 1, 1, 0.55)
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.takeBack()
            }
        }
    }
}
