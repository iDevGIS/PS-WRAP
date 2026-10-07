# PS-WRAP

English · [ภาษาไทย](README.th.md)

PS-WRAP is a PS4 / PS5 Remote Play client for Windows with a redesigned, modern interface that you can use with a controller alone.
It is built on [chiaki-ng](https://github.com/streetpea/chiaki-ng) by the BudToZai team. The streaming core (`ps-wrap/lib/`) comes from upstream.

▶️ **Demo stream:** [youtube.com/live/8sWkgmj81Aw](https://www.youtube.com/live/8sWkgmj81Aw).

**Playing with the vertical 9:16 window open:** overlays on the game screen, and next to it the vertical picture for Shorts / TikTok / Reels with a meme image on top and the facecam (with the samurai face effect) below, in the Blur fill layout.

![Stream with the 9:16 window](screenshots/stream-916.jpg)

![Home screen](screenshots/main.png)

**While streaming:** live chat, clock and play time, mic visualizer, network stats and controller overlays. You can drag and resize each one; click an overlay to edit it.

![Stream with overlays](screenshots/stream-overlay.jpg)

**Stream menu** (Ctrl+O, L1+R1+L3+R3, or the tray): End Stream, recording, microphone, volume, picture size and quality, overlays, Instant Replay, screenshots, Go Live and 9:16.

![Stream menu](screenshots/stream-menu.jpg)

| Vertical 9:16 for Shorts / TikTok / Reels | Settings › Go Live and chat on screen |
|---|---|
| ![9:16 window](screenshots/vertical-916.png) | ![Go Live settings](screenshots/go-live.png) |

| System tray menu | Disconnect (does not sleep the console by default) | Settings › Keys |
|---|---|---|
| ![Tray menu](screenshots/tray-menu.png) | ![Disconnect dialog](screenshots/disconnect.png) | ![Keys page](screenshots/settings-keys.png) |

## Download

Get the latest zip from [Releases](https://github.com/iDevGIS/PS-WRAP/releases), unzip it anywhere and run `PS-WRAP.exe`. You don't need to install anything.
If you used chiaki-ng on the same PC, PS-WRAP copies its settings and registered consoles on first launch (chiaki-ng's data is left untouched).

## What's new compared to chiaki-ng

- **A new interface throughout**:
  - Console-card home screen and Settings with a sidebar.
  - A redesigned stream menu and a new Keys page.
  - Responsive at any window size.
  - Every screen works with a controller (Steam Deck / TV friendly).
- **Overlays while streaming**: controller, network stats, facecam, mic visualizer, clock and **live chat (YouTube + Twitch)**. You can move, resize and place each one.
- **Facecam**:
  - Any camera, including DirectShow virtual cameras.
  - Zoom, pan, mirror and circle.
  - Background removal with chroma key or AI (no green screen needed).
  - 3D face effects that follow your head (MediaPipe Face Landmarker).
- **Capture**:
  - Clip recording with every overlay (H.264 / HDR HEVC, three audio tracks).
  - Instant Replay and chapter markers.
  - One-button screenshots, with HDR PNG for HDR streams.
  - Output at stream resolution, 1440p or **4K (upscaled)**.
- **Go Live**:
  - Stream to YouTube, Twitch, Facebook, Kick and custom RTMP(S) at the same time.
  - Up to 4K on YouTube/Custom.
  - **Vertical 9:16** destinations for TikTok / Shorts / Reels.
  - Stream keys are kept in Windows Credential Manager.
- **Vertical 9:16 window**:
  - Live preview with three layouts.
  - Drag the facecam and the crop.
  - Record 1080 × 1920 clips.
- **Microphone**:
  - RNNoise noise reduction and speaker echo removal that can be changed mid-stream.
  - Boost and noise gate.
  - Mic test page.
- **Game presets**: resolution, bitrate, overlays and more per game, applied automatically.
- **Picture size presets** (720p–1440p, exact 16:9 with no black bars), system tray menu, hide to tray, always on top, single instance.
- **Stability fixes**:
  - Mid-stream crashes and freezes.
  - Buttons that stayed pressed on the PS5 after packet loss.
  - Ghost overlays on Vulkan after resizing.
  - Buttons not working after closing a menu.
  - Details are in [CHANGELOG.md](CHANGELOG.md).
- Its own data storage (`HKCU\Software\PS-WRAP`, `%APPDATA%\PS-WRAP`), separate from chiaki-ng.

## Layout

| Path | Contents |
|---|---|
| `ps-wrap/` | All source code (based on chiaki-ng). The UI is in `ps-wrap/gui/src/qml/`; our C++ files start with `pswrap*`. |
| `scripts/` | PowerShell scripts: install MSYS2 and dependencies, build, run, deploy, and test tools. |
| `screenshots/` | The images in this README. |

## Build (Windows, MSYS2 mingw64)

```powershell
git clone --recurse-submodules https://github.com/iDevGIS/PS-WRAP.git
cd PS-WRAP
.\scripts\setup-msys2.ps1        # install MSYS2 + dependencies (once)
.\scripts\setup-extra-deps.ps1   # libplacebo / SDL that must be built from source (once)
.\scripts\build.ps1              # → ps-wrap\build\gui\PS-WRAP.exe
.\scripts\run.ps1                # run the build
.\scripts\deploy.ps1 -StartMenu  # portable folder dist\PS-WRAP + a Start Menu shortcut
```

Use PowerShell 7 (`pwsh`). The scripts contain Thai text that Windows PowerShell 5 reads incorrectly.
AI facecam features need `onnxruntime.dll` (ONNX Runtime 1.30 win-x64) next to `PS-WRAP.exe`.

## Credits

PS-WRAP would not exist without these projects. The same credits are shown in the app under the ⓘ **About** chip on the home screen (also in Settings › Config).

- **[chiaki-ng](https://github.com/streetpea/chiaki-ng)** by Street Pea and contributors. PS-WRAP forked from commit `a9a2805` (1.10.0 development); the whole Remote Play streaming core is theirs.
- **[Chiaki](https://git.sr.ht/~thestr4ng3r/chiaki)** by Florian Märkl and contributors, the original that chiaki-ng is based on.
- Upstream submodules: curl, nanopb, jerasure, gf-complete, munit, cpp-steam-tools, oboe, borealis.
- [Qt 6](https://www.qt.io) (LGPL-3.0), [libplacebo](https://code.videolan.org/videolan/libplacebo) (LGPL-2.1), [FFmpeg](https://ffmpeg.org) (GPL build), [SDL](https://libsdl.org) (zlib).
- [ONNX Runtime](https://github.com/microsoft/onnxruntime) (MIT); headers are in `ps-wrap/third-party/onnxruntime`.
- AI models (Apache-2.0, Google MediaPipe):
  - Selfie Segmentation (ONNX by onnx-community).
  - BlazeFace (ONNX by garavv/blazeface-onnx).
  - Face Landmarker 478 points (ONNX by senty-au).
- [RNNoise](https://github.com/xiph/rnnoise) and SpeexDSP (BSD) for microphone noise reduction and echo cancellation.
- [Input Prompts](https://kenney.nl/assets/input-prompts) by Kenney (CC0): PS4/PS5 controller art.
- Samurai armor image `samurai_photo.png`: Wikimedia Commons *MAP Expo Sujibachi kabuto Menpo 06 01 2012.jpg* (CC0), background removed and eye holes cut.

Effect images cut from a game (`fx/jin_*.png`) are for private use only. They are not in this repository or the release packages and must not be distributed.

## License

**AGPL-3.0** (with the OpenSSL exception), the same as chiaki-ng. See [LICENSE](LICENSE) and `ps-wrap/LICENSES/`.
If you give a PS-WRAP binary to anyone, you must also give them the source code of that version. Release packages include `LICENSE.txt`, `THIRD-PARTY-NOTICES.txt` and a `licenses/` folder.
PlayStation, PS4 and PS5 are trademarks of Sony Interactive Entertainment. This project is not affiliated with Sony.
