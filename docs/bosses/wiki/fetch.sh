#!/bin/sh
# Boss infobox corpus. One page per boss whose encounter is config rather than
# script; `tools/check_boss_contract.py` holds the configs to these.
UA='3draster-content-port/1.0 (mreverssci@gmail.com)'
set -e
for p in Sarachnis Thermonuclear_smoke_devil Obor Bryophyta King_Black_Dragon \
         Kraken Scorpia Chaos_Fanatic Crazy_archaeologist Deranged_archaeologist; do
  curl -sS -m 60 -A "$UA" "https://oldschool.runescape.wiki/w/${p}?action=raw" -o "${p}.wiki"
  printf '%-32s %8d bytes\n' "${p}.wiki" "$(wc -c < "${p}.wiki")"
  sleep 1
done
