# Changelog (ภาษาไทย)

[English](CHANGELOG.md) · ภาษาไทย

รูปแบบตาม [Keep a Changelog](https://keepachangelog.com/) · เวอร์ชันของ PS-WRAP เอง (0.x = pre-release) · ฐาน upstream: chiaki-ng `a9a2805`

## [Unreleased]

## [0.5.0] — 2026-10-09
สถานะบน Discord, fps ในการ์ด Stats, เครื่องมือวินิจฉัย PS-WRAP-Diagnostics และแก้ Steam shortcut
### Added
- 2026-10-09 **สถานะบน Discord** (เหมือนเกมทั่วไป): โปรไฟล์ Discord ขึ้น *Playing PS-WRAP* พร้อมชื่อเกมที่เครื่องกำลังรัน, PS5/PS4 Remote Play, Live หรือ Recording, เวลาเล่น และปุ่ม *Get PS-WRAP* · คุยกับแอป Discord ในเครื่องตรงๆ (ไม่มี DLL เพิ่ม) ไม่ได้เปิด Discord = ไม่มีผล · ระหว่างสตรีมถามชื่อเกมจากเครื่องทุก 30 วินาที (แพ็กเก็ตเล็กๆ ไปเครื่องนั้นเครื่องเดียว) · Settings › General: *Discord Status* และ *Show Game On Discord*
- 2026-10-09 **fps ในการ์ด Stats** — *fps (stream)* = เฟรมที่เครื่องส่งมาต่อวินาที · *fps on screen* = ภาพใหม่ที่ขึ้นจอต่อวินาที (เปิด Frame Gen จะขึ้นราว 120 มีป้าย "· FG") · ไม่นับการวาดซ้ำเพราะ overlay ขยับ
- 2026-10-09 **PS-WRAP-Diagnostics.exe** (อยู่ข้าง PS-WRAP.exe) เครื่องมือหาสาเหตุเวลา PS-WRAP มีปัญหา เปิดได้แม้ PS-WRAP เปิดไม่ขึ้น · ตรวจไฟล์ของ PS-WRAP, Windows, ไดรเวอร์การ์ดจอ, Vulkan ใช้ได้หรือค้าง (ทดสอบใน process แยก จำกัด 20 วิ), แอป overlay ที่เกาะเกม, crash/ค้างล่าสุดจาก Windows Error Reporting และ Event Log, log ของ PS-WRAP · **Test launch** เปิด PS-WRAP แล้วบันทึก output (เลือก verbose ได้) ถ้าแอปไม่ตอบสนองจะเก็บจุดที่ทุก thread ค้างอยู่ + dump เล็กๆ · **Test with OpenGL (safe mode)** แบบเดียวกันแต่ไม่ใช้ Vulkan · **Use OpenGL (fix freeze)** ให้ PS-WRAP เปิดด้วย OpenGL ตลอด · **Save report (.zip)** / **Copy summary** (ยาวไม่เกิน 1 ข้อความ Discord) / **Report on GitHub** ส่งผลได้ง่าย · ไม่มีข้อมูลล็อกอิน PSN, stream key, การลงทะเบียนเครื่อง, ชื่อช่อง · ชื่อ user/ชื่อเครื่อง/โฟลเดอร์ user/IP อินเทอร์เน็ต ถูกแทนด้วยคำกลางๆ
### Fixed
- 2026-10-09 **Create Steam Shortcut** บันทึกผิดที่ (`userdata//config/shortcuts.vdf`) บน Steam รุ่นใหม่ที่ไม่ติดป้าย *MostRecent* ให้ผู้ใช้แล้ว shortcut เลยไม่ขึ้นใน Steam · ตอนนี้หาผู้ใช้จาก *AutoLogin* ด้วย (cpp-steam-tools `94a31a3` จาก upstream chiaki-ng `6837fedc`, chiaki-ng#813)

## [0.4.0] — 2026-10-09
Frame Gen (60 → 120 fps), Glow, ไฟจอยบนจอ และ Stack จัด overlay ทีเดียวทั้งชุด
### Added
- 2026-10-09 **Frame Gen (เมนูสตรีม › QUALITY)** สร้างเฟรมกลางระหว่างเฟรมจริงทุกเฟรม สตรีม 60 fps เล่นได้ 120 fps บนจอที่เร็วกว่าสตรีม (ต้อง Frame Delivery = Direct Mapping + Vulkan ซึ่งเป็นค่าเริ่มต้น) · หา motion บน GPU · ส่วนนิ่งเช่น HUD คมเหมือนเดิม ส่วนที่จับคู่ไม่ได้ใช้ภาพฝั่งเดียวแทนภาพซ้อน · หน่วงเพิ่มราวครึ่งเฟรม (~8 ms ที่ 60 fps) · คลิป/ภาพหน้าจอ/9:16 ยังใช้เฟรมจริง · ค่าเริ่มต้นปิด
- 2026-10-09 **Glow (เมนูสตรีม › FIT)** แถบว่างรอบภาพ (จอกว้าง / หน้าต่างไม่ใช่ 16:9) เป็นแสงเบลอหรี่ๆ จากขอบเกมแทนสีดำ · ค่าเริ่มต้นปิด
- 2026-10-09 **Light (เมนูสตรีม › OVERLAY)** แสงเรืองรอบขอบภาพตามสีไฟจอยที่เกมสั่ง กระพริบสว่างแวบหนึ่งเมื่อสีเปลี่ยน · สีเดียวกันขึ้นที่แถบไฟบนรูปจอย (Pad) ด้วย · ค่าเริ่มต้นปิด
- 2026-10-09 **Stack (เมนูสตรีม › OVERLAY)** overlay ที่เปิดอยู่ทั้งหมด (นาฬิกา ไมค์ stats แชท facecam จอย) เรียงเป็นคอลัมน์เดียว กว้างเท่ากัน ระยะห่างเท่ากัน ชิดขอบซ้ายหรือขวาของภาพ · **Arrange** (หรือคลิก overlay ตัวไหนก็ได้) แก้ทั้งคอลัมน์ทีเดียว: ลากขึ้นลง, ลากข้ามกลางภาพ = สลับข้าง, ◢ หรือ L1/R1 = ย่อขยายทุกตัวพร้อมกัน, ▲▼ = สลับลำดับ · คอลัมน์หดเองไม่ล้นจอ · ปิด Stack แล้วทุกตัวกลับตำแหน่งเดิมที่วางไว้

## [0.3.3] — 2026-10-08
การ์ดแชทในภาพแนวตั้ง 9:16 เต็มพื้นที่ได้ทุกสัดส่วน
### Changed
- 2026-10-08 **การ์ดแชทในภาพแนวตั้ง 9:16 วาดเองตามขนาดพื้นที่** (เดิมตัดมาจากการ์ดบนจอเกม สัดส่วนตายตัว) · ค่าเริ่มต้นเต็มพื้นที่ว่างใต้เกม (Blur fill) ปรับได้ทุกสัดส่วน: scroll ย่อขยาย, **Shift+scroll** ปรับความสูงอย่างเดียว, ลากย้าย, ดับเบิลคลิก / คลิกขวา › Fill the free area คืนค่า · ตัวหนังสือคมทุกขนาด · ไม่หายตอนเปิดเมนูสตรีม · ปิดการ์ดแชทบนจอเกมได้ แต่ยังโชว์แชทในภาพแนวตั้ง

## [0.3.2] — 2026-10-07
Go Live ส่งได้เต็มความเร็ว (ไลฟ์ 4K ใช้ได้จริง)
### Fixed
- 2026-10-07 **Go Live ส่งได้ไม่เกิน ~12–15 Mbps** แม้เน็ตเร็ว → ไลฟ์ 4K (และ 1440p bitrate สูง) ข้ามภาพ "slow upload" · สาเหตุ: send buffer ของ socket เป็นค่าเริ่มต้นของ Windows (~64 KB) จำกัดการเชื่อมต่อเดียวที่ buffer ÷ ping (≈ 15 Mbps ที่ 33 ms ไป YouTube) · ตอนนี้ 4 MB — ไลฟ์ 4K 51 Mbps ขึ้น YouTube ส่งได้เต็ม

## [0.3.1] — 2026-10-07
รูป / GIF / แชทไลฟ์ในภาพแนวตั้ง 9:16 + แก้บั๊ก
### Added
- 2026-10-07 **รูป / GIF / แชทไลฟ์ ในภาพแนวตั้ง 9:16**: หน้าต่าง 9:16 ปุ่ม **＋ Image** ใส่ PNG / JPG / GIF เคลื่อนไหว / SVG (แบนเนอร์ โลโก้ ฯลฯ) จากไฟล์ หรือวางลิงก์ เช่น Giphy (แอปดาวน์โหลดให้ ไม่เกิน 50 MB) ลากย้าย, scroll ย่อขยาย, คลิกขวา = หน้าสุด/หลังสุด/ความโปร่ง/ลบ · สูงสุด 8 รูปต่อเลย์เอาต์ จำแยกต่อเลย์เอาต์ · คัดลอกไฟล์ไปโฟลเดอร์ `overlays` ของแอป (ย้ายไฟล์ต้นฉบับได้) · ปุ่ม **Chat** ใส่การ์ดแชทลงภาพแนวตั้ง (ลาก/scroll เหมือน facecam) · ติดทั้ง preview, Record 9:16 และ Go Live แนวตั้ง ไม่บังจอเกมตอนเล่น

### Changed
- 2026-10-07 ขอบ facecam เป็นแบบเดียวกับการ์ด mic / stats / นาฬิกา / แชท: เส้นบาง 1px โปร่ง + เส้น accent ขอบล่าง (เดิมขอบขาวทึบ 2px)

### Fixed
- 2026-10-07 **คลิป/ไลฟ์แนวตั้ง 9:16 เลย์เอาต์ Blur fill พื้นหลังเป็นสีเขียว** (หน้าต่าง preview ปกติ) — libplacebo วาดขอบเบลอ (PL_CLEAR_BLUR) เป็นค่า RGB ลงทุก plane ตรงๆ ไม่แปลงเป็น YCbCr ลงไฟล์ NV12 แล้วสีเพี้ยน · ตอนนี้ Blur fill วาดลง texture RGBA ก่อนแล้วค่อยแปลงเป็น NV12 อีกรอบ

## [0.3.0] — 2026-10-07
4K, แนวตั้ง 9:16, แชทไลฟ์บนจอ, แก้ปุ่มค้าง, หน้า About

### Added
- 2026-10-07 **หน้า About / เครดิตในแอป** (ชิป ⓘ ที่แถบล่างหน้าแรก, Settings › Config › About PS-WRAP): บอกชัดว่า PS-WRAP พัฒนาต่อจาก chiaki-ng (Street Pea และผู้ร่วมพัฒนา) ซึ่งมาจาก Chiaki (Florian Märkl) · รายชื่อ library/โมเดล/ภาพที่ใช้พร้อม license · AGPL-3.0 + ลิงก์ source · ปุ่ม Licenses เปิด THIRD-PARTY-NOTICES.txt (ในแพ็กเกจ release)
- 2026-10-07 **แชทไลฟ์บนจอ** (Ctrl+Shift+H / ปุ่ม Chat ในเมนูสตรีม / tray): ดึงแชท YouTube (ใส่ @handle / channel / ลิงก์ไลฟ์ + YouTube Data API key) และ Twitch (ชื่อช่อง ไม่ต้อง login) มาโชว์เป็นการ์ดบนจอ ลาก/ย่อขยาย/จำตำแหน่งได้ ติดไปในคลิป/ไลฟ์แนวนอนด้วย · ตั้งค่าที่ Settings › Go Live › Chat on screen · API key เก็บใน Windows Credential Manager
- 2026-10-07 **ไลฟ์แนวตั้ง 9:16**: ปลายทาง Go Live แต่ละอันเลือก "Vertical 9:16" ได้ (TikTok / Instagram เปิดไว้เป็นค่าเริ่มต้น) → ส่งภาพ 1080 × 1920 ตามเลย์เอาต์ในหน้าต่าง 9:16 · ไลฟ์แนวนอนกับแนวตั้งพร้อมกันได้ (เช่น YouTube + TikTok) · ปลายทางแนวตั้งล้วนใช้กับสตรีม HDR ได้ (tone-map เป็น SDR)
- 2026-10-07 **อัดคลิปแนวตั้ง 9:16** (ปุ่ม ● Record 9:16 ในหน้าต่าง 9:16 / tray): 1080 × 1920 60fps อัดพร้อมคลิปปกติได้ · จุด REC บนจอมีป้าย "9:16"
- 2026-10-07 หน้าต่าง 9:16: **ลาก facecam** เพื่อย้าย, scroll เพื่อย่อ/ขยาย, ดับเบิลคลิกคืนค่า (จำแยกต่อเลย์เอาต์) · **ลากภาพเกมซ้าย-ขวา** เพื่อเลื่อนตำแหน่งตัด · ปุ่ม Reset cam
- 2026-10-07 **Preview ภาพแนวตั้ง 9:16** (ปุ่ม 9:16 ในเมนูสตรีม / tray): หน้าต่างแยกโชว์ภาพแนวตั้งสดสำหรับ Shorts/TikTok/Reels เลือกได้ 3 เลย์เอาต์ — Cam + game (facecam บน เกมล่าง), Center crop (เกมเต็มจอ + facecam มุมบน, เลื่อนตำแหน่งตัดได้), Blur fill (เกมเต็มภาพกลางจอ พื้นหลังเบลอ) · facecam ตัดจาก overlay บนจอ
- 2026-10-07 tray **Record preset ▸** (Same as stream / 1440p / 4K) — ค่าเดียวกับ Output Resolution · จุด REC กระพริบบนจอมีป้าย **4K** / **1440p** ต่อท้ายเมื่ออัดแบบ upscale (บนจอเท่านั้น ไม่ติดไฟล์)
- 2026-10-07 **เมนู tray**: Stream menu (เปิดเมนูสตรีมได้โดยไม่ต้องกด L1+R1+L3+R3 / Ctrl+O), Settings (จากหน้าแรก), Picture size ▸ (720p–1440p / Fullscreen เหมือนปุ่ม Size)
- 2026-10-06 **Output Resolution: Same as stream / 1440p / 4K (upscaled)** (Settings › General) ใช้ร่วมกันทั้งคลิป, Instant Replay และ Go Live · ขยายด้วย upscaler ตามปุ่ม QUALITY (HQ + Spatial = FSRCNNX) · Go Live: YouTube/Custom ส่งได้ถึง 4K (bitrate สูงสุด 51 Mbps) แพลตฟอร์มอื่นย่อเป็น 1080p ให้เอง
### Changed
- 2026-10-07 แอปแสดงเลขเวอร์ชันของ PS-WRAP เอง (0.3.0) แทนเลขของ upstream (1.10.0) · หน้า About เดิมขึ้น "About PS-WRAP-ng" และบอกว่าเป็น chiaki-ng → แทนด้วยหน้า About ใหม่
- 2026-10-06 **คลิป/Replay/ไลฟ์ จับเฉพาะกรอบวิดีโอ 16:9** (เรนเดอร์จากเฟรมสตรีมตรงๆ) — ขนาดหรือสัดส่วนหน้าต่าง/จอ ultrawide ไม่มีผลอีกแล้ว ไม่มีขอบดำของหน้าต่างติดไปในไฟล์ · ความละเอียดตั้งต้น = ความละเอียดสตรีม (เดิมผูกกับความสูงหน้าต่าง)
### Fixed
- 2026-10-07 **UI ซ้อน 2 ชุดหลังเรียกหน้าต่างกลับ** (tray: Show PS-WRAP / คลิก icon / Picture size / Stream menu / Settings, เปิดแอปซ้ำ): ระหว่างสตรีมได้หน้าเกม 2 ชุด — facecam ขึ้น "Camera is busy", overlay ไมค์ซ้อน, หน้าต่าง 9:16 ซ้ำ · สาเหตุ restoreFromTray เรียก show() ที่สร้าง UI ใหม่ทั้งชุด แทนการแค่แสดงหน้าต่าง
- 2026-10-07 **ปุ่มค้างกดอยู่เองบน PS5 จังหวะต่อสู้ (แถว L1/R1/L2/R2)**: การกด/ปล่อยปุ่มส่งผ่าน UDP ครั้งเดียว ถ้าแพ็กเก็ต "ปล่อยปุ่ม" หายตอนเน็ตหลุด PS5 จะไม่รู้ว่าปล่อย · ตอนนี้ส่งซ้ำอัตโนมัติ (+20/50/100/200 ms) และพก event ย้อนหลัง 8 ตัวแทน 4 (แก้ใน lib — ADR-0004)
- 2026-10-07 tray › Facecam effect / Facecam background ไม่บอกว่าเลือกตัวไหนอยู่ (stylesheet ซ่อน indicator) → มี ✓ หน้าตัวที่เลือก

## [0.2.0] — 2026-10-06
อัดคลิป, Instant Replay, ภาพหน้าจอ, Go Live, game presets

### Added
- 2026-10-06 **ชิป Screenshots** แถบล่างหน้าแรก: เปิด Explorer พร้อมเลือกภาพหน้าจอล่าสุดให้
- 2026-10-06 **ขนาดภาพ (Size ▾) ในเมนูสตรีม**: เลือก 720p / 900p / 1080p / 1440p / Fullscreen → พื้นที่ภาพเป็น 16:9 พอดีเป๊ะ (pixel จริง แม้จอ scale 150%) ไม่มีขอบดำ คลิปอัด/ภาพหน้าจอได้ขนาดตามชื่อ · ป้าย STREAM บอกขนาดที่ตรงกับความละเอียดสตรีม · ซ่อนขนาดที่ใหญ่กว่าจอ · ใช้จอยเลือกได้
- 2026-10-06 **Instant Replay**: เก็บ 30–120 วินาทีล่าสุดในหน่วยความจำ กด Ctrl+Shift+B / ปุ่ม Save ในเมนูสตรีม / tray เพื่อเซฟย้อนหลัง · ใช้ encoder ชุดเดียวกับการอัด (อัดระหว่าง replay ได้)
- 2026-10-06 **Marker ระหว่างอัด** (Ctrl+Shift+K) → ใส่เป็น chapter ในไฟล์ (VLC กระโดดไปจุดที่ปักได้) · คลิปที่ไม่มี marker ยังเป็น MP4 แบบกันแอปตายเหมือนเดิม
- 2026-10-06 **Screenshot ปุ่มเดียว** (F12 / Ctrl+Shift+P / เมนูสตรีม / tray): ภาพเต็มความละเอียดหน้าต่าง ติด overlay ทั้งหมด · สตรีม HDR ได้ PNG SDR + `HDR.png` (PQ/BT.2020)
- 2026-10-06 **Mic boost (−12…+24 dB) + noise gate** ต่อท้ายตัวลดเสียงรบกวน มีผลทั้งเสียงเข้า PS5 แทร็ก Mic ในคลิป และ spectrum · ปรับในหน้าทดสอบไมค์
- 2026-10-06 **overlay นาฬิกา / เวลาเล่น** (Ctrl+Shift+T) ลาก/ย่อขยาย/จำตำแหน่งได้เหมือน overlay อื่น
- 2026-10-06 **Game presets**: Settings › Game presets ตั้ง preset ต่อเกม (ความละเอียด/fps/bitrate, overlay, เอฟเฟกต์กล้อง, Instant Replay) ใช้อัตโนมัติตอนเริ่มสตรีม คืนค่าเดิมตอนจบ
- 2026-10-06 **Go Live** (Settings › Go Live, Ctrl+Shift+L, เมนูสตรีม, tray): ไลฟ์ H.264 1080p60 ไป YouTube / Twitch / Facebook / Kick / Custom RTMP(S) พร้อมกัน · ต่อใหม่เองเมื่อหลุด, ปลายทางช้าไม่ถ่วงที่อื่น · stream key เก็บใน Windows Credential Manager · ยังไม่มี 9:16 (TikTok) และ YouTube HDR
- 2026-10-06 **อัดคลิป** (Ctrl+Shift+R / เมนูสตรีม / tray): ภาพบนจอ + overlay ทุกตัว (NVENC, SDR H.264 / HDR HEVC 10-bit) + เสียง 3 แทร็ก Game+Mic / Game / Mic · ถ้า Windows บล็อกโฟลเดอร์ Videos จะเซฟไป `%USERPROFILE%\PS-WRAP Recordings` แทนพร้อมแจ้ง
- 2026-10-06 mic visualizer overlay (คลิกวงไมค์ = mute/unmute), หน้าทดสอบไมค์/กล้อง (คลิกชิปหน้าแรก), เลือกไมค์ได้ทั้ง Settings / เมนูสตรีม / tray, เมนู tray: mute, เลือกไมค์, เอฟเฟกต์/พื้นหลังกล้อง
- 2026-10-06 `scripts/smoke.ps1` smoke test อัตโนมัติก่อน deploy
### Fixed
- 2026-10-06 ชิป Recordings เปิดโฟลเดอร์ที่ไม่มีคลิปล่าสุด เมื่อ Windows "Controlled folder access" บล็อก Videos จนไฟล์ไปตกโฟลเดอร์สำรอง `~\PS-WRAP Recordings` → ตอนนี้หาไฟล์ล่าสุดจากทั้งสองโฟลเดอร์แล้วเลือกให้ใน Explorer
- 2026-10-06 ปิดกล้อง (ปิดหน้าทดสอบกล้อง / ออกจาก Settings › General) แล้ว UI ค้าง ~3 วินาที → ปิดอุปกรณ์เบื้องหลัง · หน้าทดสอบกล้องบอก "No video from camera" เมื่อไม่มีภาพ (เดิมขึ้น Live หลอก) · ออกจากแอปรอปล่อยกล้องให้เรียบร้อย
- 2026-10-06 หน้าแรกล้นจอเล็ก: การ์ดเครื่อง (ปุ่ม Play หลุดขอบ) และแถบล่าง (ชิปเหลือไอคอนเมื่อแคบ)

## [0.1.1] — 2026-10-06
### Changed
- 2026-10-06 **ไฟล์โปรแกรมชื่อ `PS-WRAP.exe`** (เดิม `chiaki.exe`) — ตั้ง `OUTPUT_NAME` ใน CMake เฉพาะ Windows (target ยังชื่อ chiaki) · สคริปต์ build/run/deploy/drive/snap หา process ทั้ง `PS-WRAP` และ `chiaki` · shortcut ต้องสร้างใหม่ด้วย `deploy.ps1 -Shortcut -StartMenu` · ถ้าเคยอนุญาต `chiaki.exe` ใน Firewall/Controlled folder access ต้องอนุญาต `PS-WRAP.exe` ใหม่

## [0.1.0] — 2026-10-06
รุ่นแรก (pre-release) — UI ใหม่ทั้งชุดบนแกน chiaki-ng

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

[0.3.3]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.3.3
[0.3.2]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.3.2
[0.3.1]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.3.1
[0.3.0]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.3.0
[0.2.0]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.2.0
[0.1.1]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.1.1
[0.1.0]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.1.0
