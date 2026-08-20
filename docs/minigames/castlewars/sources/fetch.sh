#!/bin/sh
UA='3draster-content-port/1.0 (mreverssci@gmail.com)'
set -e
for p in Castle_Wars Castle_wars_ticket Bandages Explosive_potion Barricade; do
  curl -sS -m 60 -A "$UA" "https://oldschool.runescape.wiki/w/${p}?action=raw" -o "${p}.wiki"
  printf '%-26s %8d bytes\n' "${p}.wiki" "$(wc -c < "${p}.wiki")"
  sleep 1
done
