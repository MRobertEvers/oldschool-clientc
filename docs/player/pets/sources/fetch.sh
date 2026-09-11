#!/bin/sh
# Pinned OSRS Wiki sources for the follower/pet system (slice E7).
set -e
cd "$(dirname "$0")"
fetch() {
  if curl -sf -A 'osrs239-content port (contact: repo owner)' \
      "https://oldschool.runescape.wiki/w/$1?action=raw" -o "$2"; then
    echo "$2 $(wc -c < "$2") bytes"
  else
    echo "$2 MISSING ($1)"; rm -f "$2"
  fi
}
fetch Pet Pet.wiki
fetch Probita Probita.wiki
