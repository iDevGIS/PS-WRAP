import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import "controls" as C

import org.streetpea.chiaking

// PS-WRAP: หน้า About — เครดิต upstream (chiaki-ng / Chiaki) + ของที่เรานำมาใช้ + license AGPL
// Dialog แบบเดียวกับ CamPreviewDialog · ใช้ร่วมกัน: ชิป About ที่หน้าแรก (เมาส์) และปุ่ม About ใน Settings › Config (จอย)
// จอย/คีย์: ↑↓ เลื่อนข้อความ · ←→ สลับปุ่ม · ✕ (Return) กดปุ่ม · ◯ (Esc) ปิด
Dialog {
    id: about

    property Item returnFocusTo: null

    parent: Overlay.overlay
    x: Math.round((root.width - width) / 2)   // ตามแบบ CamPreviewDialog (root = ApplicationWindow) — parent.* ทำ polish loop
    y: Math.round((root.height - height) / 2)
    width: Math.min(root.width - Theme.screenMargin * 2, 820)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: Theme.space8
    onOpened: Qt.callLater(function() { closeButton.forceActiveFocus(Qt.TabFocusReason); })
    onClosed: if (returnFocusTo) returnFocusTo.forceActiveFocus(Qt.TabFocusReason)
    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusCard
        border.width: 1
        border.color: Theme.border
    }
    Overlay.modal: Rectangle { color: Theme.overlay }

    readonly property bool hasNotices: !!Chiaki.window && Chiaki.window.appFileExists("THIRD-PARTY-NOTICES.txt")

    function link(url, label) { return "<a href=\"" + url + "\" style=\"color:" + Theme.accent + "; text-decoration:none\">" + (label || url) + "</a>"; }
    function scrollBy(dy) { flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, flick.contentY + dy)); }

    contentItem: ColumnLayout {
        spacing: Theme.space6

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
                Label {
                    text: "PS-WRAP"
                    font.pixelSize: Theme.fontDisplay
                    font.weight: Font.Bold
                    color: Theme.text
                }
                Label {
                    text: qsTr("Version %1 · by BudToZai").arg(Qt.application.version)
                    font.pixelSize: Theme.fontLabel
                    color: Theme.textMuted
                }
            }
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            // สูงไม่เกินหน้าต่าง (หัว + แถวปุ่ม + padding ≈ 300) — เกินแล้วเลื่อนด้วย ↑↓ / ล้อเมาส์
            Layout.preferredHeight: Math.min(body.implicitHeight, Math.max(160, root.height - Theme.screenMargin * 2 - 300))
            Layout.minimumHeight: 120
            contentHeight: body.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: flick.contentHeight > flick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }

            ColumnLayout {
                id: body
                width: flick.width - Theme.space4
                spacing: Theme.space4

                Section {
                    title: qsTr("Built on chiaki-ng")
                    text: qsTr("PS-WRAP is a fork of %1 by Street Pea and contributors (commit a9a2805, 1.10.0 development). The streaming core comes from chiaki-ng; PS-WRAP adds a new interface, overlays, recording and live streaming on top.")
                          .arg(about.link("https://github.com/streetpea/chiaki-ng", "chiaki-ng"))
                          + "<br><br>"
                          + qsTr("chiaki-ng is itself based on %1 by Florian Märkl and contributors.")
                          .arg(about.link("https://git.sr.ht/~thestr4ng3r/chiaki", "Chiaki"))
                }

                Section {
                    title: qsTr("Also uses")
                    text: [
                        qsTr("%1 (LGPL-3.0) · %2 (LGPL-2.1) · %3 (GPL build) · %4 (zlib)")
                            .arg(about.link("https://www.qt.io", "Qt 6"))
                            .arg(about.link("https://code.videolan.org/videolan/libplacebo", "libplacebo"))
                            .arg(about.link("https://ffmpeg.org", "FFmpeg"))
                            .arg(about.link("https://libsdl.org", "SDL")),
                        qsTr("%1 (MIT) with MediaPipe Selfie Segmentation, BlazeFace and Face Landmarker models by Google (Apache-2.0)")
                            .arg(about.link("https://onnxruntime.ai", "ONNX Runtime")),
                        qsTr("%1 (BSD) and SpeexDSP (BSD) for microphone noise reduction and echo cancellation")
                            .arg(about.link("https://github.com/xiph/rnnoise", "RNNoise")),
                        qsTr("%1 loader (BSD-3-Clause) for the PlayStation Network sign-in window")
                            .arg(about.link("https://learn.microsoft.com/microsoft-edge/webview2/", "Microsoft Edge WebView2")),
                        qsTr("Controller art: %1 by Kenney (CC0) · Samurai armor photo: Wikimedia Commons (CC0)")
                            .arg(about.link("https://kenney.nl/assets/input-prompts", "Input Prompts"))
                    ].join("<br>")
                }

                Section {
                    title: qsTr("License")
                    text: qsTr("PS-WRAP is free software under the GNU Affero General Public License version 3 (with the OpenSSL exception), the same license as chiaki-ng. It comes with no warranty. If you share PS-WRAP, you must also share its source code.")
                          + "<br><br>"
                          + qsTr("Source code: %1").arg(about.link("https://github.com/iDevGIS/PS-WRAP"))
                          + (about.hasNotices ? "<br>" + qsTr("Full license texts for every bundled library are in THIRD-PARTY-NOTICES.txt and the licenses folder next to PS-WRAP.exe.") : "")
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("PlayStation, PS4 and PS5 are trademarks of Sony Interactive Entertainment. PS-WRAP is not affiliated with or endorsed by Sony.")
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textMuted
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space4
            Label {
                Layout.fillWidth: true
                text: qsTr("↑↓ scroll · ◯ / Esc close")
                font.pixelSize: Theme.fontCaption
                color: Theme.textMuted
            }
            // PS-WRAP: เช็คเวอร์ชันใหม่ (UpdateDialog ใน Main.qml)
            C.Button {
                id: updatesButton
                text: Chiaki.updater.updateAvailable ? qsTr("Update to %1").arg(Chiaki.updater.latestVersion) : qsTr("Check for updates")
                onClicked: { about.close(); root.showUpdateDialog(null); }
                KeyNavigation.right: about.hasNotices ? noticesButton : closeButton
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
                Keys.onUpPressed: about.scrollBy(-80)
                Keys.onDownPressed: about.scrollBy(80)
                Material.roundedScale: Material.SmallScale
            }
            C.Button {
                id: noticesButton
                visible: about.hasNotices
                text: qsTr("Licenses")
                onClicked: Chiaki.window.openAppFile("THIRD-PARTY-NOTICES.txt")
                KeyNavigation.left: updatesButton
                KeyNavigation.right: closeButton
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
                Keys.onUpPressed: about.scrollBy(-80)
                Keys.onDownPressed: about.scrollBy(80)
                Material.roundedScale: Material.SmallScale
            }
            C.Button {
                id: closeButton
                text: qsTr("Close")
                highlighted: true
                onClicked: about.close()
                KeyNavigation.left: about.hasNotices ? noticesButton : updatesButton
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
                Keys.onUpPressed: about.scrollBy(-80)
                Keys.onDownPressed: about.scrollBy(80)
                Material.roundedScale: Material.SmallScale
            }
        }
    }

    component Section: ColumnLayout {
        property string title
        property string text
        Layout.fillWidth: true
        spacing: Theme.space2
        Label {
            text: parent.title
            font.pixelSize: Theme.fontCaption
            font.weight: Font.DemiBold
            font.letterSpacing: 1.2
            font.capitalization: Font.AllUppercase
            color: Theme.accent
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            textFormat: Text.StyledText
            text: parent.text
            font.pixelSize: Theme.fontLabel
            lineHeight: 1.2
            color: Theme.text
            onLinkActivated: (url) => Qt.openUrlExternally(url)
            HoverHandler { cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor }
        }
    }
}
