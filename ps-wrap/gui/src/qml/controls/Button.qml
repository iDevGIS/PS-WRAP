import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

// PS-WRAP: restyled ด้วย Theme (พื้นหลัง/ขอบ/focus ring) · logic คีย์/จอยด้านล่างคงของ upstream ทั้งหมด
Button {
    id: control
    property bool firstInFocusChain: false
    property bool lastInFocusChain: false
    property bool sendOutput: false

    font.weight: Font.DemiBold
    topPadding: Theme.space3
    bottomPadding: Theme.space3
    leftPadding: Theme.space6
    rightPadding: Theme.space6

    background: Rectangle {
        implicitHeight: Theme.controlHeight
        implicitWidth: 120
        radius: Theme.radiusControl
        color: control.highlighted
                   ? (control.down ? Theme.accentPressed : Theme.accent)
                   : control.flat
                     ? (control.down ? Theme.surfaceRaised : control.hovered ? Qt.rgba(1, 1, 1, 0.06) : "transparent")
                     : (control.down ? Theme.surfaceHover : control.hovered ? Theme.surfaceRaised : Theme.surface)
        border.width: control.flat || control.highlighted ? 0 : 1
        border.color: Theme.border
        opacity: control.enabled ? 1.0 : 0.5
        Behavior on color { ColorAnimation { duration: Theme.durFast } }

        // focus ring: เห็นชัดจากไกล (TV/Deck) — แสดงเฉพาะ focus จากคีย์/จอย
        Rectangle {
            anchors.fill: parent
            anchors.margins: -Theme.focusMargin
            radius: parent.radius + Theme.focusMargin
            color: "transparent"
            border.width: Theme.focusWidth
            border.color: Theme.accent
            visible: control.visualFocus
        }
    }

    Component.onDestruction: {
        if (visualFocus) {
            let item = nextItemInFocusChain();
            if (item)
                item.forceActiveFocus(Qt.TabFocusReason);
        }
    }

    Keys.onPressed: (event) => {
        switch (event.key) {
        case Qt.Key_Up:
            if (!firstInFocusChain) {
                let item = nextItemInFocusChain(false);
                if (item)
                    item.forceActiveFocus(Qt.TabFocusReason);
                if(!sendOutput)
                    event.accepted = true;
            }
            break;
        case Qt.Key_Down:
            if (!lastInFocusChain) {
                let item = nextItemInFocusChain();
                if (item)
                    item.forceActiveFocus(Qt.TabFocusReason);
                if(!sendOutput)
                    event.accepted = true;
            }
            break;
        case Qt.Key_Return:
            if (visualFocus) {
                clicked();
            }
            event.accepted = true;
            break;
        }
    }
}
