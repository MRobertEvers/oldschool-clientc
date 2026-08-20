#!/bin/sh
# Pinned OSRS Wiki sources for the gilded altar (slice D10).
set -e
cd "$(dirname "$0")"
fetch() {
  if curl -sf -A 'osrs239-content port (contact: repo owner)' \
      "https://oldschool.runescape.wiki/w/$1?action=raw" -o "$2"; then
    echo "$2 $(wc -c < "$2") bytes"
  else
    echo "$2 MISSING ($1)"; rm -f "$2"
  fi
}
fetch "Altar_(Chapel)" Altar_Chapel.wiki
fetch Gilded_altar Gilded_altar.wiki
fetch Incense_burner Incense_burner.wiki
