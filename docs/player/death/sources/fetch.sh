#!/bin/sh
# Pinned OSRS Wiki source for Death's Office (slice E22).
set -e
cd "$(dirname "$0")"
curl -sf -A 'osrs239-content port (contact: repo owner)' \
  "https://oldschool.runescape.wiki/w/Death%27s_Office?action=raw" -o Deaths_Office.wiki
echo "Deaths_Office.wiki $(wc -c < Deaths_Office.wiki) bytes"
