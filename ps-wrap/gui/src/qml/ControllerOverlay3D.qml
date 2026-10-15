import QtQuick
import QtQuick3D

import "qrc:/pad3d"

// PS-WRAP: จอย DualSense 3D ใน overlay — ControllerOverlay.qml โหลดผ่าน Loader เมื่อเปิด Chiaki.window.pad3d
// (โหลดไม่ได้ เช่น ไม่มี QtQuick3D = Loader ผิดพลาด → ControllerOverlay ใช้ภาพ 2D ต่อเอง)
// โมเดล: res/pad3d/Pad3DModel.qml (สร้างจาก assets-src/dualsense/make_pad3d.py) — "Playstation 5 Dualsense" by AHarmlessPotato, CC-BY-4.0
//
// ฉากจัดแกนให้ตรงกับเซนเซอร์ของ SDL: +X ขวา · +Y ขึ้น (ออกจากหน้าจอยตอนวางราบ) · +Z หาผู้เล่น
// → การหมุนจากไจโรใส่ให้ Node ได้ตรงๆ · วางราบบนโต๊ะ = จอยในจอวางราบ · ถือเอียง = เอียงตามจริง (หันซ้ายขวาค่อยๆ กลับศูนย์)
Item {
    id: root
    // input รวมแล้ว (ControllerOverlay.qml ส่งมา)
    property int buttons: 0
    property real lx: 0
    property real ly: 0
    property real rx: 0
    property real ry: 0
    property real l2: 0
    property real r2: 0
    property color lightColor: "transparent"
    property var orientation: Qt.quaternion(1, 0, 0, 0)   // จาก QmlController.orientation (ไม่มีไจโร = ไม่เอียง)
    property real maxTiltDeg: 80
    property real yawFollowSeconds: 1.5

    // เงย/ก้ม + เอียงซ้ายขวา = ค่าจริงเทียบแรงโน้มถ่วง (Madgwick ใน chiaki แก้ด้วย accel → ไม่ลอย) · ถือเอียงค้าง = ค้างตาม
    // หันซ้ายขวา (yaw) ไม่มีเข็มทิศ → ลอยได้ → ให้ค่อยๆ กลับศูนย์ (เห็นตอนหัน แล้วหันกลับหน้าเอง)
    property real yawRef: 0
    property bool hasYaw: false
    property quaternion tilt: Qt.quaternion(1, 0, 0, 0)

    function nlerp(a, b, t) {
        // เส้นทางสั้น (q กับ -q คือท่าเดียวกัน)
        const s = (a.scalar * b.scalar + a.x * b.x + a.y * b.y + a.z * b.z) < 0 ? -1 : 1;
        return Qt.quaternion(a.scalar + (s * b.scalar - a.scalar) * t, a.x + (s * b.x - a.x) * t,
                             a.y + (s * b.y - a.y) * t, a.z + (s * b.z - a.z) * t).normalized();
    }
    function aboutY(rad) { return Qt.quaternion(Math.cos(rad / 2), 0, Math.sin(rad / 2), 0) }
    function recenter() { hasYaw = false }

    FrameAnimation {
        running: root.visible
        onTriggered: {
            const q = root.orientation;
            if (!q || q.length() < 0.5) { root.tilt = Qt.quaternion(1, 0, 0, 0); return; }
            const cur = q.normalized();
            // ทิศที่ไกชี้ (−Z ของจอย) ในโลก → มุมหัน
            const f = cur.times(Qt.vector3d(0, 0, -1));
            const yaw = Math.atan2(-f.x, -f.z);
            if (!root.hasYaw) { root.yawRef = yaw; root.hasYaw = true; }
            let dy = yaw - root.yawRef;
            while (dy > Math.PI) dy -= 2 * Math.PI;
            while (dy < -Math.PI) dy += 2 * Math.PI;
            root.yawRef += dy * Math.min(1, frameTime / Math.max(0.1, root.yawFollowSeconds));
            // ตัดมุมหันที่ไล่ตามออก (หมุนรอบแกนขึ้นของโลก)
            let d = root.aboutY(-root.yawRef).times(cur).normalized();
            if (d.scalar < 0) d = Qt.quaternion(-d.scalar, -d.x, -d.y, -d.z);
            const ang = 2 * Math.acos(Math.min(1, d.scalar)) * 180 / Math.PI;
            if (ang > root.maxTiltDeg)
                d = root.nlerp(Qt.quaternion(1, 0, 0, 0), d, root.maxTiltDeg / ang);
            root.tilt = root.nlerp(root.tilt, d, 0.5);   // ทำให้นุ่ม (กันสั่นจากเซนเซอร์)
        }
    }

    View3D {
        anchors.fill: parent
        environment: SceneEnvironment {
            backgroundMode: SceneEnvironment.Transparent
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }
        // มองจากตำแหน่งผู้เล่น: สูงหน่อย ด้านหลังจอย มองลงหาหน้าจอย
        PerspectiveCamera {
            position: Qt.vector3d(0, 98, 50)
            eulerRotation.x: -63   // สูงพอเห็นไหล่/ไก L2 R2 ตอนถือจอยปกติ (หน้าจอยเงยเข้าหาตัว)
            fieldOfView: 30
            clipNear: 1
            clipFar: 500
        }
        DirectionalLight { eulerRotation: Qt.vector3d(-55, -25, 0); brightness: 1.2; ambientColor: Qt.rgba(0.35, 0.36, 0.4, 1) }
        DirectionalLight { eulerRotation: Qt.vector3d(-30, 0, 0); brightness: 0.6 }
        DirectionalLight { eulerRotation: Qt.vector3d(20, 160, 0); brightness: 0.4 }

        Node {
            rotation: root.tilt
            // วางราบ หน้าจอยขึ้น (+Y) ไกชี้ออกจากผู้เล่น (−Z) · หน่วยโมเดลเป็นเมตร → ×100
            Node {
                rotation: Qt.quaternion(0, 0, 0.70710678, -0.70710678)
                scale: Qt.vector3d(100, 100, 100)
                Pad3DModel {
                    buttons: root.buttons
                    lx: root.lx
                    ly: root.ly
                    rx: root.rx
                    ry: root.ry
                    l2: root.l2
                    r2: root.r2
                    lightColor: root.lightColor
                }
            }
        }
    }
}
