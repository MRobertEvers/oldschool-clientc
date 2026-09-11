#!/bin/sh
# Dwarf Multicannon wiki corpus. Raw wikitext, for the reason the Treasure
# Trails fetcher gives: rendered views collapse {{CiteTwitter}} (Jagex
# statements, the top rung of the precedence ladder) into footnote markers.
UA='3draster-content-port/1.0 (mreverssci@gmail.com)'
set -e
for p in Dwarf_multicannon Cannon_base Cannon_stand Cannon_barrels Cannon_furnace \
         Steel_cannonball Granite_cannonball Ammo_mould Nulodion Dwarf_Cannon; do
  curl -sS -m 60 -A "$UA" "https://oldschool.runescape.wiki/w/${p}?action=raw" -o "${p}.wiki"
  printf '%-28s %8d bytes\n' "${p}.wiki" "$(wc -c < "${p}.wiki")"
  sleep 1
done
