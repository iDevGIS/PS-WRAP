#!/bin/bash
# configure + build บน macOS → ps-wrap/build/gui/PS-WRAP-MAC.app — คู่กับ build.ps1 ฝั่ง Windows
# ใช้: scripts/build-mac.sh [--clean] [--debug]
set -e
cd "$(dirname "$0")/../ps-wrap"
TYPE=Release; CLEAN=""
for a in "$@"; do
	case "$a" in
		--clean) CLEAN="--clean-first" ;;
		--debug) TYPE=Debug ;;
		*) echo "ไม่รู้จัก option: $a"; exit 1 ;;
	esac
done

BREW=$(brew --prefix)
export PKG_CONFIG_PATH="$BREW/lib/pkgconfig:$BREW/opt/openssl@3/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
PY=$(command -v python3)
"$PY" -c 'import google.protobuf' 2>/dev/null || { echo "python3 ($PY) ไม่มี google.protobuf → รัน scripts/setup-mac.sh ก่อน"; exit 1; }

# PS-WRAP Link: libdatachannel source อยู่ในโปรเจกต์ข้างๆ (PS-WRAP-LINK/spike/deps) — มี = เปิด Link · ไม่มี = build แบบไม่มี Link
# บน mac ไม่มีโปรเจกต์ข้างๆ ก็ใช้ _deps/libdatachannel (setup-mac.sh clone ให้ · gitignored)
LDC="$(cd ../.. && pwd)/PS-WRAP-LINK/spike/deps/libdatachannel"
[ -f "$LDC/CMakeLists.txt" ] || LDC="$(cd .. && pwd)/_deps/libdatachannel"
LINK_FLAGS=()
if [ -f "$LDC/CMakeLists.txt" ]; then
	LINK_FLAGS=(-DPSWRAP_LINK=ON "-DPSWRAP_LIBDATACHANNEL_DIR=$LDC")
else
	echo "ไม่เจอ libdatachannel (PS-WRAP-LINK/spike/deps หรือ _deps) → build แบบไม่มี Link · รัน scripts/setup-mac.sh เพื่อ clone"
fi

# gotcha: CMAKE_FIND_FRAMEWORK=LAST — ไม่งั้น CMake ไปเจอ zlib/openssl 1.1 header เก่าใน /Library/Frameworks/GStreamer.framework ก่อน
# gotcha: ปิด psl/ssh2/nghttp2/idn2 ของ curl — ไม่ได้ใช้ และมันลาก -isystem /opt/homebrew/include (OpenSSL 4) มาบัง openssl@3
# gotcha: Vulkan_INCLUDE_DIR ชี้ keg ตรงๆ ด้วยเหตุผลเดียวกัน
START=$(date +%s)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE="$TYPE" \
	-DCHIAKI_ENABLE_CLI=OFF -DCHIAKI_ENABLE_TESTS=OFF -DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF \
	-DPYTHON_EXECUTABLE="$PY" \
	-DCMAKE_PREFIX_PATH="$BREW/opt/openssl@3;$BREW/opt/qt@6;$BREW/opt/protobuf@29" \
	-DOPENSSL_ROOT_DIR="$BREW/opt/openssl@3" \
	-DCMAKE_FIND_FRAMEWORK=LAST \
	-DVulkan_INCLUDE_DIR="$BREW/opt/vulkan-headers/include" \
	-DCURL_USE_LIBPSL=OFF -DCURL_USE_LIBSSH2=OFF -DUSE_NGHTTP2=OFF -DUSE_LIBIDN2=OFF \
	-DCMAKE_C_FLAGS="-I$BREW/opt/ffmpeg/include" -DCMAKE_CXX_FLAGS="-I$BREW/opt/ffmpeg/include" \
	-DQT_NO_PRIVATE_MODULE_WARNING=ON \
	"${LINK_FLAGS[@]}"
cmake --build build $CLEAN --target chiaki
echo "build ผ่าน ใช้เวลา $(( $(date +%s) - START ))s → ps-wrap/build/gui/PS-WRAP-MAC.app"
