import QtQuick
import QtQuick.Window

import org.streetpea.chiaking

// PS-WRAP: window แยกสำหรับ backend OpenGL — เนื้อหาอยู่ใน StreamMenuContent (ใช้ร่วมกับเมนู inline)
// คง API ของ upstream: open/closing, signals, Shortcut Esc/Ctrl+O, Behavior on y + closeAnimationFinished, focus
Window {
    id: streamMenuWindow

    property bool open: false
    property bool closing: false
    property bool overlayEnabled: true
    readonly property alias contentImplicitHeight: menu.implicitHeight   // PS-WRAP: StreamView ผูกความสูงหน้าต่างตามเนื้อหา

    signal closeRequested()
    signal displaySettingsRequested()
    signal placeboSettingsRequested()
    signal mainViewRequested()
    signal closeAnimationFinished()
    signal overlayToggled()

    visible: open || closing
    flags: Qt.Tool | Qt.FramelessWindowHint
    color: "transparent"
    modality: Qt.NonModal

    function focusInitialItem() {
        requestActivate();
        menu.initialFocusItem.forceActiveFocus(Qt.TabFocusReason);
    }

    onVisibleChanged: {
        if (visible)
            focusTimer.restart();
    }

    onActiveChanged: {
        if (active && visible)
            focusTimer.restart();
    }

    Shortcut {
        sequence: StandardKey.Cancel
        enabled: !menu.popupOpen   // PS-WRAP: popup เลือกไมค์เปิดอยู่ → Esc/◯ ปิดแค่ popup
        onActivated: streamMenuWindow.closeRequested()
    }

    Shortcut {
        sequence: "Ctrl+O"
        onActivated: {
            if (streamMenuWindow.open)
                streamMenuWindow.closeRequested();
        }
    }

    Behavior on y {
        NumberAnimation {
            id: menuAnimation
            duration: 250
            onRunningChanged: {
                if (!running && !streamMenuWindow.open)
                    streamMenuWindow.closeAnimationFinished();
            }
        }
    }

    Timer {
        id: focusTimer
        interval: 0
        repeat: false
        onTriggered: {
            if (streamMenuWindow.visible)
                streamMenuWindow.focusInitialItem();
        }
    }

    StreamMenuContent {
        id: menu
        anchors.fill: parent
        focus: true
        overlayEnabled: streamMenuWindow.overlayEnabled
        onCloseRequested: streamMenuWindow.closeRequested()
        onDisplaySettingsRequested: streamMenuWindow.displaySettingsRequested()
        onPlaceboSettingsRequested: streamMenuWindow.placeboSettingsRequested()
        onMainViewRequested: streamMenuWindow.mainViewRequested()
        onOverlayToggled: streamMenuWindow.overlayToggled()
        onOverlayEditRequested: {}   // edit mode รองรับเฉพาะ inline (Vulkan) — window แยกใช้ค่าตำแหน่งเดียวกัน
    }
}
