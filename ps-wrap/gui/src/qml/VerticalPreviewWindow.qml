import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs

import org.streetpea.chiaking

// PS-WRAP: หน้าต่าง preview ภาพแนวตั้ง 9:16 — ภาพตัวจริงที่คนดู Shorts/TikTok/Reels จะเห็น (render จากเฟรมสตรีมตรงๆ)
// เปิด/ปิดด้วย Chiaki.window.verticalPreview (ปุ่ม Vertical ในเมนูสตรีม / tray) · ภาพมาจาก image://pswrapvertical/<เลขเฟรม>
Window {
    id: win

    property bool placed: false
    readonly property QtObject layers: Chiaki.window ? Chiaki.window.verticalLayers : null
    property string notice: ""
    function showNotice(t) { notice = t; noticeTimer.restart(); }
    Timer { id: noticeTimer; interval: 5000; onTriggered: win.notice = "" }
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

                // ลากในภาพ — รูป/GIF, การ์ดแชท, facecam: ลาก = ย้าย · scroll = ย่อ/ขยาย · ดับเบิลคลิก = ค่าเริ่มต้น (แชท/facecam)
                //              คลิกขวาที่รูป = เมนู (หน้าสุด/หลังสุด/ความโปร่ง/ลบ)
                //              ส่วนเกม: ลากซ้าย-ขวา = เลื่อนตำแหน่งตัด (Cam + game / Center crop)
                MouseArea {
                    id: dragArea
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    property string mode: ""        // "cam" | "chat" | "layer" | "crop" | ""
                    property real startX: 0
                    property real startY: 0
                    property var start: null        // {cx, cy, w} ของสิ่งที่ลาก ตอนเริ่ม
                    property real startCrop: 0.5
                    property var hover: null        // {kind, id, x, y, w, h} (สัดส่วน 0..1) ใต้เมาส์

                    function rects() { return Chiaki.window ? Chiaki.window.verticalHitRects() : null }
                    function inside(r, nx, ny) { return r && nx >= r.x && nx <= r.x + r.w && ny >= r.y && ny <= r.y + r.h }
                    function layerRect(l) {
                        const h = l.w * (9 / 16) / l.aspect;
                        return { x: l.cx - l.w / 2, y: l.cy - h / 2, w: l.w, h: h };
                    }
                    // บนสุดก่อน: facecam → แชท → รูป (บน → ล่าง)
                    function pick(nx, ny) {
                        const r = rects();
                        if (r && inside(r.cam, nx, ny))
                            return { kind: "cam", id: 0, x: r.cam.x, y: r.cam.y, w: r.cam.w, h: r.cam.h };
                        if (r && inside(r.chat, nx, ny))
                            return { kind: "chat", id: 0, x: r.chat.x, y: r.chat.y, w: r.chat.w, h: r.chat.h };
                        const ls = win.layers ? win.layers.layers : [];
                        for (let i = ls.length - 1; i >= 0; i--) {
                            const lr = layerRect(ls[i]);
                            if (inside(lr, nx, ny))
                                return { kind: "layer", id: ls[i].id, x: lr.x, y: lr.y, w: lr.w, h: lr.h, opacity: ls[i].opacity, name: ls[i].name };
                        }
                        return null;
                    }
                    function centerOf(h) { return h ? { cx: h.x + h.w / 2, cy: h.y + h.h / 2, w: h.w } : null }
                    function apply(kind, id, cx, cy, w) {
                        if (kind === "cam") Chiaki.window.setVerticalCam(cx, cy, w);
                        else if (kind === "chat") Chiaki.window.setVerticalChatPos(cx, cy, w);
                        else if (kind === "layer") { win.layers.move(id, cx, cy); win.layers.resize(id, w); }
                    }

                    cursorShape: mode === "crop" ? Qt.SizeHorCursor
                               : mode !== "" ? Qt.ClosedHandCursor
                               : hover ? Qt.OpenHandCursor
                               : (Chiaki.window && Chiaki.window.verticalLayout !== 2 ? Qt.SizeHorCursor : Qt.ArrowCursor)

                    onPositionChanged: (mouse) => {
                        const nx = mouse.x / width, ny = mouse.y / height;
                        if (mode === "") {
                            hover = pick(nx, ny);
                            return;
                        }
                        const dx = (mouse.x - startX) / width, dy = (mouse.y - startY) / height;
                        if (mode === "crop") {
                            Chiaki.window.verticalCropX = Math.max(0, Math.min(1, startCrop - dx * 1.6));
                        } else if (start && hover) {
                            apply(mode, hover.id, start.cx + dx, start.cy + dy, start.w);
                            hover = Object.assign({}, hover, { x: start.cx + dx - hover.w / 2, y: start.cy + dy - hover.h / 2 });
                        }
                    }
                    onPressed: (mouse) => {
                        const nx = mouse.x / width, ny = mouse.y / height;
                        hover = pick(nx, ny);
                        startX = mouse.x;
                        startY = mouse.y;
                        if (mouse.button === Qt.RightButton) {
                            mode = "";
                            if (hover && hover.kind === "layer") {
                                layerMenu.target = hover;
                                layerMenu.popup();
                            } else if (hover && hover.kind === "chat") {
                                chatMenu.popup();
                            }
                            return;
                        }
                        if (hover) {
                            mode = hover.kind;
                            start = centerOf(hover);
                        } else if (Chiaki.window.verticalLayout !== 2) {
                            mode = "crop";
                            startCrop = Chiaki.window.verticalCropX;
                        } else {
                            mode = "";
                        }
                    }
                    onReleased: mode = ""
                    onCanceled: mode = ""
                    onExited: if (mode === "") hover = null
                    onDoubleClicked: (mouse) => {
                        const h = pick(mouse.x / width, mouse.y / height);
                        if (h && h.kind === "cam") Chiaki.window.setVerticalCam(0, 0, 0);
                        else if (h && h.kind === "chat") Chiaki.window.setVerticalChatPos(0, 0, 0);
                    }
                    onWheel: (wheel) => {
                        const h = hover || pick(wheel.x / width, wheel.y / height);
                        const c = centerOf(h);
                        if (!c)
                            return;
                        const k = wheel.angleDelta.y > 0 ? 1.08 : 1 / 1.08;
                        const w = Math.max(0.08, Math.min(h.kind === "layer" ? 1.5 : 1, c.w * k));
                        apply(h.kind, h.id, c.cx, c.cy, w);
                        hover = pick(wheel.x / width, wheel.y / height);
                    }
                }

                // กรอบไฮไลต์สิ่งที่อยู่ใต้เมาส์ / กำลังลาก
                Rectangle {
                    readonly property var h: dragArea.hover
                    visible: !!h
                    x: h ? h.x * frameBox.width : 0
                    y: h ? h.y * frameBox.height : 0
                    width: h ? h.w * frameBox.width : 0
                    height: h ? h.h * frameBox.height : 0
                    color: "transparent"
                    radius: 4
                    border.width: 2
                    border.color: Theme.accent
                    opacity: dragArea.mode !== "" ? 1.0 : 0.7
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

        // รูป/GIF + การ์ดแชท ในภาพแนวตั้ง
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space2
            Button {
                id: addButton
                Layout.fillWidth: true
                text: win.layers && win.layers.downloading ? qsTr("Downloading…") : qsTr("＋ Image")
                enabled: !!win.layers && !win.layers.downloading && win.layers.layers.length < win.layers.maxLayers
                font.pixelSize: Theme.fontCaption
                onClicked: addMenu.popup(addButton, 0, addButton.height)
                Keys.onReturnPressed: clicked()
                Menu {
                    id: addMenu
                    MenuItem { text: qsTr("From file…"); onTriggered: imageDialog.open() }
                    MenuItem { text: qsTr("From a link (Giphy, …)"); onTriggered: linkPopup.open() }
                }
            }
            Button {
                Layout.fillWidth: true
                text: qsTr("Chat")
                checkable: true
                checked: !!Chiaki.window && Chiaki.window.verticalChat
                highlighted: checked
                font.pixelSize: Theme.fontCaption
                onClicked: Chiaki.window.verticalChat = !Chiaki.window.verticalChat
                Keys.onReturnPressed: clicked()
            }
        }
        Label {
            Layout.fillWidth: true
            visible: win.notice.length > 0
            text: win.notice
            wrapMode: Text.WordWrap
            color: Theme.warning
            font.pixelSize: Theme.fontCaption
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
            text: qsTr("Drag images, chat and the facecam to move them, scroll to resize, right-click an image for more · drag the game sideways to move the crop. Images are saved per layout.")
        }
    }

    FileDialog {
        id: imageDialog
        title: qsTr("Add an image or GIF")
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.gif *.svg)")]
        onAccepted: {
            const err = win.layers ? win.layers.addImage(selectedFile) : "";
            if (err.length > 0)
                win.showNotice(err);
        }
    }

    // วางลิงก์รูป/GIF (เช่น Giphy) → แอปดาวน์โหลดแล้วใส่ให้
    Popup {
        id: linkPopup
        parent: Overlay.overlay
        x: Math.round((win.width - width) / 2)
        y: Math.round(win.height * 0.3)
        width: win.width - Theme.space6 * 2
        modal: true
        focus: true
        padding: Theme.space4
        onOpened: { linkField.text = ""; linkField.forceActiveFocus(); }
        background: Rectangle { color: Theme.surfaceRaised; radius: Theme.radiusCard; border.width: 1; border.color: Theme.border }
        function submit() {
            if (linkField.text.trim().length === 0) return;
            win.layers.addImageUrl(linkField.text);
            close();
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: Theme.space3
            Label { text: qsTr("Paste a link to an image or GIF"); color: Theme.text; font.pixelSize: Theme.fontCaption }
            TextField {
                id: linkField
                Layout.fillWidth: true
                placeholderText: "https://media.giphy.com/…/giphy.gif"
                selectByMouse: true
                onAccepted: linkPopup.submit()
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { text: qsTr("Cancel"); flat: true; onClicked: linkPopup.close() }
                Button { text: qsTr("Add"); highlighted: true; onClicked: linkPopup.submit() }
            }
        }
    }
    Connections {
        target: win.layers
        function onAddFinished(error) { if (error.length > 0) win.showNotice(error); }
    }

    Menu {
        id: layerMenu
        property var target: null
        MenuItem { text: qsTr("Bring to front"); onTriggered: win.layers.raise(layerMenu.target.id) }
        MenuItem { text: qsTr("Send to back"); onTriggered: win.layers.lower(layerMenu.target.id) }
        MenuSeparator {}
        Repeater {
            model: [1.0, 0.75, 0.5, 0.25]
            delegate: MenuItem {
                required property real modelData
                text: qsTr("Opacity %1%").arg(Math.round(modelData * 100))
                checkable: true
                checked: !!layerMenu.target && Math.abs(layerMenu.target.opacity - modelData) < 0.01
                onTriggered: win.layers.setOpacity(layerMenu.target.id, modelData)
            }
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Remove"); onTriggered: { win.layers.remove(layerMenu.target.id); dragArea.hover = null; } }
    }

    Menu {
        id: chatMenu
        MenuItem { text: qsTr("Reset position"); onTriggered: Chiaki.window.setVerticalChatPos(0, 0, 0) }
        MenuItem { text: qsTr("Hide chat in 9:16"); onTriggered: { Chiaki.window.verticalChat = false; dragArea.hover = null; } }
    }
}
