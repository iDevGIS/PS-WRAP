# จับ screenshot ของ PS-WRAP.exe (build ของเรา) ที่ขนาดหน้าต่างกำหนด แล้วปิด — ใช้ --profile pswrap-test เสมอ
# ใช้: .\scripts\snap.ps1 -Out design\screens\after\phase2 -Name 01-main [-Sizes 1920x1080,1280x800] [-Keys "{ESC}"] [-WaitSec 6]
param(
    [Parameter(Mandatory)][string]$Out,
    [Parameter(Mandatory)][string]$Name,
    [string[]]$Sizes = @("1920x1080", "1280x800"),
    [string]$Keys = "",          # SendKeys ก่อนจับภาพ (เช่น "{ESC}") — ระวัง focus
    [string]$ClickAt = "",       # คลิกซ้ายที่พิกัด logical ในหน้าต่าง "x,y" ก่อนจับภาพ (หลังตั้งขนาดแรก) เช่น "1830,95" = ปุ่ม Settings ที่ 1920x1080
    [switch]$PressMenu,          # กดปุ่ม Menu (VK_APPS) ก่อนจับภาพ — MainView map ไปเปิด Settings
    [int]$WaitSec = 10,          # Vulkan init บางครั้ง > 6s
    [string]$Profile = "pswrap-test"
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$src = Join-Path $root "ps-wrap"
$env:PATH = "C:\msys64\mingw64\bin;$src\build\third-party\cpp-steam-tools;$env:PATH"
$env:QT_FORCE_STDERR_LOGGING = "1"   # gotcha: Qt GUI app บน Windows ส่ง qDebug/console.log ไป OutputDebugString ถ้าไม่ตั้งค่านี้ (QT_LOGGING_TO_CONSOLE deprecated)
$outDir = if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $root $Out }
New-Item -ItemType Directory -Force $outDir | Out-Null
$err = Join-Path $env:TEMP "chiaki-snap-stderr.txt"; $so = Join-Path $env:TEMP "chiaki-snap-stdout.txt"

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
if (-not ("WSnap" -as [type])) {
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class WSnap {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
}
[WSnap]::SetProcessDPIAware() | Out-Null
# gotcha: Qt วาดเป็น logical pixel แต่ SetWindowPos/CopyFromScreen เป็น physical → ขนาดที่ขอ (-Sizes) ถือเป็น logical แล้วคูณ scale
$scale = [System.Drawing.Graphics]::FromHwnd([IntPtr]::Zero).DpiX / 96.0
"DPI scale: $scale (ขนาดใน -Sizes เป็น logical px; ไฟล์ภาพเป็น physical px)"

function Ensure-Foreground($h) {
    for ($i = 0; $i -lt 5; $i++) {
        if ([WSnap]::GetForegroundWindow() -eq $h) { return $true }
        [WSnap]::ShowWindow($h, 5) | Out-Null          # SW_SHOW
        # trick: กด Alt ก่อนเพื่อปลด foreground lock แล้วค่อย SetForegroundWindow
        [WSnap]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [WSnap]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
        [WSnap]::SetForegroundWindow($h) | Out-Null
        Start-Sleep -Milliseconds 400
    }
    return ([WSnap]::GetForegroundWindow() -eq $h)
}

function Snap($file, $h) {
    if (-not (Ensure-Foreground $h)) { Write-Error "หน้าต่าง chiaki ไม่อยู่หน้าสุด — ไม่ถ่าย $file (กันจับหน้าต่างอื่น)"; return }
    $r = New-Object WSnap+RECT; [WSnap]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $hh = $r.B - $r.T
    if ($w -le 0) { Write-Warning "no window rect"; return }
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size $w, $hh))
    $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png); "saved $file"
}

# try/finally: ทุกทางออก (รวม Write-Error) ต้องคืน current_profile — เคยค้าง pswrap-test จนตัว dist ของลูกพี่เปิดเป็น profile ทดสอบ (2026-10-06)
try {
$p = Start-Process -FilePath "$src\build\gui\PS-WRAP.exe" -ArgumentList "--profile", $Profile -WorkingDirectory $src -PassThru -RedirectStandardError $err -RedirectStandardOutput $so
Start-Sleep -Seconds $WaitSec; $p.Refresh()
if ($p.HasExited -or $p.MainWindowHandle -eq 0) { if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }; Write-Error "PS-WRAP.exe ไม่ขึ้นหน้าต่างใน ${WaitSec}s (exit $($p.ExitCode)) — ดู $err" }
$h = $p.MainWindowHandle
if (-not (Ensure-Foreground $h)) { Stop-Process -Id $p.Id -Force; Write-Error "ดึงหน้าต่าง chiaki ขึ้นหน้าสุดไม่ได้ — ยกเลิก" }
if ($Keys) { [System.Windows.Forms.SendKeys]::SendWait($Keys); Start-Sleep -Seconds 2 }
if ($PressMenu) { [WSnap]::keybd_event(0x5D, 0, 0, [UIntPtr]::Zero); [WSnap]::keybd_event(0x5D, 0, 2, [UIntPtr]::Zero); Start-Sleep -Seconds 2 }
$first = $true
foreach ($s in $Sizes) {
    $w, $hh = $s -split "x"
    # ตั้ง 2 รอบ: แอป restore geometry ที่เซฟไว้ทับรอบแรกได้
    [WSnap]::SetWindowPos($h, [IntPtr]::Zero, 0, 0, [int]([int]$w * $scale), [int]([int]$hh * $scale), 0x0040) | Out-Null
    Start-Sleep -Seconds 1
    [WSnap]::SetWindowPos($h, [IntPtr]::Zero, 0, 0, [int]([int]$w * $scale), [int]([int]$hh * $scale), 0x0040) | Out-Null
    Start-Sleep -Seconds 2
    if ($first -and $ClickAt) {
        if (-not (Ensure-Foreground $h)) { Stop-Process -Id $p.Id -Force; Write-Error "หน้าต่างไม่อยู่หน้าสุด ไม่คลิก (กันคลิกโดนหน้าต่างอื่น)" }
        $cx, $cy = $ClickAt -split ","
        $r = New-Object WSnap+RECT; [WSnap]::GetWindowRect($h, [ref]$r) | Out-Null
        # พิกัดนับจากมุมซ้ายบนของหน้าต่าง (รวม title bar) เหมือนที่เห็นใน screenshot
        $px = $r.L + [int]([int]$cx * $scale); $py = $r.T + [int]([int]$cy * $scale)
        [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($px, $py)
        Start-Sleep -Milliseconds 200
        [WSnap]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero); [WSnap]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Seconds 2
    }
    $first = $false
    Snap (Join-Path $outDir "$Name-${s}@$($scale)x.png") $h
}
} finally {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    if ($p) { $p.WaitForExit(5000) | Out-Null }   # Stop-Process คืนก่อน process ตายจริง → reset ทันทีโดนทับ
    # gotcha: upstream เซฟ --profile เป็น settings/current_profile ใน default settings → ตัวติดตั้งจะเปิดเป็น profile ทดสอบด้วย
    # คืนค่าด้วยกลไกของแอปเอง: "--profile=" + คำสั่ง list (ไม่เปิด GUI)
    # gotcha: --profile "" ใช้ไม่ได้ — pwsh ทิ้ง arg ว่าง ค่าไม่เคยถูกคืนจริง (ตรวจ 2026-10-06)
    for ($i = 0; $i -lt 3; $i++) {
        & "$src\build\gui\PS-WRAP.exe" "--profile=" list *> $null
        if (-not (Get-ItemProperty "HKCU:\Software\PS-WRAP\PS-WRAP\settings" -ErrorAction SilentlyContinue).current_profile) { break }
        Start-Sleep -Seconds 1
    }
    if ((Get-ItemProperty "HKCU:\Software\PS-WRAP\PS-WRAP\settings" -ErrorAction SilentlyContinue).current_profile) { Write-Warning "current_profile ยังไม่ว่าง — เช็ค registry" }
    "current_profile reset to default"
}
$qmlErr = Get-Content $err -ErrorAction SilentlyContinue | Select-String -Pattern "qml|QML|rror|PSWRAP"
if ($qmlErr) { "--- stderr ---"; $qmlErr | Select-Object -First 15 } else { "stderr: no QML errors" }
