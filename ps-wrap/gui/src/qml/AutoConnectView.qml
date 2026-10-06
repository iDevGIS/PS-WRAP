import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

// PS-WRAP: หน้ารอ auto-connect ตาม Theme — คง behavior upstream (allowClose หลัง 1.5s, cancel ด้วย Esc/คลิกขวา/Ctrl+Q, fail timer)
Rectangle {
    id: view
    property bool allowClose: false
    property bool textVisible: false
    color: Theme.bg

    function stop() {
        if (!allowClose)
            return;
        Chiaki.stopAutoConnect();
        root.showMainView();
    }

    function cancel() {
        if(!allowClose)
            return;
        view.textVisible = false;
        infoLabel.text = qsTr("Cancelling connection...");
        failTimer.start();
    }

    Keys.onEscapePressed: view.cancel()

    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: view.cancel()
    }

    MouseArea {
        anchors.fill: parent
        enabled: view.allowClose
        acceptedButtons: Qt.RightButton
        onClicked: view.cancel()
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.space6
        width: Math.min(parent.width - Theme.screenMargin * 2, 720)

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 96
            height: 96
            sourceSize: Qt.size(96, 96)
            source: "qrc:/icons/pswrap.svg"
            opacity: 0.9
        }

        BusyIndicator {
            id: spinner
            anchors.horizontalCenter: parent.horizontalCenter
            width: 56
            height: width
            running: view.visible
        }

        Label {
            id: infoLabel
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            color: Theme.text
            opacity: view.allowClose ? 1.0 : 0.0
            visible: opacity
            text: qsTr("Waiting for console...")

            Behavior on opacity { NumberAnimation { duration: 250 } }
        }

        Label {
            id: closeMessageLabel
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.fontLabel
            color: Theme.textMuted
            opacity: textVisible ? 1.0: 0.0
            visible: opacity
            text: qsTr("Press %1 to cancel connection").arg(Chiaki.controllers.length ? (root.controllerButton("circle").includes("deck") ? "B" : "Circle") : "escape or right-click")
        }
    }

    Timer {
        interval: 1500
        running: true
        onTriggered: {
            view.allowClose = true
            view.textVisible = true
        }
    }

    Timer {
        id: failTimer
        interval: 2000
        running: false
        onTriggered: view.stop();
    }

    Connections {
        target: Chiaki

        function onWakeupStartFailed() {
            view.textVisible = false;
            infoLabel.text = qsTr("Timed out waiting for console. Exiting...");
            failTimer.start();
        }
    }
}
