import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaking

// PS-WRAP: การ์ด network stats แบบ inline (วาดใน QML scene เดียวกับวิดีโอ — ไม่ใช่หน้าต่างแยกของ upstream ที่ลอยทะลุหน้าต่างอื่น)
// ใช้บน backend Vulkan · ดีไซน์เดียวกับ StatsOverlayWidget (ไอคอน · ชื่อจาง · ค่าขวา)
Rectangle {
    id: card
    implicitWidth: grid.implicitWidth + 24
    implicitHeight: grid.implicitHeight + 18
    radius: 10
    color: Qt.rgba(20 / 255, 26 / 255, 34 / 255, 0.88)
    border.width: 1
    border.color: Theme.border

    component StatRow: RowLayout {
        property url icon
        property string label
        property string value
        property color valueColor: Theme.text
        spacing: 8
        Image {
            Layout.preferredWidth: 15
            Layout.preferredHeight: 15
            sourceSize: Qt.size(15, 15)
            source: icon
        }
        Label {
            Layout.preferredWidth: 110
            text: label
            font.pixelSize: 12
            color: Theme.textMuted
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignRight
            text: value
            font.pixelSize: 15
            font.bold: true
            color: valueColor
        }
    }

    ColumnLayout {
        id: grid
        anchors.centerIn: parent
        spacing: 3

        StatRow {
            icon: "qrc:/icons/stats/bitrate.svg"
            label: qsTr("Mbps")
            value: Chiaki.session ? Chiaki.session.measuredBitrate.toFixed(1) : "0.0"
            valueColor: Theme.accent
        }
        // PS-WRAP: fps — สตรีมส่งมา / ขึ้นจอจริง (Frame Gen ทำให้ค่าบนจอสูงกว่าสตรีม)
        StatRow {
            icon: "qrc:/icons/stats/fps.svg"
            label: qsTr("fps (stream)")
            value: String(Chiaki.window.streamFps)
            valueColor: Theme.accent
        }
        StatRow {
            icon: "qrc:/icons/stats/fps.svg"
            label: Chiaki.window.frameGenActive ? qsTr("fps on screen · FG") : qsTr("fps on screen")
            value: String(Chiaki.window.displayFps)
            valueColor: Chiaki.window.frameGenActive ? Theme.success : Theme.accent
        }
        StatRow {
            icon: "qrc:/icons/stats/rtt.svg"
            label: qsTr("rtt at connect")
            value: Chiaki.session ? qsTr("%1 ms").arg(Chiaki.session.rttMs.toFixed(1)) : ""
            valueColor: Theme.success
        }
        StatRow {
            icon: "qrc:/icons/stats/queue.svg"
            label: qsTr("queue depth avg")
            value: Chiaki.window.queueDepthAverage.toFixed(1)
            valueColor: "#90caf9"
        }
        StatRow {
            icon: "qrc:/icons/stats/latency.svg"
            label: qsTr("pending frame age")
            value: qsTr("%1 ms").arg((Chiaki.window.pendingFrameAge * 1000.0).toFixed(0))
            valueColor: "#90caf9"
        }
        StatRow {
            icon: "qrc:/icons/stats/loss.svg"
            label: qsTr("packet loss")
            value: (((Chiaki.session && isFinite(Chiaki.session.averagePacketLoss)) ? Chiaki.session.averagePacketLoss : 0) * 100).toFixed(1) + "%"
            valueColor: "#ef9a9a"
        }
        StatRow {
            icon: "qrc:/icons/stats/dropped.svg"
            label: qsTr("dropped frames")
            value: String(Chiaki.window.droppedFrames)
            valueColor: "#ef9a9a"
        }
        StatRow {
            icon: "qrc:/icons/stats/lost.svg"
            label: qsTr("lost frames")
            value: Chiaki.session ? String(Chiaki.session.framesLost) : "0"
            valueColor: "#ef9a9a"
        }
    }
}
