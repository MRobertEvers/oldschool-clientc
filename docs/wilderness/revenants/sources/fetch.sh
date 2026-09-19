#!/bin/sh
# Pinned OSRS Wiki sources for the Revenant Caves (slice C9).
set -e
cd "$(dirname "$0")"
fetch() {
  curl -sf -A 'osrs239-content port (contact: repo owner)' \
    "https://oldschool.runescape.wiki/w/$1?action=raw" -o "$2"
  echo "$2 $(wc -c < "$2") bytes"
}
fetch Revenants Revenants.wiki
fetch Revenant_Caves Revenant_Caves.wiki
fetch Amulet_of_avarice Amulet_of_avarice.wiki
fetch Revenant_maledictus Revenant_maledictus.wiki
fetch Ancient_relic Ancient_relic.wiki
