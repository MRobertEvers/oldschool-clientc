#!/bin/sh
UA='3draster-content-port/1.0 (mreverssci@gmail.com)'
set -e
for p in Dagannoth_Rex Dagannoth_Prime Dagannoth_Supreme Dagannoth_Kings; do
  curl -sS -m 60 -A "$UA" "https://oldschool.runescape.wiki/w/${p}?action=raw" -o "${p}.wiki"
  printf '%-26s %8d bytes\n' "${p}.wiki" "$(wc -c < "${p}.wiki")"
  sleep 1
done
