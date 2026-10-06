# Smoke test อัตโนมัติของ PS-WRAP.exe ที่ build แล้ว — รันก่อน deploy ทุกครั้ง · exit 0 = ผ่าน, 1 = พัง
# ใช้: .\scripts\smoke.ps1 [-Out <dir>] [-Sizes 1920x1080,1280x800,1024x768] [-Profile pswrap-test] [-SkipTray] [-SkipDevices] [-NoSnapshot]
#
# ทำอะไร (ไม่เริ่มสตรีม ไม่ต่อ PS5 — ไม่กด Enter/คลิกการ์ดคอนโซลเด็ดขาด):
#   1. copy build/gui (exe + onnxruntime.dll + fx/ + models/) ไป snapshot ใน %TEMP% แล้วรันจากตรงนั้น
#      → lead build ใหม่ระหว่างเทสได้ (ld ไม่ชน exe ที่รันอยู่ และ build.ps1 ไม่ kill เราเพราะ path ไม่ใช่ \ps-wrap\build\)
#   2. เปิดด้วย --profile <Profile> + QT_FORCE_STDERR_LOGGING=1 (stderr → ไฟล์) · รอหน้าต่างหลัก (class *QWindowIcon* ของ pid เรา)
#   3. ทุกขนาด (logical px; คูณ DPI scale เอง): home → controller popup → mic preview → facecam preview → Settings ทุกหน้า (9 หน้า)
#      - หา chip แถบล่างจาก "ภาพ" (UIA ใช้ไม่ได้: หน้าต่างหลักเรนเดอร์ QML ผ่าน QQuickRenderControl → UIA tree ว่าง ตรวจแล้ว 2026-10-06)
#        แถบล่างสูง 64, chip สูง 36 อยู่กลางแถบ → สแกนแถว (ล่าง-32-15) หา run ของพิกเซลที่ไม่ใช่สี surface #141a22
#        5 run ขวาสุด = [mic][cam][pad][Recordings][Discovery] (ตาม RowLayout ใน MainView.qml hintBar) · Recordings/Discovery ไม่คลิก
#      - Settings: ปุ่ม Menu (VK_APPS → MainView เปิด Settings) แล้ว PageDown ทีละหน้า (SettingsDialog Keys: PageDown = หน้าถัดไป)
#      - ทุก dialog ตรวจว่า "เปิดจริง" ด้วย pixel diff เทียบภาพก่อนกด และ Esc แล้วกลับ home จริง
#   4. tray menu (best effort): หาไอคอน tray ชื่อ "PS-WRAP" ด้วย UIA ของ taskbar — ถ้ามี PS-WRAP ตัวอื่นรันอยู่ (ไอคอนชื่อซ้ำ) → SKIP
#   5. FAIL ถ้า: หน้าต่างไม่ขึ้น / process ตาย / stderr มี QML error / dialog ไม่เปิด · WARN: หน้าที่ไม่เปลี่ยน/ปิดไม่ลง
#   6. ปิดแอปที่เราเปิดเท่านั้น (ตาม pid) + คืน settings/current_profile ด้วย "--profile=<เดิม>" list แล้วตรวจ registry
#      - ห้าม kill ตอนกล้องเปิด (UVC ค้างทั้งระบบ ต้องถอดเสียบ — เจอแล้ว 2026-10-06): ทุกครั้งที่ปิด cam/mic preview หรือ Settings
#        (หน้า General มี facecam preview สด — เปิดกล้องแม้ใช้ -SkipDevices) → Esc → รอ idle → รอ Windows บอกว่ากล้องถูกปล่อย
#        (ConsentStore\webcam\NonPackaged\<exe>: LastUsedTimeStop ≠ 0 · อ่านอย่างเดียว)
#      - ตอนจบ: Esc กลับ home → รอกล้องปล่อย → WM_CLOSE ให้แอปออกเอง (แอปรอ worker กล้องปิด device ก่อนจบ)
#        → ไม่ออกใน -ExitTimeoutSec ค่อย kill + เตือนถ้ากล้องอาจยังเปิด (อาจต้องถอดเสียบ)
# ผลลัพธ์: <Out>\*.png, contact-sheet.png, summary.txt, stderr.txt
param(
    [string]$Out = "",
    [string[]]$Sizes = @("1920x1080", "1280x800", "1024x768"),
    [string]$Profile = "pswrap-test",
    [string]$Exe = "",                 # default: ps-wrap\build\gui\PS-WRAP.exe (ห้ามชี้ไป dist ของลูกพี่)
    [switch]$NoSnapshot,               # รันจาก build dir ตรงๆ (lead build ระหว่างนี้จะ kill เรา)
    [switch]$SkipTray,
    [switch]$SkipDevices,              # ไม่เปิด mic/facecam preview
    [switch]$ForceDevices,             # เปิด mic/cam preview แม้มี PS-WRAP ตัวอื่นรันอยู่ (ปกติข้าม กันแย่งกล้อง/ไมค์ตอนลูกพี่เล่น)
    [int]$StartTimeoutSec = 45,
    [int]$ReleaseTimeoutSec = 10,      # รอกล้องถูกปล่อยหลังปิด preview/Settings ได้นานสุด
    [int]$ExitTimeoutSec = 15,         # หลัง WM_CLOSE รอแอปออกเองได้นานสุด ก่อน kill
    [string[]]$AllowPattern = @()      # regex ของบรรทัด stderr ที่ยอมรับได้ (ไม่ FAIL)
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$src = Join-Path $root "ps-wrap"
if (-not $Exe) { $Exe = Join-Path $src "build\gui\PS-WRAP.exe" }
if ($Exe -like "*\dist\*") { Write-Error "ห้ามเทส exe ใน dist\ (ของลูกพี่)"; exit 1 }
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
if (-not $Out) { $Out = Join-Path $env:TEMP "pswrap-smoke\$stamp" }
$outDir = if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $root $Out }
New-Item -ItemType Directory -Force $outDir | Out-Null
$errFile = Join-Path $outDir "stderr.txt"; $outFile = Join-Path $outDir "stdout.txt"
$env:PATH = "C:\msys64\mingw64\bin;$src\build\third-party\cpp-steam-tools;$env:PATH"
$env:QT_FORCE_STDERR_LOGGING = "1"

# ---------------- ผลลัพธ์ ----------------
$script:results = New-Object System.Collections.Generic.List[object]
$script:shots = New-Object System.Collections.Generic.List[object]
function Rec($status, $step, $msg = "") {
    $script:results.Add([pscustomobject]@{ Status = $status; Step = $step; Msg = $msg })
    $c = @{ PASS = "Green"; FAIL = "Red"; WARN = "Yellow"; SKIP = "DarkGray"; INFO = "Gray" }[$status]
    Write-Host ("[{0}] {1} {2}" -f $status, $step, $msg) -ForegroundColor $c
}

# ---------------- Win32 + image helpers ----------------
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
if (-not ("PSWSmoke" -as [type])) {
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public static class PSWSmoke {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  public delegate bool CB(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(CB cb, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, IntPtr w, IntPtr l, uint flags, uint timeout, out IntPtr result);
  // GUI thread ตอบ WM_NULL ภายใน ms ไหม (ค้าง = ไม่ตอบ)
  public static bool Responsive(IntPtr h, uint ms) { IntPtr r; return SendMessageTimeout(h, 0, IntPtr.Zero, IntPtr.Zero, 0x0002 /*SMTO_ABORTIFHUNG*/, ms, out r) != IntPtr.Zero; }
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int attr, out RECT r, int size);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }

  // ทุกหน้าต่างที่มองเห็นของ pid: "hwnd|class|title|L,T,R,B"
  public static string[] WindowsOf(uint pid) {
    var list = new System.Collections.Generic.List<string>();
    EnumWindows((h, l) => {
      uint wp; GetWindowThreadProcessId(h, out wp);
      if (wp == pid && IsWindowVisible(h)) {
        RECT r; GetWindowRect(h, out r);
        var c = new StringBuilder(256); GetClassName(h, c, 256);
        var t = new StringBuilder(256); GetWindowText(h, t, 256);
        list.Add(h.ToInt64() + "|" + c + "|" + t + "|" + r.L + "," + r.T + "," + r.R + "," + r.B);
      }
      return true; }, IntPtr.Zero);
    return list.ToArray();
  }
  // สัดส่วนพิกเซล (สุ่มทุก step) ที่ต่างกันเกิน thr (ผลรวม |dR|+|dG|+|dB|) — ภาพ 32bpp BGRA จาก LockBits
  public static double Diff(byte[] a, byte[] b, int w, int h, int stride, int step, int thr) { return DiffRect(a, b, stride, 0, 0, w, h, step, thr); }
  public static double DiffRect(byte[] a, byte[] b, int stride, int x0, int y0, int x1, int y1, int step, int thr) {
    if (a.Length != b.Length) return 1.0;
    long n = 0, d = 0;
    for (int y = y0; y < y1; y += step) for (int x = x0; x < x1; x += step) {
      int i = y * stride + x * 4; n++;
      int s = Math.Abs(a[i] - b[i]) + Math.Abs(a[i+1] - b[i+1]) + Math.Abs(a[i+2] - b[i+2]);
      if (s > thr) d++;
    }
    return n == 0 ? 0 : (double)d / n;
  }
  // run ของพิกเซลที่ "ไม่ใช่สีพื้น" บนแถว y: คืน [start0,end0,start1,end1,...] (รวม gap < maxGap, ทิ้ง run < minLen)
  public static int[] Runs(byte[] a, int w, int stride, int y, int r0, int g0, int b0, int thr, int minLen, int maxGap) {
    var res = new System.Collections.Generic.List<int>();
    int start = -1, last = -1;
    for (int x = 0; x < w; x++) {
      int i = y * stride + x * 4;
      int s = Math.Abs(a[i+2] - r0) + Math.Abs(a[i+1] - g0) + Math.Abs(a[i] - b0);
      if (s > thr) {
        if (start >= 0 && x - last > maxGap) { if (last - start + 1 >= minLen) { res.Add(start); res.Add(last); } start = -1; }
        if (start < 0) start = x;
        last = x;
      }
    }
    if (start >= 0 && last - start + 1 >= minLen) { res.Add(start); res.Add(last); }
    return res.ToArray();
  }
}
"@
}
# type แยก (PSWSmoke ถูก Add-Type ค้างใน session PowerShell เดิมได้ — เพิ่ม method ในนั้นจะไม่มีผลจนเปิด shell ใหม่)
if (-not ("PSWSmokeExit" -as [type])) {
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class PSWSmokeExit {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
}
"@
}
[PSWSmoke]::SetProcessDPIAware() | Out-Null
$scale = [System.Drawing.Graphics]::FromHwnd([IntPtr]::Zero).DpiX / 96.0

function Grab($x, $y, $w, $h) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($x, $y, 0, 0, (New-Object System.Drawing.Size $w, $h)); $g.Dispose()
    return $bmp
}
function Bytes($bmp) {
    $r = New-Object System.Drawing.Rectangle 0, 0, $bmp.Width, $bmp.Height
    $d = $bmp.LockBits($r, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $buf = New-Object byte[] ($d.Stride * $bmp.Height)
    [Runtime.InteropServices.Marshal]::Copy($d.Scan0, $buf, 0, $buf.Length)
    $o = [pscustomobject]@{ B = $buf; W = $bmp.Width; H = $bmp.Height; S = $d.Stride }
    $bmp.UnlockBits($d); return $o
}
function ClientRect {
    $r = New-Object PSWSmoke+RECT; [PSWSmoke]::GetClientRect($script:h, [ref]$r) | Out-Null
    $pt = New-Object PSWSmoke+POINT; [PSWSmoke]::ClientToScreen($script:h, [ref]$pt) | Out-Null
    return [pscustomobject]@{ X = $pt.X; Y = $pt.Y; W = $r.R; H = $r.B }
}
function GrabClient { $c = ClientRect; if ($c.W -le 0 -or $c.H -le 0) { return $null }; return (Bytes (Grab $c.X $c.Y $c.W $c.H)) }
# diff เฉพาะแถบ sidebar ของ Settings (x 0..260, y 110..ล่าง-70 logical) — ไฮไลต์หมวดย้าย = เปลี่ยนหน้าจริง
function DiffSidebar($a, $b) {
    if (-not $a -or -not $b -or $a.W -ne $b.W -or $a.H -ne $b.H) { return 1.0 }
    return [PSWSmoke]::DiffRect($a.B, $b.B, $a.S, 0, [int](110 * $scale), [Math]::Min($a.W, [int](260 * $scale)), $a.H - [int](70 * $scale), 3, 40)
}
function DiffFrac($a, $b) {
    if (-not $a -or -not $b -or $a.W -ne $b.W -or $a.H -ne $b.H) { return 1.0 }
    return [PSWSmoke]::Diff($a.B, $b.B, $a.W, $a.H, $a.S, 6, 40)
}

# รอให้แอป "นิ่ง" หลัง action: (1) GUI thread ตอบ WM_NULL ต่อเนื่อง 700ms (วัดช่วงที่ค้าง) (2) ภาพไม่ขยับ (animation จบ)
# gotcha: เฟรมตอน UI ค้างก็ "นิ่ง" → ต้องเช็ค responsive ก่อน ไม่งั้นถ่ายติดกลาง transition (เคยได้ภาพ General ตอนจะถ่าย Video)
function Settle($label) {
    $sw = [Diagnostics.Stopwatch]::StartNew(); $okSince = -1; $blocked = 0; $blockStart = -1
    Start-Sleep -Milliseconds 150
    while ($sw.ElapsedMilliseconds -lt 20000) {
        $t = $sw.ElapsedMilliseconds
        if ([PSWSmoke]::Responsive($script:h, 100)) {
            if ($blockStart -ge 0) { $blocked += $sw.ElapsedMilliseconds - $blockStart; $blockStart = -1 }
            if ($okSince -lt 0) { $okSince = $t }
            if ($t - $okSince -ge 700) { break }
            Start-Sleep -Milliseconds 50
        } else {
            if ($blockStart -lt 0) { $blockStart = $t }
            $okSince = -1
            if (-not (Alive)) { break }
        }
    }
    if ($blockStart -ge 0) { $blocked += $sw.ElapsedMilliseconds - $blockStart }
    $prev = GrabClient
    for ($i = 0; $i -lt 15; $i++) { Start-Sleep -Milliseconds 200; $cur = GrabClient; if ((DiffFrac $prev $cur) -lt 0.002) { break }; $prev = $cur }
    if ($blocked -ge 500) { Rec WARN "$label" "UI thread ค้าง ~${blocked}ms (ไม่ตอบ WM_NULL)" }
    $script:lastSettleMs = $sw.ElapsedMilliseconds
    return $blocked
}

# ---------------- process / window ----------------
function Alive { $script:p.Refresh(); return -not $script:p.HasExited }
function Resolve-Main {
    $best = [IntPtr]::Zero; $bestA = -1
    foreach ($line in [PSWSmoke]::WindowsOf([uint32]$script:p.Id)) {
        $f = $line -split "\|"; $rc = $f[3] -split ","
        $a = ([int]$rc[2] - [int]$rc[0]) * ([int]$rc[3] - [int]$rc[1])
        if ($f[1] -like "*QWindowIcon*" -and $a -gt $bestA) { $bestA = $a; $best = [IntPtr][int64]$f[0] }
    }
    return $best
}
function Ensure-Front {
    for ($i = 0; $i -lt 6; $i++) {
        if ([PSWSmoke]::GetForegroundWindow() -eq $script:h) { return $true }
        if ([PSWSmoke]::IsIconic($script:h)) { [PSWSmoke]::ShowWindow($script:h, 9) | Out-Null } else { [PSWSmoke]::ShowWindow($script:h, 5) | Out-Null }
        [PSWSmoke]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [PSWSmoke]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)   # ปลด foreground lock
        [PSWSmoke]::SetForegroundWindow($script:h) | Out-Null
        Start-Sleep -Milliseconds 400
    }
    return ([PSWSmoke]::GetForegroundWindow() -eq $script:h)
}
function Check-Alive($step) {
    if (Alive) { return $true }
    Rec FAIL $step "process ตาย (exit code $($script:p.ExitCode)) — crash?"
    $script:dead = $true
    return $false
}
function Snap($name) {
    if (-not (Ensure-Front)) { Rec WARN "snap $name" "หน้าต่างไม่อยู่หน้าสุด — ไม่ถ่าย (กันจับหน้าต่างอื่น)"; return $null }
    $r = New-Object PSWSmoke+RECT
    # extended frame bounds = กรอบที่เห็นจริง (ไม่รวม invisible resize border 7px ที่จะติดพื้นหลังมาด้วย)
    if ([PSWSmoke]::DwmGetWindowAttribute($script:h, 9, [ref]$r, 16) -ne 0) { [PSWSmoke]::GetWindowRect($script:h, [ref]$r) | Out-Null }
    $bmp = Grab $r.L $r.T ($r.R - $r.L) ($r.B - $r.T)
    $f = Join-Path $outDir "$name.png"; $bmp.Save($f, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    $script:shots.Add([pscustomobject]@{ Name = $name; File = $f })
    return $f
}
function Park-Mouse {
    # เมาส์ไว้ที่ title bar (กัน hover/tooltip บนภาพ) — ไม่คลิก
    $r = New-Object PSWSmoke+RECT; [PSWSmoke]::GetWindowRect($script:h, [ref]$r) | Out-Null
    $c = ClientRect
    [PSWSmoke]::SetCursorPos([int](($r.L + $r.R) / 2), [int](($r.T + $c.Y) / 2)) | Out-Null
}
function Click-Client($lx, $ly) {   # พิกัด logical ใน client area
    if (-not (Ensure-Front)) { return $false }
    $c = ClientRect
    $px = $c.X + [int]($lx * $scale); $py = $c.Y + [int]($ly * $scale)
    if ($lx -lt 0 -or $ly -lt 0 -or $px -ge $c.X + $c.W -or $py -ge $c.Y + $c.H) { return $false }
    [PSWSmoke]::SetCursorPos($px, $py) | Out-Null; Start-Sleep -Milliseconds 250
    [PSWSmoke]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero); [PSWSmoke]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 700   # กันคลิกถัดไปเป็น double-click (upstream สลับ fullscreen)
    return $true
}
function Key($sendKeys) { if (Ensure-Front) { [System.Windows.Forms.SendKeys]::SendWait($sendKeys); Start-Sleep -Milliseconds 200 } }
function Key-Menu { if (Ensure-Front) { [PSWSmoke]::keybd_event(0x5D, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60; [PSWSmoke]::keybd_event(0x5D, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 200 } }
function Set-Size($s) {
    $w, $hh = $s -split "x"
    foreach ($i in 1..2) {   # 2 รอบ: แอป restore geometry ที่เซฟไว้ทับรอบแรกได้
        if ([PSWSmoke]::IsIconic($script:h)) { [PSWSmoke]::ShowWindow($script:h, 9) | Out-Null }
        [PSWSmoke]::SetWindowPos($script:h, [IntPtr]::Zero, 0, 0, [int]([int]$w * $scale), [int]([int]$hh * $scale), 0x0040) | Out-Null
        Start-Sleep -Seconds 1
    }
    Start-Sleep -Milliseconds 800
}

# chip แถบล่าง: คืน hashtable mic/cam/pad = จุดกึ่งกลาง (logical, client coords)
function Find-Chips {
    $img = GrabClient
    if (-not $img) { return $null }
    $cy = $img.H - [int](32 * $scale)               # กลางแถบล่าง (สูง 64)
    $y = $cy - [int](15 * $scale)                    # ใกล้ขอบบน chip (สูง 36) — ใต้/เหนือไอคอนและตัวหนังสือ
    $runs = [PSWSmoke]::Runs($img.B, $img.W, $img.S, $y, 0x14, 0x1a, 0x22, 15, [int](24 * $scale), [int](4 * $scale))
    $list = @(); for ($i = 0; $i -lt $runs.Count; $i += 2) { $list += , @($runs[$i], $runs[$i + 1]) }
    $script:lastRuns = ($list | ForEach-Object { "{0}-{1}" -f [int]($_[0] / $scale), [int]($_[1] / $scale) }) -join " "
    if ($list.Count -lt 5) { return $null }
    $n = $list.Count
    $mk = { param($r) @{ X = ($r[0] + $r[1]) / 2.0 / $scale; Y = $cy / $scale } }
    return @{ mic = (& $mk $list[$n - 5]); cam = (& $mk $list[$n - 4]); pad = (& $mk $list[$n - 3]) }
}

# เปิด popup จาก chip → ถ่าย → Esc → ต้องกลับ home
function Test-Chip($which, $label, $tag) {
    $step = "$tag $label"
    $chips = Find-Chips
    if (-not $chips) { Rec FAIL $step "หา chip แถบล่างไม่เจอ (runs: $script:lastRuns)"; return }
    Park-Mouse; Start-Sleep -Milliseconds 400
    $before = GrabClient
    $pt = $chips[$which]
    if (-not (Click-Client $pt.X $pt.Y)) { Rec FAIL $step "คลิก chip ไม่ได้ (หน้าต่างไม่อยู่หน้าสุด/พิกัดนอกจอ)"; return }
    Park-Mouse; Settle "$step open" | Out-Null
    if (-not (Check-Alive $step)) { return }
    $after = GrabClient; $d = DiffFrac $before $after
    Snap "$tag-$label" | Out-Null
    if ($d -lt $script:openThr) { Rec FAIL $step ("popup ไม่เปิด (diff {0:P1}) — คลิกที่ {1:N0},{2:N0} logical" -f $d, $pt.X, $pt.Y) }
    else { Rec PASS $step ("เปิด (diff {0:P1})" -f $d) }
    Close-ToHome $step
    # preview เปิดกล้อง/ไมค์ → ต้องปล่อยก่อนเทสถัดไป (และก่อนจบ/kill)
    if ($which -eq "cam") { Wait-Released "webcam" $step $ReleaseTimeoutSec "WARN" | Out-Null }
    elseif ($which -eq "mic") { Wait-Released "microphone" $step 3 "INFO" | Out-Null }
}
function Close-ToHome($step) {
    Key "{ESC}"; Settle "$step close" | Out-Null
    $now = GrabClient; $d = DiffFrac $script:homeImg $now
    if ($d -gt $script:closeThr) {
        Rec WARN $step ("Esc แล้วยังไม่กลับ home (diff {0:P1}) — กด Esc อีกครั้ง" -f $d)
        Key "{ESC}"; Settle "$step close (2)" | Out-Null
        $now = GrabClient; $d2 = DiffFrac $script:homeImg $now
        if ($d2 -gt $script:closeThr) { Rec FAIL $step ("ปิดไม่ลง กลับ home ไม่ได้ (diff {0:P1})" -f $d2); $script:lost = $true }
    }
}

# ---------------- กล้อง/ไมค์: รอ Windows บอกว่าปล่อยแล้ว (ห้าม kill ตอนกล้องเปิด) ----------------
# desktop app: ConsentStore\<webcam|microphone>\NonPackaged\<path exe เต็ม แทน \ ด้วย #> · LastUsedTimeStop = 0 ระหว่างใช้ · อ่านอย่างเดียว
$consentRoot = "HKCU:\Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore"
$script:camHeld = $false
function Device-InUse($cap) {
    if (-not $runExe) { return $false }
    $name = $runExe -replace '\\', '#'
    try {
        $k = Get-ChildItem (Join-Path $consentRoot "$cap\NonPackaged") -ErrorAction Stop | Where-Object { $_.PSChildName -ieq $name } | Select-Object -First 1
        if (-not $k) { return $false }   # ยังไม่เคยใช้เลย
        $v = Get-ItemProperty -LiteralPath $k.PSPath -ErrorAction Stop
        return ([int64]$v.LastUsedTimeStart -ne 0 -and [int64]$v.LastUsedTimeStop -eq 0)
    } catch { return $false }
}
# คืน $true = ปล่อยแล้ว (หรือไม่เคยเปิด) · $level = ระดับที่รายงานเมื่อไม่ปล่อยในเวลา (webcam = WARN, mic = INFO: ไมค์อาจถูกใช้ต่อโดยตั้งใจ)
function Wait-Released($cap, $step, $timeoutSec, $level) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ((Device-InUse $cap) -and $sw.Elapsed.TotalSeconds -lt $timeoutSec) {
        if (-not (Alive)) { break }
        Start-Sleep -Milliseconds 200
    }
    $held = Device-InUse $cap
    if ($cap -eq "webcam") { $script:camHeld = $held }
    if ($held) { Rec $level "$step $cap" ("ยังไม่ถูกปล่อยหลังรอ {0}s (ConsentStore LastUsedTimeStop = 0)" -f $timeoutSec); return $false }
    if ($sw.ElapsedMilliseconds -ge 250) { Rec INFO "$step $cap" ("ปล่อยแล้วใน {0} ms หลังปิด" -f $sw.ElapsedMilliseconds) }
    return $true
}

function Test-Settings($tag) {
    $step = "$tag settings"
    Park-Mouse
    Key-Menu; Settle "$step open" | Out-Null
    if (-not (Check-Alive $step)) { return }
    $prev = GrabClient; $d = DiffFrac $script:homeImg $prev
    if ($d -lt $script:openThr) { Rec FAIL $step ("Settings ไม่เปิดด้วยปุ่ม Menu (diff {0:P1})" -f $d); return }
    $pages = @("General", "Video", "Stream", "Audio-WiFi", "Consoles", "Keys", "Controllers", "Remote", "Config")
    for ($i = 0; $i -lt $pages.Count; $i++) {
        if ($i -gt 0) {
            Key "{PGDN}"; Settle "$step/$($pages[$i])" | Out-Null
            if (-not (Check-Alive "$step/$($pages[$i])")) { return }
            $cur = GrabClient; $d = DiffSidebar $prev $cur; $prev = $cur; $ms = $script:lastSettleMs
            if ($d -lt 0.02) { Rec WARN "$step/$($pages[$i])" ("PageDown แล้วไฮไลต์ sidebar ไม่ย้าย (diff {0:P1}) — focus ค้างใน control? ภาพหน้านี้อาจเป็นหน้าเดิม" -f $d) }
            else { $script:pageMs += "$($pages[$i])=${ms}ms " }
        }
        Snap ("{0}-settings-{1}-{2}" -f $tag, $i, $pages[$i]) | Out-Null
    }
    Rec PASS $step "9 หน้า · เวลาจน settle: $script:pageMs"; $script:pageMs = ""
    Close-ToHome $step
    Wait-Released "webcam" $step $ReleaseTimeoutSec "WARN" | Out-Null   # หน้า General มี facecam preview สด
}

# ---------------- tray (best effort) ----------------
function Test-Tray {
    $step = "tray menu"
    if ($SkipTray) { Rec SKIP $step "-SkipTray"; return }
    $others = Get-Process PS-WRAP, chiaki -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $script:p.Id }
    if ($others) { Rec SKIP $step "มี PS-WRAP/chiaki ตัวอื่นรันอยู่ (pid $($others.Id -join ',')) — ไอคอน tray ชื่อซ้ำ แยกไม่ได้"; return }
    try {
        Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
        $A = [System.Windows.Automation.AutomationElement]
        $desk = $A::RootElement
        $btnCond = New-Object System.Windows.Automation.PropertyCondition($A::ControlTypeProperty, [System.Windows.Automation.ControlType]::Button)
        $findIcon = {
            param($scope)
            @($scope.FindAll([System.Windows.Automation.TreeScope]::Descendants, $btnCond) | Where-Object { $_.Current.Name -match '^PS-WRAP(\s|$)' -and -not $_.Current.IsOffscreen })
        }
        $tray = $desk.FindFirst([System.Windows.Automation.TreeScope]::Children, (New-Object System.Windows.Automation.PropertyCondition($A::ClassNameProperty, "Shell_TrayWnd")))
        if (-not $tray) { Rec SKIP $step "ไม่พบ Shell_TrayWnd"; return }
        $icons = & $findIcon $tray
        $openedOverflow = $false
        if ($icons.Count -eq 0) {
            # อยู่ใน overflow (^) — เปิด flyout แล้วหาใหม่
            $chev = @($tray.FindAll([System.Windows.Automation.TreeScope]::Descendants, $btnCond) | Where-Object { $_.Current.Name -match 'Hidden Icons|ซ่อน' }) | Select-Object -First 1
            if ($chev) {
                $b = $chev.Current.BoundingRectangle
                [PSWSmoke]::SetCursorPos([int]($b.X + $b.Width / 2), [int]($b.Y + $b.Height / 2)) | Out-Null; Start-Sleep -Milliseconds 200
                [PSWSmoke]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero); [PSWSmoke]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
                Start-Sleep -Milliseconds 1200; $openedOverflow = $true
                foreach ($cls in "TopLevelWindowForOverflowXamlIsland", "NotifyIconOverflowWindow") {
                    $fly = $desk.FindFirst([System.Windows.Automation.TreeScope]::Children, (New-Object System.Windows.Automation.PropertyCondition($A::ClassNameProperty, $cls)))
                    if ($fly) { $icons = & $findIcon $fly; if ($icons.Count) { break } }
                }
            }
        }
        if ($icons.Count -ne 1) {
            if ($openedOverflow) { [System.Windows.Forms.SendKeys]::SendWait("{ESC}") }
            Rec SKIP $step "เจอไอคอน tray ชื่อ PS-WRAP $($icons.Count) อัน (ต้องเจอ 1 พอดี)"; return
        }
        $b = $icons[0].Current.BoundingRectangle
        $before = @([PSWSmoke]::WindowsOf([uint32]$script:p.Id))
        [PSWSmoke]::SetCursorPos([int]($b.X + $b.Width / 2), [int]($b.Y + $b.Height / 2)) | Out-Null; Start-Sleep -Milliseconds 250
        [PSWSmoke]::mouse_event(0x0008, 0, 0, 0, [UIntPtr]::Zero); [PSWSmoke]::mouse_event(0x0010, 0, 0, 0, [UIntPtr]::Zero)   # คลิกขวา
        Start-Sleep -Milliseconds 1200
        $menu = $null; $menuA = -1
        foreach ($line in [PSWSmoke]::WindowsOf([uint32]$script:p.Id)) {
            $f = $line -split "\|"
            if ([IntPtr][int64]$f[0] -eq $script:h) { continue }
            $rc = $f[3] -split ","; $a = ([int]$rc[2] - [int]$rc[0]) * ([int]$rc[3] - [int]$rc[1])
            if ($a -gt $menuA -and $a -gt 0) { $menuA = $a; $menu = $rc }
        }
        if (-not $menu) { [System.Windows.Forms.SendKeys]::SendWait("{ESC}"); Rec FAIL $step "คลิกขวาไอคอนแล้วไม่มีเมนูของ pid เราโผล่"; return }
        $bmp = Grab ([int]$menu[0]) ([int]$menu[1]) ([int]$menu[2] - [int]$menu[0]) ([int]$menu[3] - [int]$menu[1])
        $f = Join-Path $outDir "tray-menu.png"; $bmp.Save($f, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
        $script:shots.Add([pscustomobject]@{ Name = "tray-menu"; File = $f })
        [System.Windows.Forms.SendKeys]::SendWait("{ESC}"); Start-Sleep -Milliseconds 500
        if ($openedOverflow) { [System.Windows.Forms.SendKeys]::SendWait("{ESC}"); Start-Sleep -Milliseconds 300 }
        Rec PASS $step "เมนูขนาด $([int]$menu[2] - [int]$menu[0])x$([int]$menu[3] - [int]$menu[1]) px"
    } catch { Rec SKIP $step "UIA error: $($_.Exception.Message)" }
}

# ---------------- stderr ----------------
$failRx = 'TypeError|ReferenceError|is not a type|polish\(\) loop|Binding loop|is not defined|Cannot assign|Unable to assign|is not installed|failed to load component|Cannot read property|Cannot call method|qrc:/[^\s]*:\d+(:\d+)?:? |file:///[^\s]*\.qml:\d+'
# ข้อความที่รู้แล้วว่าเป็นของ upstream และไม่เป็นอันตราย → รายงานเป็น KNOWN ไม่ FAIL (ตรวจ 2026-10-06)
$knownRx = @(
    'QML Shortcut: Shortcut: Only binding to one of multiple key bindings associated with'   # Main.qml StandardKey.Cancel (upstream a9a2805 มีเหมือนกัน)
)
function Scan-Stderr {
    $lines = @(Get-Content $errFile -ErrorAction SilentlyContinue) + @(Get-Content $outFile -ErrorAction SilentlyContinue)
    $allow = @($AllowPattern) + $knownRx
    $hit = @($lines | Where-Object { $_ -match $failRx } | Select-Object -Unique)
    $bad = @($hit | Where-Object { $l = $_; -not ($allow | Where-Object { $l -match $_ }) })
    $known = @($hit | Where-Object { $l = $_; ($allow | Where-Object { $l -match $_ }) })
    $warn = @($lines | Where-Object { $_ -notmatch $failRx -and $_ -match 'warning|Warning|rror|qml|QML|Failed|failed' } | Select-Object -Unique)
    return @{ Bad = $bad; Known = $known; Warn = $warn; Total = $lines.Count }
}

# ---------------- contact sheet ----------------
function Contact-Sheet {
    if ($script:shots.Count -eq 0) { return }
    $cols = 6; $tw = 400; $lab = 22
    $items = foreach ($s in $script:shots) { $img = [System.Drawing.Image]::FromFile($s.File); $th = [int]($img.Height * $tw / [double]$img.Width); [pscustomobject]@{ Name = $s.Name; Img = $img; TH = $th } }
    $rows = [math]::Ceiling($items.Count / $cols)
    $rowH = @(); for ($r = 0; $r -lt $rows; $r++) { $rowH += (($items | Select-Object -Skip ($r * $cols) -First $cols | Measure-Object TH -Maximum).Maximum + $lab + 8) }
    $W = $cols * ($tw + 8) + 8; $H = ($rowH | Measure-Object -Sum).Sum + 8
    $sheet = New-Object System.Drawing.Bitmap $W, ([int]$H)
    $g = [System.Drawing.Graphics]::FromImage($sheet); $g.Clear([System.Drawing.Color]::FromArgb(24, 24, 28))
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $font = New-Object System.Drawing.Font "Segoe UI", 10; $brush = [System.Drawing.Brushes]::White
    $y = 8
    for ($r = 0; $r -lt $rows; $r++) {
        $x = 8
        foreach ($it in ($items | Select-Object -Skip ($r * $cols) -First $cols)) {
            $g.DrawString($it.Name, $font, $brush, $x, $y + 2)
            $g.DrawImage($it.Img, $x, $y + $lab, $tw, $it.TH)
            $x += $tw + 8
        }
        $y += $rowH[$r]
    }
    $f = Join-Path $outDir "contact-sheet.png"; $sheet.Save($f, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $sheet.Dispose(); foreach ($it in $items) { $it.Img.Dispose() }
    return $f
}

# ---------------- reset current_profile ----------------
$regSettings = "HKCU:\Software\PS-WRAP\PS-WRAP\settings"
$origProfile = [string](Get-ItemProperty $regSettings -ErrorAction SilentlyContinue).current_profile
function Reset-Profile($exe) {
    # gotcha: --profile X เซฟ settings/current_profile=X ถาวร → ตัว dist ของลูกพี่จะเปิดเป็น profile ทดสอบ
    # คืนด้วย "--profile=<เดิม>" list (ไม่เปิด GUI) · ต้อง -Wait: exe เป็น GUI subsystem, & ไม่รอ → อ่าน registry ก่อนมันเขียน
    for ($i = 0; $i -lt 4; $i++) {
        try { Start-Process -FilePath $exe -ArgumentList "--profile=$origProfile", "list" -Wait -NoNewWindow -RedirectStandardOutput (Join-Path $outDir "reset-out.txt") -RedirectStandardError (Join-Path $outDir "reset-err.txt") } catch {}
        if ([string](Get-ItemProperty $regSettings -ErrorAction SilentlyContinue).current_profile -eq $origProfile) { return $true }
        Start-Sleep -Seconds 1
    }
    return $false
}

# ================= main =================
"PS-WRAP smoke · out: $outDir · DPI scale $scale · profile '$Profile' · current_profile เดิม '$origProfile'"
# exe อาจหายชั่วคราวระหว่าง lead build → รอ (สูงสุด 120s) จนไฟล์มีและนิ่ง
$t0 = Get-Date
while ($true) {
    if (Test-Path $Exe) {
        $w1 = (Get-Item $Exe).LastWriteTimeUtc; $l1 = (Get-Item $Exe).Length; Start-Sleep -Seconds 2
        if ((Test-Path $Exe) -and (Get-Item $Exe).LastWriteTimeUtc -eq $w1 -and (Get-Item $Exe).Length -eq $l1) { break }
    } else { Start-Sleep -Seconds 2 }
    if (((Get-Date) - $t0).TotalSeconds -gt 120) { Rec FAIL "exe" "ไม่พบ/ไม่นิ่ง: $Exe"; exit 1 }
}
$exeInfo = Get-Item $Exe
Rec INFO "exe" "$Exe ($([math]::Round($exeInfo.Length / 1MB, 1)) MB, built $($exeInfo.LastWriteTime))"

$runExe = $Exe
$snapDir = $null
if (-not $NoSnapshot) {
    $snapDir = Join-Path $env:TEMP "pswrap-smoke\bin-$stamp-$PID"
    New-Item -ItemType Directory -Force $snapDir | Out-Null
    $bdir = Split-Path $Exe
    Copy-Item $Exe $snapDir
    foreach ($extra in "onnxruntime.dll", "fx", "models") { if (Test-Path (Join-Path $bdir $extra)) { Copy-Item (Join-Path $bdir $extra) $snapDir -Recurse } }
    Get-ChildItem $bdir -Filter *.dll | Where-Object { $_.Name -ne "onnxruntime.dll" } | Copy-Item -Destination $snapDir
    $runExe = Join-Path $snapDir (Split-Path $Exe -Leaf)
}

# single instance ต่อ profile: ถ้ามีตัวที่ใช้ profile เดียวกันรันอยู่ ตัวเราจะส่ง activate แล้วออกทันที
$clash = Get-Process PS-WRAP, chiaki -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -eq "PS-WRAP:$Profile" }
if ($clash) { Rec FAIL "start" "มี PS-WRAP profile '$Profile' รันอยู่แล้ว (pid $($clash.Id -join ',')) — ใช้ -Profile อื่น เช่น pswrap-snap"; exit 1 }
$othersRunning = @(Get-Process PS-WRAP, chiaki -ErrorAction SilentlyContinue)
$devicesOk = -not $SkipDevices -and ($ForceDevices -or $othersRunning.Count -eq 0)

$script:p = $null; $script:dead = $false; $script:lost = $false
try {
    $script:p = Start-Process -FilePath $runExe -ArgumentList "--profile", $Profile -WorkingDirectory (Split-Path $runExe) -PassThru -RedirectStandardError $errFile -RedirectStandardOutput $outFile
    Rec INFO "start" "pid $($script:p.Id)"
    $script:h = [IntPtr]::Zero
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt $StartTimeoutSec) {
        if (-not (Alive)) { break }
        $script:h = Resolve-Main
        if ($script:h -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 500
    }
    if (-not (Alive)) { Rec FAIL "start" "process ออกก่อนหน้าต่างขึ้น (exit $($script:p.ExitCode)) — crash หรือชน single-instance"; throw "abort" }
    if ($script:h -eq [IntPtr]::Zero) { Rec FAIL "start" "ไม่มีหน้าต่างหลักใน ${StartTimeoutSec}s"; throw "abort" }
    Rec PASS "start" ("หน้าต่างขึ้นใน {0:N1}s" -f ((Get-Date) - $t0).TotalSeconds)
    Start-Sleep -Seconds 4   # Vulkan/QML init + discovery แรก

    foreach ($s in $Sizes) {
        if ($script:dead -or $script:lost) { break }
        $script:h = Resolve-Main
        if ($script:h -eq [IntPtr]::Zero) { Rec FAIL "size $s" "หน้าต่างหลักหายไป"; break }
        Set-Size $s
        Settle "size $s" | Out-Null
        if (-not (Check-Alive "size $s")) { break }
        $c = ClientRect
        Rec INFO "size $s" ("client {0}x{1} logical" -f [int]($c.W / $scale), [int]($c.H / $scale))
        Park-Mouse; Start-Sleep -Milliseconds 500
        Snap "$s-home" | Out-Null
        $script:homeImg = GrabClient
        # noise floor: home ขยับเองได้ (BusyIndicator ตอน discovery, สถานะคอนโซล) → threshold อิงจาก diff home-vs-home
        Start-Sleep -Milliseconds 800; $noise = DiffFrac $script:homeImg (GrabClient)
        $script:openThr = [math]::Max(0.03, $noise + 0.02); $script:closeThr = [math]::Max(0.02, $noise + 0.012)
        Rec INFO "$s noise" ("home-vs-home diff {0:P2} → open>{1:P1}, close<{2:P1}" -f $noise, $script:openThr, $script:closeThr)
        Test-Chip "pad" "controller" $s
        if ($script:dead -or $script:lost) { break }
        if ($devicesOk) {
            Test-Chip "mic" "mic-preview" $s
            if ($script:dead -or $script:lost) { break }
            Test-Chip "cam" "cam-preview" $s
            if ($script:dead -or $script:lost) { break }
        } else { Rec SKIP "$s mic/cam preview" ($(if ($SkipDevices) { "-SkipDevices" } else { "มี PS-WRAP ตัวอื่นรันอยู่ — ไม่แย่งไมค์/กล้อง (ใช้ -ForceDevices ถ้าจะเทส)" })) }
        Test-Settings $s
    }
    if (-not $script:dead) { Test-Tray; Check-Alive "end" | Out-Null }
} catch {
    if ("$_" -ne "abort") { Rec FAIL "script" "$_ @ $($_.InvocationInfo.ScriptLineNumber)" }
} finally {
    if ($script:p -and -not $script:p.HasExited) {
        # ก่อนปิด: ถ้ายังค้างใน dialog/Settings (กล้อง/ไมค์ preview อาจเปิดอยู่) กด Esc ให้ปล่อยอุปกรณ์ก่อน — kill ตอนกล้องเปิดเสี่ยงทำ webcam ค้าง
        try {
            if ($script:homeImg -and $script:h -ne [IntPtr]::Zero) {
                for ($k = 0; $k -lt 2; $k++) {
                    if ((DiffFrac $script:homeImg (GrabClient)) -le $script:closeThr) { break }
                    Key "{ESC}"; Settle "cleanup esc" | Out-Null
                }
            }
        } catch {}
        # รอแอป idle + กล้องถูกปล่อยจริง
        try { if ($script:h -ne [IntPtr]::Zero -and (Alive)) { Settle "cleanup idle" | Out-Null } } catch {}
        try { Wait-Released "webcam" "cleanup" $ReleaseTimeoutSec "WARN" | Out-Null } catch {}
        # ปิดแบบปกติ (WM_CLOSE) — แอปปิดกล้องบน worker แล้วรอ worker ก่อน process จบ · ไม่ออกเองค่อย kill
        $exited = $false
        try {
            if ($script:h -ne [IntPtr]::Zero) { [PSWSmokeExit]::PostMessage($script:h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }   # WM_CLOSE
            $exited = $script:p.WaitForExit($ExitTimeoutSec * 1000)
        } catch {}
        if ($exited) { Rec PASS "exit" "แอปออกเองหลัง WM_CLOSE (exit code $($script:p.ExitCode))" }
        else {
            $toTray = $false
            try { $toTray = -not [PSWSmokeExit]::IsWindowVisible($script:h) } catch {}
            $camOpen = $script:camHeld -or (Device-InUse "webcam")
            Stop-Process -Id $script:p.Id -Force -ErrorAction SilentlyContinue; $script:p.WaitForExit(5000) | Out-Null
            if ($camOpen) {
                Rec WARN "exit" "แอปไม่ออกเองใน ${ExitTimeoutSec}s → kill ขณะกล้องอาจยังเปิด — webcam อาจต้องถอดเสียบใหม่"
                Write-Host "!!! kill ตอนกล้องยังไม่ถูกปล่อย — ถ้า facecam/แอปอื่นเปิดกล้องไม่ได้ ให้ถอดสาย USB กล้องแล้วเสียบใหม่" -ForegroundColor Red
            } elseif ($toTray) {
                Rec INFO "exit" "WM_CLOSE แล้วหน้าต่างหายแต่ process ยังอยู่ (hide-to-tray) → kill · กล้องถูกปล่อยก่อน kill แล้ว"
            } else {
                Rec WARN "exit" "แอปไม่ออกเองใน ${ExitTimeoutSec}s หลัง WM_CLOSE (ค้างตอนปิด?) → kill · กล้องถูกปล่อยก่อน kill แล้ว — ถ้ากล้องมีปัญหาให้ถอดเสียบใหม่"
            }
        }
    }
    $resetExe = if (Test-Path $runExe) { $runExe } else { $Exe }
    if (Reset-Profile $resetExe) { Rec PASS "cleanup" "current_profile คืนเป็น '$origProfile'" }
    else { Rec FAIL "cleanup" "current_profile ยังเป็น '$((Get-ItemProperty $regSettings -ErrorAction SilentlyContinue).current_profile)' — ต้องคืนเป็น '$origProfile' เอง" }
    if ($snapDir) { Start-Sleep -Milliseconds 500; Remove-Item -Recurse -Force $snapDir -ErrorAction SilentlyContinue }
}

$se = Scan-Stderr
if ($se.Bad.Count) { Rec FAIL "stderr" "$($se.Bad.Count) บรรทัด QML error/warning:"; $se.Bad | ForEach-Object { Write-Host "    $_" -ForegroundColor Red } }
else { Rec PASS "stderr" "ไม่มี QML error ใหม่ ($($se.Total) บรรทัด, known $($se.Known.Count))" }
$sheet = Contact-Sheet
$fails = @($script:results | Where-Object Status -eq "FAIL").Count
$warns = @($script:results | Where-Object Status -eq "WARN").Count
$summary = @("PS-WRAP smoke $stamp", "exe: $Exe", "result: $(if ($fails) { 'FAIL' } else { 'PASS' }) ($fails fail, $warns warn, $($script:shots.Count) screenshots)", "")
$summary += $script:results | ForEach-Object { "[{0}] {1} {2}" -f $_.Status, $_.Step, $_.Msg }
$summary += "", "--- stderr: QML errors ($($se.Bad.Count)) ---"; $summary += $se.Bad
$summary += "", "--- stderr: known/allowed ($($se.Known.Count)) ---"; $summary += $se.Known
$summary += "", "--- stderr: other warnings ($($se.Warn.Count)) ---"; $summary += $se.Warn
$summary | Set-Content (Join-Path $outDir "summary.txt") -Encoding utf8
""
"result: $(if ($fails) { 'FAIL' } else { 'PASS' }) — $fails fail, $warns warn, $($script:shots.Count) screenshots"
"summary: $(Join-Path $outDir 'summary.txt')"
if ($sheet) { "contact sheet: $sheet" }
exit ([int]($fails -gt 0))
