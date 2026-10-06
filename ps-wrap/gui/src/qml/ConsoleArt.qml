import QtQuick

import org.streetpea.chiaking

// PS-WRAP: รูปเครื่องตามรุ่น (PS5 ยืน / PS4 นอน) + ไฟสถานะมี animation
//   state: "ready" → ไฟฟ้า เต้นช้าๆ · "standby" → ไฟส้ม หายใจ · อื่น → ไฟเทา นิ่ง
//   highlighted → ลอยขึ้นเล็กน้อย + glow ชัดขึ้น
Item {
    id: art
    property bool ps5: true
    property string state: "unknown"
    property bool highlighted: false
    readonly property string lightKey: state === "ready" ? "on" : (state === "standby" ? "standby" : "off")
    readonly property string gen: ps5 ? "ps5" : "ps4"

    implicitWidth: 200
    implicitHeight: 140

    // ลอยขึ้นตอนถูกเลือก
    transform: Translate {
        id: lift
        y: 0
        Behavior on y { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
    }
    onHighlightedChanged: lift.y = highlighted ? -4 : 0
    Component.onCompleted: lift.y = highlighted ? -4 : 0

    // glow หลังตัวเครื่อง (ไฟสถานะขยายฟุ้ง 2 ชั้น ไม่ใช้ shader)
    Image {
        id: glow1
        anchors.centerIn: parent
        width: parent.width * 1.06
        height: parent.height * 1.06
        sourceSize: Qt.size(400, 280)
        source: "qrc:/icons/console/" + art.gen + "-light-" + art.lightKey + ".svg"
        fillMode: Image.PreserveAspectFit
        opacity: 0.25
        smooth: true
    }
    Image {
        id: glow2
        anchors.centerIn: parent
        width: parent.width * 1.12
        height: parent.height * 1.12
        sourceSize: Qt.size(400, 280)
        source: glow1.source
        fillMode: Image.PreserveAspectFit
        opacity: 0.12
        smooth: true
    }

    Image {
        id: body
        anchors.fill: parent
        sourceSize: Qt.size(400, 280)
        source: "qrc:/icons/console/" + art.gen + "-body.svg"
        fillMode: Image.PreserveAspectFit
        smooth: true
    }

    Image {
        id: light
        anchors.fill: parent
        sourceSize: Qt.size(400, 280)
        source: glow1.source
        fillMode: Image.PreserveAspectFit
        smooth: true

        // ready: เต้นเบาๆ · standby: หายใจช้า · off: นิ่ง
        SequentialAnimation on opacity {
            id: pulse
            running: art.state === "ready" || art.state === "standby"
            loops: Animation.Infinite
            NumberAnimation { to: art.state === "standby" ? 0.35 : 0.7; duration: art.state === "standby" ? 1400 : 900; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1.0; duration: art.state === "standby" ? 1400 : 900; easing.type: Easing.InOutSine }
        }
        onSourceChanged: if (!pulse.running) opacity = 1.0
    }

    // ป้ายรุ่นเล็กๆ ใต้เครื่อง
    Text {
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: -2 }
        text: art.ps5 ? "PS5" : "PS4"
        font.pixelSize: 11
        font.weight: Font.Bold
        font.letterSpacing: 1.5
        color: Theme.textMuted
    }
}
