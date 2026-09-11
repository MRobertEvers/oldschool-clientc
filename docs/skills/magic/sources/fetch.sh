#!/bin/sh
# Pinned OSRS Wiki sources for the Magic spellbooks (slices D4-D7).
set -e
cd "$(dirname "$0")"
fetch() {
  if curl -sf -A 'osrs239-content port (contact: repo owner)' \
      "https://oldschool.runescape.wiki/w/$1?action=raw" -o "$2"; then
    echo "$2 $(wc -c < "$2") bytes"
  else
    echo "$2 MISSING ($1)"
    rm -f "$2"
  fi
}
fetch Standard_spellbook Standard_spellbook.wiki
fetch Lunar_spellbook Lunar_spellbook.wiki
fetch Arceuus_spellbook Arceuus_spellbook.wiki
fetch Ancient_spellbook Ancient_spellbook.wiki
fetch Teleport Teleport.wiki
