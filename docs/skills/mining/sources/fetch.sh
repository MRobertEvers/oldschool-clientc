#!/bin/sh
# Pinned OSRS Wiki sources for the Motherlode Mine (slice C17).
set -e
cd "$(dirname "$0")"
fetch() {
  curl -sf -A 'osrs239-content port (contact: repo owner)' \
    "https://oldschool.runescape.wiki/w/$1?action=raw" -o "$2"
  echo "$2 $(wc -c < "$2") bytes"
}
fetch Motherlode_Mine Motherlode_Mine.wiki
fetch Golden_nugget Golden_nugget.wiki
fetch Prospector_kit Prospector_kit.wiki
