#!/bin/sh
# Pinned OSRS Wiki sources for Agility shortcuts (slice D2).
set -e
cd "$(dirname "$0")"
fetch() {
  curl -sf -A 'osrs239-content port (contact: repo owner)' \
    "https://oldschool.runescape.wiki/w/$1?action=raw" -o "$2"
  echo "$2 $(wc -c < "$2") bytes"
}
fetch Shortcuts Shortcuts.wiki
