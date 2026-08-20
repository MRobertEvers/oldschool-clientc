#!/bin/sh
# Tears of Guthix wiki corpus. Raw wikitext — rendered views collapse
# {{CiteTwitter}} (Jagex statements) into footnote markers.
UA='3draster-content-port/1.0 (mreverssci@gmail.com)'
set -e
for p in Tears_of_Guthix Tears_of_Guthix_\(minigame\) Juna Stone_bowl Tears_of_guthix; do
  f=$(printf '%s' "$p" | tr -d '\\' | tr '/()' '___')
  curl -sS -m 60 -A "$UA" "https://oldschool.runescape.wiki/w/${p}?action=raw" -o "${f}.wiki" || true
  printf '%-40s %8d bytes\n' "${f}.wiki" "$(wc -c < "${f}.wiki" 2>/dev/null || echo 0)"
  sleep 1
done
