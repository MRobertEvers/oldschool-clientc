#!/bin/sh
# Pinned OSRS Wiki source for loot keys (slice E12).
set -e
cd "$(dirname "$0")"
if curl -sf -A 'osrs239-content port (contact: repo owner)' \
    "https://oldschool.runescape.wiki/w/Loot_key?action=raw" -o Loot_key.wiki; then
  echo "Loot_key.wiki $(wc -c < Loot_key.wiki) bytes"
else
  echo "Loot_key.wiki MISSING"; rm -f Loot_key.wiki
fi
