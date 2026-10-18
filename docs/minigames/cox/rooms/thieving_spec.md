# Thieving (creature keeper / corrupted scavenger): tick spec and content audit

Unit `crabs_thieving`, 2026-10-18. Content: the thieving procs of
`cox_puzzles.rs2` (from `~cox_thieving_point_cap` to the creature keeper's
body), the `^cox_thieving_*` / `^cox_var_thieving_*` constants in
`cox.constant`. Method: `docs/CONTENT_SOLVER_MANUAL.md` phases 1-3.

## Sources (ranked)

| rank | id | source | file |
|---|---|---|---|
| Jagex | J1 | 29 Nov 2023: success table (L1 39.2%, L1+lockpick 60.7%, L99 60.7%, L99+lockpick 82.3%); a grub guaranteed on a grub chest; 115 points a grub; grubs stack to 28; room-completion secondaries (stinkhorn 2n+3, endarkened juice 2n+3, cicely n+2) | `sources/newsposts/wiki_Update_Chambers_of_Xeric_Changes.wikitext:42-47,218-243` |
| Jagex | J2 | 31 Jan 2019: 5x points for grubs; no Thieving requirement; "The creature will now eat faster"; hunger delay 30 -> **60 s** | `sources/newsposts/wiki_Update_Chambers_of_Xeric_Kebos_and_Collection_Log_Changes.wikitext:41-45` |
| Jagex | J3 | Mod Ash: grub quantity "at least 1, or at least 2 if your level is 95+. It may be increased up to (Thieving / 25) randomly"; "random integer 0-max inclusive, then raising the result up to the minimum"; attempts "every tick"; lockpick "+21 percentage points"; bats "one per person ... minimum of 5 and a maximum of 25"; requirement "scales linearly off the party's total Thieving stat, up to a cap" | `sources/wiki_Chest_Chambers_of_Xeric.wikitext`, `wiki_Chambers_of_Xeric.wikitext:551-561` |
| wiki | W1 | Chest (Chambers of Xeric): versions 29742 closed / 29743 open (poison) / 29744 open after completion ("The cocoons have not hatched") / 29745 open grubs; skilling chart low 99 high 155, lockpick 153 / 209; 3 poison chests 1-3 damage "to anyone next to the chest", no real poison; poison chests relock | `sources/wiki_Chest_Chambers_of_Xeric.wikitext` |
| wiki | W2 | Trough 29746 empty / 29874 full; "the scavenger will begin to feed on them until they are full. When full, the scavenger will fall asleep on the hay"; minimum 16 x players, 30 solo | `sources/wiki_Trough_Chambers_of_Xeric.wikitext` |
| wiki | W3 | Strategies "Thieving": hunger tracked by the bar above its head; usually one grub; chests relock "shortly after being opened"; 4-chest circles; deposit at ~10-15 grubs "to not wait for the scavenger to finish eating" | `sources/wiki_Chambers_of_Xeric_Strategies.wikitext:828-842` |
| wiki | W4 | Corrupted scavenger 7602/7603; Cavern grubs 20885, stack cap 28; main page points (115 a grub; MaxPoints = max(4500, floor(totalThieving/6) x 150)) | `sources/wiki_Corrupted_scavenger.wikitext`, `wiki_Cavern_grubs.wikitext`, `wiki_Chambers_of_Xeric.wikitext:551-561` |
| plugin | P1 | de0 CoX Thieving: CCHEST 29742, PCHEST 29743 (poison AND bats), GCHEST 29745; chest tiles per variant; the hazard sets as 4-chest solutions per (winding, rotation) | `sources/de0/CoxThievingPlugin.java`, `ChestData.java` |
| video | V1 | Synq 0:18:13-0:20:40: 30 grubs solo; deposit ~8 at a time so it eats while you thieve; HP regen if left too long; 1-3 grubs, min 2 at 95, max 4 at 100 (Thieving cape boost); 4 special chests per raid; bats heal 23 and give points; "some points for just finding the bats"; poison deals 1 and stays static | `synq_transcript.md:207-218` |
| video | V2 | The Academy (blue bar fills with green; a poison chest damages every time it is opened), Steins;Gate (5 bats), FIFO (3 poison chests static) | `sources/transcripts/yt_7NXG1Y2NsLs.md`, `yt_-oPCM_OxZpI.md`, `yt_k4YqkvdOuGY.md` |
| ref server | N1 | Near-Reality CreatureKeeperRoom / CorruptedScavenger / chest + trough plugins (eat cadence, animations, graphics, 250 bat points, messages, scavenger and hay tiles) | `sources/nr/*.java` (NR f577de7d) |
| cache | C1 | 29742 op1 Open, forceapproach; trough 2 wide, op1 Deposit; seqs `raids_thievingchests_eat/_tired/_sleeping`, 536 `human_openchest`, 832 `human_pickuptable`; spot 184 `shilomist`, 277 `troll_zeds` | `configs/all.{loc,seq,spotanim}` |

## The tick spec (solo)

| # | mechanic | decide tick / cadence | geometry | numbers (solo) | tell the client sees | source |
|---|---|---|---|---|---|---|
| 1 | room load | build | 64 (CCW) / 66 (THRU) / 74 (CW) chests as template locs; scavenger 2x2 at CCW (7,14), THRU/CW (22,21); hay CCW (7,18), THRU (25,22), CW (23,18); trough CCW (7,13), THRU (21,21), CW (22,23) | - | 29742 chests, 29746 trough, npc 7602 | P1, N1, W2 map pins |
| 2 | hazard set | build | ONE 4-chest solution of `CHEST_SOLNS[winding][rotation]`; one of the four is the bats, three are poison; static all raid | - | none until opened (that is the puzzle the plugin solves) | P1, W1, V1 |
| 3 | open attempt | every tick while the op holds | adjacent (forceapproach) | stat_random(thieving, 99, 155); lockpick or hair clip in the pack: (153, 209); boosted level | player anim per attempt | W1 chart, J1, J3 |
| 4 | grub chest | the success tick | - | max(min, randominc(level/25)), min 1 (2 at 95+); capped so the stack stays <= 28 | chest 29745 for ~5 s (8 ticks) then 29742; "Some cavern grubs have hatched; you take them." | J1, J3, N1 |
| 5 | poison chest | the success tick | the chest's neighbours (radius 1) | 1-3 typeless damage, no poison | gas spotanim 184 on the chest; chest 29743 ~6 s (10 ticks) then relocks; reopening poisons again | W1, N1, V2 |
| 6 | bat chest | the success tick | - | max(5, min(25, party)) psykk bats (heal 23); 250 points; never relocks | chest 29743 for the raid; "The chest contained some nicely cooked bats." | J3, V1, N1 |
| 7 | after completion | the success tick | - | no grubs | chest 29744 ("The cocoons have not hatched"); "No cocoons in this chest have hatched." | W1, N1 |
| 8 | stack full | the success tick | - | - | "You already have the maximum amount of cavern grubs." chest not opened | N1, W4 |
| 9 | deposit | op Deposit | adjacent to the trough | up to 28 from the pack into the trough; 115 points a grub up to max(4500, floor(T/6) x 150) for the room | trough 29874; anim 832; "You deposit some cavern grubs in the trough." | J1, W2, W4, N1 |
| 10 | eating | every 4 ticks | - | eats max(1, min(5, trough/6)) from the trough | anim `raids_thievingchests_eat`; hunger bar fills | N1 (rate), J2 ("eat faster"), W3 |
| 11 | trough empties | the eat cycle that empties it | - | - | trough 29746 | N1 |
| 12 | hunger | per eat cycle, once 100 ticks have passed since it last ate | - | +1 grub needed per 4 ticks | the bar drops | J2 (60 s), N1 (rate) |
| 13 | full | eat cycle on T reaching the requirement | - | 30 grubs solo (16 x players otherwise; the Thieving-total scaling has no published formula) | - | W2, W4, V1 |
| 14 | the way clears | T+1 | walks to its hay | - | npc walks; room reward lands in each raider's pack (5 stinkhorn, 5 juice, 3 cicely solo) | N1, J1 |
| 15 | yawn | T+5 | - | - | overhead "Yawwwwn!", anim `raids_thievingchests_tired` | N1 |
| 16 | asleep | T+10 | on the hay | - | npc 7603; Zzz spotanim 277 every 5 ticks | N1 |

## Rulings

- **Success roll (row 3).** The wiki prose (39% -> 61%, +21) and Jagex's table
  are the headline; the wiki's skilling chart (99/155, 153/209) is the
  mechanism and reproduces Jagex's four numbers within 0.3 points. The chart
  is rolled with `stat_random`, which is how every OSRS skilling chance is.
- **Hunger delay (row 12).** NR waits 20 eat cycles (48 s); Jagex says 60 s.
  Jagex wins: 100 ticks. The rate afterwards (one grub per 4 ticks) has only
  NR as a source.
- **Bat-chest points (row 6).** Synq says finding the bats pays; only NR gives
  a number (250). Ported, flagged as NR's figure.
- **Hazard set row (row 2).** de0's tables are per room rotation; the hazard
  quadruple comes from the row for the room's turn. THRU has rows for turns 1
  and 3 only; a THRU room at turn 0 or 2 draws from both rows (no data).
  Assumption to confirm on a live seed: this tree's room turn is the instance
  chunk rotation de0 reads (`CoxUtil.room_rot`).

## Audit (mechanic / ours before / OSRS / verdict)

| # | mechanic | ours (before 2026-10-18) | OSRS | verdict / fix |
|---|---|---|---|---|
| A1 | hazard set storage | four of the BUILDING player's varps (7110-7113) | the room's | **fixed**: instance register `^cox_var_thieving_hazards` (1 + quad x 4 + bat slot) |
| A2 | success chance | linear whole percent 39..61, +21 | chart 99/155, 153/209 (J1 table) | **fixed**: stat_random on the chart |
| A3 | poison splash centre | the opener's tile | "anyone next to the chest" | **fixed** (chest tile, radius 1) + gas spotanim 184 |
| A4 | bats | no points | points for finding them | **fixed**: 250 (NR) |
| A5 | eating | a deposit counted as eaten at once; the room cleared the tick the 30th grub went in | the scavenger eats over time | **fixed**: trough -> eat 1-5 per 4 ticks |
| A6 | hunger | after the delay, -1 grub every TICK; delay restarted by a deposit | -1 per eat cycle; delay restarted by eating | **fixed** |
| A7 | chest after completion | "The chest is empty." + open loc | 29744 eggs whole, "No cocoons ... hatched." | **fixed** |
| A8 | full stack | chest opened (loc changed) | message only | **fixed** |
| A9 | completion | scavenger changed to sleeping at once, walked to the hay the same tick | walk T+1, yawn T+5, sleep T+10, Zzz every 5 | **fixed** |
| A10 | completion reward | none | 5/5/3 secondaries to each raider (solo) | **fixed** |
| A11 | hazard row | union of every rotation's rows | the row for the room's rotation | **fixed** (see ruling) |
| A12 | trough visual | full for 1 tick (`loc_change(full, 0)`) | full until eaten | **fixed** |
| A13 | deposit animation | none | 832 | **fixed** |
| A14 | the scavenger blocks the way | an npc does not block a raider's path here; nothing stops a raider walking past it | it blocks the corridor until fed (NR adds FLOOR flags under it) | **open, needs F2** (the root's exit/traversal): a blocker under its 2x2, cleared at row 14 |
| A15 | hunger bar | no overhead bar | blue progressive bar above its head | **engine work**: a script-driven headbar with no hitsplat (`npc_hudbar`-like); until then the bot counts its own deposits |
| A16 | requirement vs Thieving total | 16 x players / 30 solo | "scales linearly off the party's total Thieving stat, up to a cap"; 30 solo | kept (no formula published; solo is exact) |
| A17 | grub XP 40/60/73 | NR | unpublished | kept, open |
| A18 | storage unit hotspot | not built | a storage unit in this room (Jagex 2017) | open (supplies unit) |
| A19 | sounds | lever/chest_open only | NR synths 2402 attempt, 52 success, 2138 gas, 2655 bats, 3637 deposit, 2393 eat, 1734 yawn, 869 sleep | open |

## Probe 1 results (2026-10-18, scriptrun, `::coxtest 6 0`, 99 Thieving, no lockpick)

`thieving_probe.lua` (scratchpad): deposit 28 -> the trough shows full and the
scavenger plays its eat animation (client seq 7496) every 4 ticks; ten chests
gave 2,2,2,2,2,2,2,3,2,3 grubs (min 2 at 95+, max 3 at 99: Mod Ash's 2/2/2/3);
the second deposit filled it; it walked to the hay the next tick, said
"Yawwwwn!" with the tired animation (7497), became 7603 five ticks later and
showed the Zzz spotanim 277 every 5 ticks; the reward landed in the pack
(stinkhorn 5, juice 5, cicely 3). No `script error`, no abort, no "no primary
player bound".

## Probes

- chests open at about the chart's rate (count attempts per success over a run);
- the hazard set matches de0's solution for the room's turn (a live seed with
  the CoX Thieving plugin's solve on the first poison chest);
- the scavenger eats 5 per 4 ticks from a 28-grub trough, sleeps T+10 after full;
- the reward lands in the pack; a chest opened after completion shows 29744.

## Round 2 (2026-10-18): content found by the solver

| # | mechanic | was | now | source |
|---|---|---|---|---|
| A14 | the scavenger blocks the corridor | nothing blocked a raider | four invisible blocking centrepieces (`zogre_multi_blocking_barricade_l`: no model, no op, blockwalk, blockrange 0) under its 2x2 from the spawn; removed the tick it gets up to walk to its hay (`~cox_thieving_block`) | wiki Corrupted scavenger ("It is blocking the path"); NR CreatureKeeperRoom (FLOOR flags under the 2x2, cleared when fed) |
| A20 | a full pack holding grubs | "You don't have enough space" | a chest opens when the pack has a free slot OR already holds grubs (they stack to 28) | Jagex 29 Nov 2023 (stackable to 28); Synq 0:18:15 |
| A21 | the bat chest into a full pack | inv_add of 5 into 1 slot | as many as fit, the rest on the floor | NR `addOrDrop` |

Proof: `test/raids/cox_thieving.lua` asserts the exit Passage cannot be taken
before the scavenger is fed (`th.blocked_before`, server truth) and can after.
Found on the way, foundation/engine (reported): a script `loc_add` made before
the client loaded the zone was missing from the CLIENT's loc list and
collision on some layouts (seed 8, level 0) though present on the server.
