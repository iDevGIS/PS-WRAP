import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import "controls" as C

import org.streetpea.chiaking

// PS-WRAP: ตรวจการเชื่อมต่อไปหาเครื่อง แล้วแนะนำค่าสตรีม (Local) — วัดด้วย Chiaki.netCheck (pswrapnetcheck.h)
// เปิดจาก: ปุ่ม "Test" บนการ์ดเครื่องหน้าแรก, Settings › Stream › Check my connection
// เกณฑ์อิงผลทดสอบจริงใน README "Best tested settings" (Wi-Fi 5 ping ~1.5 ms → 1080p60 100 Mbps H.265)
// จอย/คีย์: ←→ สลับปุ่ม · ✕ กด · ◯ ปิด
Dialog {
    id: dlg

    property var host: null            // จาก Chiaki.hosts (name/address/ps5) · null = เลือกเครื่องแรกที่มี address
    property Item returnFocusTo: null
    readonly property QtObject nc: Chiaki.netCheck
    readonly property var r: nc ? nc.result : ({})
    readonly property bool hasResult: !!r && r.sent !== undefined && !nc.running
    readonly property var rec: hasResult && r.ok ? recommend(r, !!host && !!host.ps5) : null
    property bool applied: false

    parent: Overlay.overlay
    x: Math.round((root.width - width) / 2)
    y: Math.round((root.height - height) / 2)
    width: Math.min(root.width - Theme.screenMargin * 2, 820)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: Theme.space8
    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusCard
        border.width: 1
        border.color: Theme.border
    }
    Overlay.modal: Rectangle { color: Theme.overlay }

    function pickHost() {
        if (host && host.address)
            return;
        const hs = Chiaki.hosts;
        for (let i = 0; i < hs.length; ++i)
            if (hs[i].address && (hs[i].registered || hs[i].discovered)) { host = hs[i]; return; }
        for (let j = 0; j < hs.length; ++j)
            if (hs[j].address) { host = hs[j]; return; }
    }
    function start() {
        applied = false;
        pickHost();
        if (host && host.address)
            nc.run(host.address, !!host.ps5);
    }
    onOpened: {
        start();
        Qt.callLater(function() { closeButton.forceActiveFocus(Qt.TabFocusReason); });
    }
    onClosed: {
        wakeTimer.stop();
        waking = false;
        if (nc) nc.cancel();
        if (returnFocusTo) returnFocusTo.forceActiveFocus(Qt.TabFocusReason);
    }

    readonly property var resNames: ({ 1: "360p", 2: "540p", 3: "720p", 4: "1080p" })
    function fmt(ms) { return (typeof ms !== "number" || ms < 0) ? "–" : (ms < 10 ? ms.toFixed(1) : ms.toFixed(0)) + " ms"; }

    // ระดับ 3 (ดีมาก) .. 0 (อ่อน): จากชนิด/ความเร็ว link + สัญญาณ แล้วหักตาม ping/jitter/loss ที่วัดได้
    function recommend(res, ps5) {
        const link = res.linkMbps || 0;
        let tier = 2;
        let why = [];
        if (res.linkType === "ethernet") {
            tier = link >= 900 ? 3 : (link >= 90 ? 2 : 1);
        } else if (res.linkType === "wifi") {
            const sig = res.wifiSignal !== undefined ? res.wifiSignal : 0;
            if (link >= 800 && sig >= 75) tier = 3;
            else if (link >= 400 && sig >= 60) tier = 2;
            else if (link >= 150 && sig >= 40) tier = 1;
            else tier = 0;
            if (res.wifiBand === "2.4 GHz") {
                tier = Math.min(tier, 1);
                why.push(qsTr("You're on 2.4 GHz Wi-Fi. Switching the PC to 5 GHz gives more room for a higher bitrate."));
            } else if (sig < 60) {
                why.push(qsTr("The Wi-Fi signal is weak. Moving closer to the router, or using a cable, helps the most."));
            }
        }
        // ping = ICMP · เครื่องพัก (620 Standby) หรือ ICMP ถูกบล็อก = ไม่เอา ping/jitter มาตัดสิน
        if (res.standby || res.pingBlocked) {
            if (!res.pingBlocked && res.lossPct > 20) tier -= 1;
        } else if (res.lossPct > 5 || res.jitter > 15 || res.rttMedian > 15) {
            tier -= 2;
            why.push(qsTr("The console's replies were slow or got lost, so the bitrate is set lower to avoid stutter."));
        } else if (res.lossPct > 0 || res.jitter > 5 || res.rttMedian > 6) {
            tier -= 1;
            why.push(qsTr("Some replies were late or lost, so the bitrate is set a step lower."));
        }
        tier = Math.max(0, Math.min(3, tier));
        let o;
        if (ps5) {
            const table = [ { res: 3, fps: 60, mbps: 10 }, { res: 4, fps: 60, mbps: 25 }, { res: 4, fps: 60, mbps: 50 }, { res: 4, fps: 60, mbps: 100 } ];
            o = Object.assign({}, table[tier]);
            o.codec = Chiaki.settings.codecLocalPS5 === 2 ? 2 : 1;   // H.265 (คง HDR ถ้าเลือกไว้)
        } else {
            const t4 = [ { res: 2, fps: 30, mbps: 6 }, { res: 3, fps: 60, mbps: 10 }, { res: 3, fps: 60, mbps: 15 }, { res: 3, fps: 60, mbps: 15 } ];
            o = Object.assign({}, t4[tier]);
        }
        // ไม่ให้เกินครึ่งหนึ่งของความเร็ว link (เผื่อ Wi-Fi แกว่ง / อุปกรณ์อื่น)
        if (link > 0 && o.mbps > link * 0.5) {
            o.mbps = Math.max(5, Math.floor(link * 0.5 / 5) * 5);
        }
        o.tier = tier;
        o.why = why;
        o.frameGen = (res.screenHz || 60) >= 100 && ps5;
        return o;
    }
    function currentRes() { return !!host && host.ps5 ? Chiaki.settings.resolutionLocalPS5 : Chiaki.settings.resolutionLocalPS4; }
    function currentFps() { return !!host && host.ps5 ? Chiaki.settings.fpsLocalPS5 : Chiaki.settings.fpsLocalPS4; }
    function currentMbps() {
        const kbps = !!host && host.ps5 ? Chiaki.settings.bitrateLocalPS5 : Chiaki.settings.bitrateLocalPS4;
        if (kbps > 0) return Math.round(kbps / 1000);
        return ({ 1: 2, 2: 6, 3: 10, 4: 15 })[currentRes()] || 0;   // 0 = ค่าเริ่มต้นตาม resolution (เหมือน Settings)
    }
    // ปลุกเครื่องแล้ววัดใหม่หลังเครื่องตื่น (~20 วิ)
    property bool waking: false
    function wakeAndRetest() {
        const hs = Chiaki.hosts;
        for (let i = 0; i < hs.length; ++i) {
            if (hs[i].address === host.address && hs[i].name === host.name) {
                Chiaki.wakeUpHost(i);
                waking = true;
                wakeTimer.restart();
                return;
            }
        }
    }
    Timer { id: wakeTimer; interval: 20000; onTriggered: { dlg.waking = false; dlg.start(); } }
    function apply() {
        if (!rec) return;
        if (host.ps5) {
            Chiaki.settings.resolutionLocalPS5 = rec.res;
            Chiaki.settings.fpsLocalPS5 = rec.fps;
            Chiaki.settings.bitrateLocalPS5 = rec.mbps * 1000;
            Chiaki.settings.codecLocalPS5 = rec.codec;
        } else {
            Chiaki.settings.resolutionLocalPS4 = rec.res;
            Chiaki.settings.fpsLocalPS4 = rec.fps;
            Chiaki.settings.bitrateLocalPS4 = rec.mbps * 1000;
        }
        applied = true;
    }

    component Fact: ColumnLayout {
        property string label
        property string value
        property color valueColor: Theme.text
        spacing: 2
        Layout.fillWidth: true
        Label { text: parent.label; font.pixelSize: Theme.fontCaption; color: Theme.textMuted; font.letterSpacing: 1 }
        Label { text: parent.value; font.pixelSize: Theme.fontLabel; font.weight: Font.DemiBold; color: parent.valueColor; wrapMode: Text.WordWrap; Layout.fillWidth: true }
    }
    component RecRow: RowLayout {
        property string label
        property string now
        property string next
        Layout.fillWidth: true
        spacing: Theme.space3
        Label { text: parent.label; Layout.preferredWidth: 140; color: Theme.textMuted; font.pixelSize: Theme.fontLabel }
        Label { text: parent.now; Layout.preferredWidth: 150; color: Theme.textMuted; font.pixelSize: Theme.fontLabel }
        Label { text: "→"; color: Theme.textMuted }
        Label {
            text: parent.next
            font.pixelSize: Theme.fontLabel
            font.weight: Font.DemiBold
            color: parent.now === parent.next ? Theme.text : Theme.accent
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.space4

        Label {
            Layout.fillWidth: true
            text: qsTr("Connection check")
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Bold
            color: Theme.text
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            text: !dlg.host || !dlg.host.address ? qsTr("No console with a known address yet. Turn on the console or add it, then try again.")
                : qsTr("From this PC to %1").arg(dlg.host.name || (Chiaki.settings.streamerMode ? qsTr("your console") : dlg.host.address))
        }

        // ระหว่างวัด
        ColumnLayout {
            Layout.fillWidth: true
            visible: !!dlg.nc && dlg.nc.running
            spacing: Theme.space2
            Label { text: qsTr("Measuring… (%1 of %2)").arg(dlg.nc ? dlg.nc.done : 0).arg(dlg.nc ? dlg.nc.total : 0); color: Theme.text }
            ProgressBar { Layout.fillWidth: true; from: 0; to: dlg.nc ? dlg.nc.total : 1; value: dlg.nc ? dlg.nc.done : 0 }
        }

        Label {
            Layout.fillWidth: true
            visible: dlg.hasResult && !dlg.r.ok
            wrapMode: Text.WordWrap
            color: Theme.danger
            text: dlg.r && dlg.r.error ? dlg.r.error : ""
        }

        // ผลวัด
        GridLayout {
            Layout.fillWidth: true
            visible: dlg.hasResult && dlg.r.ok
            columns: 3
            columnSpacing: Theme.space6
            rowSpacing: Theme.space3
            Fact {
                label: qsTr("CONNECTION")
                value: {
                    const x = dlg.r;
                    if (!x || !x.linkType) return "–";
                    if (x.linkType === "ethernet") return qsTr("Cable · %1 Mbps").arg(Math.round(x.linkMbps || 0));
                    if (x.linkType === "wifi")
                        return (x.wifiPhy || "Wi-Fi") + (x.wifiBand ? " · " + x.wifiBand : "") + " · " + qsTr("%1 Mbps").arg(Math.round(x.linkMbps || 0));
                    return qsTr("Unknown");
                }
            }
            Fact {
                label: qsTr("WI-FI SIGNAL")
                visible: dlg.r && dlg.r.linkType === "wifi"
                value: dlg.r && dlg.r.wifiSignal !== undefined ? dlg.r.wifiSignal + "%" : "–"
                valueColor: !dlg.r || dlg.r.wifiSignal === undefined ? Theme.text : dlg.r.wifiSignal >= 70 ? Theme.success : dlg.r.wifiSignal >= 45 ? Theme.warning : Theme.danger
            }
            Fact {
                label: qsTr("PING TO CONSOLE")
                value: !dlg.r ? "–" : dlg.r.pingBlocked ? qsTr("Ping blocked") : dlg.fmt(dlg.r.rttMedian) + "  ·  " + qsTr("jitter %1").arg(dlg.fmt(dlg.r.jitter))
                valueColor: !dlg.r || dlg.r.pingBlocked || dlg.r.standby ? Theme.textMuted : dlg.r.rttMedian <= 6 && dlg.r.jitter <= 5 ? Theme.success : dlg.r.rttMedian <= 15 ? Theme.warning : Theme.danger
            }
            Fact {
                label: qsTr("LOST")
                value: !dlg.r || dlg.r.pingBlocked ? "–" : qsTr("%1 of %2").arg(dlg.r.sent - dlg.r.received).arg(dlg.r.sent)
                valueColor: !dlg.r || dlg.r.pingBlocked ? Theme.text : dlg.r.lossPct === 0 ? Theme.success : dlg.r.lossPct <= 5 ? Theme.warning : Theme.danger
            }
            Fact {
                label: qsTr("DISPLAY")
                value: dlg.r && dlg.r.screenHz ? qsTr("%1 Hz · %2p").arg(dlg.r.screenHz).arg(dlg.r.screenHeight) : "–"
            }
            Fact {
                label: qsTr("RATING")
                value: !dlg.rec ? "–" : [qsTr("Weak"), qsTr("Fair"), qsTr("Good"), qsTr("Excellent")][dlg.rec.tier]
                valueColor: !dlg.rec ? Theme.text : [Theme.danger, Theme.warning, Theme.success, Theme.success][dlg.rec.tier]
            }
        }

        // ค่าแนะนำ เทียบกับปัจจุบัน
        Rectangle {
            Layout.fillWidth: true
            visible: !!dlg.rec
            implicitHeight: recCol.implicitHeight + Theme.space4 * 2
            radius: Theme.radiusCard
            color: Qt.rgba(1, 1, 1, 0.03)
            border.width: 1
            border.color: Theme.border
            ColumnLayout {
                id: recCol
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.space4 }
                spacing: Theme.space2
                Label {
                    text: qsTr("RECOMMENDED FOR PLAYING AT HOME")
                    font.pixelSize: Theme.fontCaption
                    font.letterSpacing: 1.5
                    font.weight: Font.DemiBold
                    color: Theme.textMuted
                }
                RecRow { label: qsTr("Resolution"); now: dlg.resNames[dlg.currentRes()] || "–"; next: dlg.rec ? dlg.resNames[dlg.rec.res] : "" }
                RecRow { label: qsTr("Frame rate"); now: dlg.currentFps() + " fps"; next: dlg.rec ? dlg.rec.fps + " fps" : "" }
                RecRow { label: qsTr("Bitrate"); now: dlg.currentMbps() + " Mbps"; next: dlg.rec ? dlg.rec.mbps + " Mbps" : "" }
                RecRow {
                    visible: !!dlg.host && !!dlg.host.ps5
                    label: qsTr("Codec")
                    now: ["H.264", "H.265", "H.265 HDR"][Chiaki.settings.codecLocalPS5] || "–"
                    next: dlg.rec && dlg.rec.codec !== undefined ? ["H.264", "H.265", "H.265 HDR"][dlg.rec.codec] : ""
                }
                Repeater {
                    model: dlg.rec ? dlg.rec.why : []
                    Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.warning; text: "• " + modelData }
                }
                Label {
                    Layout.fillWidth: true
                    visible: !!dlg.rec && dlg.rec.frameGen
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    text: qsTr("Your display runs at %1 Hz: try Frame Gen in the stream menu (QUALITY) for 120 fps.").arg(dlg.r ? dlg.r.screenHz : 0)
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontCaption
                    text: qsTr("Playing away from home uses your home upload speed instead: about 20–30 Mbps in Settings › Stream › Remote.")
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: dlg.hasResult && !!dlg.r.standby
            wrapMode: Text.WordWrap
            color: Theme.warning
            text: dlg.waking ? qsTr("Waking the console… testing again in a moment.")
                : qsTr("The console is in rest mode, so its ping isn't counted. Wake it and test again for an exact result.")
        }

        Label {
            Layout.fillWidth: true
            visible: dlg.applied
            color: Theme.success
            text: qsTr("Applied. The new settings are used the next time you connect.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space3
            C.Button {
                id: applyButton
                highlighted: true
                visible: !!dlg.rec
                enabled: !dlg.applied
                text: dlg.applied ? qsTr("Applied") : qsTr("Apply")
                onClicked: dlg.apply()
                KeyNavigation.right: againButton
            }
            C.Button {
                id: againButton
                flat: true
                enabled: !!dlg.nc && !dlg.nc.running && !!dlg.host && !dlg.waking
                text: dlg.hasResult && dlg.r.standby ? qsTr("Wake console and test again") : qsTr("Test again")
                onClicked: dlg.hasResult && dlg.r.standby ? dlg.wakeAndRetest() : dlg.start()
                KeyNavigation.left: applyButton
                KeyNavigation.right: closeButton
            }
            Item { Layout.fillWidth: true }
            C.Button {
                id: closeButton
                flat: true
                text: qsTr("Close")
                onClicked: dlg.close()
                KeyNavigation.left: againButton
            }
        }
    }

    // ได้ผลแล้วโฟกัส Apply (จอยกด ✕ ได้เลย)
    Connections {
        target: dlg.nc
        function onFinished() { if (dlg.opened && dlg.rec) applyButton.forceActiveFocus(Qt.TabFocusReason); }
    }
}
