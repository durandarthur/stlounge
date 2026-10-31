#!/usr/bin/env bash
# Re-renders the PNG icons from assets/icons/icon.svg (needs rsvg-convert, from librsvg).
# Only needed after editing the SVG; the PNGs are committed, so builds don't need it.
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../assets/icons" && pwd)"
command -v rsvg-convert >/dev/null 2>&1 || { echo "rsvg-convert not found (package librsvg)" >&2; exit 1; }
for size in 16 32 48 64 128 256; do
    rsvg-convert -w "$size" -h "$size" "$DIR/icon.svg" -o "$DIR/$size.png"
done
echo "rendered $DIR/{16,32,48,64,128,256}.png"
