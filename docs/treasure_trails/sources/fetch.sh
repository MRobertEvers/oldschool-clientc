#!/bin/sh
# Treasure Trails wiki corpus — the rung-2 source for docs/treasure_trails/.
# Raw wikitext, because rendered views collapse {{CiteTwitter}}/{{CiteDiscord}}
# (Jagex statements, rung 1) into footnote markers with the quote thrown away.
UA='3draster-content-port/1.0 (mreverssci@gmail.com)'
set -e
for p in \
  'Treasure_Trails' \
  'Treasure_Trails/Guide' \
  'Clue_scroll' \
  'Clue_scroll_(beginner)' \
  'Clue_scroll_(easy)' \
  'Clue_scroll_(medium)' \
  'Clue_scroll_(hard)' \
  'Clue_scroll_(elite)' \
  'Clue_scroll_(master)' \
  'Reward_casket_(beginner)' \
  'Reward_casket_(easy)' \
  'Reward_casket_(medium)' \
  'Reward_casket_(hard)' \
  'Reward_casket_(elite)' \
  'Reward_casket_(master)' \
  'Casket' \
  'Sextant' \
  'Puzzle_box' \
  'Light_box' \
  'Mimic' \
  'Uri' \
  'Watson' \
  'Sherlock' \
  'STASH_unit' \
  'Charlie_the_Tramp' \
  'Hot_cold' \
  'Anagram' \
  'Cipher' \
  'Emote_clue' \
  'Cryptic_clue' \
  'Coordinates' \
  'Map_clue' \
  'Falo_the_Bard' \
  'Fairy_ring' \
  'Scroll_box' \
  ; do
  f=$(printf '%s' "$p" | tr '/' '_')
  curl -sS -m 60 -A "$UA" "https://oldschool.runescape.wiki/w/${p}?action=raw" -o "${f}.wiki"
  printf '%-36s %8d bytes\n' "${f}.wiki" "$(wc -c < "${f}.wiki")"
  sleep 1
done
