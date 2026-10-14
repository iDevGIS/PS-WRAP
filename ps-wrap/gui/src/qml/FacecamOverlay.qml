import QtQuick
import QtQuick.Layouts

import org.streetpea.chiaking

// PS-WRAP Link: facecam ของเพื่อน — วงกลมเรียงแถวมุมซ้ายล่างของภาพ (Chiaki.link.cams + image://linkcam)
// เพื่อนเปิดกล้องเองจากหน้าเว็บ (ยินยอมแล้วว่าภาพขึ้นจอเจ้าของเครื่อง) · ภาพมา P2P ไม่ผ่านเซิร์ฟเวอร์
// กำลังพูด = วงแหวนฟ้าเรือง · ชี้แล้วมี ✕ ปิดกล้องคนนั้น (เหมือนปิดไมค์ — อนุญาตใหม่ที่การ์ด Link) · ติดไปในภาพที่ส่งให้เพื่อน/คลิปด้วย
//   ( ◉ )  ( ◉ )
//    Jay    Mint
Item {
    id: root

    readonly property var link: Chiaki.link !== undefined ? Chiaki.link : null
    readonly property var cams: link ? link.cams : ({})
    readonly property var friends: link ? link.friends : []
    property var hiddenPeers: ({})   // peer → true (เจ้าของเครื่องกด ✕)
    readonly property var shown: {
        const out = [];
        for (let i = 0; i < friends.length; ++i) {
            const f = friends[i];
            if (cams[f.peer] !== undefined && !hiddenPeers[f.peer])
                out.push(f);
        }
        return out;
    }
    readonly property int bubble: 112
    readonly property color speakColor: "#5cc8ff"

    implicitWidth: row.implicitWidth
    implicitHeight: bubble + 28
    width: implicitWidth
    height: implicitHeight
    visible: shown.length > 0

    function hide(peer) {
        const h = Object.assign({}, hiddenPeers);
        h[peer] = true;
        hiddenPeers = h;
    }
    // เพื่อนปิดกล้องแล้ว = ล้างการซ่อน (เปิดใหม่ครั้งหน้าขึ้นอีก)
    onCamsChanged: {
        let changed = false;
        const h = Object.assign({}, hiddenPeers);
        for (const p in h)
            if (cams[p] === undefined) {
                delete h[p];
                changed = true;
            }
        if (changed)
            hiddenPeers = h;
    }

    Row {
        id: row
        spacing: 14
        Repeater {
            model: root.shown
            delegate: Item {
                id: cell
                required property var modelData
                width: root.bubble
                height: root.bubble + 28
                // วงแหวนพูด (ฟ้าเรืองตามเสียง)
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: root.bubble + 10
                    height: width
                    radius: width / 2
                    y: -5
                    color: "transparent"
                    border.width: 3
                    border.color: cell.modelData.speaking ? root.speakColor : Qt.rgba(1, 1, 1, 0.18)
                    opacity: cell.modelData.speaking ? 1 : 0.8
                    Behavior on border.color { ColorAnimation { duration: 120 } }
                    SequentialAnimation on scale {
                        running: cell.modelData.speaking
                        loops: Animation.Infinite
                        NumberAnimation { from: 1.0; to: 1.05; duration: 380; easing.type: Easing.OutQuad }
                        NumberAnimation { from: 1.05; to: 1.0; duration: 380; easing.type: Easing.InQuad }
                    }
                }
                Rectangle {   // พื้นหลังระหว่างรอเฟรมแรก
                    width: root.bubble
                    height: width
                    radius: width / 2
                    color: Qt.rgba(0, 0, 0, 0.45)
                }
                Image {
                    width: root.bubble
                    height: root.bubble
                    cache: false
                    smooth: true
                    // เลขเฟรมเปลี่ยน = โหลดใหม่ (provider ให้ภาพกลมแล้ว)
                    source: "image://linkcam/" + cell.modelData.peer + "?" + (root.cams[cell.modelData.peer] || 0)
                }
                // ชื่อ
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: root.bubble + 4
                    height: 22
                    width: Math.min(root.bubble + 20, nameText.implicitWidth + 18)
                    radius: 11
                    color: Qt.rgba(0, 0, 0, 0.62)
                    border.width: 1
                    border.color: cell.modelData.controller ? "#3fb950" : Qt.rgba(1, 1, 1, 0.12)
                    Text {
                        id: nameText
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, root.bubble + 4)
                        elide: Text.ElideRight
                        text: cell.modelData.name
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        color: cell.modelData.controller ? "#aff5b4" : "white"
                    }
                }
                // ✕ ซ่อนกล้องคนนี้ (โผล่ตอนชี้)
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                }
                Rectangle {
                    visible: hover.containsMouse || closeArea.containsMouse
                    width: 24
                    height: 24
                    radius: 12
                    x: root.bubble - 22
                    y: 2
                    color: closeArea.containsMouse ? "#e5534b" : Qt.rgba(0, 0, 0, 0.7)
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, 0.3)
                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        font.pixelSize: 12
                        font.weight: Font.Bold
                        color: "white"
                    }
                    MouseArea {
                        id: closeArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        // ปิดกล้องเพื่อน (หน้าเว็บเขาปิดกล้อง) · เปิดใหม่ได้ที่ปุ่มกล้องบนการ์ด Link
                        onClicked: root.link ? root.link.setCamBlocked(cell.modelData.peer, true) : root.hide(cell.modelData.peer)
                    }
                }
            }
        }
    }
}
