import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

// PS-WRAP: หน้าต่าง preview ภาพแนวตั้ง 9:16 — ภาพตัวจริงที่คนดู Shorts/TikTok/Reels จะเห็น (render จากเฟรมสตรีมตรงๆ)
// เปิด/ปิดด้วย Chiaki.window.verticalPreview (ปุ่ม Vertical ในเมนูสตรีม / tray) · ภาพมาจาก image://pswrapvertical/<เลขเฟรม>
Window {
    id: win

    property bool placed: false

    title: qsTr("PS-WRAP · Vertical 9:16")
    flags: Qt.Window
    color: Theme.bg
    Material.theme: Material.Dark
    Material.accent: Theme.accent
    width: 380
    height: 820
    minimumWidth: 260
    minimumHeight: 520

    onVisibleChanged: {
        // ครั้งแรก: วางชิดขวาของหน้าต่างเกม (ถ้าล้นจอ ชิดซ้ายแทน)
        if (!visible || placed || !Chiaki.window)
            return;
        placed = true;
        const gx = Chiaki.window.x, gy = Chiaki.window.y, gw = Chiaki.window.width;
        const scr = screen;
        const right = gx + gw + 12;
        x = scr && right + width > scr.virtualX + scr.width ? Math.max(scr ? scr.virtualX : 0, gx - width - 12) : right;
        y = gy;
    }
    onClosing: Chiaki.window.verticalPreview = false

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space3
        spacing: Theme.space3

        // ภาพ 9:16 — คงสัดส่วนในพื้นที่ที่มี
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                id: frameBox
                readonly property real fitW: Math.min(parent.width, parent.height * 9 / 16)
                width: fitW
                height: fitW * 16 / 9
                anchors.centerIn: parent
                radius: Theme.radiusControl
                color: "black"
                border.width: 1
                border.color: Theme.border
                clip: true

                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    cache: false
                    asynchronous: false
                    smooth: true
                    fillMode: Image.PreserveAspectFit
                    source: Chiaki.window ? "image://pswrapvertical/" + Chiaki.window.verticalFrame : ""
                }

                Label {
                    anchors.centerIn: parent
                    visible: !Chiaki.session
                    text: qsTr("Start a stream to see the vertical view")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontCaption
                }
            }
        }

        // เลย์เอาต์
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            Repeater {
                model: [qsTr("Cam + game"), qsTr("Center crop"), qsTr("Blur fill")]
                delegate: Button {
                    required property int index
                    required property string modelData
                    Layout.fillWidth: true
                    text: modelData
                    checkable: true
                    checked: Chiaki.window.verticalLayout === index
                    highlighted: checked
                    font.pixelSize: Theme.fontCaption
                    onClicked: Chiaki.window.verticalLayout = index
                    Keys.onReturnPressed: clicked()
                }
            }
        }

        // ตำแหน่งตัดแนวนอน (Cam + game / Center crop)
        RowLayout {
            Layout.fillWidth: true
            visible: Chiaki.window.verticalLayout !== 2
            spacing: Theme.space2
            Label {
                text: qsTr("Crop")
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
            }
            Slider {
                Layout.fillWidth: true
                from: 0
                to: 1
                value: Chiaki.window.verticalCropX
                onMoved: Chiaki.window.verticalCropX = value
            }
            Button {
                text: qsTr("Center")
                flat: true
                font.pixelSize: Theme.fontCaption
                onClicked: Chiaki.window.verticalCropX = 0.5
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
            text: qsTr("1080 × 1920 · facecam comes from the overlay on the game screen (turn it on and place it there).")
        }
    }
}
