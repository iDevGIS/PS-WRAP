# รัน PS-WRAP.exe ที่ build เอง (ต้องรันผ่าน mingw64 env ให้หา DLL เจอ)
# ใช้: .\scripts\run.ps1 [--help | --zoom stream <nickname> <host> ...]
param([Parameter(ValueFromRemainingArguments)][string[]]$ChiakiArgs)
$ErrorActionPreference = "Stop"
$env:MSYSTEM = "MINGW64"
# gotcha: USERPROFILE หายใน MSYS2 login shell → Python/meson หา home ไม่ได้
$env:USERPROFILE = [Environment]::GetFolderPath("UserProfile")
$env:HOMEDRIVE = $env:USERPROFILE.Substring(0,2); $env:HOMEPATH = $env:USERPROFILE.Substring(2)
$bash = "C:\msys64\usr\bin\bash.exe"
$src = (Resolve-Path (Join-Path $PSScriptRoot "..\ps-wrap")).Path -replace '\\','/' -replace '^([A-Za-z]):','/$1'
# gotcha: libcpp-steam-tools.dll อยู่ใน build/third-party ไม่ได้อยู่ใน /mingw64/bin → exit 127 เงียบๆ ถ้าไม่ใส่ PATH
& $bash -lc "cd '$src' && export PATH=`"`$PWD/build/third-party/cpp-steam-tools:`$PATH`" && ./build/gui/PS-WRAP.exe $($ChiakiArgs -join ' ')"
exit $LASTEXITCODE
