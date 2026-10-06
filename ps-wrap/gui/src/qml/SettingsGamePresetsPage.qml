import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

// PS-WRAP: หน้า Settings › Game presets — preset รายเกม (C++: PsWrapGameProfiles = Chiaki.window.gameProfiles)
// - เกมที่ PS5 กำลังรัน (จาก discovery) → สร้าง preset จาก setting ปัจจุบัน / แบบว่าง / แก้ของเดิม
// - รายการ preset ที่บันทึกไว้ → แก้ / ลบ
// - ตัวแก้: ทุกช่องมี "Keep current" = ไม่ตั้ง (ใช้ค่าปกติ) · ช่องที่ window ยังไม่มี (clock/replay) ซ่อนเอง
// - นำทางด้วยจอย/คีย์: ↑↓ ไล่ตาม focus chain ของ controls/ (ไม่มีกริดซับซ้อน)
Item {
    id: page

    property real uiScale: 1.0
    property int labelWidth: 270
    property int controlWidth: 400
    property var confirm: null                   // (title, text, callback) — root.showConfirmDialog ของ Main
    readonly property alias flick: presetsFlick
    readonly property Item firstItem: enableCheck

    readonly property QtObject gp: Chiaki.window.gameProfiles

    // ตัวแก้ preset (ว่าง = ไม่ได้แก้อยู่)
    property var draft: null
    readonly property bool editing: draft !== null

    function openEditor(p) {
        draft = Object.assign({}, p);
        nameField.text = draft.name || "";
        Qt.callLater(() => nameField.forceActiveFocus(Qt.TabFocusReason));
    }
    function closeEditor() {
        draft = null;
        presetsFlick.contentY = 0;
        Qt.callLater(() => enableCheck.forceActiveFocus(Qt.TabFocusReason));
    }
    function setField(key, value) {
        let d = Object.assign({}, draft);
        if (value === undefined || value === null || value === -1)
            delete d[key];
        else
            d[key] = value;
        draft = d;
    }
    function fieldValue(key) {
        return draft && draft[key] !== undefined && draft[key] !== null ? draft[key] : -1;
    }

    readonly property var resolutionNames: ["", qsTr("360p"), qsTr("540p"), qsTr("720p"), qsTr("1080p")]
    readonly property var fxNames: [qsTr("None"), qsTr("Sunglasses"), qsTr("Mustache"), qsTr("Clown nose"), qsTr("Crown"), qsTr("Bane mask"), qsTr("Party (glasses + mustache + crown)"), qsTr("Samurai mask"), qsTr("Ninja"), qsTr("Ghost (Tsushima)"), qsTr("Samurai armor (kabuto + mask)"), qsTr("Samurai armor (photo)"), qsTr("Jin mask (private)"), qsTr("Jin mask + headband (private)")]
    readonly property var bgNames: [qsTr("Keep"), qsTr("Remove green screen"), qsTr("Remove blue screen"), qsTr("AI remove (no green screen)")]

    function bitrateText(kbps) {
        return kbps === 0 ? qsTr("Auto (by resolution)") : qsTr("%1 Mbps").arg(Math.round(kbps / 100) / 10);
    }

    // สรุปช่องที่ preset ตั้งไว้ (แสดงในรายการ)
    function summary(p) {
        let parts = [];
        if (p.resolution !== undefined) parts.push(resolutionNames[p.resolution] || "");
        if (p.fps !== undefined) parts.push(qsTr("%1 fps").arg(p.fps));
        if (p.bitrate !== undefined) parts.push(bitrateText(p.bitrate));
        const flags = [["padOverlay", qsTr("Controller")], ["camOverlay", qsTr("Facecam")], ["statsOverlay", qsTr("Stats")],
                       ["micOverlay", qsTr("Mic")], ["clockOverlay", qsTr("Clock")], ["replayEnabled", qsTr("Replay")]];
        for (const f of flags)
            if (p[f[0]] !== undefined) parts.push((p[f[0]] ? "+" : "−") + f[1]);
        if (p.camFx !== undefined) parts.push(qsTr("Effect: %1").arg(fxNames[p.camFx] || p.camFx));
        if (p.camBackground !== undefined) parts.push(qsTr("Background: %1").arg(bgNames[p.camBackground] || p.camBackground));
        return parts.length ? parts.join(" · ") : qsTr("No overrides (uses current settings)");
    }

    // การ์ดพื้นหลังแบบเดียวกับหน้าอื่นของ SettingsDialog
    component Card: Rectangle {
        id: card
        default property alias content: cardCol.data
        property string title: ""
        Layout.fillWidth: true
        implicitHeight: cardCol.implicitHeight + Theme.space4 * 2
        radius: Theme.radiusCard
        color: Theme.surface
        border.width: 1
        border.color: Theme.border
        ColumnLayout {
            id: cardCol
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.space4 }
            spacing: Theme.space3
            RowLayout {
                Layout.fillWidth: true
                visible: card.title.length > 0
                spacing: Theme.space2
                Rectangle { Layout.preferredWidth: 4; Layout.preferredHeight: Math.round(Theme.fontLabel * page.uiScale); radius: 2; color: Theme.accent }
                Label {
                    Layout.fillWidth: true
                    text: card.title.toUpperCase()
                    font.pixelSize: Theme.fontCaption
                    font.letterSpacing: 1.2
                    font.weight: Font.DemiBold
                    color: Theme.textMuted
                    elide: Text.ElideRight
                }
            }
        }
    }

    // ช่องเลือกค่าแบบ "Keep current" + ตัวเลือก · options = [{ text, value }] (value -1 = ไม่ตั้ง)
    component FieldRow: RowLayout {
        id: row
        property string label: ""
        property string key: ""
        property var options: []
        readonly property alias combo: fieldCombo
        Layout.fillWidth: true
        spacing: Theme.space4
        Label {
            Layout.preferredWidth: page.labelWidth
            Layout.maximumWidth: page.labelWidth
            wrapMode: Text.WordWrap
            color: Theme.text
            text: row.label
        }
        C.ComboBox {
            id: fieldCombo
            Layout.preferredWidth: page.controlWidth
            model: [{ text: qsTr("Keep current"), value: -1 }].concat(row.options)
            textRole: "text"
            valueRole: "value"
            onActivated: (index) => page.setField(row.key, model[index].value)
        }
        // ตั้ง index แบบ imperative (ผู้ใช้เลือกแล้ว binding ของ currentIndex จะหลุด) — callLater ให้ model ใหม่ (bitrate) มาก่อน
        function sync() {
            fieldCombo.currentIndex = page.editing ? Math.max(0, fieldCombo.indexOfValue(page.fieldValue(row.key))) : 0;
        }
        Connections {
            target: page
            function onDraftChanged() { Qt.callLater(row.sync) }
        }
        Component.onCompleted: sync()
    }

    readonly property var boolOptions: [{ text: qsTr("On"), value: true }, { text: qsTr("Off"), value: false }]

    Flickable {
        id: presetsFlick
        anchors {
            fill: parent
            topMargin: 32
            bottomMargin: 20
        }
        clip: true
        contentWidth: width
        contentHeight: content.height + Theme.space12
        flickableDirection: Flickable.AutoFlickIfNeeded
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AlwaysOn
            visible: presetsFlick.contentHeight > presetsFlick.height
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

            // ---------- สวิตช์รวม + สถานะ ----------
            Card {
                C.CheckBox {
                    id: enableCheck
                    text: qsTr("Apply game presets automatically when a stream starts")
                    firstInFocusChain: true
                    Layout.maximumWidth: content.width - Theme.space8
                    checked: page.gp ? page.gp.enabled : false
                    onClicked: if (page.gp) page.gp.enabled = checked   // C.CheckBox: Enter/✕ ส่ง clicked ไม่ส่ง toggled
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textMuted
                    text: qsTr("PS-WRAP reads the game your PS5 is running when you connect and applies its preset for that stream only. Fields left on \"Keep current\" use your normal settings. Resolution, FPS and bitrate apply to local PS5 streams.")
                }
                Label {
                    Layout.fillWidth: true
                    visible: page.gp && page.gp.activeProfileName.length > 0
                    font.pixelSize: Theme.fontLabel
                    font.weight: Font.DemiBold
                    color: Theme.success
                    text: page.gp ? qsTr("Preset in use: %1").arg(page.gp.activeProfileName) : ""
                }
            }

            // ---------- ตัวแก้ preset ----------
            Card {
                visible: page.editing
                title: qsTr("Edit preset")
                Label {
                    Layout.fillWidth: true
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textMuted
                    text: page.editing ? qsTr("Title ID: %1").arg(page.draft.titleId) : ""
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.space4
                    Label {
                        Layout.preferredWidth: page.labelWidth
                        Layout.maximumWidth: page.labelWidth
                        color: Theme.text
                        text: qsTr("Name")
                    }
                    C.TextField {
                        id: nameField
                        Layout.preferredWidth: page.controlWidth
                        text: page.editing ? (page.draft.name || "") : ""
                        onEditingFinished: page.setField("name", text)
                    }
                }
                FieldRow {
                    label: qsTr("Resolution (local PS5)")
                    key: "resolution"
                    options: [{ text: qsTr("360p"), value: 1 }, { text: qsTr("540p"), value: 2 }, { text: qsTr("720p"), value: 3 }, { text: qsTr("1080p"), value: 4 }]
                }
                FieldRow {
                    label: qsTr("FPS (local PS5)")
                    key: "fps"
                    options: [{ text: qsTr("30 fps"), value: 30 }, { text: qsTr("60 fps"), value: 60 }]
                }
                FieldRow {
                    label: qsTr("Bitrate (local PS5)")
                    key: "bitrate"
                    options: {
                        let list = [0, 5000, 10000, 15000, 20000, 25000, 30000, 40000, 50000, 70000, 100000];
                        const cur = page.fieldValue("bitrate");
                        if (cur > 0 && list.indexOf(cur) < 0) {
                            list.push(cur);
                            list.sort((a, b) => a - b);
                        }
                        return list.map(v => ({ text: page.bitrateText(v), value: v }));
                    }
                }
                FieldRow { label: qsTr("Controller overlay"); key: "padOverlay"; options: page.boolOptions }
                FieldRow { label: qsTr("Facecam overlay"); key: "camOverlay"; options: page.boolOptions }
                FieldRow { label: qsTr("Stats overlay"); key: "statsOverlay"; options: page.boolOptions }
                FieldRow { label: qsTr("Mic spectrum overlay"); key: "micOverlay"; options: page.boolOptions }
                FieldRow {
                    label: qsTr("Clock overlay")
                    key: "clockOverlay"
                    options: page.boolOptions
                    visible: page.gp ? page.gp.windowSupports("clockOverlay") : false
                }
                FieldRow {
                    label: qsTr("Facecam effect")
                    key: "camFx"
                    options: page.fxNames.map((t, i) => ({ text: t, value: i }))
                }
                FieldRow {
                    label: qsTr("Facecam background")
                    key: "camBackground"
                    options: page.bgNames.map((t, i) => ({ text: t, value: i }))
                }
                FieldRow {
                    label: qsTr("Instant Replay")
                    key: "replayEnabled"
                    options: page.boolOptions
                    visible: page.gp ? page.gp.windowSupports("replayEnabled") : false
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.space4
                    C.Button {
                        text: qsTr("Save preset")
                        highlighted: true
                        onClicked: {
                            let d = Object.assign({}, page.draft);
                            d.name = nameField.text;
                            if (page.gp && page.gp.saveProfile(d))
                                page.closeEditor();
                        }
                    }
                    C.Button {
                        text: qsTr("Cancel")
                        lastInFocusChain: true
                        onClicked: page.closeEditor()
                    }
                }
            }

            // ---------- เกมที่ PS5 รันอยู่ ----------
            Card {
                visible: !page.editing
                title: qsTr("Running now")
                Label {
                    Layout.fillWidth: true
                    visible: !page.gp || page.gp.runningGames.length === 0
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    text: qsTr("No game detected. Start a game on your PS5 and it appears here once discovery sees it.")
                }
                Repeater {
                    model: page.gp ? page.gp.runningGames : []
                    delegate: ColumnLayout {
                        id: runRow
                        required property var modelData
                        // ผูกกับ profiles (property มี NOTIFY) → คำนวณใหม่เมื่อรายการ preset เปลี่ยน
                        readonly property bool has: page.gp ? page.gp.profiles.some(p => p.titleId === modelData.titleId) : false
                        Layout.fillWidth: true
                        spacing: Theme.space2
                        Label {
                            Layout.fillWidth: true
                            text: modelData.name
                            font.pixelSize: Theme.fontBody
                            font.weight: Font.DemiBold
                            color: Theme.text
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("%1 · on %2").arg(modelData.titleId).arg(modelData.console)
                            font.pixelSize: Theme.fontCaption
                            color: Theme.textMuted
                            elide: Text.ElideRight
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.space4
                            C.Button {
                                visible: runRow.has
                                text: qsTr("Edit preset")
                                onClicked: page.openEditor(page.gp.profile(modelData.titleId))
                            }
                            C.Button {
                                visible: !runRow.has
                                text: qsTr("Create from current settings")
                                highlighted: true
                                onClicked: page.openEditor(page.gp.snapshotCurrent(modelData.titleId, modelData.name))
                            }
                            C.Button {
                                visible: !runRow.has
                                text: qsTr("Create empty preset")
                                onClicked: page.openEditor({ titleId: modelData.titleId, name: modelData.name })
                            }
                        }
                    }
                }
            }

            // ---------- preset ที่บันทึกไว้ ----------
            Card {
                visible: !page.editing
                title: qsTr("Saved presets")
                Label {
                    Layout.fillWidth: true
                    visible: !page.gp || page.gp.profiles.length === 0
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    text: qsTr("No presets yet.")
                }
                Repeater {
                    model: page.gp ? page.gp.profiles : []
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.space4
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                Layout.fillWidth: true
                                text: modelData.name + (page.gp && page.gp.activeTitleId === modelData.titleId ? "  " + qsTr("(in use)") : "")
                                font.pixelSize: Theme.fontBody
                                font.weight: Font.DemiBold
                                color: Theme.text
                                elide: Text.ElideRight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.titleId + " · " + page.summary(modelData)
                                font.pixelSize: Theme.fontCaption
                                color: Theme.textMuted
                                wrapMode: Text.WordWrap
                            }
                        }
                        C.Button {
                            text: qsTr("Edit")
                            onClicked: page.openEditor(modelData)
                        }
                        C.Button {
                            text: qsTr("Delete")
                            Material.accent: Material.Red
                            onClicked: {
                                const tid = modelData.titleId;
                                if (page.confirm)
                                    page.confirm(qsTr("Delete preset"), qsTr("Delete the preset for %1?").arg(modelData.name), () => page.gp.removeProfile(tid));
                                else
                                    page.gp.removeProfile(tid);
                            }
                        }
                    }
                }
            }
        }
    }
}
