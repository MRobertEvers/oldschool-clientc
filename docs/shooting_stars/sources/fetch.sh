#!/bin/sh
# Shooting Stars wiki corpus. Raw wikitext.
UA='3draster-content-port/1.0 (mreverssci@gmail.com)'
set -e
for p in Shooting_Stars Crashed_star Star_dust Star_sprite Stardust; do
  curl -sS -m 60 -A "$UA" "https://oldschool.runescape.wiki/w/${p}?action=raw" -o "${p}.wiki" || true
  printf '%-24s %8d bytes\n' "${p}.wiki" "$(wc -c < "${p}.wiki" 2>/dev/null || echo 0)"
  sleep 1
done
