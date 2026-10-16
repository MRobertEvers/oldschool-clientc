#!/bin/sh
# Pinned OSRS Wiki sources for Tutorial Island -- the quest Learning the Ropes,
# the October 2025 tutorial (server/scripts/tutorial/, test/quests/tutorialisland.lua).
# Refresh: sh fetch.sh   (raw wikitext, one file per page)
UA='3draster-content-port/1.0'
set -e
for p in Learning_the_Ropes Transcript:Learning_the_Ropes Tutorial_Island \
    Gielinor_Guide Survival_Expert Master_Chef Quest_Guide Mining_Instructor Vannaka \
    Account_Guide Poll_booth Brother_Brace Magic_Instructor \
    Ironman_tutor_\(Tutorial_Island\) Transcript:Ironman_tutor; do
  f=$(echo "$p" | tr ':()' '___').wiki
  code=$(curl -sS -m 60 -A "$UA" -o "$f" -w '%{http_code}' "https://oldschool.runescape.wiki/w/${p}?action=raw" || echo 000)
  printf '%-45s %s %8d bytes\n' "$f" "$code" "$(wc -c < "$f")"
  [ "$code" = 200 ] && [ -s "$f" ] || rm -f "$f"
  sleep 1
done
