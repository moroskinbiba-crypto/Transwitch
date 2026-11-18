#!/usr/bin/env bash
set -euo pipefail

DESTINATION="${1:?usage: package-runtime-assets.sh <sdmc-deskNX-directory>}"
FONT_DIR="$DESTINATION/media/fonts"
mkdir -p "$FONT_DIR"

fetch() {
    local url="$1"
    local destination="$2"
    curl --fail --location --silent --show-error --retry 3 --retry-delay 1 \
        "$url" -o "$destination"
    test -s "$destination"
}

# These open-source Google Fonts families include Cyrillic and Cyrillic Extended.
# Variable Noto/Open Sans are supported by the bundled FreeType/SDL_ttf build.
fetch "https://raw.githubusercontent.com/google/fonts/main/ofl/notosans/NotoSans%5Bwdth%2Cwght%5D.ttf" "$FONT_DIR/NotoSans.ttf"
fetch "https://raw.githubusercontent.com/google/fonts/main/ofl/opensans/OpenSans%5Bwdth%2Cwght%5D.ttf" "$FONT_DIR/OpenSans.ttf"
fetch "https://raw.githubusercontent.com/google/fonts/main/ofl/ptsans/PT_Sans-Web-Regular.ttf" "$FONT_DIR/PTSans.ttf"

fetch "https://raw.githubusercontent.com/google/fonts/main/ofl/notosans/OFL.txt" "$FONT_DIR/NotoSans-OFL.txt"
fetch "https://raw.githubusercontent.com/google/fonts/main/ofl/opensans/OFL.txt" "$FONT_DIR/OpenSans-OFL.txt"
fetch "https://raw.githubusercontent.com/google/fonts/main/ofl/ptsans/OFL.txt" "$FONT_DIR/PTSans-OFL.txt"

cat > "$FONT_DIR/README.txt" <<'EOF'
DeskNX built-in fonts
=====================

Noto Sans — Noto Project Authors — SIL Open Font License 1.1.
Open Sans — The Open Sans Project Authors — SIL Open Font License 1.1.
PT Sans — Alexandra Korolkova, Olga Umpeleva and Vladimir Yefimov — SIL Open Font License 1.1.

All three families include Cyrillic glyphs. Select a font in DeskNX:
Settings > Choose custom font > media/fonts.
The matching OFL license files are included beside each font.
EOF

echo "Packaged DeskNX fonts:"
ls -lh "$FONT_DIR"/*.ttf
