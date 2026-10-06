# Waves loop spec ledger

One heading per spec pass; one line per unit: rows, grade counts, presentation rows, open rows, content bugs.
Tables are `docs/minigames/<game>/encounters/<unit>.tsv`; open rows are `docs/minigames/<game>/OPEN.md`;
content bugs are `docs/minigames/waves_loop/CONTENT_BUGS.md`.

## matthew-mbp-m4-waves-b1-spec-inferno-a (the Inferno, batch A: the six system units, 2026-10-03)

Closed by the Opus closer. Parent commit b7b61ab98, content 1ea3ebb602 (comments and tags only; no constant value changed).
Total: 268 rows, A 92 / B 101 / C 12 / D 36 / E 27.

- **entry_and_cape**: 27 rows, A 23 / B 2 / C 0 / D 1 / E 1; presentation 11; open M3; bugs ENTRY-1..4 (new), ENG-6, ENG-7, INF-AV-001, INF-AV-008 (cited).
- **wave_table**: 105 rows, A 1 / B 99 / C 0 / D 3 / E 2; presentation 1; open M1, M2; bugs WT-1..5 (new), ENG-20, ENG-37, ENG-38 (cited).
- **pause_and_logout**: 19 rows, A 3 / B 0 / C 7 / D 5 / E 4; presentation 2 (both E); open M4, M5, M6; bugs PL-1..5 (new), ENG-8, ENG-42, ENTRY-1 (cited).
- **death_and_failure_reward**: 22 rows, A 8 / B 0 / C 0 / D 9 / E 5; presentation 0; open M7-M10; bugs DEATH-1..4 (new).
- **completion_reward_and_pet**: 77 rows, A 52 / B 0 / C 4 / D 16 / E 5; presentation 6; open M11-M14; bugs REWARD-1..5 (new), INF-AV-008, INF-AV-009, DEATH-3, DEATH-4 (cited).
- **practice_mode**: 18 rows, A 5 / B 0 / C 1 / D 2 / E 10; presentation 2; open M15-M17; bugs PRACTICE-1..3 (new), ENG-6 (cited).

What the closer changed and what it sends back:

- Re-derived three or more rows per table from the source files. Two corrections: `reward.pet_roll_denominator_on_task` C to D
  (the guide, STATED.md:14, says "on task it's actually one out of 50"; only the wiki states 75, though the guide's
  combined 1 in 43 is consistent with 75); `practice.osrs_mode_exists` pointed at wiki_Inferno_Strategies.wikitext:68, which
  is `{{Recommended equipment`; re-pointed to wiki_Inferno.wikitext:398 (C kept: that line plus the 2021 newspost).
- `wave_table.wave_delay` stays B, but its count is overstated: 5 of the 20 runs (1b92d513, 6a865311, 7d6a3542,
  b1e47c9b, b338bcb1) have reconstructed starts, so 340 of their gaps are Blert's asserted 6. Observed: 901 of 909 gaps
  are 6 (the 8 others are 22 x7, the pause before wave 69, and 14 x1).
- `inferno.constant`: four tag comments were reverted to HEAD because the source they named contradicts the value
  (the wave delay, `^inferno_arena_lz`, the combat spawn tiles header, the Jad tiles header). The disagreements are
  rows ENG-38, ENTRY-4, WT-1 and WT-4.
- Sent back (measured_by): the completion rows were taken under `::god` and `::zukhp 0` although a real solo run can
  reach a Zuk kill; the pet and exchange rates are static reads of the constant. The death rows used `::die` (the driver
  aborts a fought death at hp 0) and exit pay was read with varps set by a cheat. Several of our-side readings are a
  single instance: the landing tile, the teleport, the paused prayer tick, the resume-vs-continuous tiles, our 8-tick
  wave delay. Waves above 2 were read at entry, not fought.
- The death table has no presentation rows (no death sequence, message or respawn row). The pause rows are both
  E text rows. The pet model ids (33005, 34586) are not an inventory kind; every sequence and sound named is in
  AV_INVENTORY.tsv.
- Held patches (eat delay, prayer regeneration and drain): PL-1's paused prayer tick is measured without the prayer
  patch and must be measured again once it lands.

## matthew-mbp-m4-waves-b1-spec-colosseum (the Fortis Colosseum, tables only: 22 units, 2026-10-03)

Closed by the Opus closer. Parent commit 75d0b6ea5; no content commit (the Colosseum has no content yet, nothing
was built or measured: every row's measured_by is "not built"). Open rows are `COLOSSEUM_PLAN.md`'s M table
(M1-M97; this pass added M77-M97). Total: 1650 rows, A 629 / B 397 / C 64 / D 324 / E 236.

- **arena_and_spawn_tiles**: 58 rows, A 19 / B 29 / C 2 / D 4 / E 4; presentation 4; open M3, M4, M50, M52, M53, M54, M55; bugs none.
- **wave_table**: 112 rows, A 6 / B 90 / C 0 / D 5 / E 11; presentation 9; open M2, M5, M37, M38, M43, M49, M51; bugs none.
- **wave_reinforcements**: 51 rows, A 6 / B 34 / C 0 / D 6 / E 5; presentation 5; open M1, M2, M4, M5; bugs none.
- **wave_presentation**: 74 rows, A 21 / B 25 / C 1 / D 12 / E 15; presentation 55; open M10, M16, M38, M39, M40, M41, M47, M51, M80, M81; bugs none.
- **fremennik_trio**: 85 rows, A 35 / B 19 / C 5 / D 9 / E 17; presentation 28; open M6, M7, M10, M11, M40, M47, M51, M56, M57, M58; bugs none.
- **javelin_colossus**: 75 rows, A 24 / B 23 / C 5 / D 14 / E 9; presentation 16; open M6, M8, M9, M10, M11, M47, M51, M59, M60, M61; bugs none.
- **jaguar_warrior**: 67 rows, A 22 / B 15 / C 4 / D 16 / E 10; presentation 15; open M2, M6, M11, M18, M40, M51, M62, M63, M64; bugs none.
- **serpent_shaman**: 79 rows, A 26 / B 19 / C 2 / D 19 / E 13; presentation 16; open M6, M10, M11, M15, M16, M40, M51, M65, M66; bugs none.
- **manticore**: 99 rows, A 32 / B 23 / C 8 / D 15 / E 21; presentation 25; open M6, M10, M11, M12, M13, M14, M51, M67, M68, M69, M70, M71; bugs none.
- **shockwave_colossus**: 70 rows, A 26 / B 16 / C 3 / D 9 / E 16; presentation 21; open M6, M10, M11, M17, M47, M51, M52, M72, M73; bugs none.
- **minotaur**: 100 rows, A 30 / B 28 / C 5 / D 25 / E 12; presentation 20; open M6, M11, M15, M47, M50, M51, M74, M75, M76; bugs none.
- **modifier_system**: 194 rows, A 121 / B 19 / C 3 / D 32 / E 19; presentation 8; open M29, M30, M31, M42, M48, M50; bugs none.
- **sol_heredit_attacks**: 105 rows, A 33 / B 32 / C 11 / D 12 / E 17; presentation 25; open M19, M20, M21, M25, M26, M38, M41, M46, M47, M77; bugs none.
- **sol_heredit_phases**: 44 rows, A 2 / B 17 / C 4 / D 7 / E 14; presentation 9; open M22, M23, M24, M27, M28, M38, M78, M79, M80; bugs none.
- **colosseum_entry_and_minimus**: 58 rows, A 20 / B 3 / C 2 / D 22 / E 11; presentation 9; open M35, M36, M37, M81, M82; bugs none.
- **reward_pool_and_cash_out**: 77 rows, A 34 / B 0 / C 3 / D 33 / E 7; presentation 4; open M32, M33, M83, M84, M85, M86, M87, M88; bugs none.
- **glory**: 59 rows, A 26 / B 0 / C 1 / D 25 / E 7; presentation 4; open M31; bugs none.
- **reward_items**: 77 rows, A 53 / B 0 / C 3 / D 17 / E 4; presentation 8; open M46, M89, M90; bugs none.
- **quiver_and_pet**: 41 rows, A 22 / B 0 / C 0 / D 16 / E 3; presentation 6; open M33, M91; bugs none.
- **death_and_fee**: 29 rows, A 7 / B 5 / C 2 / D 7 / E 8; presentation 2; open M34, M44, M47, M48; bugs none.
- **colosseum_music**: 16 rows, A 4 / B 0 / C 0 / D 9 / E 3; presentation 2; open M45; bugs none.
- **colosseum_combat_achievements**: 80 rows, A 60 / B 0 / C 0 / D 10 / E 10; presentation 0; open M15, M46, M75, M92, M93, M94, M95, M96, M97; bugs none.

What the closer checked and changed:
- spec_check prints ok on all 22 tables; every E row and every `closes` names an M id present in the plan; the
  reports' grade counts matched the tables before the closer's edits. Three rows per table were re-derived from the
  file the source_ref names (66 rows); the javelin's special was recounted from `observed_npc_events.tsv` (4 autos
  before the first toss in 220 of 220, 4 between tosses in 230 of 230: the toss is every fifth attack).
- Downgraded: `serpent_shaman.attack.hits_per_attack` C->D (Blert records the cast animation, not hits) and
  `manticore.burst.orb_count` B->D (events 2 and 3 of a burst are asserted by the plugin, PROVENANCE.md:75).
- Removed: `modifier_system.doom.stack_hitsplat` (its value 201 is Blert's COLOSSEUM_DOOM_APPLIED event type, not a
  game hitsplat id; the row itself said the id is in no source). Kept in the pass state as close.removed_rows.tsv.
- `glory.completion.wave12.server`: the '?' became the disclosed guess 1200 (E, M31) per ruling R1; the A row
  `glory.completion.wave12.client_display` (2200) records the cache's side and must not be built to (open issue).
- `colosseum_combat_achievements.speed_chaser.limit_ticks` cited the wiki page's name line; now its description line.
- Left as found, noted: `sol_heredit_attacks.triple.in_phases_gate` is C on two wiki pages plus Blert (the Blert
  observation supports B; not promoted). colosseum_combat_achievements has no presentation rows (no source names a
  CA toast or sound). Model ids in presentation rows are not an inventory kind; the other presentation ids are in
  AV_INVENTORY.tsv except `jaguar_warrior.presentation.off_rig_jaguar_seqs` (12491, 12492, 12498, 12499: the
  quadruped jaguar's rig, named only to say they are not this npc's; RIG_ANIMATIONS.md:49).
- Not run: no build, no quest test, no Blert or wiki fetch (tables-only pass; another pass builds here).

## matthew-mbp-m4-waves-b1-spec-inferno-b (the Inferno, batch B: the six monster units, fought, 2026-10-04)

Closed by the Opus closer. Parent commit 1940d0927, content c9c4e63a0f (comments and tags only; no constant value changed,
so no quest test was run; `make -C src torirsserver-scripts` compiled clean; the full quest suite was not run by this pass).
Total: 301 rows, A 82 / B 88 / C 58 / D 38 / E 35. Content bug rows: 42 appended to CONTENT_BUGS.md (new ids and re-measures of known ones).

- **nibblers_and_pillars**: 37 rows, A 4 / B 11 / C 10 / D 7 / E 5; presentation 7; open M18-M21; bugs NIB-CAD, NIB-TARGET, NIB-FIRST, NIB-ACC, NIB-RADIUS, NIB-PILLAR-1HP (source disagreement), ENG-55-b, ENG-48-b.
- **bat**: 42 rows, A 15 / B 8 / C 7 / D 5 / E 7; presentation 11; open M22-M27; bugs BAT-RUN, BAT-DRAIN, BAT-CAD, BAT-REVIVE, WT-1-bat, ENG-12-b, ENG-13-bat.
- **blob_and_splits**: 55 rows, A 17 / B 15 / C 11 / D 5 / E 7; presentation 13; open M28-M32; bugs BLOB-SCAN, BLOB-NOPRAY, BLOB-MELEE, BLOB-FIRST, BLOBLET-IDLE, BLOBLET-OFFSET, BLOB-RANGE.
- **melee**: 51 rows, A 17 / B 11 / C 8 / D 10 / E 5; presentation 17; open M33-M35; bugs MELEE-DIG, MELEE-DIG-DELAY, MELEE-DIG-LAND, MELEE-REVIVE-HP, WT-1-melee.
- **ranger**: 48 rows, A 14 / B 15 / C 10 / D 3 / E 6; presentation 12; open M36-M40; bugs RANGER-MELEE-MAX, RANGER-MELEE-CHANCE, RANGER-RANGE, RANGER-FIRST, WT-1-ranger, ENG-13-ranger.
- **mager_resurrection**: 68 rows, A 15 / B 28 / C 12 / D 8 / E 5; presentation 15; open M41-M45; bugs MAGER-REVIVE-TRIGGER, -GAP, -NIBBLER, -TILE, -SPAWN, MAGER-MELEE-MAX, MAGER-MELEE-CHANCE, MAGER-RANGE, ENG-13-46-mager.

What the closer changed and what it sends back:

- Re-derived three or more rows per table from the source files (pillar 255 at wiki_Rocky_support:30, the half-hp end
  collapse at wiki_Inferno:264, bat run drain at the Easter post:25, bat gap minimum 3 of 1872, blob 3-tick read at
  Strategies:592, ImKot dig 50 at Jal_ImKot:49, ranger hp at the Phosani post and the cache, mager 1/10 at Jal_Zek:48's
  Mod Ash quote, and others). One correction: `blob.water_weakness` A to D, tags `[wiki]` only: the Summer Sweep Up
  post line 83 names no monster and no percent; only the wiki infobox (Jal_Ak:38-39) and its change line (:62) give 40.
- `melee.hit_delay` and `melee.prayer_read_tick` (D, `[wiki]`) rest on ENCOUNTER_TIMING.md section 1, the ToB doc's
  generic pipeline quoting the wiki's Tick manipulation page, not on an Inferno source: kept, sent back for a direct cite.
- Sent back (measured_by): the bat run drain rows are by reading (no driver verb reads run energy); the melee dig rows
  are static (the dig was not reached by a fight); `mager.prayer_read_tick` rests on one flick pulse; no mager or ranger
  kill to completion, no revive chain, no pillar line-of-sight run (ENG-13 rows unmeasured); the midwave collapse
  damage was not isolated; the runs share one headless rng stream, so samples are correlated (melee unprayed 31/35/38
  repeat). No row was taken under a god mode: the wave-66 scratches used three `::setvar` resume lines as bring-alongs.
- Presentation: every seq, spotanim, projectile and sound id named by a presentation row is in AV_INVENTORY.tsv; model
  ids are not an inventory kind. A worker's notebook had been written into `sources/blert_api/`; it was moved to the
  pass state directory and not committed.
- Not run: no Blert or wiki fetch by the closer (the workers used cached Blert data; no fetch recorded).

## matthew-mbp-m4-waves-b1-spec-inferno-c (the Inferno, batch C: mixed waves, the Jads, Zuk, presentation; 2026-10-04)

Closed by the Opus closer after the relaunch. Parent commit 982647a71, content 3fb2bf8662 (comments and tags only; no
constant value changed, so no quest test was run; `make -C src torirsserver-scripts` compiled clean; the full quest suite
was not run by this pass). Total: 489 rows, A 108 / B 175 / C 47 / D 75 / E 84. Content bug rows: 30 appended to
CONTENT_BUGS.md (11 by the workers, 19 copied from the reports by the closer; the parent commit message says 41, which is wrong).

- **mixed_late_waves**: 36 rows, A 0 / B 32 / C 0 / D 2 / E 2; presentation 3; open none new (M29, M30, M38, M43); bugs MIXED-LOCKSTEP, MIXED-NIB-TILE, MIXED-RE-SEEN.
- **single_jad**: 67 rows, A 14 / B 28 / C 5 / D 11 / E 9; presentation 23; open M46-M51; bugs JAD-PRAYER-READ, JAD-OPEN, JAD-HEALER-STRAND, JAD-HEALER-PLACE, JAD-DEATH-CLIP, JAD-SOUND-TELL, JAD-RE-SEEN.
- **triple_jad**: 79 rows, A 14 / B 36 / C 5 / D 13 / E 11; presentation 25; open M52-M53; bugs JAD-TRIPLE-STAGGER, JAD-TRIPLE-RE-SEEN.
- **zuk_glyph_shield**: 37 rows, A 12 / B 12 / C 2 / D 8 / E 3; presentation 10; open M54-M59; bugs WZ-DWELL, WZ-ALIGN, WZ-DIR, WZ-ROW, WZ-START, WZ-WINDOW, GLYPH-DEATH-CLIP.
- **zuk_fight**: 62 rows, A 13 / B 24 / C 12 / D 9 / E 4; presentation 13; open M60-M62, M66; bugs ZUK-HEAL-GAP, -HEAL-FIRST, -ENRAGE-GAP, -SET-TILE, -SET-OPEN, -JAD-OPEN, -MEJJAK-GAP, -DEATH-CLIP, -MIN-HIT.
- **zuk_sets_and_healers**: 86 rows, A 21 / B 30 / C 15 / D 11 / E 9; presentation 19; open M66 (closes nothing new); bugs ZUK-SETS-RE-SEEN.
- **presentation_av**: 122 rows, A 34 / B 13 / C 8 / D 21 / E 46; presentation 118 (+9 reward); open M63-M65, M66; bugs AV-RE-SEEN.

What the closer changed and what it sends back:

- Re-derived three or more rows per table: mixed first wave 35 (wiki_Inferno:227), mager/ranger same first tick 32 of 225
  (MIXED_LATE_ANALYSIS:21), Strategies:526; Jad hp 350 (cache_npc:317), gap 8 (JAD_ANALYSIS:3-5), healers at 175
  (Yt_HurKot:70); triple gap 9 741 of 742 + JalTok_Jad:51, first swings 4,7,10 14 of 14, max 113 (JalTok_Jad:12, C060);
  glyph dwell 5 (ZUK_ANALYSIS:7), 5x3 (Ancestral_Glyph:36), direction saved (QoL newspost:57); Zuk enraged gap 7
  275 of 275 (ZUK_FIGHT_ANALYSIS:2), sets 350/175/600-480 (wiki_Inferno:287); jalnib ready 7573 / walk 7572.
- Downgraded: hurkot_sounds and mejjak_sounds (sets and presentation_av) C to E and zuk_fight.sound_hit /
  presentation_av.zuk_sound_hit D to E, new M66: INFERNO_SOUNDS.md gives them layers c, f and t (a name join, the Fight
  Caves lineage, a triple member), not a wiki or plugin statement. zuk_sets_and_healers.mejjak_heal_amount C to D (Blert
  is a recorder; a B candidate). Added the trainer (CODE_CONSTANTS C080, 5..10) to both mejjak_lava_damage rows, whose
  guide line states only the max. presentation_av.nibbler_ready_walk_seq reordered to 7573,7572 (ready, walk).
- Kept C: the Jad hit_delay rows ([plugin][blert]) rest on two plugin lineages, trainer and kotori (CODE_CONSTANTS C058).
- inferno.constant: eight tags removed (back to HEAD) where the cited source contradicts the value: jad67_lx, glyph_run_lz,
  glyph_row_lz, player_zuk_lz, glyph_pause, jad_open_delay, jad_healer_dx/dz (batch B's rule: a mismatched constant stays
  untagged with its bug named).
- Sent back (measured_by): god mode on rows a fight can reach: single_jad and triple_jad after-survival rows (healers,
  death), zuk_fight thresholds (enrage gap, MejJak heal, Jad healers, death) by ::zukhp under ::god, zuk_sets Jad-healer
  rows. mixed: the bot died on most waves, so style-change and duration rows are not measured on ours. presentation_av:
  no fresh fight, sounds read from scripts not measured. Headless runs share one rng stream (correlated samples).
  single_jad.sound_defend 410 rests on a wiki line naming TzTok-Jad, applied to JalTok-Jad.
- Not run: no Blert or wiki fetch by the closer or the workers (cached data only).
