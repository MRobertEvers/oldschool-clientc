#!/bin/sh
# Pinned OSRS Wiki sources for Pyramid Plunder (slice C11).
set -e
cd "$(dirname "$0")"
fetch() {
  curl -sf -A 'osrs239-content port (contact: repo owner)' \
    "https://oldschool.runescape.wiki/w/$1?action=raw" -o "$2"
  echo "$2 $(wc -c < "$2") bytes"
}
fetch Pyramid_Plunder Pyramid_Plunder.wiki
fetch Pharaoh%27s_sceptre Pharaohs_sceptre.wiki
