# deps ที่ pacman ไม่มี: libplacebo (pin ตาม CI), ffmpeg prebuilt, SDL3 + sdl2-compat — ดู docs/02-build-windows.md
# ของที่ build เองอยู่ที่ PS-WRAP\_deps (gitignored) · log อยู่ที่ _deps\*.log
$ErrorActionPreference = "Stop"
$env:MSYSTEM = "MINGW64"
# gotcha: USERPROFILE หายใน MSYS2 login shell → Python/meson หา home ไม่ได้
$env:USERPROFILE = [Environment]::GetFolderPath("UserProfile")
$env:HOMEDRIVE = $env:USERPROFILE.Substring(0,2); $env:HOMEPATH = $env:USERPROFILE.Substring(2)
$bash = "C:\msys64\usr\bin\bash.exe"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$rootU = $root -replace '\\','/' -replace '^([A-Za-z]):','/$1'
$deps = Join-Path $root "_deps"
New-Item -ItemType Directory -Force $deps | Out-Null
$libplacebo = "3be579e6c7f6b421d9ac3ab4860edc437c50b3eb"

# gotcha: bash -lc ของ MSYS2 ไม่รักษา cwd → ต้อง cd path เต็มเสมอ
Write-Host "[1/4] submodules" -ForegroundColor Cyan
& $bash -lc "cd '$rootU/ps-wrap' && git config core.autocrlf false && git config core.eol lf && git submodule update --init --recursive"

Write-Host "[2/4] libplacebo $libplacebo (log: _deps\libplacebo-build.log)" -ForegroundColor Cyan
& $bash -lc "cd '$rootU/ps-wrap' && LIBPLACEBO_VERSION=$libplacebo scripts/build-libplacebo-windows.sh ../_deps > '$rootU/_deps/libplacebo-build.log' 2>&1"
if ($LASTEXITCODE -ne 0) { Write-Error "libplacebo ล้มเหลว ดู _deps\libplacebo-build.log" }

Write-Host "[3/4] ffmpeg prebuilt" -ForegroundColor Cyan
& $bash -lc "cd '$rootU/_deps' && [ -f ffmpeg.zip ] || curl -sSL -o ffmpeg.zip https://github.com/streetpea/FFmpeg-Builds/releases/download/latest/ffmpeg-n7.1-latest-win64-gpl-shared-7.1.zip; unzip -q -o ffmpeg.zip && D=ffmpeg-n7.1-latest-win64-gpl-shared-7.1 && cp -a \$D/bin/. /mingw64/bin && cp -a \$D/include/. /mingw64/include && cp -a \$D/lib/. /mingw64/lib"
if ($LASTEXITCODE -ne 0) { Write-Error "ffmpeg ล้มเหลว" }

Write-Host "[4/4] SDL3 + sdl2-compat (log: _deps\sdl2-build.log)" -ForegroundColor Cyan
& $bash -lc "cd '$rootU/ps-wrap' && INSTALL_PREFIX=/mingw64 scripts/build-sdl2-compat.sh ../_deps > '$rootU/_deps/sdl2-build.log' 2>&1 && cp /mingw64/lib/pkgconfig/sdl2-compat.pc /mingw64/lib/pkgconfig/sdl2.pc"
if ($LASTEXITCODE -ne 0) { Write-Error "sdl2-compat ล้มเหลว ดู _deps\sdl2-build.log" }

Write-Host "`nเสร็จ ถัดไป: .\scripts\build.ps1" -ForegroundColor Green
