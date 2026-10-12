# configure + build chiaki-ng ใน MSYS2 mingw64 → ps-wrap\build\gui\PS-WRAP.exe
# ใช้: .\scripts\build.ps1 [-Clean] [-Debug]
param([switch]$Clean, [switch]$Debug)
$ErrorActionPreference = "Stop"
$env:MSYSTEM = "MINGW64"
# gotcha: USERPROFILE หายใน MSYS2 login shell → Python/meson หา home ไม่ได้
$env:USERPROFILE = [Environment]::GetFolderPath("UserProfile")
$env:HOMEDRIVE = $env:USERPROFILE.Substring(0,2); $env:HOMEPATH = $env:USERPROFILE.Substring(2); $env:CHERE_INVOKING = "1"
$bash = "C:\msys64\usr\bin\bash.exe"
$src = (Resolve-Path (Join-Path $PSScriptRoot "..\ps-wrap")).Path -replace '\\','/' -replace '^([A-Za-z]):','/$1'
$type = if ($Debug) { "Debug" } else { "Release" }
$cleanFlag = if ($Clean) { "--clean-first" } else { "" }
# PS-WRAP Link: libdatachannel source อยู่ในโปรเจกต์ข้างๆ (PS-WRAP-LINK/spike/deps) — มี = เปิด Link · ไม่มี = build แบบไม่มี Link
# beta key อ่านจาก %LOCALAPPDATA%\PS-WRAP-build\link-beta-key (นอก repo · ดู gui/CMakeLists.txt)
$ldc = Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) "PS-WRAP-LINK\spike\deps\libdatachannel"
$linkFlags = if (Test-Path (Join-Path $ldc "CMakeLists.txt")) { "-DPSWRAP_LINK=ON -DPSWRAP_LIBDATACHANNEL_DIR='" + ($ldc -replace '\\','/') + "'" } else { Write-Host "ไม่เจอ libdatachannel ที่ $ldc → build แบบไม่มี Link" -ForegroundColor Yellow; "" }

# gotcha: ถ้า PS-WRAP.exe จาก build dir ยังรันอยู่ ld เขียน output ไม่ได้ (exit 1) → ปิดให้ก่อน
$running = Get-Process PS-WRAP,chiaki -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "*\ps-wrap\build\*" }
if ($running) { Write-Host "ปิด PS-WRAP.exe ที่รันจาก build dir (pid $($running.Id -join ',')) ก่อน build" -ForegroundColor Yellow; $running | Stop-Process -Force; Start-Sleep 1 }
$sw = [Diagnostics.Stopwatch]::StartNew()
& $bash -lc "set -e; cd '$src'; cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=$type -DCHIAKI_ENABLE_CLI=OFF $linkFlags && cmake --build build $cleanFlag --target chiaki pswrap-diagnostics"
$sw.Stop()
if ($LASTEXITCODE -ne 0) { Write-Error "build ล้มเหลว (exit $LASTEXITCODE)" }
Write-Host "build ผ่าน ใช้เวลา $([int]$sw.Elapsed.TotalSeconds)s → ps-wrap\build\gui\PS-WRAP.exe" -ForegroundColor Green
