import QtQuick
import QtQuick.Effects
import QtMultimedia

import org.streetpea.chiaking

// PS-WRAP: facecam overlay — กล้องเว็บแคม (Qt Multimedia, backend WMF บน Windows) วาดทับวิดีโอขณะสตรีม
// - active: เปิด/ปิดกล้อง (ปิด = ปล่อย device) · cameraId = ชื่อกล้อง (description) ว่าง = กล้องเริ่มต้นของระบบ
// - mirror / circle (วงกลม ไม่งั้นมุมโค้ง)
// - zoom (1–3) + panX/panY (-1..1): ครอปดิจิทัลเข้าหน้า
// - keyEnabled + keyColor + keyTolerance + keySoftness: chroma key ตัดฉากเขียว/น้ำเงินเป็นโปร่งใส (shader qrc:/shaders/chroma.frag.qsb)
Item {
    id: root
    property bool active: false
    property bool mirror: true
    property bool circle: false
    property string cameraId: ""
    property real frameOpacity: 1.0
    property real zoom: 1.0
    property real panX: 0.0
    property real panY: 0.0
    property bool keyEnabled: false          // chroma key (green/blue)
    property bool aiEnabled: false           // AI segmentation (ไม่ต้องใช้ฉากเขียว)
    property color keyColor: "#00ff00"
    property real keyTolerance: 0.25
    property real keySoftness: 0.10
    property real aiThreshold: 0.5
    property int fx: 0                       // 0 none · 1 sunglasses · 2 mustache · 3 clown nose · 4 crown · 5 bane mask · 6 party (1+2+4) · 7 samurai · 8 ninja · 9 ghost (Tsushima) · 10 kabuto (ชุดเต็ม หมวก+หน้ากาก) · 11 samurai photo (ภาพจริง fx/samurai_photo.png ข้าง exe) · 12 jin mask · 13 jin set (mask+headband) — fx/jin_*.png ส่วนตัว
    readonly property bool fxActive: fx > 0 && faceTracker.ready && faceTracker.faceFound
    readonly property bool fxMesh: faceTracker.meshActive
    readonly property int fxMs: faceTracker.inferenceMs
    readonly property string fxError: faceTracker.error
    readonly property bool cutout: keyEnabled || aiEnabled
    readonly property bool aiActive: aiEnabled && segmenter.ready
    readonly property int aiMs: segmenter.inferenceMs
    readonly property string aiError: segmenter.error
    // มุมโค้งเป็นสัดส่วนของกรอบ (ไม่ใช่ค่าคงที่) — กรอบเล็กลงมุมไม่กลมเกิน
    readonly property real cornerRadius: circle ? Math.min(width, height) / 2 : Math.round(Math.min(width, height) * 0.09)
    // กล้องที่ Qt (WMF) เห็น vs กล้อง DirectShow (รวม virtual: NVIDIA Broadcast / OBS / Streamlabs) — ใช้ DshowCamera เมื่อชื่อที่เลือกไม่อยู่ในรายชื่อ Qt
    readonly property bool qtHasSelected: {
        if (!root.cameraId.length) return true;
        for (let i = 0; i < devices.videoInputs.length; ++i)
            if (devices.videoInputs[i].description === root.cameraId) return true;
        return false;
    }
    readonly property bool useFake: dshowCam.fakeSource.length > 0   // ทดสอบ: ไฟล์วิดีโอแทนกล้อง (env PSWRAP_FAKE_CAM)
    readonly property bool useDshow: !useFake && root.cameraId.length > 0 && !qtHasSelected && dshowCam.devices.indexOf(root.cameraId) >= 0
    readonly property bool hasCamera: useFake || devices.videoInputs.length > 0 || dshowCam.devices.length > 0
    readonly property string cameraName: useDshow ? root.cameraId : camera.cameraDevice.description
    readonly property var dshowDevices: dshowCam.devices

    MediaDevices { id: devices }

    DshowCamera {
        id: dshowCam
        deviceName: root.useDshow ? root.cameraId : ""
        active: root.active && root.visible && root.useDshow
        videoSink: root.firstSink
        onErrorChanged: if (error.length) console.warn("PSWRAP dshow camera:", error)
    }

    MediaPlayer {
        id: fakePlayer
        source: root.useFake ? "file:///" + dshowCam.fakeSource.split("\\").join("/") : ""
        loops: MediaPlayer.Infinite
        // ต้อง null เมื่อไม่ใช้ไฟล์ — ถ้าผูก sink ไว้ MediaPlayer จะยึด sink ไปจาก CaptureSession/DshowCamera → กล้องจริงไม่ขึ้นภาพ (บั๊ก 2026-10-04)
        videoOutput: root.useFake ? ((root.fx > 0 || root.aiEnabled) ? root.firstSink : output) : null
        audioOutput: null
        Component.onCompleted: if (root.useFake) play()
        onSourceChanged: if (root.useFake && root.active) play()
    }
    // สายส่งเฟรม: กล้อง → FaceTracker (ถ้ามี effect) → Segmenter (ถ้า AI) → VideoOutput
    readonly property QtObject afterFaceSink: root.aiEnabled ? segmenter.inputSink : output.videoSink
    readonly property QtObject firstSink: root.fx > 0 ? faceTracker.inputSink : afterFaceSink
    FaceTracker {
        id: faceTracker
        enabled: root.fx > 0 && root.active && root.visible
        forwardSink: root.afterFaceSink
        onErrorChanged: if (error.length) console.warn("PSWRAP face tracker:", error)
    }

    // AI: เฟรมเข้า segmenter.inputSink → ส่งต่อ output + mask ไป maskOut
    Segmenter {
        id: segmenter
        enabled: root.aiEnabled && root.active && root.visible
        forwardSink: output.videoSink
        maskSink: maskOut.videoSink
        onErrorChanged: if (error.length) console.warn("PSWRAP segmenter:", error)
    }
    VideoOutput {
        id: maskOut
        width: 256; height: 256
        visible: false
        fillMode: VideoOutput.Stretch
    }
    ShaderEffectSource {
        id: maskSource
        sourceItem: maskOut
        live: true
        hideSource: true
    }
    // crop ของเฟรมกล้องที่ VideoOutput (PreserveAspectCrop) แสดงในกรอบ — ใช้ map mask/จุดบนหน้าให้ตรงภาพ
    function cropRectFor(fw, fh) {
        if (fw <= 0 || fh <= 0 || width <= 0 || height <= 0) return Qt.rect(0, 0, 1, 1);
        const fa = fw / fh, ba = width / height;
        if (fa > ba) { const vis = ba / fa; return Qt.rect((1 - vis) / 2, 0, (1 + vis) / 2, 1); }
        const vis = fa / ba; return Qt.rect(0, (1 - vis) / 2, 1, (1 + vis) / 2);
    }
    // จุด normalized ในเฟรมกล้อง → พิกัด px ในกรอบ (crop → mirror → zoom → pan เหมือนชั้นภาพ)
    function frameToBox(p, fw, fh) {
        const rc = cropRectFor(fw, fh);
        let cx = (p.x - rc.x) / Math.max(1e-4, rc.width - rc.x), cy = (p.y - rc.y) / Math.max(1e-4, rc.height - rc.y);
        if (root.mirror) cx = 1 - cx;
        cx = (cx - 0.5) * root.zoom + 0.5 - root.panX * (root.zoom - 1) / 2;
        cy = (cy - 0.5) * root.zoom + 0.5 - root.panY * (root.zoom - 1) / 2;
        return Qt.point(cx * width, cy * height);
    }
    readonly property rect maskRect: {
        const fw = segmenter.frameWidth, fh = segmenter.frameHeight;
        if (fw <= 0 || fh <= 0 || width <= 0 || height <= 0) return Qt.rect(0, 0, 1, 1);
        const fa = fw / fh, ba = width / height;
        if (fa > ba) { const vis = ba / fa; return Qt.rect((1 - vis) / 2, 0, (1 + vis) / 2, 1); }
        const vis = fa / ba; return Qt.rect(0, (1 - vis) / 2, 1, (1 + vis) / 2);
    }

    // ชื่อกล้องถัดไป (วนลูป) สำหรับคีย์ N ในโหมดย้าย — คืน "" = กล้องเริ่มต้นของระบบ
    function nextCameraName() {
        const list = devices.videoInputs;
        if (list.length < 2) return "";
        let idx = -1;
        for (let i = 0; i < list.length; ++i)
            if (list[i].description === camera.cameraDevice.description) { idx = i; break; }
        return list[(idx + 1) % list.length].description;
    }

    CaptureSession {
        id: session
        camera: Camera {
            id: camera
            active: root.active && root.visible && root.hasCamera && !root.useDshow && !root.useFake
            cameraDevice: {
                // cameraId = ชื่อกล้อง (description) — id ของ Qt เป็น QByteArray เทียบกับ string ใน QSettings ไม่ได้
                if (root.cameraId.length) {
                    for (let i = 0; i < devices.videoInputs.length; ++i)
                        if (devices.videoInputs[i].description === root.cameraId)
                            return devices.videoInputs[i];
                }
                return devices.defaultVideoInput;
            }
            onErrorOccurred: (error, errorString) => console.warn("PSWRAP webcam error:", error, errorString)
        }
        videoOutput: (root.fx > 0 || root.aiEnabled) ? root.firstSink : output
    }

    // ชั้น 1: ภาพกล้อง + zoom/pan + mirror (ซ่อน แล้วส่งเป็น texture ให้ shader)
    Item {
        id: zoomLayer
        anchors.fill: parent
        clip: true
        visible: false
        VideoOutput {
            id: output
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectCrop
            transform: [
                Scale { origin.x: output.width / 2; xScale: root.mirror ? -1 : 1 },
                Scale { origin.x: output.width / 2; origin.y: output.height / 2; xScale: root.zoom; yScale: root.zoom },
                Translate { x: -root.panX * output.width * (root.zoom - 1) / 2; y: -root.panY * output.height * (root.zoom - 1) / 2 }
            ]
        }
    }
    ShaderEffectSource {
        id: zoomSource
        sourceItem: zoomLayer
        live: true
        hideSource: true
    }

    // ชั้น 2: chroma key (ถ้าเปิด) + mask มุมโค้ง/วงกลม
    Item {
        id: videoHolder
        anchors.fill: parent
        opacity: root.frameOpacity
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: maskItem
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }
        // พื้นหลังตอนยังไม่มีภาพ (โปร่งใสเมื่อ chroma key เปิด เพื่อให้ "เหลือแต่หน้า" จริงๆ)
        Rectangle {
            anchors.fill: parent
            color: Theme.surface
            visible: !root.cutout || !(root.useDshow ? dshowCam.running : camera.active)
            Text {
                anchors.centerIn: parent
                width: parent.width - 16
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
                text: !root.hasCamera ? qsTr("No camera found") : (root.useDshow && dshowCam.error.length ? dshowCam.error : qsTr("Starting camera…"))
                visible: !root.hasCamera || (root.useFake ? false : (root.useDshow ? !dshowCam.running : (camera.error !== Camera.NoError || !camera.active)))
            }
        }
        ShaderEffect {
            id: keyed
            anchors.fill: parent
            property variant source: zoomSource   // ต้องชื่อ source — default shader ของ ShaderEffect อ่านจาก property นี้ (ตอนไม่ตัดพื้นหลัง)
            property variant mask: maskSource
            property color keyColor: root.keyColor
            property vector4d maskRect: Qt.vector4d(root.maskRect.x, root.maskRect.y, root.maskRect.width, root.maskRect.height)
            property vector2d pan: Qt.vector2d(root.panX, root.panY)
            property real tolerance: root.keyTolerance
            property real softness: root.aiActive ? Math.max(0.02, root.keyTolerance * 0.5) : root.keySoftness
            property real mode: root.aiActive ? 2 : (root.keyEnabled ? 1 : 0)
            property real mirror: root.mirror ? 1 : 0
            property real zoom: root.zoom
            property real aiThreshold: root.aiThreshold
            fragmentShader: root.cutout ? "qrc:/shaders/facecam.frag.qsb" : ""
            onStatusChanged: if (status === ShaderEffect.Error) console.warn("PSWRAP facecam shader error:", log)
        }

        // ---- face effects v2: จุดจาก face mesh 478 (fallback 6 จุด BlazeFace) map ผ่าน frameToBox · head pose 3 แกน → sticker หมุนแบบ 3D (Rotation axis x/y/z)
        Item {
            id: fxLayer
            anchors.fill: parent
            visible: root.fx > 0
            readonly property int fw: faceTracker.frameWidth
            readonly property int fh: faceTracker.frameHeight
            function mapPt(p) { return root.frameToBox(p, fw, fh) }
            readonly property point eL: mapPt(faceTracker.leftEye)
            readonly property point eR: mapPt(faceTracker.rightEye)
            readonly property point eOL: mapPt(faceTracker.eyeOuterL)
            readonly property point eOR: mapPt(faceTracker.eyeOuterR)
            readonly property point nose: mapPt(faceTracker.nose)
            readonly property point bridge: mapPt(faceTracker.noseBridge)
            readonly property point lipTop: mapPt(faceTracker.mouth)
            readonly property point lipBot: mapPt(faceTracker.mouthBottom)
            readonly property point mL: mapPt(faceTracker.mouthL)
            readonly property point mR: mapPt(faceTracker.mouthR)
            readonly property point forehead: mapPt(faceTracker.forehead)
            readonly property point chin: mapPt(faceTracker.chin)
            readonly property point tL: mapPt(faceTracker.templeL)
            readonly property point tR: mapPt(faceTracker.templeR)
            // สเกลเฟรม→กรอบ (px ในกรอบต่อ 1.0 ของความกว้างเฟรม หลัง crop/zoom)
            readonly property real unit: { const a = mapPt(Qt.point(0, 0.5)), b = mapPt(Qt.point(1, 0.5)); return Math.abs(b.x - a.x) }
            readonly property real faceW: Math.max(16, faceTracker.faceWidth * unit)      // ขมับ-ขมับ จริง (3D)
            readonly property real faceH: Math.max(16, faceTracker.faceHeight * unit)     // หน้าผาก-คาง จริง
            readonly property real eyeSpan: Math.max(8, Math.hypot(eOR.x - eOL.x, eOR.y - eOL.y))
            readonly property real mouthW: Math.max(8, Math.hypot(mR.x - mL.x, mR.y - mL.y))
            readonly property real cxEyes: (eL.x + eR.x) / 2
            readonly property real cyEyes: (eL.y + eR.y) / 2
            // pose: roll จากจุดที่ map แล้ว (ถูกต้องทั้ง mirror/ไม่), yaw กลับทิศเมื่อ mirror, pitch คงเดิม
            readonly property point pA: eL.x <= eR.x ? eL : eR
            readonly property point pB: eL.x <= eR.x ? eR : eL
            readonly property real roll: Math.atan2(pB.y - pA.y, pB.x - pA.x) * 180 / Math.PI
            readonly property real yaw: (root.mirror ? -1 : 1) * faceTracker.yaw
            readonly property real pitch: faceTracker.pitch
            readonly property bool on: root.fxActive
            function has(id) { return root.fx === id || (root.fx === 6 && (id === 1 || id === 2 || id === 4)) || (root.fx === 13 && (id === 12 || id === 14)) }
            // ขยับจุดยึดลง/ขึ้นตามเส้นกึ่งกลางหน้า (จากตาไปคาง) — ใช้แทน "เลื่อนลง y" ให้ถูกทิศแม้หน้าเอียง
            function along(from, to, t) { return Qt.point(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t) }

            component FaceSprite: Image {
                id: sp
                property bool enabledFx: false
                property point anchor: Qt.point(0, 0)   // จุดยึด (px ในกรอบ)
                property real ax: 0.5                   // จุดยึดในภาพ (0..1)
                property real ay: 0.5
                property real spriteW: 100
                property real aspect: 1
                property real yawGain: 1.0              // sticker ที่ "แบน" บนหน้า (แว่น/หนวด) หมุนตาม yaw เต็ม; ของที่ลอย (มงกุฎ) น้อยกว่า
                property real pitchGain: 1.0
                visible: enabledFx && opacity > 0
                opacity: enabledFx && fxLayer.on ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 150 } }
                width: spriteW
                height: spriteW / aspect
                x: anchor.x - width * ax
                y: anchor.y - height * ay
                smooth: true
                mipmap: true
                transform: [
                    Rotation { origin.x: sp.width * sp.ax; origin.y: sp.height * sp.ay; axis { x: 1; y: 0; z: 0 } angle: -fxLayer.pitch * sp.pitchGain },
                    Rotation { origin.x: sp.width * sp.ax; origin.y: sp.height * sp.ay; axis { x: 0; y: 1; z: 0 } angle: fxLayer.yaw * sp.yawGain },
                    Rotation { origin.x: sp.width * sp.ax; origin.y: sp.height * sp.ay; axis { x: 0; y: 0; z: 1 } angle: fxLayer.roll }
                ]
            }

            // แว่นตาดำ: กว้าง = หางตา-หางตา ×1.55, lens center (y 58/110) ที่กึ่งกลางตา
            FaceSprite { enabledFx: fxLayer.has(1); source: "qrc:/icons/fx/sunglasses.svg"; sourceSize: Qt.size(900, 330); aspect: 300 / 110
                spriteW: fxLayer.eyeSpan * 1.55; ax: 0.5; ay: 58 / 110; anchor: Qt.point(fxLayer.cxEyes, fxLayer.cyEyes) }
            // หนวด: กว้าง = ปาก ×1.35, กึ่งกลางระหว่างปลายจมูกกับริมฝีปากบน (ค่อนไปทางปาก 60%)
            FaceSprite { enabledFx: fxLayer.has(2); source: "qrc:/icons/fx/mustache.svg"; sourceSize: Qt.size(600, 200); aspect: 300 / 100
                spriteW: fxLayer.mouthW * 1.35; ax: 0.5; ay: 0.5; anchor: fxLayer.along(fxLayer.nose, fxLayer.lipTop, 0.6) }
            // จมูกตัวตลก: ที่ปลายจมูก ขนาด 0.26×ความกว้างหน้า ไม่หมุนตาม yaw (ลูกบอลกลม)
            FaceSprite { enabledFx: fxLayer.has(3); source: "qrc:/icons/fx/clown-nose.svg"; sourceSize: Qt.size(200, 200); aspect: 1
                spriteW: fxLayer.faceW * 0.26; ax: 0.5; ay: 0.5; anchor: fxLayer.nose; yawGain: 0; pitchGain: 0 }
            // มงกุฎ: กว้าง 0.9×หน้า ฐานเหนือหน้าผาก (ยกตามแนวหน้า: จากคาง→หน้าผาก ต่อไปอีก 18%)
            FaceSprite { enabledFx: fxLayer.has(4); source: "qrc:/icons/fx/crown.svg"; sourceSize: Qt.size(600, 320); aspect: 300 / 160
                spriteW: fxLayer.faceW * 0.9; ax: 0.5; ay: 150 / 160; anchor: fxLayer.along(fxLayer.chin, fxLayer.forehead, 1.18); yawGain: 0.6; pitchGain: 0.6 }
            // หน้ากาก Bane: กว้าง 1.05×หน้า ขอบบน (y 20/200) ที่สันจมูก→ปลายจมูก 50%
            FaceSprite { enabledFx: fxLayer.has(5); source: "qrc:/icons/fx/mask.svg"; sourceSize: Qt.size(600, 400); aspect: 300 / 200
                spriteW: fxLayer.faceW * 1.05; ax: 0.5; ay: 20 / 200; anchor: fxLayer.along(fxLayer.bridge, fxLayer.nose, 0.5) }
            // หน้ากากซามูไร (เมมโป): กว้าง 1.08×หน้า ขอบบน (y 30/220) ที่สันจมูก→ปลายจมูก 35%, สูงพอดีถึงคาง
            FaceSprite { enabledFx: fxLayer.has(7); source: "qrc:/icons/fx/samurai.svg"; sourceSize: Qt.size(600, 440); aspect: 300 / 220
                spriteW: fxLayer.faceW * 1.08; ax: 0.5; ay: 30 / 220; anchor: fxLayer.along(fxLayer.bridge, fxLayer.nose, 0.35) }
            // นินจา: คลุมทั้งหัว เปิดช่องตา — ช่องตา (y 120/300) ที่กึ่งกลางตา, กว้าง = หน้า ×1.25 (ผ้าหุ้มเลยหัว)
            FaceSprite { enabledFx: fxLayer.has(8); source: "qrc:/icons/fx/ninja.svg"; sourceSize: Qt.size(600, 600); aspect: 1
                spriteW: fxLayer.faceW * 1.25; ax: 0.5; ay: 120 / 300; anchor: Qt.point(fxLayer.cxEyes, fxLayer.cyEyes); yawGain: 0.8; pitchGain: 0.8 }
            // Ghost (Tsushima): ผ้าคลุม+เมมโปแดง — ช่องตา (y 112/300) ที่กึ่งกลางตา, กว้าง = หน้า ×1.22
            FaceSprite { enabledFx: fxLayer.has(9); source: "qrc:/icons/fx/ghost.svg"; sourceSize: Qt.size(600, 600); aspect: 1
                spriteW: fxLayer.faceW * 1.22; ax: 0.5; ay: 112 / 300; anchor: Qt.point(fxLayer.cxEyes, fxLayer.cyEyes); yawGain: 0.85; pitchGain: 0.85 }
            // ซามูไรชุดเต็ม (คาบูโตะ+เขากวาง+เกราะคอ+เมมโป): ช่องตา (y 232/420) ที่กึ่งกลางตา, ความกว้างหน้าอ้างอิง 150/400 → spriteW = faceW×(400/150)
            FaceSprite { enabledFx: fxLayer.has(10); source: "qrc:/icons/fx/kabuto.svg"; sourceSize: Qt.size(800, 840); aspect: 400 / 420
                spriteW: fxLayer.faceW * (400 / 150); ax: 0.5; ay: 232 / 420; anchor: Qt.point(fxLayer.cxEyes, fxLayer.cyEyes); yawGain: 0.7; pitchGain: 0.7 }
            // ซามูไรภาพจริง (PNG โปร่งใสจากภาพพิพิธภัณฑ์ CC0, ตัดพื้นหลัง + เจาะช่องตา) — ไฟล์อยู่ข้าง exe: fx/samurai_photo.png (เปลี่ยนภาพได้ไม่ต้อง build)
            //   ค่าอ้างอิงจากภาพ: ช่องตาที่ y=0.565 ของความสูง, ความกว้างหน้า (ขอบหน้ากาก) = 0.53 ของความกว้างภาพ, อัตราส่วน 1059 / 1714
            FaceSprite { id: photoSprite; enabledFx: fxLayer.has(11); source: fxLayer.has(11) ? "file:///" + dshowCam.appDir + "/fx/samurai_photo.png" : ""
                sourceSize: Qt.size(1059, 1714); aspect: 1059 / 1714; asynchronous: true
                spriteW: fxLayer.faceW / 0.50; ax: 0.5; ay: 0.535; anchor: Qt.point(fxLayer.cxEyes, fxLayer.cyEyes); yawGain: 0.75; pitchGain: 0.75
                onStatusChanged: if (status === Image.Error) console.warn("PSWRAP photo fx missing:", source) }
            // Jin (ภาพจากเกม — ใช้ส่วนตัว): หน้ากากครึ่งหน้า ช่องตาเจาะ · ผ้าคาดหัวที่หน้าผาก (ระหว่างตากับหน้าผากบน 60%)
            FaceSprite { id: jinMask; enabledFx: fxLayer.has(12); source: fxLayer.has(12) ? "file:///" + dshowCam.appDir + "/fx/jin_mask.png" : ""
                sourceSize: Qt.size(820, 600); aspect: 1.3396; asynchronous: true
                spriteW: fxLayer.faceW * 1.310; ax: 0.489; ay: 0.218; anchor: Qt.point(fxLayer.cxEyes, fxLayer.cyEyes); yawGain: 0.85; pitchGain: 0.85
                onStatusChanged: if (status === Image.Error) console.warn("PSWRAP jin mask missing:", source) }
            FaceSprite { id: jinBand; enabledFx: fxLayer.has(14); source: fxLayer.has(14) ? "file:///" + dshowCam.appDir + "/fx/jin_band.png" : ""
                sourceSize: Qt.size(950, 320); aspect: 2.9000; asynchronous: true
                spriteW: fxLayer.faceW * 1.502; ax: 0.397; ay: 0.391; anchor: fxLayer.along(Qt.point(fxLayer.cxEyes, fxLayer.cyEyes), fxLayer.forehead, 0.62); yawGain: 0.7; pitchGain: 0.7
                onStatusChanged: if (status === Image.Error) console.warn("PSWRAP jin band missing:", source) }
        }
    }
    Item {
        id: maskItem
        anchors.fill: parent
        visible: false
        layer.enabled: true
        Rectangle {
            anchors.fill: parent
            radius: root.cornerRadius
            color: "black"
        }
    }

    // ขอบบาง (ซ่อนเมื่อ chroma key เปิด — เหลือแต่ตัวคน)
    Rectangle {
        anchors.fill: parent
        radius: root.cornerRadius
        color: "transparent"
        border.width: 2
        border.color: Qt.rgba(1, 1, 1, 0.55)
        opacity: root.frameOpacity
        visible: !root.cutout
    }
}
