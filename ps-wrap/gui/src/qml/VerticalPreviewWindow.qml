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
    function elapsed(s) { const m = Math.floor(s / 60); const ss = s % 60; return m + ":" + (ss < 10 ? "0" : "") + ss; }

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

                // เฟส 2: ลากในภาพ — facecam: ลาก = ย้าย · scroll = ย่อ/ขยาย · ดับเบิลคลิก = ค่าเริ่มต้น
                //                   ส่วนเกม: ลากซ้าย-ขวา = เลื่อนตำแหน่งตัด (Cam + game / Center crop)
                MouseArea {
                    id: dragArea
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton
                    property string mode: ""        // "cam" | "crop" | ""
                    property real startX: 0
                    property real startY: 0
                    property var startCam: null     // {cx, cy, w} ตอนเริ่มลาก
                    property real startCrop: 0.5
                    property bool overCam: false

                    function rects() { return Chiaki.window ? Chiaki.window.verticalHitRects() : null }
                    function inside(r, nx, ny) { return r && nx >= r.x && nx <= r.x + r.w && ny >= r.y && ny <= r.y + r.h }
                    function camNow() {
                        const r = rects();
                        return r && r.cam ? { cx: r.cam.x + r.cam.w / 2, cy: r.cam.y + r.cam.h / 2, w: r.cam.w } : null;
                    }

                    cursorShape: mode === "cam" ? Qt.ClosedHandCursor
                               : mode === "crop" ? Qt.SizeHorCursor
                               : overCam ? Qt.OpenHandCursor
                               : (Chiaki.window && Chiaki.window.verticalLayout !== 2 ? Qt.SizeHorCursor : Qt.ArrowCursor)

                    onPositionChanged: (mouse) => {
                        const nx = mouse.x / width, ny = mouse.y / height;
                        if (mode === "") {
                            const r = rects();
                            overCam = !!r && inside(r.cam, nx, ny);
                            return;
                        }
                        const dx = (mouse.x - startX) / width, dy = (mouse.y - startY) / height;
                        if (mode === "cam" && startCam)
                            Chiaki.window.setVerticalCam(startCam.cx + dx, startCam.cy + dy, startCam.w);
                        else if (mode === "crop")
                            Chiaki.window.verticalCropX = Math.max(0, Math.min(1, startCrop - dx * 1.6));
                    }
                    onPressed: (mouse) => {
                        const nx = mouse.x / width, ny = mouse.y / height;
                        const r = rects();
                        startX = mouse.x;
                        startY = mouse.y;
                        if (r && inside(r.cam, nx, ny)) {
                            mode = "cam";
                            startCam = camNow();
                        } else if (Chiaki.window.verticalLayout !== 2) {
                            mode = "crop";
                            startCrop = Chiaki.window.verticalCropX;
                        } else {
                            mode = "";
                        }
                    }
                    onReleased: mode = ""
                    onCanceled: mode = ""
                    onDoubleClicked: (mouse) => {
                        const r = rects();
                        if (r && inside(r.cam, mouse.x / width, mouse.y / height))
                            Chiaki.window.setVerticalCam(0, 0, 0);
                    }
                    onWheel: (wheel) => {
                        const c = camNow();
                        if (!c)
                            return;
                        const k = wheel.angleDelta.y > 0 ? 1.08 : 1 / 1.08;
                        Chiaki.window.setVerticalCam(c.cx, c.cy, Math.max(0.1, Math.min(1, c.w * k)));
                    }
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

        // เฟส 3: อัดคลิปแนวตั้ง 1080x1920 (pipeline แยก อัดพร้อมคลิปปกติได้)
        Button {
            id: vrecButton
            readonly property QtObject rec: Chiaki.window ? Chiaki.window.verticalRecorder : null
            readonly property bool on: !!rec && rec.recording
            Layout.fillWidth: true
            enabled: !!Chiaki.session && !!rec && !rec.busy
            highlighted: on
            Material.accent: on ? Theme.danger : Theme.accent
            text: on ? qsTr("■  Stop  %1").arg(win.elapsed(rec.seconds)) : qsTr("●  Record 9:16")
            onClicked: Chiaki.window.toggleVerticalRecording()
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
            Button {
                text: qsTr("Reset cam")
                flat: true
                font.pixelSize: Theme.fontCaption
                onClicked: Chiaki.window.setVerticalCam(0, 0, 0)
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
            text: qsTr("Drag the facecam to move it, scroll to resize, double-click to reset · drag the game sideways to move the crop. The facecam comes from the overlay on the game screen.")
        }
    }
}
