import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import "controls" as C

import org.streetpea.chiaking

// PS-WRAP: chrome ของทุก sub-dialog (Settings, Regist, ManualHost, ConsolePin, Profile, SteamShortcut, PSN…)
// คง API/behavior ของ upstream: header/title/buttonText/buttonEnabled/buttonVisible, accepted/rejected, close(), focus restore, Keys Escape/Menu
Item {
    id: dialog
    property alias header: headerLabel.text
    property alias title: titleLabel.text
    property alias buttonText: okButton.text
    property alias buttonEnabled: okButton.enabled
    property alias buttonVisible: okButton.visible
    property Item restoreFocusItem
    default property Item mainItem: null

    signal accepted()
    signal rejected()

    // PS-WRAP responsive: ย่อฟอนต์/ความกว้าง control ตามหน้าต่าง (1.0 ที่ ≥1380 logical px, ต่ำสุด 0.7)
    readonly property real uiScale: Math.max(0.7, Math.min(1.0, (width - 380) / 1000))
    readonly property int controlWidth: Math.round(400 * uiScale)

    function close() {
        root.closeDialog();
    }

    Keys.onEscapePressed: close()

    Keys.onMenuPressed: {
        if (okButton.enabled)
            okButton.clicked()
    }

    StackView.onDeactivating: {
        restoreFocusItem = Window.window.activeFocusItem;
    }

    StackView.onActivated: {
        if (!restoreFocusItem) {
            let item = mainItem.nextItemInFocusChain();
            if (item)
                item.forceActiveFocus(Qt.TabFocusReason);
        } else {
            restoreFocusItem.forceActiveFocus(Qt.TabFocusReason);
            restoreFocusItem = null;
        }
    }

    onMainItemChanged: {
        if (mainItem) {
            mainItem.parent = contentItem;
            mainItem.anchors.fill = contentItem;
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    // ---- header bar ----
    Item {
        id: toolBar
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: 80

        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }

        RowLayout {
            anchors {
                fill: parent
                leftMargin: Theme.space6
                rightMargin: Theme.space6
            }
            spacing: Theme.space4

            C.Button {
                flat: true
                focusPolicy: Qt.NoFocus
                text: qsTr("Back")
                icon.source: root.controllerButton("moon")
                icon.width: 26
                icon.height: 26
                icon.color: "transparent"
                onClicked: {
                    dialog.rejected();
                    dialog.close();
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    id: titleLabel
                    font.pixelSize: Theme.fontTitle
                    font.weight: Font.DemiBold
                    color: Theme.text
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    id: headerLabel
                    visible: text.length > 0
                    font.pixelSize: Theme.fontCaption
                    color: Theme.textMuted
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            C.Button {
                id: okButton
                highlighted: true
                focusPolicy: Qt.NoFocus
                Layout.preferredHeight: 48
                icon.source: "qrc:/icons/options.svg"
                icon.width: 24
                icon.height: 24
                onClicked: dialog.accepted()
            }
        }
    }

    // หมายเหตุ: ฟอนต์ย่อตาม uiScale ถูกตั้งที่ StackView ใน Main.qml (ตั้งตรงนี้ไม่มีผล เพราะ mainItem ถูก reparent หลังสร้าง)
    Item {
        id: contentItem
        anchors {
            top: toolBar.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
    }
}
