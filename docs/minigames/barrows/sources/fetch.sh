#!/bin/sh
# Pinned OSRS Wiki sources for Barrows (slice C10).
set -e
cd "$(dirname "$0")"
fetch() {
  curl -sf -A 'osrs239-content port (contact: repo owner)' \
    "https://oldschool.runescape.wiki/w/$1?action=raw" -o "$2"
  echo "$2 $(wc -c < "$2") bytes"
}
fetch Barrows Barrows.wiki
fetch Barrows/Strategies Barrows_Strategies.wiki
fetch "Chest_(Barrows)" Chest_Barrows.wiki
