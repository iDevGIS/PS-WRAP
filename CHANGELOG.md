# Changelog

รูปแบบตาม [Keep a Changelog](https://keepachangelog.com/) · อิงเวอร์ชัน upstream + suffix ของเรา (เช่น `1.9.9-pswrap.1`)

## [Unreleased]
### Changed
- 2026-10-06 **ไอคอนปุ่มจอยชุดใหม่**: ✕ ○ □ △ เป็นแผ่นเข้มไล่เฉด ขอบบาง สัญลักษณ์สีแบบ PS (ฟ้า/แดง/ชมพู/เขียว) · L1/R1 ทรง pill, L3/R3 วงกลม (เดิมของ upstream ล้นกรอบจน L1 เป็นข้าวหลามตัด R3 เป็นครึ่งวง) · สร้างจาก `scripts/gen-glyphs.py` · ชุด Steam Deck ใช้แบบเดิม
- 2026-10-06 แถบล่างหน้าแรก: Discovery เป็นชิปแบบเดียวกับ mic/cam/จอย (ไอคอน + จุดเปิด/ปิด) แทนปุ่มไอคอนฟ้าลอยเดี่ยว · เลขเวอร์ชันย้ายไปท้ายแถบ
- 2026-10-06 **ที่เก็บข้อมูลของแอปเป็นของ PS-WRAP เอง**: registry `HKCU\Software\PS-WRAP\PS-WRAP`, log/cache `%APPDATA%\PS-WRAP\PS-WRAP` (เดิมใช้ร่วมกับ chiaki-ng ใน `Chiaki\Chiaki`) · เปิดครั้งแรกย้าย settings, เครื่องที่ลงทะเบียน, placebo และทุก profile มาให้อัตโนมัติ ไม่ลบของเดิม · โฟลเดอร์ source เปลี่ยนจาก `chiaki-ng/` เป็น `ps-wrap/`
- 2026-10-04 facecam effect: ขนาดแว่นอิงความกว้างหน้า + ยึดเทียบจมูก (เงย/ก้มไม่ลอยขึ้นหน้าผาก) · tracking ตามเร็วขึ้นตอนขยับไว (smoothing ปรับตามความเร็ว), คาดการณ์ตำแหน่งตอนหน้าหายชั่วคราว, แว่นจางเข้า/ออกแทนหายวับ, ย่อภาพก่อนป้อนโมเดลเร็วขึ้น
- 2026-10-04 **หน้าต่างตอนสตรีม = หน้าต่างหน้าแรก** (ขนาด/ตำแหน่ง/maximize ชุดเดียว) — ไม่ resize ตามความละเอียดสตรีมอีก ปรับขนาดระหว่างเล่นจำให้ทั้งสองหน้า
- 2026-10-04 facecam มุมโค้งเป็นสัดส่วนของกรอบ (9% ของด้านสั้น) — ย่อแล้วมุมไม่กลมเกิน
- 2026-10-04 overlay จอย/กล้อง/stats ย่อ-ขยายและ**ยึดกรอบวิดีโอจริง** (กรอบ 16:9 กลางจอ ไม่ลอยบนแถบดำ) — หน้าต่างแคบ-สูงไม่ทำให้ overlay ใหญ่เกิน · stats การ์ด scale 0.45–1.5×
- 2026-10-04 การ์ด stats ย่อ ~25% (font 12/15, icon 15) ให้ไม่แย่งสายตาตอนเล่น · แถว dropped frames แสดงตลอด (upstream โชว์เฉพาะ > 0 ทำให้การ์ดกระพริบ/เปลี่ยนความสูงตอนเล่น)
- 2026-10-04 เปลี่ยนทิศทางจาก "Python wrapper รอบ chiaki.exe" เป็น "fork chiaki-ng + redesign UI" — ของเก่าย้ายไป `archive/pswrap-python/`
### Fixed
- 2026-10-06 **ตัดเสียงลำโพงย้อนเข้าไมค์ (echo cancellation) ใช้ไม่ได้เมื่อลำโพงหน่วงเกิน ~100 ms** (upstream: เก็บเสียงอ้างอิงตอน decode ก่อนเข้าคิวเสียง + filter ยาว 100 ms) → เก็บเสียงอ้างอิงตอนส่งเข้าการ์ดเสียงจริง คำนวณว่า sample ไหนกำลังออกลำโพงเทียบกับเวลาที่ไมค์อัดแต่ละเฟรม + filter 300 ms (ทนความหน่วงที่คาดผิด −60…+240 ms เช่นลำโพง Bluetooth) · ตั้ง sample rate 48 kHz ให้ speex echo (upstream ไม่ได้ตั้ง ค่าเริ่มต้น 8 kHz)
- 2026-10-06 `scripts/snap.ps1` คืน profile ปกติไม่ได้จริง (pwsh ทิ้ง `--profile ""`, script error ออกก่อนคืนค่า, คืนค่าก่อน process ปิด) → ตัว dist เปิดเป็น profile `pswrap-test` · ตอนนี้ใช้ `"--profile="` ใน try/finally + รอ process ปิด + ตรวจซ้ำ
- 2026-10-06 build จาก clone ใหม่บน Windows: แพตช์ curl พังเมื่อ `core.autocrlf=true` (curl เป็น CRLF แพตช์เป็น LF) → แปลงเป็น LF ก่อนแพตช์ · submodule oboe/borealis (Android/Switch) ไม่ดึงตอน clone (`update = none`) เพราะ oboe path ยาวเกิน MAX_PATH ทำ `--recurse-submodules` ล้ม
- 2026-10-06 build: แพตช์ Schannel AIA ของ curl ถูกข้ามแบบเงียบๆ เมื่อโฟลเดอร์ build อยู่ใต้ git repo อื่น (`git apply --directory`) → `CURLSSLOPT_SCHANNEL_AIA undeclared` · ตอนนี้ apply ใน copy ของ curl ที่ `git init` เป็นรากเอง
- 2026-10-05 **แอป crash/ค้างกลางสตรีม (ทำให้กดปุ่มไม่ติดเป็นช่วงๆ)**: (1) `QQuickRenderControl::sync()` ถูกเรียกข้าม thread แบบไม่บล็อก GUI (upstream) → แข่งกับ overlay ที่ขยับตลอด → SIGSEGV ใน Qt6Gui · ตอนนี้ sync 2 จังหวะ: render thread `beginFrame()` (รอ GPU) ก่อน แล้ว GUI บล็อกเฉพาะ sync (~0.2ms) — เวอร์ชันแรกที่บล็อกทั้ง sync ทำให้สะดุดรัวเมื่อหน้าต่างใหญ่ (GUI รอ GPU ทุกเฟรม) (2) libplacebo assert `!tex_vk->held` (dialog ค้างจอ) เมื่อ render ส่ง overlay texture ที่ Qt ยังถืออยู่ — upstream มีช่องนี้ ตอนนี้ endFrame ก่อนใช้เสมอ (3) render แทรกหลัง resize ทำให้เปิดรอบซ้อนวนไม่จบ
- 2026-10-05 คลิกสลับ overlay ระหว่างแก้แล้วจอยไม่เข้าเกมจนตัดสตรีม (grab input ซ้อน 2 ชั้น release ชั้นเดียว)
- 2026-10-05 คลิกที่แค่ปลุกหน้าต่างไม่เข้าโหมดแก้ overlay · สลับไปแอปอื่นระหว่างแก้ = จบโหมดแก้ · แถบบนจอบอกว่าจอยหยุดส่งเข้าเกมระหว่างแก้
- 2026-10-05 ปิดเมนู/dialog ระหว่างสตรีมแล้วปุ่มไม่ติดจนกว่าจะปล่อยทุกปุ่ม (upstream) → กันเฉพาะปุ่ม/ไกที่ค้างอยู่ตอนปิด ปุ่มอื่นใช้ได้ทันที
- 2026-10-04 facecam ไม่ขึ้นภาพหลังเพิ่มโหมดทดสอบไฟล์ (MediaPlayer ยึด video sink) — ผูก sink เฉพาะเมื่อใช้ไฟล์
- 2026-10-04 Settings: เปิดหน้าที่เนื้อหายาวกว่าจอแล้วเลื่อนลงเอง (ensureVisible เล็งทั้ง GridLayout) — เปิดหน้า General เห็น preview บนสุด
- 2026-10-04 Vulkan overlay texture สร้างด้วย blit_dst — ก่อนหน้า `pl_tex_clear` ยิง validation error ทุกเฟรม (log บวม/ช้า) หลังแก้ ghost
- 2026-10-04 facecam ภาพดำหลังเพิ่ม chroma key (ShaderEffect ต้องใช้ property ชื่อ `source`) — กล้องกลับมาติด
- 2026-10-04 **overlay/dialog ซ้อนเป็นเงาหลังย่อ-ขยายหน้าต่างบน Vulkan** (PIN dialog 2 อัน, stats/จอย 2 ชุด): upstream ไม่ล้าง texture ของชั้น QML ฝั่ง Vulkan (OpenGL มี) → `pl_tex_clear` หลังสร้าง texture และก่อน Qt วาดทุกเฟรม
- 2026-10-04 tray: คลิกเดียว = สลับซ่อน/โชว์ (รอ double-click interval), double-click = โชว์เสมอ — เดิมยิงซ้อนกัน
- 2026-10-04 หลัง Console PIN dialog ปิด คีย์ลัด/เมนูสตรีมไม่ตอบ (focus หาย) → คืน focus ให้ StreamView
- 2026-10-04 stats overlay บน Vulkan วาด inline ใน QML (`StatsOverlay.qml`) แทนหน้าต่างแยกของ upstream ที่ลอยทะลุหน้าต่างอื่น · widget แยกเหลือเฉพาะ OpenGL
- 2026-10-04 tray ระหว่างสตรีม: ซ่อน = minimize (ไม่ hide) + เรียกกลับบังคับ sync overlay — แก้ overlay ซ้อนค้างหลาย ชุดหลัง double-click tray
- 2026-10-04 dialog Disconnect Session ใหม่ (`DisconnectDialogContent.qml`): ปุ่มหลัก/ค่าเริ่มต้น = **Disconnect** (เครื่องยังเปิด) · Sleep console เป็นปุ่มรอง · ธีมเดียวกับแอป — เดิม upstream focus ที่ Sleep กดจอยพลาดเครื่องหลับ
- 2026-10-04 ไอคอน system tray ขึ้นเสมอ (เดิมขึ้นเฉพาะเมื่อเปิด Hide To Tray ทำให้ผู้ใช้เห็นว่า "tray หาย")
- 2026-10-04 `scripts/deploy.ps1`: เขียน `qt.conf` ลง dist (Qt หา plugins/qml ข้าง exe ไม่ fallback ไป C:\msys64) + ฆ่า dist exe ที่ค้างก่อน deploy ทับ
- 2026-10-04 หน้าต่างหลักจำสถานะ maximize และไม่เอาขนาดตอน maximize ไปทับขนาดปกติ (เซฟหลังหยุดขยับ 400ms, key ใหม่ `settings/window_maximized`)
- 2026-10-04 จำขนาด/ตำแหน่งหน้าต่างตอนสตรีมที่ผู้ใช้ปรับ (เดิม upstream จำเฉพาะโหมด Adjust Manually) และ overlay จอยจำตำแหน่ง/ขนาดทุกครั้งที่ลากหรือย่อขยาย
- 2026-10-04 modal Controllers: ปุ่มจอย/คีย์ไม่หลุดไปสั่ง Play หรือปิด modal อีก (✕ △ □ d-pad ใช้ทดสอบเท่านั้น, ◯/Esc ปิด)
### Added
- 2026-10-06 **ลดเสียงรบกวน + ตัดเสียงลำโพงในหน้าทดสอบไมค์**: แถว Noise reduction / Speaker echo เลือก Off/Low/Medium/High (จอย: ↓ ลงมาจากรายการไมค์ แล้ว ←→) · มีผลทันทีทั้งในหน้าทดสอบและกลางสตรีม ไม่ต้องเริ่มสตรีมใหม่ (ใช้ค่า Speech Processing เดิมใน Settings) · ลดเสียงรบกวนด้วย **RNNoise** (dependency ใหม่ `rnnoise` ใน MSYS2 — ไม่มีก็ build ได้ ใช้ speex แทน) · โค้ดอยู่ `pswrapvoiceproc.{h,cpp}`
- 2026-10-06 ชิป **Recordings** ที่แถบล่างหน้าแรก กดแล้วเปิดโฟลเดอร์คลิปที่อัด
- 2026-10-05 `scripts/deploy.ps1 -StartMenu` สร้าง shortcut PS-WRAP ใน Start Menu (ระดับ user ไม่ต้อง admin)
- 2026-10-05 **คลิกที่ overlay (จอย/กล้อง/stats) = เข้าโหมดแก้ตัวนั้น** ไม่ต้องจำคีย์ลัด · คลิกอีกตัวสลับได้ทันที · คลิกที่ว่าง = จบ · การ์ด stats ลาก/ย่อขยายได้แล้วและจำตำแหน่ง (statsX/Y/Scale) · C++: เมาส์ในกรอบ overlay ส่งให้ QML ไม่เข้าเกม (`setOverlayHitRects`)
- 2026-10-05 Facecam effects **Jin mask (private)** / **Jin mask + headband (private)**: sprite ตัดจากภาพเกมที่ลูกพี่ส่ง (ลิขสิทธิ์ Sucker Punch/Sony — ใช้ส่วนตัวเท่านั้น ไฟล์ `fx/jin_*.png` ข้าง exe ไม่อยู่ใน repo asset) · เครื่องมือตัดชิ้นส่วน `cutpart.py` + polygon trace
- 2026-10-04 Facecam effect **Samurai armor (photo)**: sprite ภาพจริงจากภาพพิพิธภัณฑ์ CC0 (Commons: MAP Expo Sujibachi kabuto + menpo) ตัดพื้นหลังด้วย AI + เจาะช่องตา · โหลดจาก `fx/samurai_photo.png` ข้าง exe (เปลี่ยนภาพได้ไม่ต้อง build) · +Samurai armor (kabuto+mask) แบบวาด
- 2026-10-04 **Facecam effects v2 (LINE-grade)**: MediaPipe Face Landmarker 478 จุด (ONNX, Apache-2.0, 4.9 MB) ติดตามจากครอปของเฟรมก่อน + head pose 3 แกน → sticker หมุนแบบ 3D ตามหัน/เงย/เอียง, ยึดกับจุดจริง (หางตา/ปาก/สันจมูก/คาง), 1-Euro filter กันสั่น · เพิ่ม **Samurai mask**, **Ninja**, **Ghost (Tsushima)** · ไม่มี face_mesh.onnx = ถอยไปใช้ 6 จุดอัตโนมัติ
- 2026-10-04 dev: env `PSWRAP_FAKE_CAM=<ไฟล์วิดีโอ>` ใช้ไฟล์แทนกล้องเพื่อทดสอบ facecam/effects ซ้ำได้
- 2026-10-04 Facecam effects เพิ่ม: Mustache / Clown nose / Crown / Bane mask / Party (แว่น+หนวด+มงกุฎ) — F ในโหมดย้ายวน 7 โหมด
- 2026-10-04 **Facecam effect: แว่นตาดำ** (Settings › Facecam Effect › Sunglasses, หรือ F ในโหมดย้าย) — BlazeFace (ONNX, MediaPipe weights Apache-2.0) ติดตามตา 2 ข้างบน CPU, แว่น SVG วาดเอง ขยาย/หมุนตามหน้า, ถูก mask มุมโค้ง/วงกลมด้วย, ใช้ร่วมกับ AI ตัดพื้นหลังได้
- 2026-10-04 **Facecam: AI ตัดพื้นหลังโดยไม่ใช้ฉากเขียว** (Settings › Facecam Background › AI remove) — MediaPipe Selfie Segmentation (ONNX, Apache-2.0) บน ONNX Runtime (MIT) CPU ~5 ms/frame, โหลด DLL ตอนรัน ไม่มี = ปิดฟีเจอร์เงียบๆ · ไฟล์ `onnxruntime.dll` + `models/selfie_segmentation.onnx` ข้าง exe
- 2026-10-04 **Facecam รองรับกล้องเสมือน DirectShow** (NVIDIA Broadcast / OBS Virtual Camera / Streamlabs) ผ่าน libavdevice ของ ffmpeg ที่มีอยู่ — Qt WMF backend มองไม่เห็นกล้องพวกนี้
- 2026-10-04 Settings › General: **Facecam Preview** สด (เห็น mirror/shape/zoom/pan/chroma key ตามจริงขณะปรับ) + แถว Facecam Pan X/Y · กล้องเปิดเฉพาะตอนอยู่หน้า General
- 2026-10-04 **Single instance**: เปิดซ้ำจะดึงหน้าต่างเดิมขึ้นแทน (รวมคืนจาก tray/minimize) — แยกต่อโปรไฟล์ด้วย QLocalServer; CLI (list/stream/wakeup) ไม่กระทบ · ต้องมี Qt6Network ใน dist
- 2026-10-04 **Tray menu** ใหม่: Show · Controller overlay · Facecam · Network stats · Always on top (checkable + ● ON / ○ OFF) · Quit พร้อมไอคอน — toggle overlay ได้แม้ระหว่างสตรีม · ค่า pad/cam overlay ย้ายไปอยู่ C++ (`Chiaki.window.padOverlay/camOverlay/statsOverlay`, key เดิม)
- 2026-10-04 Facecam: zoom/pan (ครอปเข้าหน้า), chroma key ฉากเขียว/น้ำเงิน (shader `chroma.frag.qsb`), Settings Facecam Zoom / Background / Key Tolerance, คีย์ Z/X WASD G ในโหมดย้าย
- 2026-10-04 **Facecam overlay** (`WebcamOverlay.qml`, Qt Multimedia backend WMF): กล้องเว็บแคมทับวิดีโอขณะสตรีม มุมโค้ง/วงกลม, mirror, ลาก/ย่อขยาย/จำตำแหน่ง (โหมดย้าย: M = mirror, C = circle) · เมนู OVERLAY: Cam / Move cam · hotkey Ctrl+Shift+C เปิด-ปิด, Ctrl+Shift+V ย้าย · เลือกกล้อง/mirror/รูปทรงได้ที่ Settings › General (Facecam Camera/Mirror/Shape) และกด N ในโหมดย้ายเพื่อสลับกล้อง · Vulkan backend เท่านั้น
- 2026-10-04 การ์ด stats: แถว **rtt at connect** (RTT ที่ senkusha วัดตอนเชื่อมต่อ, `StreamSession.rttMs` additive) — ยังไม่ใช่ ping สด (ต้องแตะ lib)
- 2026-10-04 การ์ด network stats ขณะสตรีม: ไอคอนต่อแถว (bitrate/queue/latency/loss/dropped/lost, `gui/res/stats/*.svg`), ชื่อชิดซ้ายสีจาง ค่าชิดขวา พื้น/ขอบตาม Theme · เมนูสตรีม: ป้ายกลุ่มอยู่ในแคปซูล สูงเท่ากันทุกกลุ่ม
- 2026-10-04 **เมนูสตรีม v3 "control deck"**: แถวตัวเลือกเป็นกลุ่ม segmented (FIT / QUALITY / Display / OVERLAY) ห่อบรรทัดอัตโนมัติเมื่อจอแคบ (ไม่มีเลื่อนแนวนอน) · stats บรรทัดเดียว · แถว hint hotkey · ความสูงเมนูตามเนื้อหา — ทดสอบจริง 1100/1554/1920
- 2026-10-04 hotkey **Ctrl+Shift+S** สลับกล่อง network stats ขณะสตรีม (เดิมต้องเปิดเมนู Ctrl+O → Stats) · ชุด hotkey ตอนสตรีม: Ctrl+O เมนู, Ctrl+Shift+O overlay จอย, Ctrl+Shift+E ย้าย/ย่อ overlay, Ctrl+Shift+S stats
- 2026-10-04 Settings › Keys ใหม่ (`SettingsKeysPage.qml`): 26 ปุ่มจัดเป็น 6 การ์ดตามหมวด (Face / D-Pad / Shoulders & triggers / System / Left stick / Right stick) คอลัมน์ย่อ/ขยายตามความกว้าง ไม่ล้นจอเล็ก, ปุ่มคีย์เป็น chip, นำทางจอย ↑↓←→ ข้ามการ์ดได้
- 2026-10-04 Settings ทั้ง 9 หน้า: ชื่อ setting ชิดซ้ายคอลัมน์คงที่ (ตัดโคลอน), ค่า default เป็นตัวเล็กสีจาง, การ์ดพื้นหลังจัดกลุ่มต่อหน้า, ระยะแถวสม่ำเสมอ — กระชับและอ่านง่ายขึ้น
- 2026-10-04 **Hide to tray** (ปุ่ม To tray, ปิดด้วย X เมื่อเปิดใน Settings, tray icon มีเมนู Show/Always on top/Quit) และ **Always on top** (ปุ่ม pin ใน header, Settings › General, จำค่า) — key `settings/pswrap_hide_to_tray`, `pswrap_always_on_top`
- 2026-10-04 รูปเครื่อง PS5/PS4 ใหม่ตามรุ่น (`ConsoleArt.qml` + `gui/res/console/*`): ไฟสถานะเต้น/หายใจตาม ready/standby, glow, ลอยขึ้นเมื่อเลือก
- 2026-10-04 เมนูสตรีม v2: 2 แถว (End Stream · Mic · Volume / FIT · QUALITY · Display · OVERLAY) มีไอคอน แถวล่างเลื่อนแนวนอนได้ ไม่ล้นจอเล็ก
- 2026-10-04 overlay: โหมดย้าย/ย่อขยาย (เมาส์/คีย์/จอย) จำตำแหน่ง, ซ่อน/แสดงด้วยเมนูหรือ Ctrl+Shift+O, ปุ่ม Stats ซ่อน/แสดงกล่อง network ในเมนูสตรีม
- 2026-10-04 **Controller overlay ขณะสตรีม** พอร์ตจาก BudToZaiDualSenseTracker (SVG DualSense ปุ่มกดสว่าง สติ๊กเลื่อน L2/R2 ตามแรงกด) เปิด/ปิดจากเมนูสตรีม + preview สดใน modal Controllers · เมนูสตรีมรวมเป็น `StreamMenuContent` ใช้ทั้ง Vulkan/OpenGL
- 2026-10-04 **Responsive**: ฟอนต์/ความกว้าง control ใน Settings และ sub-dialog ย่อตามหน้าต่าง (ไม่ล้นที่ 1180–1280 px) · ไอคอนจอยตามรุ่น (Kenney Input Prompts CC0: PS4/PS5, badge EDGE)
- 2026-10-04 สถานะ controller: chip ไอคอนจอยในแถบล่าง + popup รายละเอียด (ชนิด/ชื่อ/VID:PID/GUID/แบตหรือ USB/ปุ่ม Map Buttons) — เพิ่ม `name/type/vidpid/guid/powerLevel()` ใน `QmlController`, ไอคอน `gui/res/controller.svg`
- 2026-10-04 **Phase 3 Settings**: sidebar แนวตั้งจัดกลุ่ม Basics/Advanced แทน tab แนวนอน, header ใหม่ (Back/Title/subtitle), เนื้อหาชิดซ้าย · `DialogView` chrome ใหม่ให้ทุก sub-dialog · `AutoConnectView` ใหม่
- 2026-10-04 **Phase 4 Stream menu**: `StreamMenuWindow` ปุ่ม pill จัดกลุ่ม FIT/QUALITY, ปุ่ม End Stream, stats ตาม Theme (คง navigation จอย)
- 2026-10-04 **Branding**: ชื่อแอป PS-WRAP (window title, display name, installer) + ไอคอนใหม่ (`gui/res/pswrap.svg`, `gui/pswrap.ico/.png`) · settings ยังอยู่ที่เดิม (`HKCU\Software\Chiaki`) ใช้ config เก่าต่อได้
- 2026-10-04 **Phase 2 หน้าหลัก**: `MainView.qml` ใหม่ — console card พร้อม status chip (ready/standby/remote), ปุ่ม Play/Register, hint bar ปุ่มจอย, empty state, setup banner แบบไม่ modal แทน dialog Steam/PSN ที่เด้งซ้อนกัน, responsive สำหรับ Steam Deck
- 2026-10-04 **Phase 1 design system**: `controls/Theme.qml` tokens singleton, `FocusRing.qml`, restyle `controls/*` (focus ring ชัดสำหรับจอย/TV), root Material palette ผูก Theme → ToolBar/พื้นหลังเปลี่ยนเป็น slate เข้ม
- 2026-10-04 clone upstream `a9a2805` (v1.9.9) เป็น `chiaki-ng/` branch `ps-wrap/ui`, เอกสารชุดใหม่, design inventory, ADR-0001..0003
