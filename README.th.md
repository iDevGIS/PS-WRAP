# PS-WRAP

[English](README.md) · ภาษาไทย

PS4 / PS5 Remote Play client สำหรับ Windows ที่ออกแบบ UI ใหม่ทั้งหมด ให้ทันสมัยและเล่นด้วยจอยอย่างเดียวได้
พัฒนาต่อจาก [chiaki-ng](https://github.com/streetpea/chiaki-ng) โดยทีม BudToZai — แกน streaming (`ps-wrap/lib/`) คงเดิมจาก upstream

▶️ **Demo stream:** [youtube.com/live/8sWkgmj81Aw](https://www.youtube.com/live/8sWkgmj81Aw)

**เล่นพร้อมเปิดหน้าต่าง 9:16** — overlay บนจอเกม และข้างๆ เป็นภาพแนวตั้งสำหรับ Shorts / TikTok / Reels มีรูปมีมด้านบน + facecam (เอฟเฟกต์หน้ากากซามูไร) ด้านล่าง เลย์เอาต์ Blur fill

![สตรีมพร้อมหน้าต่าง 9:16](screenshots/stream-916.jpg)

![หน้าหลัก](screenshots/main.png)

**ระหว่างสตรีม** — แชทไลฟ์, นาฬิกา/เวลาเล่น, mic visualizer, network stats และ overlay จอย ลากย้าย/ย่อขยายได้ทุกตัว (คลิกที่ overlay เพื่อแก้)

![สตรีมพร้อม overlay](screenshots/stream-overlay.jpg)

**เมนูระหว่างสตรีม** — Ctrl+O, L1+R1+L3+R3 หรือจาก tray · End Stream, อัดคลิป, ไมค์, เสียง, ขนาด/คุณภาพภาพ, overlay, Instant Replay, ภาพหน้าจอ, Go Live, 9:16

![เมนูสตรีม](screenshots/stream-menu.jpg)

| แนวตั้ง 9:16 สำหรับ Shorts / TikTok / Reels | Settings › Go Live + แชทบนจอ |
|---|---|
| ![หน้าต่าง 9:16](screenshots/vertical-916.png) | ![ตั้งค่า Go Live](screenshots/go-live.png) |

| เมนู system tray | Disconnect (ค่าเริ่มต้นไม่สั่งเครื่องหลับ) | Settings › Keys |
|---|---|---|
| ![Tray menu](screenshots/tray-menu.png) | ![Disconnect dialog](screenshots/disconnect.png) | ![หน้า Keys](screenshots/settings-keys.png) |

## ดาวน์โหลด

โหลด zip ล่าสุดที่ [Releases](https://github.com/iDevGIS/PS-WRAP/releases) แตกไฟล์ไว้ที่ไหนก็ได้ แล้วเปิด `PS-WRAP.exe` — ไม่ต้องติดตั้งอะไรเพิ่ม
ถ้าเคยใช้ chiaki-ng ในเครื่องนี้ PS-WRAP จะคัดลอก settings และเครื่องที่ลงทะเบียนไว้มาให้ตอนเปิดครั้งแรก (ของ chiaki-ng ไม่ถูกแตะ)

## มีอะไรใหม่จาก chiaki-ng

- **UI ใหม่ทั้งชุด** — หน้าหลักแบบ console card, Settings แบบ sidebar, เมนูสตรีมใหม่, หน้า Keys ใหม่, responsive ทุกขนาดหน้าต่าง, ใช้จอยนำทางได้ทุกหน้า (Steam Deck / TV)
- **Overlay ระหว่างสตรีม** — จอย, network stats, facecam, mic visualizer, นาฬิกา และ **แชทไลฟ์ (YouTube + Twitch)** ย้าย/ย่อขยาย/จำตำแหน่งได้ทุกตัว
- **Facecam** — กล้องทุกตัวรวมกล้องเสมือน DirectShow, zoom/pan/mirror/วงกลม, ตัดพื้นหลังด้วย chroma key หรือ AI (ไม่ต้องใช้ฉากเขียว), face effects 3D ที่เกาะหน้า (MediaPipe Face Landmarker)
- **อัดคลิป** พร้อม overlay ทุกตัว (H.264 / HDR HEVC, เสียง 3 แทร็ก), **Instant Replay**, marker เป็น chapter, **ภาพหน้าจอปุ่มเดียว** (HDR ได้ PNG HDR ด้วย) · ความละเอียดตามสตรีม / 1440p / **4K (upscale)**
- **Go Live** — ไลฟ์ไป YouTube / Twitch / Facebook / Kick / Custom RTMP(S) พร้อมกัน ถึง 4K (YouTube/Custom) และปลายทาง **แนวตั้ง 9:16** สำหรับ TikTok / Shorts / Reels · stream key เก็บใน Windows Credential Manager
- **หน้าต่าง 9:16** — preview แนวตั้งสด 3 เลย์เอาต์, ลาก facecam/ตำแหน่งตัดได้, อัดคลิป 1080 × 1920
- **ไมโครโฟน** — ลดเสียงรบกวน RNNoise + ตัดเสียงลำโพง ปรับกลางสตรีมได้, boost + noise gate, หน้าทดสอบไมค์
- **Game presets** — ตั้งความละเอียด/bitrate/overlay ฯลฯ ต่อเกม ใช้อัตโนมัติ
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
