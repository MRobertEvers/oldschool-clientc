#!/bin/sh
# Pinned OSRS Wiki sources for the Falador Party Room (slice C12).
set -e
cd "$(dirname "$0")"
fetch() {
  curl -sf -A 'osrs239-content port (contact: repo owner)' \
    "https://oldschool.runescape.wiki/w/$1?action=raw" -o "$2"
  echo "$2 $(wc -c < "$2") bytes"
}
fetch Falador_Party_Room Falador_Party_Room.wiki
fetch Party_balloon Party_balloon.wiki
