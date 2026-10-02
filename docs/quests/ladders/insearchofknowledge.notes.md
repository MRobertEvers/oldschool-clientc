In Search of Knowledge: driving notes (parity pass b52)

- Surface ladder hosdun_entrance_ladder_hole (1702,3574) lands at 1830,9973 via
  ladders_stairs/configs/maplink.dbrow, NOT at Aimeri. The quest no longer overrides it.
- Aimeri stands at 1840,9926 in a pocket: from 1838,9927 every press said "I can't reach
  that!". Stand at 1840,9929 (north of him) and use_on / talk_to work.
- Aimeri is a multinpc: feed with the injured form (hosdun_aimeri_injured), talk to
  hosdun_aimeri_healed after the fifth food. Resolve the target with
  t.player.by_symbol("npc", ...) (a {kind,id} table), not t.npc.by_symbol.
- Food: cooked fish, meat, vegetables only. Bread is refused ("isn't suitable food") and kept.
- Dialogue gate (Transcript:In_Search_of_Knowledge): after 5 foods the healed menu is "What is
  this place?" / "Who are you?" / "How did you get injured?" / "I'm fine, thanks.". "Who are
  you?" writes stage 1 at "The greater good."; the other three do not start it.
- Shelves (all need stage 1): temple 1796,9935; sun 1805,9935; moon 1804,9943. Stand a tile south.
- Pages: use a page on its tome (either direction). One stack consumes min(held, 4-inserted).
- Logosia (1633,3808): using ONE complete tome hands over every complete tome in the pack in the
  same action; the third return writes stage 2. Talk to her then for the lamp (stage 3).
- Dungeon is aggressive (druids, spiders, dragons): a fresh character dies; use ::godmode.
  Single-way combat answers "I'm already under attack." while something else is on you.
- Page drops are random tertiaries (spider 1/30, baby red dragon 1/25, red dragon 1/10, druid
  1/20), Forthos Dungeon (map square 28_155) only; pages stay ground items. The run's clock is
  deterministic, so a loop that rolls 17 misses repeats them: an idle beat shifts the stream.
- Different from the guide: the lamp is thosf_reward_lamp (shared id) with no rub UI yet;
  Sarachnis page drop and the web/knife cut on the route are not driven (route taken by goto).
