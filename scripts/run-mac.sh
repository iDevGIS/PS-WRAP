#!/bin/bash
# รัน build ล่าสุดบน macOS — คู่กับ run.ps1 ฝั่ง Windows · log ออก terminal
# ใช้: scripts/run-mac.sh [args ของแอป]
cd "$(dirname "$0")/../ps-wrap"
APP=build/gui/chiaki.app/Contents/MacOS/chiaki
[ -x "$APP" ] || { echo "ยังไม่มี build → scripts/build-mac.sh"; exit 1; }
exec "$APP" "$@"
