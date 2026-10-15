#!/bin/bash
# ติดตั้ง deps ทั้งหมดสำหรับ build PS-WRAP บน macOS ด้วย Homebrew (ทำครั้งเดียว) — คู่กับ setup-msys2.ps1 ฝั่ง Windows
# ใช้: scripts/setup-mac.sh
# ทดสอบจริง 2026-10-15 บน Apple M1 / macOS 26.2 / Xcode 26.6 / Homebrew 6
set -e
cd "$(dirname "$0")/.."

command -v brew >/dev/null || { echo "ไม่พบ Homebrew → https://brew.sh"; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "ไม่พบ Xcode Command Line Tools → xcode-select --install"; exit 1; }

echo "[1/3] Homebrew packages"
# gotcha: libplacebo ต้อง >= 7.349 (brew เก่าอาจเป็น 6.x → brew upgrade libplacebo)
# gotcha: curl ใน submodule ยังไม่รองรับ OpenSSL 4 → ใช้ openssl@3 (build-mac.sh ชี้ให้) · ffmpeg/libplacebo/qt ใช้ของ brew ตรงๆ
brew install cmake ninja nasm pkgconf protobuf@29 qt@6 ffmpeg libplacebo sdl2 opus openssl@3 \
	speexdsp json-c libevent miniupnpc vulkan-headers vulkan-loader molten-vk
brew upgrade libplacebo ffmpeg qt@6 2>/dev/null || true

echo "[2/3] python protobuf (nanopb generator)"
# gotcha: python3 ของ brew ติด PEP 668 → --break-system-packages · pyenv/python.org ไม่ต้อง
python3 -m pip install --quiet 'protobuf>=5,<6' --break-system-packages 2>/dev/null \
	|| python3 -m pip install --quiet 'protobuf>=5,<6'
python3 -c 'import google.protobuf; print("protobuf", google.protobuf.__version__)'

echo "[3/3] submodules"
git submodule update --init --recursive

echo
echo "เสร็จ ถัดไป: scripts/build-mac.sh"
