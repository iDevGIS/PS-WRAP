# ขับ chiaki.exe (build ของเรา) ที่ "เปิดอยู่แล้ว" ทีละขั้น: ตั้งขนาด / คลิก / กดคีย์ / จับภาพ — ไม่เปิด/ปิดโปรแกรมเอง
# ใช้: .\scripts\drive.ps1 -Out design\screens\after\phase4 -Actions "size:1920x1080","click:1751,479","wait:8","snap:stream-01","key:ctrl+o","wait:2","snap:menu"
#   พิกัดเป็น logical px นับจากมุมซ้ายบนของหน้าต่าง (รวม title bar) เหมือนที่เห็นใน screenshot · key: ctrl+o, right, left, up, down, return, esc, pageup, pagedown, menu
param(
    [Parameter(Mandatory)][string]$Out,
    [Parameter(Mandatory)][string[]]$Actions
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$outDir = if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $root $Out }
New-Item -ItemType Directory -Force $outDir | Out-Null

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
if (-not ("WDrive" -as [type])) {
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class WDrive {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
}
[WDrive]::SetProcessDPIAware() | Out-Null
$scale = [System.Drawing.Graphics]::FromHwnd([IntPtr]::Zero).DpiX / 96.0

$p = Get-Process chiaki -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "*\ps-wrap\build\*" } | Select-Object -First 1
if (-not $p) { Write-Error "ไม่พบ chiaki.exe จาก build dir ที่รันอยู่" }
# gotcha: ตอนสตรีม upstream สร้าง StatsOverlayWidget เป็น top-level window ชื่อ 'PS-WRAP' เหมือนกัน (ToolTip, ไม่รับ input)
# และ $p.MainWindowHandle อาจชี้ไปตัวนั้น → เลือกหน้าต่างที่ "ใหญ่ที่สุด" ของ process แทน
if (-not ("WEnum2" -as [type])) {
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public static class WEnum2 { public delegate bool CB(IntPtr h, IntPtr l); [DllImport("user32.dll")] public static extern bool EnumWindows(CB cb, IntPtr l); [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid); [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h); [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r); [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n); [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; } }
"@
}
function Resolve-Handle {
    # เลือกหน้าต่าง QQuickWindow หลัก (class *QWindowIcon*) เท่านั้น — StatsOverlayWidget เป็น QWidget class อื่น (2026-10-04: เคยเลือกผิดตัวตอน stats เปิด คีย์/คลิกหลุด)
    $targetPid = [uint32]$p.Id; $script:best = [IntPtr]::Zero; $script:bestArea = -1; $script:fallback = [IntPtr]::Zero; $script:fallbackArea = -1
    [WEnum2]::EnumWindows({ param($hw, $l)
        [uint32]$wp = 0; [WEnum2]::GetWindowThreadProcessId($hw, [ref]$wp) | Out-Null
        if ($wp -eq $targetPid -and [WEnum2]::IsWindowVisible($hw)) {
            $r = New-Object WEnum2+RECT; [WEnum2]::GetWindowRect($hw, [ref]$r) | Out-Null
            $a = ($r.R - $r.L) * ($r.B - $r.T)
            $sb = New-Object System.Text.StringBuilder 256; [WEnum2]::GetClassName($hw, $sb, 256) | Out-Null
            if ($sb.ToString() -like "*QWindowIcon*") { if ($a -gt $script:bestArea) { $script:bestArea = $a; $script:best = $hw } }
            elseif ($a -gt $script:fallbackArea) { $script:fallbackArea = $a; $script:fallback = $hw }
        }
        $true }, [IntPtr]::Zero) | Out-Null
    if ($script:best -ne [IntPtr]::Zero) { return $script:best }
    return $script:fallback
}
$h = Resolve-Handle
if ($h -eq [IntPtr]::Zero) { Write-Error "chiaki.exe ไม่มีหน้าต่าง" }

function Ensure-Front {
    for ($i = 0; $i -lt 5; $i++) {
        if ([WDrive]::GetForegroundWindow() -eq $h) { return $true }
        [WDrive]::ShowWindow($h, 5) | Out-Null
        [WDrive]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [WDrive]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
        [WDrive]::SetForegroundWindow($h) | Out-Null
        Start-Sleep -Milliseconds 400
    }
    return ([WDrive]::GetForegroundWindow() -eq $h)
}
function Snap($name) {
    if (-not (Ensure-Front)) { Write-Error "หน้าต่างไม่อยู่หน้าสุด — ไม่ถ่าย $name" }
    $r = New-Object WDrive+RECT; [WDrive]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $hh = $r.B - $r.T
    $bmp = New-Object System.Drawing.Bitmap $w, $hh
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size $w, $hh))
    $f = Join-Path $outDir "$name.png"; $bmp.Save($f, [System.Drawing.Imaging.ImageFormat]::Png); "saved $f"
}
$vk = @{ ctrl=0x11; shift=0x10; o=0x4F; e=0x45; right=0x27; left=0x25; up=0x26; down=0x28; return=0x0D; esc=0x1B; pageup=0x21; pagedown=0x22; menu=0x5D; plus=0xBB; minus=0xBD }
# gotcha: keybd_event ไม่ถึง Qt สำหรับคีย์ตัวอักษร/ลูกศรระหว่างสตรีม (Ctrl+O ไม่ติด) แต่ SendKeys ติด → ใช้ SendKeys ยกเว้น Menu (VK_APPS ไม่มีใน SendKeys)
$sendKeysMap = @{ right="{RIGHT}"; left="{LEFT}"; up="{UP}"; down="{DOWN}"; return="{ENTER}"; esc="{ESC}"; pageup="{PGUP}"; pagedown="{PGDN}"; plus="{+}"; minus="-"; o="o"; e="e" }
function Key($combo) {
    $c = $combo.ToLower()
    if ($c -ne "menu") {
        $parts = $c -split "\+"; $main = $parts[-1]; $prefix = ""
        foreach ($m in $parts[0..($parts.Count - 2)]) { if ($parts.Count -gt 1) { if ($m -eq "ctrl") { $prefix += "^" } elseif ($m -eq "shift") { $prefix += "+" } elseif ($m -eq "alt") { $prefix += "%" } } }
        if ($parts.Count -eq 1) { $prefix = "" }
        $k = if ($sendKeysMap.ContainsKey($main)) { $sendKeysMap[$main] } else { $main }
        [System.Windows.Forms.SendKeys]::SendWait($prefix + $k); Start-Sleep -Milliseconds 150
        return
    }
    $parts = $combo.ToLower() -split "\+"
    $mods = $parts[0..($parts.Count - 2)]; $main = $parts[-1]
    if ($parts.Count -eq 1) { $mods = @() }
    foreach ($m in $mods) { [WDrive]::keybd_event($vk[$m], 0, 0, [UIntPtr]::Zero) }
    [WDrive]::keybd_event($vk[$main], 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60
    [WDrive]::keybd_event($vk[$main], 0, 2, [UIntPtr]::Zero)
    foreach ($m in $mods) { [WDrive]::keybd_event($vk[$m], 0, 2, [UIntPtr]::Zero) }
    Start-Sleep -Milliseconds 150
}
function ClickAt($cx, $cy) {
    if (-not (Ensure-Front)) { Write-Error "หน้าต่างไม่อยู่หน้าสุด — ไม่คลิก" }
    $r = New-Object WDrive+RECT; [WDrive]::GetWindowRect($h, [ref]$r) | Out-Null
    $px = $r.L + [int]([int]$cx * $scale); $py = $r.T + [int]([int]$cy * $scale)
    if ($px -ge $r.R -or $py -ge $r.B) { Write-Error "พิกัดคลิกตกนอกหน้าต่าง (rect $($r.L),$($r.T)-$($r.R),$($r.B)) — ไม่คลิก" }
    [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($px, $py)
    Start-Sleep -Milliseconds 200
    [WDrive]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero); [WDrive]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 700   # gotcha: คลิกถัดไปภายใน ~500ms = double-click → upstream สลับ fullscreen
}
function DragTo($x1, $y1, $x2, $y2) {
    if (-not (Ensure-Front)) { Write-Error "หน้าต่างไม่อยู่หน้าสุด — ไม่ลาก" }
    $r = New-Object WDrive+RECT; [WDrive]::GetWindowRect($h, [ref]$r) | Out-Null
    $sx = $r.L + [int]([int]$x1 * $scale); $sy = $r.T + [int]([int]$y1 * $scale)
    $ex = $r.L + [int]([int]$x2 * $scale); $ey = $r.T + [int]([int]$y2 * $scale)
    if ($sx -ge $r.R -or $sy -ge $r.B -or $ex -ge $r.R -or $ey -ge $r.B) { Write-Error "พิกัดลากตกนอกหน้าต่าง (rect $($r.L),$($r.T)-$($r.R),$($r.B)) — ไม่ลาก" }
    [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($sx, $sy); Start-Sleep -Milliseconds 150
    [WDrive]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 150
    $steps = 12
    for ($i = 1; $i -le $steps; $i++) {
        $cx = $sx + ($ex - $sx) * $i / $steps; $cy = $sy + ($ey - $sy) * $i / $steps
        [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point([int]$cx, [int]$cy); Start-Sleep -Milliseconds 30
    }
    [WDrive]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
}

Ensure-Front | Out-Null
foreach ($a in $Actions) {
    $kind, $arg = $a -split ":", 2
    # resolve ใหม่ทุก action: หน้าต่างอาจถูกซ่อน/สร้างใหม่ — ไม่ถ่าย/คลิกใส่หน้าต่างอื่นเด็ดขาด
    $h = Resolve-Handle
    if ($h -eq [IntPtr]::Zero) { if ($kind -eq "visible") { "visible: False" ; continue } ; Write-Error "chiaki.exe ไม่มีหน้าต่างที่มองเห็น — หยุดก่อน action '$a'" }
    if ($kind -eq "visible") { "visible: True"; continue }
    switch ($kind) {
        "size"  { $w, $hh = $arg -split "x"; [WDrive]::SetWindowPos($h, [IntPtr]::Zero, 0, 0, [int]([int]$w * $scale), [int]([int]$hh * $scale), 0x0040) | Out-Null; Start-Sleep -Seconds 1; [WDrive]::SetWindowPos($h, [IntPtr]::Zero, 0, 0, [int]([int]$w * $scale), [int]([int]$hh * $scale), 0x0040) | Out-Null; Start-Sleep -Seconds 1 }
        "click" { $x, $y = $arg -split ","; ClickAt $x $y }
        "drag"  { $x1, $y1, $x2, $y2 = $arg -split ","; DragTo $x1 $y1 $x2 $y2 }
        "key"   { Key $arg }
        "keys"  { $k, $n = $arg -split "\*"; for ($i = 0; $i -lt [int]$n; $i++) { Key $k } }
        "wait"  { Start-Sleep -Seconds ([double]$arg) }
        "snap"  { Snap $arg }
        "rect"  { $r = New-Object WDrive+RECT; [WDrive]::GetWindowRect($h, [ref]$r) | Out-Null; "rect physical $($r.L),$($r.T)-$($r.R),$($r.B) = logical $([int](($r.R-$r.L)/$scale))x$([int](($r.B-$r.T)/$scale))" }
        default { Write-Warning "unknown action $a" }
    }
}
"done"
