# Changelog

English · [ภาษาไทย](CHANGELOG.th.md)

The format follows [Keep a Changelog](https://keepachangelog.com/). PS-WRAP has its own version numbers (0.x releases are pre-releases). Upstream base: chiaki-ng `a9a2805`.

## [Unreleased]

## [0.9.0] — 2026-10-10
Redesigned Settings, the console PIN with a controller, and the mouse wheel works again.
### Added
- **Enter the console PIN with a controller**: the Console Login PIN prompt when connecting (also over PSN) and *Set console pin* on the home screen now work like the passcode screen on the PS5: each controller button types one digit (◀1 ▲2 ▶3 ▼4 R1 5 R2 6 L1 7 L2 8 △9 □0, shown on an on-screen keypad), the fourth digit confirms by itself and Circle deletes the last digit (or cancels when empty). Typing on a keyboard and clicking the keypad also work. L2 and R2 now also work in menus.
- **Join Discord** in About and **Ask on Discord** in PS-WRAP-Diagnostics open the PS-WRAP Discord server (help forum, news, beta builds): <https://discord.gg/6VTSxu8JRf>.
### Changed
- **Redesigned Settings**: the sections are regrouped so related options sit together, and every page is split into titled cards with an icon and a short explanation. The sidebar has an icon for each page, grouped into **Play** (General, Stream, Video, Audio, Game presets), **Controls** (Controllers, Keyboard), **Connection** (Consoles, Remote Play), **Create** (Facecam, Recording, Go Live) and **App** (System). Things that were mixed into General moved to where you would look for them: facecam has its own page, the recording folder and output resolution are under Recording, the Steam Deck options and the stream menu button combo are under Controllers, the Wi-Fi and packet loss warnings are under Stream, and the log folder is under System. Every setting is still there with the same value and still works with a controller (L1/R1 switch page).
- **Settings on small windows**: below 1100 px wide the sidebar shrinks to icons only, and when a page is too narrow for name | setting | default side by side, each setting stacks with its name on top (the Stream table keeps Local and Remote side by side). Nothing runs off the right edge any more.
- Settings is about 15% more compact than the rest of the app (smaller text, narrower controls, tighter cards), so more fits on screen; the home screen and stream menu keep their large TV-friendly size. Scroll bars in Settings are a slim line that widens when you point at it.
- **About**: a new *PS-WRAP on GitHub* section at the top (source and downloads, Issues, Changelog, Discord), and *BudToZai* links to our GitHub.
### Fixed
- **Mouse wheel scrolling**: the mouse wheel did nothing anywhere in PS-WRAP (Settings, lists, dialogs) because the window never passed it on. It now scrolls everywhere; during a stream it only scrolls the overlays under the pointer.
- The list of pages on the left of Settings can be scrolled when the window is too short to show them all, and follows L1/R1 so the selected page stays in view.

## [0.8.0] — 2026-10-09
Updates inside the app, a connection check that recommends settings, and an easier PSN sign-in.
### Added
- **Updates inside the app**: PS-WRAP checks GitHub for a new version when it starts and every 12 hours. When there is one, an **Update x.y.z** chip appears on the home screen bar. **Update now** downloads it, checks its SHA-256, unpacks it, then **Restart and update** closes PS-WRAP, puts the new files in place and opens it again; your settings stay as they are. Check by hand in **About › Check for updates**; automatic checks can be turned off there. **Later** hides the chip until the next version comes out. A copy in a read-only folder opens the release page instead.
- **Connection check with recommended settings**: **Test** on a console card (or **Settings › Stream › Check my connection**, which works with a controller) measures the link to the console (cable or Wi-Fi, link speed, Wi-Fi standard, band and signal) and the ping, jitter and loss to it, then recommends resolution, frame rate, bitrate and codec next to your current values. **Apply** sets them. If the console is in rest mode, **Wake console and test again** wakes it up and measures again.
### Changed
- **Easier PSN sign-in**: **Login to PSN** (Settings › Remote, Connect PSN, expired credentials, and account lookup when registering a console) now opens a PlayStation Network sign-in window inside PS-WRAP. When you finish signing in, the window closes and PS-WRAP completes the setup by itself; there's no link to copy and no address to paste back. PS-WRAP remembers the sign-in, so renewing expired credentials is usually one click; **Use a different account** signs out first. It uses Microsoft Edge WebView2, which comes with Windows 10 and 11.
- If you prefer your own browser (**Use my web browser instead**), just copy the address after signing in: PS-WRAP picks it up from the clipboard, so there's no paste step.

## [0.7.0] — 2026-10-09
A status bar while playing and a redesigned stream menu.
### Added
- **Status bar while playing**: move the mouse to the bottom of the window and a bar with the home screen chips appears — ☰ stream menu, mute the mic, speaker and volume, facecam, controller overlay, record, screenshot, **Go Live** (start or stop the live stream; shows the time while live), and network (Mbps, click for stream stats). The 📌 chip keeps it on screen. Mouse clicks outside the bar still go to the game.
- **Click the button hints on the home screen bar**: ✕ Play, △ Wake Up, □ Hide, L1 Console PIN, R3 Add Console and ≡ Settings now work with the mouse too, not only with a controller.
### Changed
- **Redesigned stream menu**: a compact top row (End Stream, Record, mic, microphone and speaker devices, volume, stats) with a **✕** close button, and four cards — **Picture**, **Quality**, **Overlay**, **Capture** — made of small icon tiles. Cards wrap onto a new line on narrow windows instead of being cut off. The five Move buttons are now one **Move** tile that asks which overlay to move. With a controller, the D-pad goes to the nearest button on screen.
- The bottom bars no longer show tooltips on hover.
### Fixed
- Clicking outside a menu now closes it (the speaker menu on the home screen and on the status bar, and the device/size lists in the stream menu).

## [0.6.0] — 2026-10-09
Choose the speaker and its volume, and switch it mid-stream.
### Added
- **Choose the speaker (audio output device) during a stream**: a speaker button next to the microphone in the stream menu (works with a controller) a **speaker chip** on the home screen bar (next to the microphone, with a volume slider at the top of its menu), and **Speaker device ▸** in the tray. The sound switches right away without restarting the stream, and the choice is remembered (the same setting as Settings › Audio › Output Device).

## [0.5.0] — 2026-10-09
Discord status, frame rate in the stats card, the PS-WRAP-Diagnostics troubleshooting tool, and a Steam shortcut fix.
### Added
- **Discord status** (like regular games): your Discord profile shows *Playing PS-WRAP* with the game running on the console, PS5/PS4 Remote Play, Live or Recording, the play time and a *Get PS-WRAP* button. Talks to the Discord app on your PC directly (no extra DLLs); nothing happens if Discord isn't running. During a stream the console is asked for the game name every 30 seconds (one small packet to that console only). Settings › General: *Discord Status* and *Show Game On Discord*.
- **Frame rate in the stats card**: *fps (stream)* is the frames the console sends per second, *fps on screen* is the new pictures shown per second (it reads about 120 with Frame Gen on, marked "· FG"). Redraws caused only by overlays are not counted.
- **PS-WRAP-Diagnostics.exe** (next to PS-WRAP.exe): a troubleshooting tool for when PS-WRAP has problems, and it opens even if PS-WRAP won't. It checks the PS-WRAP files, Windows, the graphics driver, whether Vulkan works or freezes (tested in a separate process with a 20-second limit), overlay apps that hook into games, recent crashes and freezes from Windows Error Reporting and the Event Log, and PS-WRAP's logs. **Test launch** opens PS-WRAP and records its output, with an optional verbose log. If PS-WRAP stops responding, the tool saves where every thread is stuck plus a small dump. **Test with OpenGL (safe mode)** does the same without Vulkan. **Use OpenGL (fix freeze)** makes PS-WRAP always start with OpenGL. **Save report (.zip)**, **Copy summary** (fits one Discord message) and **Report on GitHub** make the result easy to send. The PSN sign-in, stream keys, console registration and channel names are never included. The Windows user name, PC name, home folder and internet IP addresses are replaced with placeholders.
### Fixed
- **Create Steam Shortcut** saved to the wrong place (`userdata//config/shortcuts.vdf`) on current Steam clients that no longer mark a *MostRecent* user, so the shortcut never showed up in Steam. The Steam user is now also found from *AutoLogin* (cpp-steam-tools `94a31a3`, from upstream chiaki-ng `6837fedc`, chiaki-ng#813).

## [0.4.0] — 2026-10-09
Frame Gen (60 → 120 fps), Glow, controller light on screen, and one-click overlay Stack.
### Added
- **Frame Gen (stream menu › QUALITY).** Adds an in-between frame for every stream frame, so a 60 fps stream plays at 120 fps on a display faster than the stream (needs Frame Delivery = Direct Mapping and the Vulkan renderer, which are the defaults). Motion is estimated on the GPU; still parts such as the HUD stay sharp, and areas that can't be matched use one side instead of a double image. It adds about half a frame of delay (~8 ms at 60 fps). Recordings, screenshots and the 9:16 picture still use the real frames only. Off by default.
- **Glow (stream menu › FIT).** The empty bars around the picture (wide screens, windows that aren't 16:9) are filled with a soft, darkened blur of the game's edges instead of black. Off by default.
- **Light (stream menu › OVERLAY).** A glow around the edge of the picture in the controller light color set by the game, with a short flash when the color changes. The same color is shown on the light bars of the controller overlay (Pad). Off by default.
- **Stack (stream menu › OVERLAY).** Lines up every overlay that is on (clock, mic, stats, chat, facecam, controller) in one column at the same width with even gaps, on the left or right edge of the picture. **Arrange** (or click any overlay) edits the whole column at once: drag to move it up or down, drag it past the middle to switch sides, ◢ or L1/R1 to resize all of them, and ▲▼ to reorder. The column shrinks by itself so it never runs off the screen. Turning Stack off puts every overlay back where you placed it before.

## [0.3.3] — 2026-10-08
The chat card in the vertical 9:16 picture can fill any area.
### Changed
- **The chat card in the vertical 9:16 picture is now drawn at its own size** instead of being copied from the chat card on the game screen. By default it fills the free area below the game (Blur fill), and you can resize it to any shape: scroll to resize, **Shift+scroll** to change only the height, drag to move, double-click or right-click › *Fill the free area* to reset. The text stays sharp at any size, the chat no longer disappears from the vertical picture while the stream menu is open, and you can hide the chat card on the game screen while still showing chat in the vertical picture.

## [0.3.2] — 2026-10-07
Go Live sends at full speed (4K live works).
### Fixed
- **Go Live was capped at about 12–15 Mbps** even on fast connections, so 4K (and high-bitrate 1440p) streams skipped video with "slow upload". The socket send buffer was left at the Windows default (~64 KB), which limits a single connection to buffer ÷ ping (≈ 15 Mbps at 33 ms to YouTube). It is now 4 MB; a 4K stream at 51 Mbps to YouTube now sends at full speed.

## [0.3.1] — 2026-10-07
Images, GIFs and live chat in the vertical 9:16 picture, plus fixes.
### Added
- **Images, GIFs and live chat in the vertical 9:16 picture.** In the 9:16 window, **＋ Image** adds a PNG, JPG, animated GIF or SVG (banner, logo and so on) from a file or from a link such as Giphy (downloaded by the app, up to 50 MB). Drag to move, scroll to resize, and right-click for bring to front / send to back / opacity / remove. Up to 8 images per layout, saved per layout; files are copied to the app's `overlays` folder, so moving the original is fine. **Chat** puts the live chat card into the vertical picture (drag/scroll like the facecam). Everything appears in the preview, Record 9:16 and vertical Go Live, and never covers the game screen while you play.

### Changed
- The facecam frame now matches the mic, stats, clock and chat cards: a thin 1 px edge with an accent line along the bottom, instead of a solid white 2 px border.

### Fixed
- **Vertical 9:16 clips and live streams in the Blur fill layout had a green background** (the preview window looked fine). libplacebo writes its blurred border as RGB straight into every plane without converting to YCbCr, so the NV12 file got the wrong colors. The Blur fill layout is now drawn to an RGBA texture first and then converted to NV12 in a second pass.

## [0.3.0] — 2026-10-07
4K output, vertical 9:16, live chat on screen, a fix for stuck buttons, and an About page.

### Added
- **About page with credits** (ⓘ chip on the home screen bar, or Settings › Config › About PS-WRAP). It states that PS-WRAP is built on chiaki-ng by Street Pea and contributors, which comes from Chiaki by Florian Märkl. It lists every library, model and image we use with its license, shows the AGPL-3.0 license and a link to the source, and has a Licenses button that opens `THIRD-PARTY-NOTICES.txt` (in release packages).
- **Live chat on screen** (Ctrl+Shift+H, Chat in the stream menu, or the tray). Shows YouTube chat (an @handle, channel ID or live link, plus a YouTube Data API key) and Twitch chat (a channel name, no login) as a card you can drag, resize and place. The card is also included in landscape clips and live streams. Set it up in Settings › Go Live › Chat on screen. The API key is kept in Windows Credential Manager.
- **Vertical 9:16 live streaming**: each Go Live destination can be set to "Vertical 9:16" (on by default for TikTok and Instagram). It sends a 1080 × 1920 picture using the layout from the 9:16 window. Landscape and vertical destinations can run at the same time (for example YouTube and TikTok). Vertical-only destinations also work with HDR streams (tone-mapped to SDR).
- **Vertical 9:16 clips** (● Record 9:16 in the 9:16 window or the tray): 1080 × 1920 at 60 fps, and you can record them alongside a normal clip. The on-screen REC dot shows a "9:16" label.
- 9:16 window: **drag the facecam** to move it, scroll to resize it, and double-click to reset it (saved per layout). **Drag the game picture sideways** to move the crop. Includes a Reset cam button.
- **Vertical 9:16 preview** (9:16 in the stream menu or the tray): a separate window that shows the vertical picture live for Shorts, TikTok and Reels. It has three layouts:
  - Cam + game: facecam on top, game below.
  - Center crop: the game fills the frame with the facecam in a top corner, and you can move the crop.
  - Blur fill: the whole game in the middle over a blurred background.
- **Output Resolution: Same as stream / 1440p / 4K (upscaled)** (Settings › General). One setting for clips, Instant Replay and Go Live. Upscaling uses the upscaler picked under QUALITY (HQ + Spatial = FSRCNNX). For Go Live, YouTube and Custom can send up to 4K (bitrate up to 51 Mbps); other platforms are scaled to 1080p automatically.
- Tray **Record preset ▸** (Same as stream / 1440p / 4K), the same value as Output Resolution. When recording upscaled, the blinking REC dot shows a **4K** or **1440p** label. The label only appears on screen, not in the file.
- **Tray menu** entries:
  - **Stream menu**: opens the in-stream menu without L1+R1+L3+R3 or Ctrl+O.
  - **Settings**: opens Settings from the home screen.
  - **Picture size ▸**: 720p–1440p or Fullscreen, the same as the Size button.

### Changed
- The app now shows PS-WRAP's own version (0.3.0) instead of the upstream version (1.10.0). The old About box said "About PS-WRAP-ng" and described the app as chiaki-ng; it has been replaced by the new About page.
- **Clips, Instant Replay and live streams capture only the 16:9 video area.** They are rendered directly from the stream frames, so the window size, its aspect ratio or an ultrawide screen no longer matter, and no window bars end up in the file. The default resolution is the stream resolution (it used to follow the window height).

### Fixed
- **The UI was built twice after the window was restored** (tray Show PS-WRAP, clicking the icon, Picture size, Stream menu or Settings, or launching the app again). During a stream this gave two game screens: the facecam showed "Camera is busy", the mic overlay was doubled and the 9:16 window opened twice.
- **Buttons stuck down on the PS5 in hectic moments (around L1/R1/L2/R2).** Button presses and releases were sent once over UDP, so a lost "release" packet left the PS5 thinking the button was still held. They are now resent automatically (+20/50/100/200 ms) and carry the last 8 events instead of 4. This is a change in `lib/`, recorded in ADR-0004.
- Tray › Facecam effect / Facecam background did not show which option was selected; the selected one now has a ✓.

## [0.2.0] — 2026-10-06
Recording, Instant Replay, screenshots, Go Live and game presets.

### Added
- **Screenshots chip** on the home screen bar: opens Explorer with the latest screenshot selected.
- **Picture size (Size ▾) in the stream menu**: pick 720p, 900p, 1080p, 1440p or Fullscreen.
  - The video area becomes exactly 16:9 in real pixels, even at 150 % display scaling, so there are no black bars.
  - Clips and screenshots come out at the size in the name.
  - A STREAM tag marks the size that matches the stream resolution.
  - Sizes larger than the screen are hidden, and you can pick a size with a controller.
- **Instant Replay**: keeps the last 30–120 seconds in memory. Press Ctrl+Shift+B, Save in the stream menu or the tray to save them. It uses the same encoder as recording, so you can record while replay is on.
- **Markers while recording** (Ctrl+Shift+K) are written as chapters in the file, so VLC can jump to them. Clips without markers are still crash-safe MP4s.
- **One-button screenshot** (F12, Ctrl+Shift+P, the stream menu or the tray): a full-resolution image of the window with every overlay. HDR streams also save a PQ/BT.2020 `HDR.png` next to the SDR PNG.
- **Mic boost (−12…+24 dB) and noise gate** after noise reduction. They affect the audio sent to the PS5, the Mic track in clips and the spectrum. Adjust them on the mic test page.
- **Clock / play time overlay** (Ctrl+Shift+T), which you can drag, resize and place like the other overlays.
- **Game presets** (Settings › Game presets): per-game resolution, fps, bitrate, overlays, camera effects and Instant Replay. A preset applies automatically when a stream starts and is undone when it ends.
- **Go Live** (Settings › Go Live, Ctrl+Shift+L, the stream menu or the tray): stream H.264 1080p60 to YouTube, Twitch, Facebook, Kick and custom RTMP(S) at the same time. It reconnects by itself if it drops, and a slow destination does not hold back the others. Stream keys are kept in Windows Credential Manager.
- **Clip recording** (Ctrl+Shift+R, the stream menu or the tray): the screen with every overlay (NVENC; SDR H.264 or HDR HEVC 10-bit) plus three audio tracks: Game+Mic, Game and Mic. If Windows blocks the Videos folder, clips are saved to `%USERPROFILE%\PS-WRAP Recordings` with a notice.
- Microphone extras:
  - A mic visualizer overlay; click the mic circle to mute or unmute.
  - Mic and camera test pages; click the chips on the home screen.
  - Microphone selection in Settings, the stream menu and the tray.
  - Tray entries for mute, microphone, and camera effect and background.
- `scripts/smoke.ps1`, an automatic smoke test before deploying.

### Fixed
- The Recordings chip opened a folder without the latest clip when Windows "Controlled folder access" blocked Videos and clips went to the fallback folder `~\PS-WRAP Recordings`. It now finds the latest file in both folders and selects it in Explorer.
- Closing the camera (leaving the camera test page or Settings › General) froze the UI for about 3 seconds; the device now closes in the background. The camera test page says "No video from camera" when there is no picture (it used to show a false "Live"). Quitting waits for the camera to be released.
- The home screen overflowed on small windows: the console card's Play button went off the edge, and the bottom bar now shrinks chips to icons when narrow.

## [0.1.1] — 2026-10-06
### Changed
- **The program file is now `PS-WRAP.exe`** (it was `chiaki.exe`). This uses `OUTPUT_NAME` in CMake on Windows only; the build target is still called chiaki. Recreate shortcuts with `deploy.ps1 -Shortcut -StartMenu`. If you had allowed `chiaki.exe` in the firewall or Controlled folder access, allow `PS-WRAP.exe` again.

## [0.1.0] — 2026-10-06
First pre-release: a complete new interface on top of chiaki-ng.

### Added
- **Live noise reduction and speaker echo removal on the mic test page.** Pick Off, Low, Medium or High for Noise reduction and Speaker echo (with a controller, press ↓ from the microphone list, then ←→). Changes apply immediately, on the test page and in the middle of a stream. Noise reduction uses **RNNoise**, a new optional MSYS2 dependency; without it the build falls back to speex.
- **Recordings** chip on the home screen bar opens the clips folder.
- `scripts/deploy.ps1 -StartMenu` creates a PS-WRAP Start Menu shortcut (per user, no admin needed).
- **Click an overlay (controller, camera or stats) to edit it**, with no shortcut to remember. Clicking another overlay switches to it; clicking empty space finishes editing. The stats card can now be dragged and resized, and it remembers its position.
- Private-use facecam effects **Jin mask** and **Jin mask + headband**. The images are not in the repository or the release packages.
- Facecam effect **Samurai armor (photo)**, made from a CC0 museum photo (Wikimedia Commons). It is loaded from `fx/samurai_photo.png` next to the exe, so you can swap the image without rebuilding. Also a drawn Samurai armor (kabuto + mask).
- **Facecam effects v2**:
  - MediaPipe Face Landmarker with 478 points and 3-axis head pose, so stickers rotate in 3D as you turn, nod and tilt.
  - Stickers are anchored to real facial points, with a 1-Euro filter against jitter.
  - New effects: Samurai mask, Ninja and Ghost (Tsushima).
  - Without the model, it falls back to 6-point tracking.
- Developer option: set `PSWRAP_FAKE_CAM=<video file>` to use a video file instead of the camera for repeatable facecam tests.
- More facecam effects: Mustache, Clown nose, Crown, Bane mask and Party.
- **Facecam effect: sunglasses** (Settings › Facecam Effect). It tracks both eyes with BlazeFace on the CPU and works together with AI background removal.
- **Facecam AI background removal without a green screen** (Settings › Facecam Background › AI remove). It uses MediaPipe Selfie Segmentation on ONNX Runtime, about 5 ms per frame on the CPU. The DLL is loaded at run time; without it the feature is turned off quietly.
- **Facecam supports DirectShow virtual cameras** (NVIDIA Broadcast, OBS Virtual Camera, Streamlabs).
- Settings › General: live **facecam preview** and Facecam Pan X/Y.
- **Single instance**: opening the app again brings the existing window forward, separately for each profile.
- **Tray menu**: Show, Controller overlay, Facecam, Network stats, Always on top and Quit, with ● ON / ○ OFF states. Overlays can be toggled during a stream.
- Facecam zoom and pan, green/blue screen chroma key, and Settings for zoom, background and key tolerance.
- **Facecam overlay** over the stream:
  - Rounded or circular, with mirror, drag, resize and a remembered position.
  - Shortcuts: Ctrl+Shift+C toggles it, Ctrl+Shift+V moves it.
  - Camera, mirror and shape can be set in Settings › General.
  - Vulkan renderer only.
- Stats card: an **rtt at connect** row (the RTT measured when the stream connects).
- Network stats card during a stream: an icon per row, muted labels on the left, values on the right.
- **Stream menu "control deck"**: options in segmented groups (FIT / QUALITY / Display / OVERLAY) that wrap on narrow screens, one-line stats, and a shortcut hint row.
- **Ctrl+Shift+S** toggles the network stats card during a stream.
- New Settings › Keys page: 26 buttons in 6 cards by group, columns that adapt to the width, and full controller navigation.
- All 9 Settings pages tidied: fixed label column, muted defaults and grouped cards.
- **Hide to tray** and **Always on top** (pin button in the header and in Settings › General).
- New PS5/PS4 console art by model, with status lights that pulse for ready and breathe for standby.
- **Controller overlay during a stream**, ported from BudToZaiDualSenseTracker: a DualSense image with lit buttons, moving sticks and pressure-sensitive L2/R2. It can be moved, resized and hidden.
- **Responsive layout** for Settings and dialogs, and controller icons by model (Kenney Input Prompts, CC0).
- Controller status chip on the home screen bar with a details popup (type, name, VID:PID, GUID, battery or USB, Map Buttons).
- **Settings redesign**: a vertical sidebar grouped into Basics and Advanced, a new header, and a new frame for every sub-dialog.
- **Stream menu redesign** with grouped pill buttons and an End Stream button, keeping controller navigation.
- **Branding**: the app is called PS-WRAP (window title, display name, installer) with a new icon.
- **New home screen**:
  - A console card with a status chip (ready, standby, remote) and a Play/Register button.
  - A controller hint bar and an empty state.
  - A non-blocking setup banner instead of stacked Steam/PSN dialogs.
  - Responsive layout for Steam Deck.
- **Design system**: `controls/Theme.qml` tokens, a clear focus ring for controllers and TVs, and a dark slate palette.
- Forked from upstream chiaki-ng `a9a2805`.

### Changed
- **New controller button icons**: ✕ ○ □ △ on dark tiles with PS-style colors. L1/R1 are pill-shaped and L3/R3 are round; upstream's icons overflowed their frame. The Steam Deck set is unchanged.
- Home screen bar: Discovery is now a chip like mic, camera and controller. The version number moved to the end of the bar.
- **PS-WRAP keeps its own data**: registry `HKCU\Software\PS-WRAP\PS-WRAP` and log/cache `%APPDATA%\PS-WRAP\PS-WRAP`. They used to be shared with chiaki-ng in `Chiaki\Chiaki`. The first launch copies settings, registered consoles, placebo settings and every profile automatically, without deleting the originals.
- Facecam sunglasses scale with face width and anchor to the nose. Tracking follows fast movement better, predicts briefly lost faces and fades in and out.
- **The streaming window is the same window as the home screen** (one size, position and maximized state). It no longer resizes to the stream resolution.
- Facecam corner radius is proportional to the frame (9 % of the short side).
- Controller, camera and stats overlays scale with and **stick to the real video area**, not the black bars.
- The stats card is about 25 % smaller. The dropped frames row is always shown, so the card no longer jumps in height.
- The project moved from a Python wrapper around chiaki.exe to a fork of chiaki-ng with a redesigned UI.

### Fixed
- **Speaker echo removal failed when speaker latency was above about 100 ms** (upstream). The reference audio is now captured when it actually goes to the sound card, aligned with each mic frame, and the filter is 300 ms long. It tolerates misestimated delays of −60…+240 ms, such as Bluetooth speakers. speex echo is set to 48 kHz; upstream left it at 8 kHz.
- `scripts/snap.ps1` did not restore the normal profile, so the installed copy opened with the test profile.
- Building from a fresh clone on Windows: the curl patch broke with `core.autocrlf=true`. The Android/Switch submodules are no longer fetched on clone, because their paths exceed MAX_PATH.
- Build: curl's Schannel AIA patch was skipped silently when the build folder was inside another git repository.
- **Crashes and freezes mid-stream, which made buttons stop responding at times.**
  - Qt Quick sync ran across threads without blocking the GUI (upstream) and crashed in Qt6Gui; it now syncs in two steps.
  - A libplacebo assert fired when a texture was still held by Qt.
  - Rendering after a resize could loop forever.
- Switching overlays while editing could leave the controller cut off from the game until the stream ended.
- A click that only woke the window entered overlay edit mode. Switching to another app now ends edit mode, and a banner says controller input to the game is paused while editing.
- After closing a menu or dialog during a stream, buttons did not work until every button was released (upstream). Now only buttons still held at that moment are blocked.
- Facecam showed no picture after the file test mode was added.
- Settings pages taller than the screen scrolled down on their own when opened.
- Vulkan overlay texture validation errors on every frame.
- Facecam went black after chroma key was added.
- **Overlays and dialogs left ghost copies after resizing the window on Vulkan** (two PIN dialogs, two sets of stats or controller overlays). Upstream did not clear the QML layer texture on Vulkan.
- Tray: a single click toggles hide/show and a double-click always shows; they used to fire together.
- Shortcuts and the stream menu stopped responding after the console PIN dialog closed.
- The stats overlay on Vulkan is drawn inside the window instead of upstream's separate window that floated over other apps.
- Hiding to tray during a stream now minimizes instead of hiding, which fixes duplicated overlays after a double-click on the tray.
- New Disconnect Session dialog whose default button is **Disconnect**, keeping the console on. Upstream focused Sleep, so a stray controller press could put the console to sleep.
- The tray icon is always shown; it used to appear only with Hide To Tray on.
- `scripts/deploy.ps1` writes `qt.conf` so the portable folder does not fall back to `C:\msys64`.
- The main window remembers the maximized state without overwriting the normal size.
- The streaming window size and position, and the controller overlay position, are remembered.
- Controllers dialog: controller buttons and keys no longer trigger Play or close the dialog by accident.

[0.9.0]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.9.0
[0.8.0]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.8.0
[0.7.0]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.7.0
[0.6.0]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.6.0
[0.5.0]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.5.0
[0.4.0]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.4.0
[0.3.3]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.3.3
[0.3.2]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.3.2
[0.3.1]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.3.1
[0.3.0]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.3.0
[0.2.0]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.2.0
[0.1.1]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.1.1
[0.1.0]: https://github.com/iDevGIS/PS-WRAP/releases/tag/v0.1.0
