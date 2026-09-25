#!/bin/sh
# Pinned OSRS Wiki source for the gnome gliders (slice E22).
set -e
cd "$(dirname "$0")"
curl -sf -A 'osrs239-content port (contact: repo owner)' \
  "https://oldschool.runescape.wiki/w/Gnome_glider?action=raw" -o Gnome_glider.wiki
echo "Gnome_glider.wiki $(wc -c < Gnome_glider.wiki) bytes"
