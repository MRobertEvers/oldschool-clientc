# Wave test ledger -- the waves loop

One line per test per test pass: the sampler's verdict, the ledger's rows, coverage, the pictures the
sampler opened, and what was sent back and why. A test sent back is not committed; its file stays
untracked in the worktree for the next pass to re-author.

## test pass matthew-mbp-m4-waves-b1-test1 (2026-10-04, tree 7304e6c39, content 90791aac55)

The first wave-test pass: the six early monster units. Nothing kept (no test green); one committed
as a blocked record; five sent back. Every test ends blocked on the npc-record seam (CONTENT_BUGS
TEST-2). Evidence is not published: run.py publishes a PASS only.

- **inferno_bat** -- COMMITTED, blocked (content_bug BAT-DRAIN, open; MON-OPEN-5: its chance is [M24]). 53 rows: 49 PASS, 3 FAIL (the three stat_drain rows: 0 drops over 27 unprayed hits), 1 BLOCKED; coverage 25 of 38 in-scope rows (13 wait on TEST-2 or the mager unit's revive). Enters wave 1 only; no cheat inside run(). Techniques pillar_safespot, bat_outranged, one_tick_flick, pray_by_danger proved from the tick log. Sampler re-derived hitpoints 25 (cache_npc.txt:83 stat4=25, wiki_Jal_MejRah:19), attack_speed 3 (cache_npc.txt:100 attackrate 3, wiki:15), max_hit 19 grade C (wiki:11, autozuk index.html:398); ledger rows 23, 26, 28 agree. Pictures: reviewer 0 (Read hook timeout); sampler opened 3 (006 prayer.missiles, 013 kill.main, 020 technique.pillar_safespot): the arena, the prayer tab, the player beside the south pillar; no bat in frame on any of them (shots fire at row time, after the action). For the next pass: hit_delay reports one value 2 for a 1-2 range, write the distribution.
- **inferno_blob_and_splits** -- SENT BACK: not reviewed; the author gave up after 25 runs (run 24 green but the losing-sight row, the last edit never re-run, run 25 died). 26 of 41 rows on run 24. Blocked on TEST-2 (15 rows).
- **inferno_melee** -- SENT BACK: a wave skip. `sweep()` (inferno_melee.lua:351) enters every wave 1-16, 25-32, 42-49, 57-64 by t.wave.enter to count melees per wave (the same sweep inferno_mager_resurrection was rejected for; TEST-4). Also row spec.melee.sounds PASSes with "measured ?" (unmeasured, must be a gap). Blocker real: MELEE-DIG-LAND (dig_landing offset 1_0 twice vs nw_of_player) and TEST-2 (19 rows). 52 rows, 50 PASS 1 FAIL 1 BLOCKED; 27 of 46. Pictures: reviewer 0, sampler 0 (not kept).
- **inferno_ranger** -- SENT BACK: a wave skip. `sweep()` / `count_wave` (inferno_ranger.lua:386-405) enters every wave 1-66 by t.wave.enter (TEST-4). Otherwise clean: 53 PASS 0 FAIL, 28 of 39, techniques pillar_safespot, one_tick_flick, pray_by_danger, do_not_stand_beside_it; blocked on TEST-2 (11 rows). Pictures: reviewer 0, sampler 0.
- **inferno_mager_resurrection** -- SENT BACK (reviewer rejected): `sweep()` (line 395) enters every wave 34-67, a wave skip; the spec-row shots are Lumbridge after a death, not the fight; hit_delay_by_distance writes a set (2,3,4,6), not delay per distance, and passes out of tolerance. Blockers cited: MAGER-RANGE, MAGER-MELEE-CHANCE (both OPEN), TEST-2 (15 rows). Pictures: reviewer 4 of 71.
- **inferno_nibblers_and_pillars** -- SENT BACK: entries at waves 2, 66, 67 and 68 (lines 702, 842, 923-924) are wave skips past the unit's first wave (TEST-4); the wave-66 end collapse is not reachable by a practice entry (one wave, then it leaves). Gate red also on two shots cut at 71 characters (TEST-3). New content row TEST-1: pillar_hit_gap_modal measured 2 (576 gaps: 1x134 2x347 3x95, none 4) against Blert's 4. 121 rows, 119 PASS 1 FAIL 1 BLOCKED; 29 of 36. Pictures: reviewer 0, sampler 0.

## test pass matthew-mbp-m4-waves-b1-test2 (2026-10-05, tree ad411bdc0, content 442ee08155)

The second wave-test pass on the same six units, after seam pass 7 (t.npc.record, t.seq.length,
long shot names, scope sidecars). Nothing kept: five rejected by their reviewers, the sixth
(content_bug) sent back by the sampler. No evidence published. Sampler re-derived three spec
numbers: mager.hit_delay_by_distance (autozuk index.html:447, list minus 1, grade D: agrees),
blob.projectile_anim_length (cache_seq.txt:627-631: 7616 is 48 cycles, the table's 30 is wrong:
TEST-6) and nibblers_and_pillars.pillar_hit_gap_modal (NIBBLER_PILLAR_ANALYSIS.txt:6 (4,1494):
agrees, grade B).

- **inferno_bat** -- SENT BACK (reviewer rejected). 60 PASS; coverage 35 of 36 (stat_drain_boosts_bat unmeasured, no gap row). The spec rows' shots (022, 038, 047, 020) are one post-fight scene with no bat; the entry and kill shots miss the bat and its death animation; bat_outranged rests on one hit. Pictures: reviewer 7, sampler 0. The tree's edits to the committed (pass-1 blocked) file are left uncommitted.
- **inferno_blob_and_splits** -- SENT BACK (reviewer rejected). 72 PASS 1 BLOCKED; 37 of 38. water_weakness never tried (no runes given); spec.blob.other_sounds PASSes on "measured ?"; the run ends in a death (071 shows Lumbridge). Table fault TEST-6 for the owner. Pictures: reviewer 3.
- **inferno_melee** -- SENT BACK (reviewer rejected). 69 PASS; FULL 41 of 41. spec.melee.dig_landing writes "measured nw_of_player" (the spec restated; the log says -3_-3 on both digs) and dig_trigger "measured unreachable": restated constants. End-of-run rows share one late scene: no dig, death or overhead icon seen; three interaction rows carry no shot. Pictures: reviewer 8.
- **inferno_ranger** -- SENT BACK (reviewer rejected). 58 PASS; FULL 35 of 35. sweep() (inferno_ranger.lua:405-421, 844) still enters waves 1-65 inside run(): a wave skip (TEST-4); entry and flick shots show no ranger. Pictures: reviewer 3.
- **inferno_mager_resurrection** -- SENT BACK by the sampler (reviewer: content_bug). 76 PASS 1 BLOCKED; 42 of 45. Its blocker row spec.mager.hit_delay_by_distance cannot be re-derived: the test keys distance from a client sample (d3 x45) while the tick log puts those wave-A swings at d9 (npc_spawn SW tile, size 4), where the delay 3 matches the spec; the modal list "3,3,6" is graded by position against a 16-slot spec and passes. The fault is real but narrower: TEST-5 (d1 +2, d15 +1). Shots 007 and 015 (sampler opened 2) are taken after "You leave the Inferno", no mager in frame. Pictures: reviewer 5, sampler 2.
- **inferno_nibblers_and_pillars** -- SENT BACK (reviewer rejected). 121 PASS 1 FAIL 1 BLOCKED; 32 of 33. The t.blocked cites stale NIB-CAD gaps; pillar_hit_gap_modal measured 2 (1x133 2x347 3x94) is to be written as measured and the table row (nibblers_and_pillars.tsv:10, spec 4) raised with its owner (TEST-1), not a content_bug. Pictures: reviewer 1 (post-wave).

## test pass matthew-mbp-m4-waves-b1-test3 (2026-10-05, tree a8ee397da, content 442ee08155)

The third wave-test pass on the same six units, after five spec rows were restated. Nothing
kept: all six rejected by their reviewers; the sampler opened 3 of the cited shots and agrees.
No evidence published. No accepted test, so no spec number re-derived this pass. New driver
row TEST-7 (live npc current levels, for bat.stat_drain_boosts_bat). The recurring fault in
five of six: shots taken after the fight (practice-leave or Lumbridge scene) instead of at
the event, with the npc alive and the prayer lit.

- **inferno_bat** -- SENT BACK (reviewer rejected). Gate 86/86; 35 of 36 (stat_drain_boosts_bat: TEST-7). The player dies in the run; 068 technique.one_tick_flick is Lumbridge after the death (sampler opened it); 053-088 spec shots post-fight; 023 death_seq has no bat; 038 pillar_safespot and 019 prayer shots show no bat. Pictures: reviewer 12, sampler 1.
- **inferno_melee** -- SENT BACK (reviewer rejected). Gate green; FULL 43 of 43. spec.melee.dig_trigger, dig_landing and dig_landing_blert still write the spec's own string ("measured unreachable", "ladder_nw3_under_w3_n3_nw1", "ladder_observed") naming no tick-log rows (sampler read the ledger); landing on two digs only; 004/005/014 have no melee in frame. Pictures: reviewer 6.
- **inferno_ranger** -- SENT BACK (reviewer rejected). Gate green; FULL 35 of 35; sweep deleted. 014, 028, 030, 042, 043 are one post-fight pillar scene with no ranger (sampler opened 042: practice-leave chat); wave 18 entered four times; hit delay at distances 1 and 5 only; one ranger kill. Pictures: reviewer 9, sampler 1.
- **inferno_nibblers_and_pillars** -- SENT BACK (reviewer rejected). Gate green 124/124; FULL 35 of 35; gaps by adjacent count measured. 068/069 safespot shots no bat; 075 ice barrage shot no nibblers; 095 and 118 stale end-of-run scene, pillar fall (seq 7561, npc 7710) not pictured; no_pillars() dead code. Pictures: reviewer 8.
- **inferno_blob_and_splits** -- SENT BACK (reviewer rejected; author content_bug). 38 of 39; gate red on blob.water_weakness. Block NOT copied to CONTENT_BUGS: 18 hits from 92 cast calls (the ledger's water.done shows the calls timing out), max 4; P(no 5 in 18 uniform 0..5) is about 4%. Next pass: cast on the 5-tick speed, >= 60 Water Strike hits. 064/078 post-fight, no blob (sampler opened 064). Pictures: reviewer 4, sampler 1.
- **inferno_mager_resurrection** -- SENT BACK (reviewer rejected; author content_bug). Gate 43/46. TEST-5 stands but the ladder needs more than 7 swings at d2 and more distances. MAGER-MELEE-CHANCE not reproduced: 14 of 21 against 50% (random(2)) is within noise; drop it from the block. 007/011 show no mager, no lit prayer; technique rows carry no shots; water weakness untried. Pictures: reviewer 7.

## test pass 4 (2026-10-05): stopped by the owner

- The six Inferno wave tests under test/waves/ were committed as they stood when the owner stopped test pass 4, for the landing on v3. None was kept by a reviewer; all six remain work in progress.
