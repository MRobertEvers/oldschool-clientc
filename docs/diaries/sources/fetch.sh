#!/bin/sh
# Achievement Diary wiki corpus. Raw wikitext: each diary page carries its tasks
# as numbered rows inside `data-diary-name`/`data-diary-tier` tables, which is a
# table rather than prose and can be parsed.
UA='3draster-content-port/1.0 (mreverssci@gmail.com)'
set -e
for p in Ardougne_Diary Desert_Diary Falador_Diary Fremennik_Diary Kandarin_Diary \
         Karamja_Diary Kourend_%26_Kebos_Diary Lumbridge_%26_Draynor_Diary \
         Morytania_Diary Varrock_Diary Western_Provinces_Diary Wilderness_Diary \
         Achievement_Diary; do
  f=$(printf '%s' "$p" | sed 's/%26/and/')
  curl -sS -m 60 -A "$UA" "https://oldschool.runescape.wiki/w/${p}?action=raw" -o "${f}.wiki"
  printf '%-40s %8d bytes\n' "${f}.wiki" "$(wc -c < "${f}.wiki")"
  sleep 1
done
