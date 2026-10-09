import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import "controls" as C

import org.streetpea.chiaking

// PS-WRAP: อัปเดตในแอป (Chiaki.updater — pswrapupdater.h) · เปิดจากชิป "Update x.y.z" หน้าแรก และปุ่ม Check for updates ใน About
// Update now → ดาวน์โหลด + ตรวจ SHA-256 + แตกไฟล์ → Restart and update (ปิดแอป คัดลอกทับ เปิดใหม่)
// ตัวที่ติดตั้งในโฟลเดอร์ที่เขียนไม่ได้ / build สำหรับพัฒนา → ปุ่มหลักเปิดหน้า release แทน
// จอย/คีย์: ←→ สลับปุ่ม · ↑ ไปช่องเช็คอัตโนมัติ · ✕ กด · ◯ ปิด
Dialog {
    id: dlg

    property Item returnFocusTo: null
    readonly property QtObject up: Chiaki.updater
    readonly property string st: up ? up.state : "idle"
    readonly property bool busy: st === "checking" || st === "downloading"

    parent: Overlay.overlay
    x: Math.round((root.width - width) / 2)
    y: Math.round((root.height - height) / 2)
    width: Math.min(root.width - Theme.screenMargin * 2, 760)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: Theme.space8
    onOpened: Qt.callLater(function() { (mainButton.visible ? mainButton : laterButton).forceActiveFocus(Qt.TabFocusReason); })
    onClosed: if (returnFocusTo) returnFocusTo.forceActiveFocus(Qt.TabFocusReason)
    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusCard
        border.width: 1
        border.color: Theme.border
    }
    Overlay.modal: Rectangle { color: Theme.overlay }

    function sizeText(bytes) { return bytes > 0 ? qsTr("%1 MB").arg((bytes / 1048576).toFixed(0)) : ""; }

    contentItem: ColumnLayout {
        spacing: Theme.space4

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space4
            Image {
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                sourceSize: Qt.size(112, 112)
                source: "qrc:/icons/pswrap.svg"
            }
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.Bold
                    color: Theme.text
                    text: {
                        switch (dlg.st) {
                        case "checking": return qsTr("Checking for updates…");
                        case "uptodate": return qsTr("PS-WRAP is up to date");
                        case "downloading": return qsTr("Downloading PS-WRAP %1…").arg(dlg.up.latestVersion);
                        case "ready": return qsTr("PS-WRAP %1 is ready to install").arg(dlg.up.latestVersion);
                        case "error": return qsTr("Couldn't update");
                        default: return dlg.up && dlg.up.updateAvailable ? qsTr("PS-WRAP %1 is available").arg(dlg.up.latestVersion) : qsTr("Updates");
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fontLabel
                    color: dlg.st === "error" ? Theme.danger : Theme.textMuted
                    text: {
                        if (!dlg.up) return "";
                        if (dlg.st === "error") return dlg.up.error;
                        if (dlg.st === "uptodate") return qsTr("You have the latest version (%1).").arg(dlg.up.currentVersion);
                        if (dlg.st === "ready") return qsTr("PS-WRAP will close, update itself and open again. Your settings stay as they are.");
                        if (dlg.up.updateAvailable)
                            return qsTr("You have %1").arg(dlg.up.currentVersion) + (dlg.up.downloadSize > 0 ? "  ·  " + qsTr("Download %1").arg(dlg.sizeText(dlg.up.downloadSize)) : "");
                        return qsTr("You have %1").arg(dlg.up.currentVersion);
                    }
                }
            }
        }

        ProgressBar {
            Layout.fillWidth: true
            visible: dlg.st === "downloading" || dlg.st === "checking"
            indeterminate: dlg.st === "checking"
            from: 0
            to: 1
            value: dlg.up ? dlg.up.progress : 0
        }

        // release notes (markdown) — เฉพาะตอนมีเวอร์ชันใหม่
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(notes.implicitHeight, Math.max(120, root.height - Theme.screenMargin * 2 - 330))
            visible: !!dlg.up && dlg.up.updateAvailable && dlg.up.notes.length > 0
            contentHeight: notes.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: flick.contentHeight > flick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
            Label {
                id: notes
                width: flick.width - 14
                text: dlg.up ? dlg.up.notes : ""
                textFormat: Text.MarkdownText
                wrapMode: Text.WordWrap
                color: Theme.text
                font.pixelSize: Theme.fontLabel
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
        }

        Label {
            Layout.fillWidth: true
            visible: !!dlg.up && dlg.up.updateAvailable && !dlg.up.canInstall
            wrapMode: Text.WordWrap
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
            text: qsTr("This copy of PS-WRAP can't update itself (its folder is read-only or it is a development build). Download opens the release page.")
        }

        Label {
            Layout.fillWidth: true
            visible: dlg.st === "ready" && !!Chiaki.session
            wrapMode: Text.WordWrap
            color: Theme.warning
            text: qsTr("End the stream first, then restart to update.")
        }

        C.CheckBox {
            id: autoBox
            text: qsTr("Check for updates automatically")
            checked: !!dlg.up && dlg.up.autoCheck
            onToggled: dlg.up.autoCheck = checked
            KeyNavigation.down: mainButton.visible ? mainButton : laterButton
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space3

            C.Button {
                id: mainButton
                highlighted: true
                visible: dlg.st !== "checking" && dlg.st !== "downloading"
                enabled: !(dlg.st === "ready" && !!Chiaki.session)
                text: {
                    if (dlg.st === "ready") return qsTr("Restart and update");
                    if (dlg.up && dlg.up.updateAvailable) return dlg.up.canInstall ? qsTr("Update now") : qsTr("Download");
                    return qsTr("Check again");
                }
                onClicked: {
                    if (dlg.st === "ready") dlg.up.install();
                    else if (dlg.up.updateAvailable) dlg.up.download();
                    else dlg.up.check(true);
                }
                KeyNavigation.right: notesButton.visible ? notesButton : laterButton
                KeyNavigation.up: autoBox
            }
            C.Button {
                id: notesButton
                flat: true
                visible: !!dlg.up && dlg.up.updateAvailable
                text: qsTr("Release page")
                onClicked: dlg.up.openPage()
                KeyNavigation.left: mainButton
                KeyNavigation.right: laterButton
                KeyNavigation.up: autoBox
            }
            Item { Layout.fillWidth: true }
            C.Button {
                id: laterButton
                flat: true
                text: dlg.up && dlg.up.updateAvailable && dlg.st !== "ready" ? qsTr("Later") : qsTr("Close")
                onClicked: {
                    if (dlg.up && dlg.up.updateAvailable && dlg.st === "available")
                        dlg.up.dismiss();   // ไม่เตือนเวอร์ชันนี้ที่แถบล่างอีก (เวอร์ชันถัดไปเตือนใหม่)
                    dlg.close();
                }
                KeyNavigation.left: notesButton.visible ? notesButton : mainButton
                KeyNavigation.up: autoBox
            }
        }
    }

}
