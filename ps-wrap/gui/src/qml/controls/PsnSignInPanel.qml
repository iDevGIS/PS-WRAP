import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import org.streetpea.chiaking

import "." as C

// PS-WRAP: ล็อกอิน PSN แบบง่าย — เปิดหน้าต่างล็อกอินของ Sony (WebView2) แล้วแอปรับ redirect เอง ไม่ต้อง copy/paste URL
// ใช้ใน PSNTokenDialog (PSN Remote Connection) และ PSNLoginDialog (หา Account ID ตอนลงทะเบียนเครื่อง)
// dialog เจ้าของรับ Chiaki.psnWebLoginRedirect(url) แล้วทำขั้นต่อไปเอง · panel นี้แค่แสดงสถานะ
Item {
    id: panel
    property string heading: qsTr("Sign in with your PlayStation account")
    property string body: ""
    property bool working: false        // ได้ URL แล้ว กำลังขอ token / Account ID
    property string error: ""
    property bool windowOpen: false
    signal externalRequested()          // ใช้เบราว์เซอร์ภายนอกแทน (ทางเดิม)

    function start(differentAccount) {
        error = "";
        windowOpen = Chiaki.psnWebLogin(!!differentAccount);
    }
    function focusMain() { signInButton.forceActiveFocus(Qt.TabFocusReason); }

    Connections {
        target: Chiaki
        function onPsnWebLoginRedirect(url) { panel.windowOpen = false; panel.working = true; }
        function onPsnWebLoginClosed() { panel.windowOpen = false; }
        function onPsnWebLoginFailed(err) {
            panel.windowOpen = false;
            panel.error = qsTr("The sign-in window could not open (%1). Use your web browser instead.").arg(err);
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(620, parent.width - 48)
        spacing: Theme.space4

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: panel.heading
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            color: Theme.text
            wrapMode: Text.WordWrap
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            visible: text.length > 0
            text: panel.working ? qsTr("Signed in. Finishing the setup…")
                : panel.windowOpen ? qsTr("Sign in in the PlayStation Network window. It closes by itself when you're done — nothing to copy or paste.")
                : panel.body
            font.pixelSize: Theme.fontLabel
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            visible: panel.windowOpen || panel.working
            running: visible
            implicitWidth: 56
            implicitHeight: 56
        }

        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            visible: panel.error.length > 0
            text: panel.error
            color: Theme.danger
            wrapMode: Text.WordWrap
        }

        C.Button {
            id: signInButton
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.space2
            Layout.preferredWidth: 380
            visible: !panel.working
            highlighted: true
            text: panel.windowOpen ? qsTr("Show the sign-in window") : qsTr("Sign in to PlayStation Network")
            firstInFocusChain: true
            onClicked: panel.start()
            KeyNavigation.down: browserButton
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            visible: !panel.working
            spacing: Theme.space2
            C.Button {
                id: browserButton
                flat: true
                text: qsTr("Use my web browser instead")
                onClicked: { Chiaki.psnWebLoginClose(); panel.externalRequested(); }
                KeyNavigation.up: signInButton
                KeyNavigation.right: otherAccountButton
            }
            C.Button {
                id: otherAccountButton
                flat: true
                text: qsTr("Use a different account")
                // เปิดหน้าต่างใหม่แบบลบคุกกี้ PSN ที่จำไว้
                lastInFocusChain: true
                onClicked: panel.start(true)
                KeyNavigation.up: signInButton
                KeyNavigation.left: browserButton
            }
        }
    }
}
