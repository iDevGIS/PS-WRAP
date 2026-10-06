# สร้าง portable folder ที่เปิด chiaki.exe ได้ตรงๆ (copy DLL/Qt plugins/QML ทั้งหมดมาไว้ข้าง exe) — ใช้ deploy script ของ upstream
# ใช้: .\scripts\deploy.ps1        → dist\PS-WRAP\chiaki.exe (+ shortcut PS-WRAP.lnk บน Desktop ถ้าใส่ -Shortcut, ใน Start Menu ถ้าใส่ -StartMenu)
param([switch]$Shortcut, [switch]$StartMenu)
$ErrorActionPreference = "Stop"
$env:MSYSTEM = "MINGW64"
$env:USERPROFILE = [Environment]::GetFolderPath("UserProfile")
$env:HOMEDRIVE = $env:USERPROFILE.Substring(0,2); $env:HOMEPATH = $env:USERPROFILE.Substring(2)
$bash = "C:\msys64\usr\bin\bash.exe"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$rootU = $root -replace '\\','/' -replace '^([A-Za-z]):','/$1'
$out = Join-Path $root "dist\PS-WRAP"
# กันพลาด: ถ้า dist ตัวเก่ายังรันอยู่ ลบไม่ได้ และห้ามเปิด exe จนกว่าสคริปต์จะพิมพ์ "deploy ผ่าน" (เคยเปิดชนกลาง deploy 2026-10-04 → "no Qt platform plugin could be initialized")
$running = Get-Process chiaki -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$out*" }
if ($running) { $running | Stop-Process -Force; Start-Sleep 1 }
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null

$sw = [Diagnostics.Stopwatch]::StartNew()
& $bash -lc "cd '$rootU/ps-wrap' && ./scripts/deploy-windows-msys2.sh '$rootU/dist/PS-WRAP' build/gui/chiaki.exe build/third-party/cpp-steam-tools /mingw64 gui/src/qml"
if ($LASTEXITCODE -ne 0) { Write-Error "deploy ล้มเหลว (exit $LASTEXITCODE)" }
# qt.conf: ให้ Qt หา plugins/qml ข้าง exe เท่านั้น ไม่ fallback ไป C:\msys64 (windeployqt6 ของ MSYS2 ไม่เขียนให้)
Set-Content -Path (Join-Path $out "qt.conf") -Value "[Paths]`nPrefix=.`nPlugins=.`nQmlImports=qml`nQml2Imports=qml" -Encoding ascii
$sw.Stop()
$dlls = (Get-ChildItem $out -Filter *.dll -Recurse | Measure-Object).Count
$size = [math]::Round((Get-ChildItem $out -Recurse | Measure-Object Length -Sum).Sum / 1MB)
Write-Host "deploy ผ่าน $([int]$sw.Elapsed.TotalSeconds)s → $out ($dlls DLL, ${size} MB)" -ForegroundColor Green

function New-PsWrapShortcut($dir) {
    $lnk = Join-Path $dir "PS-WRAP.lnk"
    $s = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
    $s.TargetPath = Join-Path $out "chiaki.exe"; $s.WorkingDirectory = $out
    $s.IconLocation = (Join-Path $out "chiaki.exe") + ",0"; $s.Description = "PS-WRAP — PlayStation Remote Play"; $s.Save()
    Write-Host "shortcut: $lnk" -ForegroundColor Green
}
if ($Shortcut) { New-PsWrapShortcut ([Environment]::GetFolderPath("Desktop")) }
# Start Menu ระดับ user (%APPDATA%\Microsoft\Windows\Start Menu\Programs) — ไม่ต้อง admin, ค้นหา "PS-WRAP" ใน Start ได้
if ($StartMenu) { New-PsWrapShortcut ([Environment]::GetFolderPath("Programs")) }
