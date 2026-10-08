#!/usr/bin/env bash
# Regenerates src/fonts/*.c (Montserrat with French accents + a few FontAwesome icons).
# The generated files are committed, so you only need this to change sizes/glyphs.
#
# One-time setup (installs only inside tools/fontgen):
#   cd tools/fontgen && npm install --cache ./.npm-cache lv_font_conv@1.5.3
# The TTF/WOFF sources come with LVGL (downloaded by PlatformIO into .pio/libdeps).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONV="$ROOT/tools/fontgen/node_modules/.bin/lv_font_conv"
FONTS="$ROOT/.pio/libdeps/cyd_st7789/lvgl/scripts/built_in_font"
OUT="$ROOT/src/fonts"
mkdir -p "$OUT"

TEXT="$FONTS/Montserrat-Medium.ttf"
ICONS="$FONTS/FontAwesome5-Solid+Brands+Regular.woff"

# ASCII, Latin-1 (é è à ç ...), Œ œ, dashes, quotes, bullet, ellipsis, arrow, euro
TEXT_RANGE="0x20-0x7E,0xA0-0xFF,0x152-0x153,0x2013-0x2014,0x2018-0x2019,0x201C-0x201D,0x2022,0x2026,0x2192,0x20AC"
# check-circle, warning, info-circle, times-circle, wifi, moon, clock, train
ICON_RANGE="0xF058,0xF071,0xF05A,0xF057,0xF1EB,0xF186,0xF017,0xF238"

for size in 12 14 16 20; do
  "$CONV" --no-compress --no-prefilter --bpp 4 --size "$size" --format lvgl \
    --font "$TEXT" -r "$TEXT_RANGE" \
    --font "$ICONS" -r "$ICON_RANGE" \
    --lv-font-name "font_ui_$size" -o "$OUT/font_ui_$size.c"
done

# Big countdown digits
"$CONV" --no-compress --no-prefilter --bpp 4 --size 44 --format lvgl \
  --font "$TEXT" -r "0x20,0x2D,0x30-0x3A,0x3C,0x68" \
  --lv-font-name "font_big_44" -o "$OUT/font_big_44.c"

echo "Fonts written to $OUT"
