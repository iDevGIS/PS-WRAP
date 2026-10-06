# ติดตั้ง MSYS2 + deps ทั้งหมดสำหรับ build chiaki-ng (ทำครั้งเดียว) — ดู docs/02-build-windows.md
# ใช้: .\scripts\setup-msys2.ps1
# ทดสอบจริง 2026-10-04 บนเครื่อง 4090 (ดู บันทึกการทำงานทุกอย่าง.md ข้อ 22-24)
$ErrorActionPreference = "Stop"
$msys = "C:\msys64"

if (-not (Test-Path "$msys\usr\bin\bash.exe")) {
    Write-Host "ติดตั้ง MSYS2 ผ่าน winget..." -ForegroundColor Cyan
    winget install --id MSYS2.MSYS2 --exact --accept-package-agreements --accept-source-agreements --disable-interactivity
}
if (-not (Test-Path "$msys\usr\bin\bash.exe")) { Write-Error "ไม่พบ MSYS2 ที่ $msys" }

$env:MSYSTEM = "MINGW64"
$bash = "$msys\usr\bin\bash.exe"

Write-Host "อัปเดต pacman (รอบแรกอาจ exit 1 เพราะ core update — รัน 2 รอบตามปกติ)..." -ForegroundColor Cyan
& $bash -lc "pacman -Syu --noconfirm"
& $bash -lc "pacman -Su --noconfirm"

# gotcha: pacboy ไม่มีใน MSYS2 สด → ใช้ pacman + prefix ตรงๆ
# gotcha: diffutils / ca-certificates เป็น package ฝั่ง msys (ไม่มี prefix)
# gotcha: pip ติด PEP 668 → ใช้ python-protobuf จาก pacman
$mingw = @(
  "cc","cmake","curl","fast_float","fftw","hidapi","json-c","libevent","lcms2","libdovi",
  "meson","miniupnpc","gcc","nasm","ninja","openssl","opus","pkgconf","protobuf",
  "python","python-psutil","python-glad","python-jinja","python-pip","python-protobuf",
  "qt6-base","qt6-declarative","qt6-svg","shaderc","speexdsp","spirv-cross","vulkan","vulkan-headers"
) | ForEach-Object { "mingw-w64-x86_64-$_" }

Write-Host "ติดตั้ง deps (ประมาณ 1.7 GB)..." -ForegroundColor Cyan
& $bash -lc "pacman -Syy --noconfirm && pacman -S --noconfirm --needed git make unzip zip diffutils ca-certificates"
& $bash -lc "pacman -S --noconfirm --needed $($mingw -join ' ')"
# PS-WRAP facecam: Qt Multimedia + backend WMF — ห้ามให้ pacman ลาก mingw-w64-x86_64-ffmpeg มาทับ ffmpeg prebuilt ของ streetpea (แกน streaming ใช้ตัวนั้น)
& $bash -lc "pacman -S --noconfirm --needed --assume-installed mingw-w64-x86_64-qt6-multimedia-ffmpeg mingw-w64-x86_64-qt6-multimedia mingw-w64-x86_64-qt6-multimedia-wmf"
if ($LASTEXITCODE -ne 0) { Write-Error "pacman ล้มเหลว — ถ้าเจอ 404 จาก mirror ให้รันซ้ำ" }

& $bash -lc "which gcc cmake ninja meson qmake6 qmllint && python -c 'import google.protobuf; print(\"protobuf\", google.protobuf.__version__)'"
Write-Host "`nถัดไป: .\scripts\setup-extra-deps.ps1 (libplacebo, ffmpeg, sdl2-compat)" -ForegroundColor Yellow
