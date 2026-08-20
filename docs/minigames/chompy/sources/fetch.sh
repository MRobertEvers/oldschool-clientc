#!/bin/sh
# Pinned OSRS Wiki sources for Chompy bird hunting (slice C16).
set -e
cd "$(dirname "$0")"
fetch() {
  curl -sf -A 'osrs239-content port (contact: repo owner)' \
    "https://oldschool.runescape.wiki/w/$1?action=raw" -o "$2"
  echo "$2 $(wc -c < "$2") bytes"
}
fetch Chompy_bird_hunting Chompy_bird_hunting.wiki
