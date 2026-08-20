#!/bin/sh
UA='3draster-content-port/1.0 (mreverssci@gmail.com)'
set -e
for p in Araxxor Araxxor/Strategies Mirrorback_Araxyte Ruptura_Araxyte Acidic_Araxyte; do
  f=$(printf '%s' "$p" | tr '/' '_')
  curl -sS -m 60 -A "$UA" "https://oldschool.runescape.wiki/w/${p}?action=raw" -o "${f}.wiki"
  printf '%-28s %8d bytes\n' "${f}.wiki" "$(wc -c < "${f}.wiki")"
  sleep 1
done
