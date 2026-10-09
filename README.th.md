# PS-WRAP

[English](README.md) · ภาษาไทย

PS4 / PS5 Remote Play client สำหรับ Windows ที่ออกแบบ UI ใหม่ทั้งหมด ให้ทันสมัยและเล่นด้วยจอยอย่างเดียวได้
พัฒนาต่อจาก [chiaki-ng](https://github.com/streetpea/chiaki-ng) โดยทีม BudToZai — แกน streaming (`ps-wrap/lib/`) คงเดิมจาก upstream

▶️ **Demo stream:** [youtube.com/live/8sWkgmj81Aw](https://www.youtube.com/live/8sWkgmj81Aw)

![เล่นพร้อมเปิดหน้าต่าง 9:16](screenshots/stream-916.jpg)

*เล่นพร้อมเปิดหน้าต่าง 9:16 — overlay บนจอเกม และข้างๆ เป็นภาพแนวตั้งสำหรับ Shorts / TikTok / Reels มีรูปด้านบน + facecam ด้านล่าง (เลย์เอาต์ Blur fill)*

## ภาพหน้าจอ

### หน้าแรก

![หน้าหลัก](screenshots/main.png)

- การ์ดเครื่องละใบ: รูป PS5 / PS4 ตามรุ่น, ชิปสถานะ (Ready / Standby / Remote) และปุ่ม **Play** ใหญ่
- แถบล่าง: ซ้ายเป็นคำใบ้ปุ่มจอย ขวาเป็นชิปสถานะ — ไมค์, กล้อง, จอย, **Recordings**, **Screenshots**, Discovery และ **ⓘ About** คลิกชิปเพื่อเปิดหน้านั้น
- ใช้จอยได้ทั้งหน้า: ✕ Play, △ ปลุกเครื่อง, □ ซ่อน, L1 ใส่ PIN, R3 เพิ่มเครื่อง, ☰ Settings

| ⓘ About และเครดิต | สถานะจอย | ทดสอบไมค์ |
|---|---|---|
| ![About](screenshots/about.png) | ![Controllers](screenshots/controllers.png) | ![ทดสอบไมค์](screenshots/mic-test.png) |
| เวอร์ชัน, เครดิต chiaki-ng และ Chiaki, library ทุกตัวพร้อม license และปุ่ม **Licenses** | จอยที่ต่ออยู่ ชนิดการต่อ/แบต และภาพจอยที่สว่างตามปุ่มที่กดจริง | เลือกไมค์ ปรับลดเสียงรบกวน ตัดเสียงลำโพง mic boost และ noise gate พร้อมดูผลสด |

### ระหว่างสตรีม

![สตรีมพร้อม overlay](screenshots/stream-overlay.jpg)

เกมพร้อม overlay ครบทุกตัว ลากย้าย/ย่อขยายได้ทุกตัว (คลิกที่ overlay เพื่อแก้ หรือใช้ปุ่ม **Move …** ในเมนูสตรีมด้วยจอย)

- **แชทไลฟ์** (ซ้าย): ข้อความ YouTube และ Twitch พร้อมสีชื่อและป้าย
- **นาฬิกา + เวลาเล่น** (ขวาบน)
- **mic visualizer**: บอกว่าปิดไมค์อยู่หรือกำลังพูด คลิกเพื่อ mute
- **network stats**: bitrate, ping ตอนเชื่อมต่อ, packet loss, เฟรมที่ทิ้ง/หาย
- **overlay จอย** (ขวาล่าง): ปุ่มสว่างและสติ๊กขยับตามที่เล่น

![เมนูสตรีม](screenshots/stream-menu.jpg)

เมนูสตรีม (Ctrl+O, L1+R1+L3+R3 หรือจาก tray) เรียงเป็นแถว:

1. **End Stream**, **Record**, เปิด/ปิดไมค์ + เลือกไมค์, เสียง และสถิติสตรีมสด
2. **FIT** (Zoom / Stretch / **Size ▾** ขนาดหน้าต่าง 16:9 พอดี), **QUALITY** (Default → HQ + Spatial upscale) และ Display
3. **OVERLAY**: Pad, Cam, Spectrum, Clock, Chat, Stats และปุ่ม **Move** ของแต่ละตัว
4. **CAPTURE**: Instant Replay (เปิด/ปิด, ความยาว, Save), Screenshot, **Live** และ **9:16**
5. รายการคีย์ลัด

![ขนาดภาพ](screenshots/picture-size.jpg)

**Size ▾** ตั้งหน้าต่างให้พื้นที่ภาพเป็น 720p, 900p, 1080p หรือ 1440p พอดี (16:9 ไม่มีขอบดำ pixel จริงแม้จอ scale 150%) ป้าย **STREAM** บอกขนาดที่ตรงกับความละเอียดสตรีม และซ่อนขนาดที่ใหญ่กว่าจอ

### แนวตั้ง 9:16 และ Go Live

| หน้าต่าง 9:16 | Settings › Go Live + แชทบนจอ |
|---|---|
| ![หน้าต่าง 9:16](screenshots/vertical-916.png) | ![ตั้งค่า Go Live](screenshots/go-live.png) |
| ภาพแนวตั้งสดสำหรับ Shorts / TikTok / Reels (ในภาพเป็น Blur fill + แบนเนอร์, GIF "LIVE" และการ์ดแชท) เลือก Cam + game, Center crop หรือ Blur fill · ลาก facecam, ตำแหน่งตัด, รูปและการ์ดแชทได้ · ใส่รูป PNG/JPG/GIF จากไฟล์หรือลิงก์ (Giphy) · อัดคลิป 1080 × 1920 | เพิ่มปลายทาง YouTube, Twitch, Facebook, Kick หรือ Custom RTMP(S) ตั้ง bitrate แยกกัน ติ๊ก Vertical 9:16 ได้ทีละปลายทาง และตั้งแชทบนจอ (แหล่ง YouTube, API key, ช่อง Twitch) · key เก็บใน Windows Credential Manager |

![อัดคลิปแนวตั้ง](screenshots/recording-916.jpg)

อัดคลิป 9:16 ระหว่างเล่น: ปุ่มเปลี่ยนเป็น **■ Stop** พร้อมเวลา และมีจุด REC กระพริบป้าย **9:16** ที่มุมซ้ายบนของจอเกม (บนจอเท่านั้น ไม่ติดในไฟล์)

### Settings

| Video | Stream |
|---|---|
| ![Settings › Video](screenshots/settings-video.png) | ![Settings › Stream](screenshots/settings-stream.png) |
| ตัวถอดรหัส, ชนิดหน้าต่าง, render preset (ถึง HQ + Spatial upscale), frame delivery ฯลฯ | ความละเอียด, fps, bitrate และ codec แยก PS5 / PS4 และเล่นในบ้าน (local) / นอกบ้าน (remote) |

![Settings › Keys](screenshots/settings-keys.png)

Settings › Keys: ผูกปุ่ม PlayStation ทุกปุ่มกับคีย์บอร์ด จัดเป็นการ์ดตามหมวด (ปุ่มหน้า, D-pad, ไหล่/ไก, ระบบ, สติ๊ก) ใช้จอยเลื่อนได้ทั้งหน้า · sidebar จัดหน้าเป็น Basics, Advanced, Games (game presets) และ Broadcast (Go Live)

### เมนู tray และ Disconnect

| เมนู system tray | Disconnect |
|---|---|
| ![Tray menu](screenshots/tray-menu.png) | ![Disconnect dialog](screenshots/disconnect.png) |
| เปิดเมนูสตรีมหรือ Settings, ตั้งขนาดภาพ, เปิดหน้าต่าง 9:16, อัดคลิป, เปิด/ปิด overlay, เลือกเอฟเฟกต์/พื้นหลัง facecam, เปลี่ยนไมค์, record preset, Instant Replay, ภาพหน้าจอ, Go Live และ always on top ได้โดยไม่ต้องแตะหน้าต่างเกม | ตอนจบสตรีมจะถามว่าจะให้เครื่องเปิดอยู่ไหม ปุ่มเริ่มต้นคือ **Disconnect** กดจอยพลาดก็ไม่ทำให้ PS5 หลับ |

## ดาวน์โหลด

โหลด zip ล่าสุดที่ [Releases](https://github.com/iDevGIS/PS-WRAP/releases) แตกไฟล์ไว้ที่ไหนก็ได้ แล้วเปิด `PS-WRAP.exe` — ไม่ต้องติดตั้งอะไรเพิ่ม
ถ้าเคยใช้ chiaki-ng ในเครื่องนี้ PS-WRAP จะคัดลอก settings และเครื่องที่ลงทะเบียนไว้มาให้ตอนเปิดครั้งแรก (ของ chiaki-ng ไม่ถูกแตะ)

## ค่าที่ทดสอบแล้วดีที่สุด

ชุดนี้ให้ภาพคมและลื่นที่สุดในการทดสอบของเรา: PS5 ต่อ Wi-Fi 5 บ้านๆ (802.11ac, ping ราว 1.5 ms), NVIDIA RTX 4090, จอ 165 Hz, เล่น Ghost of Yōtei — ภาพคมกว่าเล่นหน้าเครื่องผ่านทีวีด้วยซ้ำ

| ค่า | อยู่ที่ | ตั้งเป็น | ทำไม |
|---|---|---|---|
| Resolution | Settings › Stream › Local | **1080p** | สูงสุดที่ Remote Play ให้ |
| FPS | Settings › Stream › Local | **60 fps** | 30 fps ไม่ลื่นอย่างเห็นได้ชัด |
| Bitrate | Settings › Stream › Local | **100 Mbps** (สูงสุด) | Wi-Fi 5 ก็รับไหวในการทดสอบ · ถ้า Stats ขึ้น packet loss ให้ลดลง |
| Codec | Settings › Stream › Local | **H.265** | ภาพดีกว่า H.264 ที่ bitrate เท่ากัน |
| Hardware Decoder | Settings › Video | **cuda** บนการ์ด NVIDIA (การ์ดอื่น auto จะเลือกตัวถอดรหัสบน GPU ให้) | ถอดรหัสบนการ์ดจอ |
| Render Preset | Settings › Video (หรือเมนูสตรีม › QUALITY) | **HQ + Advanced Spatial Upscaling** | ขยายภาพ 1080p ด้วย AI upscaler (FSRCNNX) แทนการยืดธรรมดา — ตัวนี้แหละที่ทำให้คมกว่าหน้าเครื่อง |
| Frame Delivery | Settings › Video | **Direct Mapping** (ค่าเริ่มต้น) | หน่วงน้อยสุด · Frame Gen ต้องใช้ |

**เล่นผ่านเน็ตนอกบ้าน** (Settings › Stream › Remote): 1080p / 60 fps เหมือนเดิม แต่ตั้ง bitrate ราว **20–30 Mbps** — Remote Play ใช้ความเร็ว upload ของเน็ตบ้าน 100 Mbps จะกระตุกหรือหลุดในเน็ตส่วนใหญ่

**เสริม (เมนูสตรีม Ctrl+O):**
- **Frame Gen** (QUALITY): 60 → 120 fps บนจอที่เร็วกว่า 60 Hz · ลื่นสุดเมื่อเปิด G-SYNC / FreeSync (VRR) หรือจอ 120 Hz · จอ 144/165 Hz ที่ไม่มี VRR จังหวะเฟรมอาจไม่สม่ำเสมอ ลองเปิด-ปิดเทียบดู · หน่วงเพิ่มราว 8 ms
- **Glow** (FIT): แถบดำรอบภาพเป็นแสงเบลอจากเกม เหมาะกับจอ ultrawide
- **Light** (OVERLAY): แสงเรืองรอบภาพตามสีไฟจอย เมื่อเกมสั่งสี

## แจ้งปัญหา

PS-WRAP เป็น fork อิสระ **ไม่ใช่ chiaki-ng ทางการ** เจอบั๊กหรือมีไอเดีย แจ้งที่ [Issues](https://github.com/iDevGIS/PS-WRAP/issues) ของ repo นี้ ไม่ใช่ที่ chiaki-ng

**วิธีที่ง่ายที่สุด: เปิด `PS-WRAP-Diagnostics.exe`** (อยู่ในโฟลเดอร์ PS-WRAP ข้าง `PS-WRAP.exe`) ใช้ได้แม้ PS-WRAP เปิดไม่ขึ้น
1. ตรวจเครื่องให้เอง: ไฟล์ของ PS-WRAP, Windows, ไดรเวอร์การ์ดจอ, Vulkan ใช้ได้หรือค้าง, แอป overlay ที่เกาะเกม, crash/ค้างล่าสุด (Windows Error Reporting + Event Log), log ของ PS-WRAP
2. **Test launch** เปิด PS-WRAP แล้วบันทึกทุกอย่างที่แอปพิมพ์ออกมา (เลือก verbose ได้) ถ้าแอปไม่ตอบสนองจะเก็บจุดที่ค้างไว้ให้ · **Test with OpenGL (safe mode)** แบบเดียวกันแต่ไม่ใช้ Vulkan
3. **Save report (.zip)** ได้ zip ไฟล์เดียวบน Desktop ไว้แนบ issue · **Copy summary** ข้อความสั้นพอดี 1 ข้อความ Discord · **Report on GitHub** เปิด issue ใหม่พร้อมสรุปให้

![PS-WRAP Diagnostics](screenshots/diagnostics.png)

ถ้า PS-WRAP ขึ้น "Not responding" ทันทีที่เปิด กด **Use OpenGL (fix freeze)** แล้ว PS-WRAP จะเปิดด้วย OpenGL แทน Vulkan (กดปุ่มเดิมเพื่อเปลี่ยนกลับ)

รายงานไม่มีข้อมูลล็อกอิน PSN, stream key, การลงทะเบียนเครื่อง หรือชื่อช่อง · ชื่อ user, ชื่อเครื่อง, โฟลเดอร์ user และ IP อินเทอร์เน็ต ถูกแทนด้วยคำกลางๆ

## มีอะไรใหม่จาก chiaki-ng

- **UI ใหม่ทั้งชุด** — หน้าหลักแบบ console card, Settings แบบ sidebar, เมนูสตรีมใหม่, หน้า Keys ใหม่, responsive ทุกขนาดหน้าต่าง, ใช้จอยนำทางได้ทุกหน้า (Steam Deck / TV)
- **Overlay ระหว่างสตรีม** — จอย, network stats, facecam, mic visualizer, นาฬิกา และ **แชทไลฟ์ (YouTube + Twitch)** วางอิสระได้ทุกตัว หรือเปิด **Stack** ให้เรียงเป็นคอลัมน์เดียวกว้างเท่ากัน แล้วย้าย/ย่อขยาย/สลับลำดับทั้งชุดทีเดียว
- **ภาพ** — **Frame Gen** สร้างเฟรมกลาง 60 → 120 fps บนจอเร็ว (หา motion บน GPU) · **Glow** แถบดำรอบภาพเป็นแสงเบลอจากเกม (เหมาะจอ ultrawide) · **Light** สีไฟจอยที่เกมสั่งเรืองรอบภาพและบนรูปจอย · AI upscale (FSRCNNX) จาก preset QUALITY — ดู [ค่าที่ทดสอบแล้วดีที่สุด](#ค่าที่ทดสอบแล้วดีที่สุด)
- **Facecam** — กล้องทุกตัวรวมกล้องเสมือน DirectShow, zoom/pan/mirror/วงกลม, ตัดพื้นหลังด้วย chroma key หรือ AI (ไม่ต้องใช้ฉากเขียว), face effects 3D ที่เกาะหน้า (MediaPipe Face Landmarker)
- **อัดคลิป** พร้อม overlay ทุกตัว (H.264 / HDR HEVC, เสียง 3 แทร็ก), **Instant Replay**, marker เป็น chapter, **ภาพหน้าจอปุ่มเดียว** (HDR ได้ PNG HDR ด้วย) · ความละเอียดตามสตรีม / 1440p / **4K (upscale)**
- **Go Live** — ไลฟ์ไป YouTube / Twitch / Facebook / Kick / Custom RTMP(S) พร้อมกัน ถึง 4K (YouTube/Custom) และปลายทาง **แนวตั้ง 9:16** สำหรับ TikTok / Shorts / Reels · stream key เก็บใน Windows Credential Manager
- **หน้าต่าง 9:16** — preview แนวตั้งสด 3 เลย์เอาต์, ลาก facecam/ตำแหน่งตัดได้, อัดคลิป 1080 × 1920
- **ไมโครโฟน** — ลดเสียงรบกวน RNNoise + ตัดเสียงลำโพง ปรับกลางสตรีมได้, boost + noise gate, หน้าทดสอบไมค์
- **ลำโพง** — เลือกอุปกรณ์เสียงออกและระดับเสียงได้จากชิปหน้าแรก เมนูสตรีม หรือ tray สลับกลางสตรีมได้ (PS5 ส่งเสียง Remote Play มาเป็น stereo — อยากได้เสียงรอบทิศแบบจำลอง เปิด Windows Sonic / Dolby Atmos for Headphones ใน Windows)
- **Game presets** — ตั้งความละเอียด/bitrate/overlay ฯลฯ ต่อเกม ใช้อัตโนมัติ
- **สถานะบน Discord** — โปรไฟล์ Discord ขึ้น *Playing PS-WRAP* พร้อมเกมที่เครื่องรันอยู่, Remote Play / Live / Recording และเวลาเล่น (คุยกับแอป Discord ตรงๆ ไม่มี DLL เพิ่ม)
- **fps ในการ์ด Stats** — fps ที่เครื่องส่งมา และ fps ที่ขึ้นจอจริง (ราว 120 เมื่อเปิด Frame Gen)
- **เครื่องมือวินิจฉัย** (`PS-WRAP-Diagnostics.exe`) — ตรวจเครื่อง, จับจุดที่ PS-WRAP ค้าง, แก้ Vulkan ค้างได้ในคลิกเดียว, รวมทุกอย่างเป็น zip เดียวส่งกลับมา ดู [แจ้งปัญหา](#แจ้งปัญหา)
- **ขนาดภาพสำเร็จรูป** (720p–1440p 16:9 พอดี ไม่มีขอบดำ), เมนู system tray, hide to tray, always on top, single instance
- **แก้ความเสถียร** — crash/ค้างกลางสตรีม, ปุ่มค้างบน PS5 ตอนเน็ตหลุด, overlay ซ้อนเป็นเงาบน Vulkan, ปุ่มไม่ติดหลังปิดเมนู ฯลฯ — รายละเอียดใน [CHANGELOG.th.md](CHANGELOG.th.md)
- ที่เก็บข้อมูลแยกจาก chiaki-ng (`HKCU\Software\PS-WRAP`, `%APPDATA%\PS-WRAP`)

## โครงสร้าง

| ที่ | มีอะไร |
|---|---|
| `ps-wrap/` | source ทั้งหมด (ฐานจาก chiaki-ng) — UI อยู่ที่ `ps-wrap/gui/src/qml/`, โค้ด C++ ของเราขึ้นต้นด้วย `pswrap*` |
| `scripts/` | PowerShell: ติดตั้ง MSYS2 + deps, build, run, deploy, เครื่องมือทดสอบ |
| `screenshots/` | ภาพใน README |

## Build (Windows, MSYS2 mingw64)

```powershell
git clone --recurse-submodules https://github.com/iDevGIS/PS-WRAP.git
cd PS-WRAP
.\scripts\setup-msys2.ps1        # ติดตั้ง MSYS2 + deps (ครั้งเดียว)
.\scripts\setup-extra-deps.ps1   # libplacebo / SDL ที่ต้อง build เอง (ครั้งเดียว)
.\scripts\build.ps1              # → ps-wrap\build\gui\PS-WRAP.exe
.\scripts\run.ps1                # รันตัวที่ build
.\scripts\deploy.ps1 -StartMenu  # รวมเป็นโฟลเดอร์พกพา dist\PS-WRAP + shortcut ใน Start Menu
```

ใช้ PowerShell 7 (`pwsh`) — สคริปต์มีภาษาไทย Windows PowerShell 5 อ่านเพี้ยน
Facecam แบบ AI ต้องมี `onnxruntime.dll` (ONNX Runtime 1.30 win-x64) วางข้าง `PS-WRAP.exe`

## Credits

PS-WRAP เกิดขึ้นไม่ได้ถ้าไม่มีโปรเจกต์เหล่านี้ — เครดิตชุดเดียวกันอยู่ในแอปที่ชิป ⓘ **About** หน้าแรก (และ Settings › Config)

- **[chiaki-ng](https://github.com/streetpea/chiaki-ng)** โดย Street Pea และผู้ร่วมพัฒนา — PS-WRAP แยกมาจาก commit `a9a2805` (1.10.0 development) แกน streaming Remote Play ทั้งหมดเป็นงานของเขา
- **[Chiaki](https://git.sr.ht/~thestr4ng3r/chiaki)** โดย Florian Märkl และผู้ร่วมพัฒนา — ต้นฉบับของ chiaki-ng
- Submodules ของ upstream: curl, nanopb, jerasure, gf-complete, munit, cpp-steam-tools, oboe, borealis
- [Qt 6](https://www.qt.io) (LGPL-3.0), [libplacebo](https://code.videolan.org/videolan/libplacebo) (LGPL-2.1), [FFmpeg](https://ffmpeg.org) (GPL build), [SDL](https://libsdl.org) (zlib)
- [ONNX Runtime](https://github.com/microsoft/onnxruntime) (MIT) — headers ใน `ps-wrap/third-party/onnxruntime`
- โมเดล AI (Apache-2.0, Google MediaPipe): Selfie Segmentation (ONNX โดย onnx-community), BlazeFace (ONNX โดย garavv/blazeface-onnx), Face Landmarker 478 จุด (ONNX โดย senty-au)
- [RNNoise](https://github.com/xiph/rnnoise) และ SpeexDSP (BSD) — ลดเสียงรบกวน/ตัดเสียงสะท้อนของไมค์
- [Input Prompts](https://kenney.nl/assets/input-prompts) โดย Kenney (CC0) — ภาพจอย PS4/PS5
- ภาพเกราะซามูไร `samurai_photo.png`: Wikimedia Commons *MAP Expo Sujibachi kabuto Menpo 06 01 2012.jpg* (CC0) — ตัดพื้นหลังและเจาะช่องตา

ภาพ effect ที่ตัดจากเกม (`fx/jin_*.png`) ใช้ส่วนตัวเท่านั้น ไม่อยู่ใน repo และแพ็กเกจ release และห้ามแจก

## License

**AGPL-3.0** (with OpenSSL exception) ตาม chiaki-ng — ดู [LICENSE](LICENSE) และ `ps-wrap/LICENSES/`
ถ้าแจก binary ของ PS-WRAP ให้ใคร ต้องให้ source code ของเวอร์ชันนั้นด้วย · แพ็กเกจ release มี `LICENSE.txt`, `THIRD-PARTY-NOTICES.txt` และโฟลเดอร์ `licenses/`
PlayStation, PS4 และ PS5 เป็นเครื่องหมายการค้าของ Sony Interactive Entertainment — โปรเจกต์นี้ไม่เกี่ยวข้องกับ Sony
