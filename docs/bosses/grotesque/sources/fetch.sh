#!/bin/sh
UA='3draster-content-port/1.0 (mreverssci@gmail.com)'
set -e
for p in Grotesque_Guardians Dusk Dawn Grotesque_Guardians/Strategies; do
  f=$(printf '%s' "$p" | tr '/' '_')
  curl -sS -m 60 -A "$UA" "https://oldschool.runescape.wiki/w/${p}?action=raw" -o "${f}.wiki"
  printf '%-34s %8d bytes\n' "${f}.wiki" "$(wc -c < "${f}.wiki")"
  sleep 1
done
