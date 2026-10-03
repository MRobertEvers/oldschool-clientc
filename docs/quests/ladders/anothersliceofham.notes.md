# Another Slice of H.A.M. -- driver notes (b55 parity)

Stages 5 to 11 were driven through the real client (62 rows, 0 fail). Stages 0-4 (Ur-tag,
Tegdak digs, Zanik, Scribe, Oldak) were not re-driven this pass.

Map positions differ from the guide: the port uses its own regions.
- Goblin Village tower: after Wartface (stage 5) you are teleported to 2443,5421 (region m38_84),
  not the real village. Ladder `slice_goblin_ladder_bottom` -> tower top 2447,5417 level 2.
- The Mage and Archer both stand on 2447,5417. Melee is refused ("You cannot reach...").
  Bring a bow or staff: both have 35 hp, speed 10 (slice.npc).
- Both dead -> you are teleported to the generals (stage 7); they hand over the Ancient mace.
- Swamp: Slimetoes/Mossfists at 3169,3170 / 3171,3170 (slice_sergeant_*_swamp). Briefing needs a
  LIT light (any lit lantern, torch, lamp, candle; a tinderbox alone is refused).
- Dark hole `goblin_cave_entrance` 3169,3172 needs a rope the first time; it is tied
  (varb279_swamp_caves_roped_entrance) and consumed. Arrive 3169,9571; the ladder
  `slice_ladder_laddertop_swamp` 3171,9568 leads to the base at 2397,5558.
- Base puzzle: click `slice_stealth_crate_stacked` (hiding=1, you stand at ~2400,5538), wait
  ~10 ticks for a guard at the corner, then walk to 2412,5537 (reached_snipers=1). Walking
  while not hiding in a guard's sight line puts you back at the entrance. Sergeant dialogue
  (One wait / Follow me) is an alternate route to the same flags.
- Then `slice_laddertop` 2413,5526 -> Sigmund's room 2543,5511 (Sigmund speaks first).
- Sigmund: 70 hp, level 64. Any style makes him switch his protection prayer; only the Ancient
  mace special drops it. Wield the mace, arm the special bar (widget
  combat_interface:special_attack, ui.invoke op 0), Attack once -> `noprayer`, then a normal fight
  (about 78 ticks with attack 60, strength 80). `slice_sigmund_protected` hands the armed special to
  player_combat_start (slice_sigmund.rs2) -- before this pass the special could never fire.
- Zanik tied at 2542,5513 (`slice_zanik_tied_up`, op1) completes the quest; the mace is worn,
  not in the pack, after the fight.

Not ported: train, Oldak sphere, diary hook, cutscenes (spec pending, 5 scenes).
