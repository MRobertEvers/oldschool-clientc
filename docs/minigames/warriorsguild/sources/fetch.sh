#!/bin/sh
UA='3draster-content-port/1.0 (mreverssci@gmail.com)'
set -e
for p in Warriors%27_Guild Warrior_guild_token Animated_Armour Defender Dragon_defender; do
  f=$(printf '%s' "$p" | sed 's/%27//')
  curl -sS -m 60 -A "$UA" "https://oldschool.runescape.wiki/w/${p}?action=raw" -o "${f}.wiki"
  printf '%-28s %8d bytes\n' "${f}.wiki" "$(wc -c < "${f}.wiki")"
  sleep 1
done
