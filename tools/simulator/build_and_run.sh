#!/usr/bin/env bash
# Desktop UI simulator: compiles the real NFC_Toolkit/ui.cpp + LVGL for the host with the
# NFC hardware stubbed out, renders every screen at 410x502, then checks nothing lands in the
# T-Watch Ultra's rounded display corners and writes PNGs to docs/screenshots/.
#
# Usage: tools/simulator/build_and_run.sh [path/to/Arduino/libraries]   (default ~/Arduino/libraries)
# Needs: gcc/g++, python3 + pillow, and LilyGoLib + LilyGoLib-ThirdParty (for lvgl) installed.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
LIBS="${1:-$HOME/Arduino/libraries}"
LV="$LIBS/lvgl"
SVC="$LIBS/LilyGoLib/src/nfc/LilyGoNfcService.h"
OUT="$HERE/build"
mkdir -p "$OUT/obj" "$HERE/shots" "$HERE/png"

# Public NFC service types (enums/structs), without the RFAL-dependent class.
start=$(grep -n 'define LILYGO_NFC_MAX_UID_LEN' "$SVC" | cut -d: -f1)
cls=$(grep -n '^class LilyGoNfcService' "$SVC" | cut -d: -f1)
end=$(head -n "$cls" "$SVC" | grep -n '^};' | tail -1 | cut -d: -f1)
sed -n "$((start-1)),${end}p" "$SVC" > "$HERE/nfc_types.inc"
# Host lv_conf: use libc malloc instead of PSRAM.
cp "$HERE/lv_conf.h" "$OUT/lv_conf.h"

echo "Compiling LVGL (once)..."
find "$LV/src" -name '*.c' | while read -r f; do
  o="$OUT/obj/$(echo "$f" | md5sum | cut -c1-12).o"
  [ -f "$o" ] || echo "$f $o"
done | xargs -r -P "$(nproc)" -n 2 sh -c 'gcc -c -O1 -w -DLV_CONF_INCLUDE_SIMPLE -I'"$OUT"' -I'"$LV"' -I'"$LV"'/src "$0" -o "$1"'

g++ -std=c++17 -O1 -w -DLV_CONF_INCLUDE_SIMPLE -I"$OUT" -I"$HERE" -I"$LV" -I"$LV/src" -I"$REPO/NFC_Toolkit" \
    "$HERE/sim_main.cpp" "$REPO/NFC_Toolkit/ndef_builder.cpp" "$OUT"/obj/*.o -lm -o "$OUT/sim"
(cd "$HERE" && rm -f shots/*.raw && "$OUT/sim")
python3 "$HERE/check.py" "${CORNER_RADIUS:-100}"
cp "$HERE"/png/*.png "$REPO/docs/screenshots/"
echo "Screenshots updated in docs/screenshots/"
