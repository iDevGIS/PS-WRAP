import QtQuick

import org.streetpea.chiaking

// PS-WRAP: overlay แชทไลฟ์ขณะสตรีม (YouTube + Twitch + เพื่อนใน Link จาก Chiaki.window.liveChat) — การ์ดกระจกแบบเดียวกับ ClockOverlay
// วาดที่ขนาดออกแบบ 340×420 เสมอ — StreamView ย่อขยายด้วย scale · ข้อความใหม่อยู่ล่างสุด ของเก่าเลื่อนขึ้นแล้วจางหาย
// ถูกบันทึกลงคลิป/ไลฟ์แนวนอนด้วย (อยู่ใน QML scene เหมือน overlay อื่น)
Item {
    id: root

    property real overlayOpacity: 0.95
    readonly property QtObject chat: Chiaki.window ? Chiaki.window.liveChat : null
    readonly property var messages: chat ? chat.messages : []

    implicitWidth: 340
    implicitHeight: 420
    width: implicitWidth
    height: implicitHeight
    opacity: overlayOpacity

    function esc(s) { return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;"); }

    // ---------- การ์ดกระจก ----------
    Rectangle {
        anchors.fill: parent
        radius: 16
        color: Qt.rgba(8 / 255, 12 / 255, 18 / 255, 0.58)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.09)
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: parent.radius; rightMargin: parent.radius }
            height: 1
            color: Theme.accent
            opacity: 0.45
        }
    }

    // หัวการ์ด: LIVE CHAT + สถานะ
    Row {
        id: head
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
        spacing: 8
        Rectangle {
            width: 8; height: 8; radius: 4
            anchors.verticalCenter: parent.verticalCenter
            color: root.chat && root.chat.running ? "#ff4e45" : Theme.textMuted
            SequentialAnimation on opacity {
                running: !!root.chat && root.chat.running
                loops: Animation.Infinite
                NumberAnimation { to: 0.35; duration: 900 }
                NumberAnimation { to: 1.0; duration: 900 }
            }
        }
        Text {
            text: qsTr("LIVE CHAT")
            font.pixelSize: 13
            font.weight: Font.DemiBold
            font.letterSpacing: 1.5
            color: Theme.text
        }
    }

    ListView {
        id: list
        anchors { left: parent.left; right: parent.right; top: head.bottom; bottom: statusText.top; leftMargin: 14; rightMargin: 14; topMargin: 8; bottomMargin: 6 }
        clip: true
        interactive: false
        spacing: 6
        model: root.messages
        verticalLayoutDirection: ListView.TopToBottom
        onCountChanged: positionViewAtEnd()
        onHeightChanged: positionViewAtEnd()
        delegate: Item {
            id: row
            required property var modelData
            required property int index
            readonly property bool hasAvatar: !!modelData.avatar
            width: ListView.view.width
            height: Math.max(hasAvatar ? 24 : 0, msg.implicitHeight)
            // ข้อความเก่าจางลงตามลำดับ (12 ล่าสุดชัด)
            opacity: Math.max(0.35, 1.0 - Math.max(0, root.messages.length - 1 - index - 11) * 0.08)
            // PS-WRAP Link: รูปโปรไฟล์เพื่อน (PNG 64×64 ที่ pswraplink encode เอง)
            Rectangle {
                id: avatarBox
                visible: row.hasAvatar
                width: 24; height: 24; radius: 7
                color: Qt.rgba(1, 1, 1, 0.1)
                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    source: row.hasAvatar ? row.modelData.avatar : ""
                    fillMode: Image.PreserveAspectCrop
                    sourceSize: Qt.size(48, 48)
                    smooth: true
                }
            }
            Text {
                id: msg
                anchors { left: row.hasAvatar ? avatarBox.right : parent.left; leftMargin: row.hasAvatar ? 8 : 0; right: parent.right }
                wrapMode: Text.Wrap
                textFormat: Text.StyledText
                font.pixelSize: 15
                color: Theme.text
                style: Text.Raised
                styleColor: Qt.rgba(0, 0, 0, 0.45)
                readonly property string platformMark: row.modelData.platform === "twitch" ? "<font color='#a970ff'>●</font> "
                                                     : row.modelData.platform === "link" ? "<font color='#00a7ff'>●</font> "
                                                     : "<font color='#ff4e45'>●</font> "
                readonly property string badgeMark: row.modelData.badge === "owner" ? "★ " : row.modelData.badge === "mod" ? "MOD " : row.modelData.badge === "super" ? "$ " : ""
                text: platformMark + "<b><font color='" + row.modelData.color + "'>" + badgeMark + root.esc(row.modelData.author) + "</font></b>  " + root.esc(row.modelData.text)
            }
        }
    }

    Text {
        id: statusText
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 12 }
        visible: text.length > 0
        text: root.chat ? root.chat.status : ""
        wrapMode: Text.Wrap
        maximumLineCount: 3
        elide: Text.ElideRight
        font.pixelSize: 11
        color: Theme.textMuted
    }

    Text {
        anchors.centerIn: list
        width: list.width
        horizontalAlignment: Text.AlignHCenter
        visible: root.messages.length === 0
        wrapMode: Text.Wrap
        text: qsTr("Waiting for chat messages…")
        font.pixelSize: 13
        color: Theme.textMuted
    }
}
