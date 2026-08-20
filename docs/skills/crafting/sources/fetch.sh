#!/bin/sh
# Pinned OSRS Wiki source for crystal equipment (slice E16).
set -e
cd "$(dirname "$0")"
curl -sf -A 'osrs239-content port (contact: repo owner)' \
  "https://oldschool.runescape.wiki/w/Crystal_equipment?action=raw" -o Crystal_equipment.wiki
echo "Crystal_equipment.wiki $(wc -c < Crystal_equipment.wiki) bytes"
