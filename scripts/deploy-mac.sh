#!/bin/bash
# สร้าง PS-WRAP-MAC.app ที่เปิดได้เองบนเครื่องอื่น (Qt/ffmpeg/libplacebo/MoltenVK/onnxruntime ฝังใน .app) + .dmg — คู่กับ deploy.ps1 ฝั่ง Windows
# ใช้: scripts/deploy-mac.sh [--no-dmg]   → dist/PS-WRAP-MAC.app (+ dist/PS-WRAP-MAC-v<ver>-macos-arm64.zip สำหรับ release / ตัวอัปเดตในแอป, dist/PS-WRAP-MAC.dmg)
# ทำตามขั้นของ upstream .github/workflows/build-macos-arm.yml · ต้อง build-mac.sh ก่อน
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
BREW=$(brew --prefix)
MAKE_DMG=1
[ "$1" = "--no-dmg" ] && MAKE_DMG=0
SRC=ps-wrap/build/gui/PS-WRAP-MAC.app
[ -d "$SRC" ] || { echo "ยังไม่มี $SRC → scripts/build-mac.sh ก่อน"; exit 1; }
VER=$(plutil -extract CFBundleShortVersionString raw "$SRC/Contents/Info.plist")
OUT=dist
APP=$OUT/PS-WRAP-MAC.app
# กันพลาด: .app ใน dist กำลังรันอยู่ → ปิดก่อน (ไม่แตะตัวที่รันจาก build/)
pkill -f "$ROOT/$APP/Contents/MacOS/PS-WRAP-MAC" 2>/dev/null || true
rm -rf "$APP" "$OUT/PS-WRAP-MAC.dmg" "$OUT"/PS-WRAP-MAC-v*-macos-arm64.zip
mkdir -p "$OUT"
START=$(date +%s)
cp -a "$SRC" "$APP"
FW="$APP/Contents/Frameworks"
mkdir -p "$FW" "$APP/Contents/Resources/vulkan/icd.d"

echo "[1/5] libs นอก Qt: onnxruntime · vulkan loader · MoltenVK ICD"
# onnxruntime (AI facecam) — โค้ดหา Contents/Frameworks/libonnxruntime.dylib ก่อน (pswrapsegmenter.cpp)
cp -L "$BREW/opt/onnxruntime/lib/libonnxruntime.dylib" "$FW/libonnxruntime.dylib"
# vulkan loader — main.cpp ตั้ง QT_VULKAN_LIB=Contents/Frameworks/libvulkan.1.dylib · libplacebo ก็ลิงก์ libvulkan (macdeployqt จะ copy อีกชุดถ้าหาเจอ)
cp -L "$BREW/opt/vulkan-loader/lib/libvulkan.1.dylib" "$FW/libvulkan.1.dylib"
# SDL3: brew sdl2 = sdl2-compat ซึ่ง dlopen "@loader_path/libSDL3.dylib" ตอน init — ไม่มี = NSAlert ค้างก่อน main() (เจอ 2026-10-15)
cp -L "$BREW/opt/sdl3/lib/libSDL3.0.dylib" "$FW/libSDL3.dylib"
# MoltenVK: loader หา ICD json ใน <bundle>/Contents/Resources/vulkan/icd.d · ตัว dylib วางใน Frameworks (codesign --deep เซ็นให้ — ถ้าวางใน Resources
# แล้วแก้ install name จะโดน SIGKILL "Code Signature Invalid" ตอน loader dlopen · เจอ 2026-10-15) · library_path เทียบกับโฟลเดอร์ json
cp -L "$BREW/opt/molten-vk/lib/libMoltenVK.dylib" "$FW/libMoltenVK.dylib"
cat > "$APP/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json" <<'JSON'
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "../../../Frameworks/libMoltenVK.dylib",
        "api_version": "1.2.0",
        "is_portability_driver": true
    }
}
JSON
chmod -R u+w "$APP"   # ไฟล์จาก Cellar เป็น read-only → strip/codesign ของ macdeployqt ล้มเหลว

echo "[2/5] macdeployqt (Qt frameworks/plugins/QML + dylib จาก Homebrew ที่ exe ใช้)"
# gotcha (upstream): ต้องมี import QtWebEngine ใน qmldir ไม่งั้น macdeployqt ไม่ bundle WebEngine (PSNLoginDialog สร้าง WebEngineView ตอนรัน)
cp ps-wrap/scripts/qtwebengine_import.qml ps-wrap/gui/src/qml/
trap 'rm -f "$ROOT/ps-wrap/gui/src/qml/qtwebengine_import.qml"' EXIT
"$BREW/opt/qt@6/bin/macdeployqt" "$APP" -qmldir="$ROOT/ps-wrap/gui/src/qml" -libpath="$BREW/lib" \
	-executable="$FW/libonnxruntime.dylib" -executable="$FW/libvulkan.1.dylib" -executable="$FW/libSDL3.dylib" -executable="$FW/libMoltenVK.dylib" \
	> "$OUT/macdeployqt.log" 2>&1 || { echo "macdeployqt ล้มเหลว ดู $OUT/macdeployqt.log"; exit 1; }
rm -f ps-wrap/gui/src/qml/qtwebengine_import.qml
# WebEngine helper ต้องเห็น Frameworks ของแอป (upstream)
if [ -d "$FW/QtWebEngineCore.framework/Helpers/QtWebEngineProcess.app" ]; then
	ln -sfn ../../../../../../../Frameworks "$FW/QtWebEngineCore.framework/Helpers/QtWebEngineProcess.app/Contents/Frameworks"
fi
ln -sfn libvulkan.1.dylib "$FW/vulkan"

echo "[3/5] แก้ dylib/framework ที่ยังชี้ /opt/homebrew (macdeployqt ข้ามของ QtWebEngineProcess helper)"
chmod -R u+w "$APP"   # ของที่ macdeployqt copy มาใหม่ก็ read-only (จาก Cellar) → install_name_tool เขียนไม่ได้ (เจอ 2026-10-15: libplacebo ยังอ้าง libvulkan ของ brew)
# brew build Qt ด้วย install name แบบ absolute · helper อ้าง /opt/homebrew/opt/qtbase/lib/QtCore.framework/... → ชี้ไป Frameworks ของแอป
# (helper มี Contents/Frameworks → ../../../../../../../Frameworks symlink ตาม upstream · ดังนั้น @executable_path/../Frameworks ใช้ได้ทั้ง exe หลักและ helper)
find "$APP" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 | while IFS= read -r -d '' bin; do
	otool -L "$bin" 2>/dev/null | awk 'NR>1 {print $1}' | grep -E '^/opt/homebrew|^/usr/local' | while read -r dep; do
		base=$(basename "$dep")
		case "$dep" in
			*.framework/*) fwname=$(echo "$dep" | sed -E 's|.*/([^/]+)\.framework/.*|\1|'); new="@executable_path/../Frameworks/$fwname.framework/Versions/A/$fwname" ;;
			*) new="@executable_path/../Frameworks/$base" ;;
		esac
		# ID ของตัวเอง (บรรทัดแรก) ไม่ใช่ dependency — แต่เปลี่ยนให้เป็น @rpath ไว้ด้วยกันปน
		if [ "$(otool -D "$bin" 2>/dev/null | sed -n 2p)" = "$dep" ]; then
			install_name_tool -id "@rpath/$base" "$bin" 2>/dev/null || true
			continue
		fi
		target="$FW/$base"; [ -e "$target" ] || target="$FW/$fwname.framework"
		if [ -e "$target" ] || [ -e "$FW/$(basename "$new")" ]; then
			install_name_tool -change "$dep" "$new" "$bin" || echo "  install_name_tool ล้มเหลว: $bin ($dep)"
		else
			echo "  ไม่มีใน Frameworks: $dep ← $bin"
		fi
	done
done
LEFT=$(find "$APP" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 | xargs -0 -n1 sh -c 'otool -L "$0" 2>/dev/null | awk -v f="$0" "NR>1 && /^[[:space:]]+\\/(opt\\/homebrew|usr\\/local)/ {print f\": \"\$1}"' | sort -u || true)
if [ -n "$LEFT" ]; then
	echo "ยังเหลืออ้าง Homebrew (เครื่องอื่นจะเปิดไม่ได้):"; echo "$LEFT"; exit 1
fi

echo "[4/5] codesign (ad-hoc) + ตัด quarantine"
codesign --force --deep --sign - --entitlements ps-wrap/gui/entitlements.xml "$APP" > "$OUT/codesign.log" 2>&1 || { echo "codesign ล้มเหลว ดู $OUT/codesign.log"; exit 1; }
xattr -dr com.apple.quarantine "$APP" 2>/dev/null || true

echo "[5/5] zip (สำหรับ GitHub release + ตัวอัปเดตในแอป: ชื่อต้องลงท้าย -macos-arm64.zip)${MAKE_DMG:+ + dmg}"
ZIP="PS-WRAP-MAC-v$VER-macos-arm64.zip"
(cd "$OUT" && ditto -c -k --keepParent PS-WRAP-MAC.app "$ZIP")
shasum -a 256 "$OUT/$ZIP" | tee "$OUT/$ZIP.sha256"
if [ "$MAKE_DMG" = 1 ]; then
	hdiutil create -quiet -srcfolder "$APP" -volname "PS-WRAP-MAC" "$OUT/PS-WRAP-MAC.dmg"
fi
SIZE=$(du -sh "$APP" | cut -f1)
echo "deploy ผ่าน ใช้เวลา $(( $(date +%s) - START ))s → $APP ($SIZE) · $OUT/$ZIP"
