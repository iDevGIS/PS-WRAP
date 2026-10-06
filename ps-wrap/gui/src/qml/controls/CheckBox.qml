import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

import org.streetpea.chiaking

// PS-WRAP: เพิ่ม Theme font + focus ring · logic คีย์/จอยคงของ upstream
CheckBox {
    id: control
    property bool firstInFocusChain: false
    property bool lastInFocusChain: false
    property bool sendOutput: false

    // ข้อความยาวให้ขึ้นบรรทัดใหม่แทนการดันคอลัมน์ของ GridLayout จนล้นจอ (Material CheckLabel wrap เมื่อถูกจำกัดความกว้าง)
    Layout.maximumWidth: Math.round(26 * font.pixelSize)   // ย่อตาม uiScale ของ DialogView (≈520 ที่ 20px)

    FocusRing { target: control }

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
                toggle();
                clicked();
            }
            event.accepted = true;
            break;
        }
    }
}
