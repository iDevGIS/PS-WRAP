// PS-WRAP design tokens (singleton) — registered from C++ as org.streetpea.chiaking/Theme
// ที่มา: PS-WRAP/design/tokens.md · แก้ค่าที่นี่ที่เดียว ทุก control/หน้าจอดึงจากที่นี่
pragma Singleton
import QtQuick

QtObject {
    // ---- สี (dark เป็นหลัก ใช้ในห้องมืดหน้าทีวี) ----
    readonly property color bg: "#0b0f14"
    readonly property color surface: "#141a22"
    readonly property color surfaceRaised: "#1c242e"
    readonly property color surfaceHover: "#232d39"
    readonly property color border: "#2a3441"
    readonly property color text: "#e8edf2"
    readonly property color textMuted: "#8b97a5"
    readonly property color accent: "#00a7ff"          // คงของ upstream
    readonly property color accentPressed: "#0086cc"
    readonly property color accentText: "#ffffff"
    readonly property color success: "#3ddc84"
    readonly property color warning: "#ffb648"
    readonly property color danger: "#ff5c5c"
    readonly property color overlay: Qt.rgba(0, 0, 0, 0.6)

    // ---- ระยะ (base 4) ----
    readonly property int space1: 4
    readonly property int space2: 8
    readonly property int space3: 12
    readonly property int space4: 16
    readonly property int space6: 24
    readonly property int space8: 32
    readonly property int space12: 48
    readonly property int screenMargin: 48            // TV safe area
    readonly property int cardGap: 16

    // ---- ตัวหนังสือ (10-foot UI: ใหญ่กว่า desktop ปกติ) ----
    readonly property int fontDisplay: 36
    readonly property int fontTitle: 24
    readonly property int fontBody: 20                 // = ค่าเดิมของ upstream (qtquickcontrols2.conf PixelSize=20)
    readonly property int fontLabel: 16
    readonly property int fontCaption: 13

    // ---- รูปทรง ----
    readonly property int radiusCard: 16
    readonly property int radiusControl: 10
    readonly property int radiusChip: 999
    readonly property int focusWidth: 3
    readonly property int focusMargin: 4
    readonly property int controlHeight: 48

    // ---- motion ----
    readonly property int durFast: 120
    readonly property int durDialog: 200

    // สีสถานะ console: "ready" | "standby" | อื่นๆ (unknown/offline)
    function stateColor(state) {
        if (state === "ready") return success;
        if (state === "standby") return warning;
        return textMuted;
    }
}
