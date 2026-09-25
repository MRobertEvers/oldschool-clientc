#!/bin/sh
# Pinned OSRS Wiki sources for Pest Control (slice C4).
set -e
cd "$(dirname "$0")"
fetch() {
  out="$2"
  curl -sf -A 'osrs239-content port (contact: repo owner)' \
    "https://oldschool.runescape.wiki/w/$1?action=raw" -o "$out"
  echo "$out $(wc -c < "$out") bytes"
}
fetch Pest_Control Pest_Control.wiki
fetch Void_Knight_equipment Void_Knight_equipment.wiki
fetch Elite_Void_Knight_equipment Elite_Void_Knight_equipment.wiki
fetch Void_Knights%27_Outpost Void_Knights_Outpost.wiki
fetch "Barricade_(Pest_Control)" Barricade_PestControl.wiki
