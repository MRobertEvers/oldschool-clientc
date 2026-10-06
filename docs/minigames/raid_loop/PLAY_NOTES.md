# The play library: skills and room plans (raid seam27)

The owner, 2026-10-05: "I noticed that the driver is not very fast or good. That is not going
to work in normal mode. You will need to code up the agents a lot smarter using the actual
strategies." This file is the strategy half of `t.raid.play`. Since raid seam29 the code is in
its own driver parts: the loop and the shared skills in `script/plugins/quest_driver/raid_play.lua`,
and one file per room plan, `raid_play_tob_<room>.lua` (maiden, bloat, nylocas, sotetseg,
xarpus, verzik). They follow `raid.lua` in `DRIVE_SCRIPT_PARTS` (src/plugin/torirs_plugin_drive.c),
because the driver is one chunk and `dofile`/`loadfile`/`load` are removed from the sandbox
(torirs_plugin_lua.c:3980). A room file registers its plan once with
`QD.raid._play_plan("<id>", {...})`; a second registration of one id asserts at load. A plan
with no `decide` answers `unsupported`, "has no decide function yet: <its `unsupported` line>".
Today only `tob_bloat` has a decide function; the other five name raid seam30 (SEAM_TRIAGE_2026-10-05i.md).
A new part needs the C list edit and a rebuild of `src/torirs_questtest` and of the profile's
`src/torirs` (`make -C src -j EMBED_SERVER=1 torirs`); a Lua edit to an existing part is read live.

`W` below is `docs/minigames/theater_of_blood/sources/wiki_Theatre_of_Blood_Strategies.wikitext`.
`ET` is `docs/minigames/theater_of_blood/ENCOUNTER_TIMING.md`.

## The loop

`t.raid.play(plan_id, {mode=, weapon=, max_ticks=})` returns `result, detail, record`. It runs
`QD.raid._play_tick` once per server tick, and each tick has three steps:

1. **SEE**: what a person at the screen sees. That is the boss's animation, tile and health
   bar; the floor markers (`t.world.spotanims`); the player's own hitpoints, prayer points,
   lit prayers and tile; and the player's own swings. On the leader the swings come from the
   tick log's `player_anim` rows for its own pid. A member has no tick log, so it counts them
   from its presses and the weapon speed. The loop never reads server registers, `::tob*`
   readouts, the seed, or any hidden tick-log column.
2. **DECIDE**: the plan's decide function returns the whole intent for the tick. That is the
   prayers wanted for the next tick, a food and a potion, one step, and whether to attack.
3. **SEND**: prayers, potion, food and the step go in ONE `t.together`. The attack press comes
   after the block, because a slow verb cannot sit inside one. A walk is re-issued only when
   its target changes or the player has stopped short of it. The loop never waits for an
   arrival. The loop's only wait is for the next tick.

The record holds `inputs[tick]` (presses sent per tick), `hp_at`, `prayer_at` (the prayers read
lit, per tick), `tile_at`, `swings`, `eats`, `drinks`, `downs` (Bloat: `tick`, `pre_stomp`,
`flinch`), `flinches` (`from`, `moved` after 3 ticks), `boss_slot` and `death_tick`. A room test
reads its technique rows from the record and the tick log.

## Skills (each a function in raid_play.lua)

| Skill | Function | Rule | Source |
|---|---|---|---|
| Attack on cooldown | `_play_attack`, `_play_next_swing` | A click only STARTS a fight. After that the weapon swings every `speed` ticks by itself, so the library presses Attack only when it is not engaged (after a step), or when no swing has been seen for speed + 2 ticks. | wiki Attack speed; scythe speed 5; swing seq 8056 measured in build/quest_gate/tob_bloat/ticklog.tsv |
| Pray by the telegraph | `_play_pray` | The plan names the set wanted on the NEXT tick, because a press made between ticks T-1 and T is in force for T. Bloat: Protect from Missiles from age 32 of the down onwards (the first fly is at T+33), and Piety for the attackable window. Pinned exceptions from DRIVER_NOTES seam20 are carried by those rooms' plans: Verzik P3 on hit, Sotetseg's ball at impact, urn bombs at landing. | ET 1.1; DRIVER_NOTES "Several inputs in one tick", "Which ToB attacks read the prayer on the send tick" |
| Supplies | `_play_supplies` | Eat when hitpoints <= the largest total the plan's `threat(h)` says can land before the next chance to eat. On a FREE tick (not attacking, or the weapon is ready, so eating adds no delay) h is the gap to the next free tick + `QD.TOGETHER_CONFIRM_TICKS` + 1. Between swings h = 2, so a bite there costs the attack 3 ticks and only an imminent hit forces it. If the best food alone is short, a brew rides in the same tick. Restores are drunk when the missing prayer is at least one dose (32), or prayer is at 2 or less. | wiki Food ("If your weapon is ready ... eating does not add any new delay"; 3-tick eat delay), Potions ("do not incur the standard 3 tick attack or eat delay"), Food/Fast foods (combo), Super restore (8 + 25%); all quoted at consume_shared.rs2:28-49 |
| Hazards | `_play_hazard` | A marked tile is avoided from the tick it is seen. The library moves to the safe tile nearest the wanted tile, and the shortest step from the player breaks ties. It never stands on a tank or off the floor. | ET 1.3 / 3.4 (judged on the previous tick's tile); "simply don't stand on the shadows" (transcripts/yt_4i4lv-srJkw.md:71) |
| Safe step (seam29) | `_play_safe_step` | Every walk a plan sends goes through it, after `_play_hazard`. A hand is judged on the tile the player ends the tick BEFORE the impact on, so every tick-end of a walk matters, not only its last tile. If `want` and the first two tiles of both route shapes toward it (diagonal first, and straight along the longer axis first: the server was measured doing both) are unmarked, the walk goes to `want`. Otherwise it goes to the unmarked floor tile within two that gets nearest `want`, provided the first step of either shape is unmarked too. If no tile gets nearer and the player's own tile is unmarked, it stays. | ET 1.1 (T-1), ET 3.4; measured routes in svaplaysmoke t164-166 and s29n3 t187-189 |
| The attack's own path (seam29) | `_play_reach` | An attack press out of reach makes the SERVER path the player, through no skill. While no marker is on the floor the press goes out as before. While one is, the player walks (through the two skills above) to the unmarked floor tile sharing an edge with the npc's footprint (`size` from the npc row; corners are left out, the seam's conservative choice) that is nearest, and presses from there. If every reach tile is marked, the press is held for that tick. A marker that appears under a standing raider is stepped off the same way: the plan sends the raider's own tile through `_play_hazard`. | playn3 t437-440 (seam27); ET 3.3 "5x5" |
| Loadouts | `intent.gear` (worn items) in the tick's one `t.together` block, after food and before the step; `intent.spec` arms the special from the orb before the attack press (raid seam32) | A gear swap is one tick, and rides the same tick as a walk ("the scythe back the same tick"). Bloat Normal's run-by uses both. | DRIVER_NOTES "Several inputs in one tick"; DRIVER_NOTES "Bloat: Defence reads 80 of 80 after a Dragon warhammer special" |
| Roles | `opts.role` / `t.party.role()` | Every raider runs the same library, and the plan picks the role's line. See "Bloat, Normal trio" for what is coded today. | the room plans below |

**The supplies margin is the seam's own choice.** The `+ TOGETHER_CONFIRM_TICKS + 1` was
measured, not sourced. Under `svbbloat` a bite pressed on the swing two ticks before the stomp
had not been read back one tick later, so `tech.eat_before_stomp` (copied unchanged from
tob_bloat.lua) read 34 before that stomp. With the margin it read 54. No source gives a margin.

## Bloat, Entry solo (`tob_bloat`, mode `entry`), proved

| When | Intent | Source |
|---|---|---|
| Entry | Cross when Bloat is on the far side of the tank (the north row heading west). | W:687 "enter the room when Bloat is on the opposite side of the pillar" |
| Walk | Stand on the tile straight behind the tank from where Bloat will be in two ticks (its walk is visible). Re-issue the walk as that tile moves. Keep Protect from Missiles lit. | W:687 "Hug the pillar and hide from Bloat"; W:673 "reduced by 25% if Protect from Missiles"; tob_bloat.lua :1206 for the mirror tile |
| Walk, shadow | Take the shortest step off any shadow tile, to the safe tile nearest the hide tile. | ET 3.4; yt_4i4lv-srJkw.md:71 |
| Down (T) | Attack at once and keep swinging on cooldown with Piety lit. | W:689 "As soon as Bloat deactivates ... begin attacking with melee ... five attacks when close" |
| Stomp (T+29) | Entry: tick-eat it in place. The supplies rule counts the stomp (40) in the threat. | W:675 "It is possible to tick eat this attack"; tob.constant:760 entry stomp 40 |
| Rise (T+30) | Click back to the hide tile (6 tiles in every run), with Protect from Missiles up by T+32. | ET 3.1 flinch guide "when he starts to get back up ... that's when you click back"; W:673 |
| Not done | No Dragon warhammer special: the stomp restores Defence (W:675), so a drain lasts one down. The kept solo test drained only to measure it. | W:675 |

**Where the sources disagree.** For the stomp, W:689 says to run away after the last attack,
while the flinch guide (ET 3.1) stays through it and clicks back on the rise. Entry follows the
flinch guide: its stomp maximum is 40 against 99 hitpoints, and staying keeps the fifth swing.
Normal follows W:689, where the stomp is 40-80.

**Measured, five names, `test/raids/_play_smoke.lua`** (`play_measure.py` over each tick log;
build/seam_state/matthew-mbp-m4-raid-b1-seam27/play_measure.txt). Every row is 13/13 PASS,
the technique rows included.

| Run | Room ticks | Downs | Damage taken | Eat+drink | Swings per down | First swing after down | Lost to late presses |
|---|---|---|---|---|---|---|---|
| kept tob_bloat (own name) | 329 | 5 | 724 | 41 | 5/5, 0, 4/4, 2/3, kill | 5, 10, 18, 6 | 1 (+ a down with no swing) |
| kept, svabloat (survey) | never killed (1239+) | 6 | 847 | 48 | | | |
| kept, svbbloat (survey) | never killed (1204+) | 7 | 848 | 48 | | | |
| kept, svcbloat (survey) | 208 | 3 | 377 | 19 | | | |
| kept, svdbloat (survey) | 484 | 7 | 845 | 45 | | | |
| library, svabloat | 206 | 3 | 73 | 4 | 5/5, 5/5, kill | 6, 5, 6 | 0 |
| library, svbbloat | 263 | 4 | 132 | 8 | 5/5 x3, kill | 7, 7, 6, 6 | 0 |
| library, svcbloat | 196 | 3 | 160 | 8 | 5/5 x2, kill | 6, 6, 5 | 0 |
| library, svdbloat | 205 | 3 | 66 | 3 | 5/5 x2, kill | 7, 7, 6 | 0 |
| library, playbloat | 267 | 4 | 117 | 8 | 5/5 x3, kill | 7, 7, 6, 6 | 0 |

Inputs per tick (the library's own count): most ticks carry one input, which is the hide walk
re-issued as Bloat moves. Between 4 and 8 ticks a run carry 2 or 3 inputs (a prayer switch with
a step or a bite), and at most 1 carries 4 or more. Bloat does not need more: there is no gear
swap. The first swing comes 5 to 7 ticks after the down because the player runs around the
tank from the hide tile. That is the cost of W:687's hiding, and it is why the count is W:689's
"five attacks", not six.

## Bloat, Normal trio (`tob_bloat`, mode `normal`), proved (raid seam32 play_tob_bloat_normal)

The harness is `test/raids/_play_bloat.lua` (`party = 3`, so `seed_survey.py _play_bloat` and
`party_repeat.py _play_bloat --runs 3` need no `--party`). It is `_play_smoke.lua`'s old
`--party 3` branch moved to its own id; `_play_smoke.lua` is the Entry solo harness again and
refuses a party run.

| Who / when | Intent | Source |
|---|---|---|
| p1, entry | Cross when Bloat is on the far side; hide behind the tank on the walk. | W:687 "have one raider enter the room when Bloat is on the opposite side of the pillar. Hug the pillar and hide" |
| p1, first walk | THE RUN-BY: the Dragon warhammer on in one block, the special armed from the orb with the attack press the next tick, the swing seen as the 500 energy it spends, the hammer kept on until the special's splat shows, then the scythe back on in the same block as the walk back to the hide tile. One special only; while it is swung the raider eats only for the next three ticks of flies. | W:687 "one or two players should do a run-by on the boss with a Bandos godsword special to lower its Defence" (the BGS is not in this cache: tob_bloat_normal.lua:5); yt_4i4lv-srJkw.md:45 "a dragon warhammer is basically essential"; DRIVER_NOTES "Bloat: Defence reads 80 of 80 after a Dragon warhammer special" (orb, energy, keep the hammer on) |
| p2, p3 | Wait at the barrier; cross on the first down. | W:689 "As soon as Bloat deactivates, the rest of the team should enter and begin attacking with melee" |
| all, walk | Hide straight behind the tank from where Bloat will be; Protect from Missiles lit; off every shadow. | W:687; W:673; yt_4i4lv-srJkw.md:69-71 "Everyone should have protection from ranged on", "simply don't stand on the shadows" |
| all, down | Attack on cooldown with Piety until the leave age, then run beyond the stomp's reach. | W:689 "five attacks when close ... run away after the last attack" |
| all, supplies | The threat counts a fly at 15 (Protect from Missiles is lit for every fly the plan meets), and no hand on a shadow the tick's own step leaves. | W:673 "up to 20 damage every tick, reduced by 25%"; ET 3.4 (judged on the tile of the tick before) |
| all, potions | A super restore when Attack reads below base (a brew drained it); the super combat re-sipped on a walk when Attack is under base + 10. | wiki Saradomin brew :56 (Attack/Strength drained), br_potion.rs2:78-79, :93-94; yt_4i4lv-srJkw.md:47 "three super combats" |
| kit | `::maxmelee`, prayer 99, Agility 99, 14 anglerfish, 2 restores, 1 super combat, 4 brews; p1 the Dragon warhammer. | tob_bloat_normal.lua's kit; yt_4i4lv-srJkw.md 0:12:09 "especially if you have less than 70 agility" |

**What was wrong before (read from the logs, never replayed).**
- `b32n3a` (the seam27 plan, unchanged): all three died. Every raider ate and drank brews at
  120 hitpoints: the threat counted 20 for every unhidden tick ahead, so the need was 120 or more
  on every walk. Each raider drank 4-8 brew doses, and a brew drains Attack and Strength, so downs
  2-5 dealt 65, 82, 34 and 101 against down 1's 353. Bloat took 772 of 1500.
- `svbplaysmoke` (supplies fixed): a fresh character's run energy was gone by the fourth walk; the
  party walked one tile a tick beside a running Bloat (40-60 %, W:679) and took a fly each tick for
  twenty ticks (t346-361). Agility 99 fixed it.
- `svcplaybloat`: the plan's TANK box was one column west of the real one. Every Bloat tick log
  of the pass has raiders on 6428,93..98 and none on 6429..6434 x 93..98, so the real tank is local
  x 29..34, not 28..33. A hide tile at x 6434 was unreachable, the server's route to it ran
  through 6435,92 under a shadow (t309-311), the hand stunned all three and the next hand killed
  two. This was seam29's open row "a hand still lands on a pathed tile": the route was to a tile
  that is not floor.
- A second run-by special after a 0 splat kept p1 in the flies for 26 ticks, eating every other
  tick and never swinging (`_play_bloat` t65-91): one special only.

**Measured, `seed_survey.py _play_bloat` 5 of 5 green, `party_repeat.py _play_bloat --runs 3`
AGREE** (tick log sha 190b5b65bb97, 403 boundaries). Leader = pid 1.

| Leader name | Kill tick (room ticks) | Downs | Dealt per down (zeros) | Taken pid0 / pid1 (leader) / pid2 | Eats/drinks p1 p2 p3 | Run-by splat |
|---|---|---|---|---|---|---|
| _play_bloat | t388 (332) | 5 | 383 (4), 343 (5), 332 (7), 414 (1), 28 (1) | 277 / 185 / 206 | 11/9, 7/8, 9/6 | 0: no drain |
| svaplaybloat | t406 (325) | 5 | 342 (7), 342 (2), 447 (4), 267 (4), 82 (0) | 266 / 143 / 182 | 10/9, 6/6, 8/6 | 20 |
| svbplaybloat | t388 (332) | 5 | 371 (5), 336 (2), 331 (7), 377 (3), 64 (0) | 277 / 185 / 206 | 11/9, 7/8, 9/6 | 21 |
| svcplaybloat | t348 (267) | 4 | 477 (4), 356 (4), 411 (6), 221 (4) | 216 / 82 / 129 | 7/8, 4/4, 6/5 | 35 |
| svdplaybloat | t348 (267) | 4 | 390 (2), 405 (2), 448 (4), 220 (2) | 216 / 82 / 129 | 7/8, 4/4, 6/5 | 37 |

No deaths, no stomp hit on any raider in any down that reached it. Zeros: the first down
(after a drain) 2-7 of 30 hits; the later downs (the stomp restored Defence, W:675) 1-7 of 26-36.
`hit_npc` carries no dealer pid, so zeros are per down, not per raider. Inputs per tick, leader:
1 on 157-208 ticks, 2 on 14-18, 3 on 6-7, 4+ on 1-2.

**Not done.** Swings per raider per down are 3-4, not W:689's five: the first swing comes 5-7
ticks into the down (the walk around the tank from the hide tile) and the leave is at age 24.

## Maiden: the full plan (decide function: the re-author pass)

| Mechanic | Sourced answer | Source |
|---|---|---|
| Blackstorm, every 10 ticks, first tick 9 | Protect from Magic lit from the room start; it halves the hit and cannot be tick-eaten, so eat by the 10-tick cadence (threat = 36.5 + 3.5c per 10 ticks, halved). | W:589-590; spec maiden.cad / maiden.first (grade B) |
| Stat drain on Blackstorm (50%) | Ignored in the Entry solo plan; the trio switches to an irrelevant highest-attack-bonus item right before her attack. | W:590 |
| Target priority | Closest player, then north/east side, then orb order. The freezers lead in orb order. | W:589 |
| Defence drain at the start | Hammers on the first tick, BGS one tick after the hammers, then attack normally below 10 Defence (the trio lines W:603-633). Entry solo: one Dragon warhammer special, then scythe. | W:603-633 |
| Weapon | Scythe only for melee; otherwise ranged. | W:634 |
| Nylocas Matomenos at 70/50/30 | Freeze them with Ice Barrage before they reach her (north freezer N1, N2, N3, then the 4s; south mirrors). Entry solo: barrage the spawn group, and the HAZARD skill keeps the player off blood. | W:637-642; test notes.tob_maiden.md (seam27) for the swap blocks |
| Blood splats on the player's tile | In melee distance, move before she attacks (the T-1 rule: step on the tick before her attack tick). A splat locks the attack out for two attacks. Never stand on a splat or a trail. | W:593-595; ET 1.1 |
| Blood spawns | Kill or avoid. Never stand on their trail. | W:596-598 |
| Skipping 30s | Trio only: kill her before the 30s unfreeze. | W:645-646 |

The table above is the strategy the seam30 plan was written from; the plan itself is the next
section.

## Maiden, Entry solo (`tob_maiden`, mode `entry`), proved (raid seam30 play_tob_maiden)

REPLACES the decide line of "Maiden: the full plan": the plan is
`QD.raid._play_maiden_decide` in `raid_play_tob_maiden.lua`, proved by
`test/raids/_play_maiden.lua` (`seed_survey.py _play_maiden`: 5 of 5 green). Weapon: the
twisted bow (W:633 "the twisted bow is highly effective against her"; the library's bow row,
seq 426 every 6 ticks, lives in the plan file because raid_play.lua has only the scythe).

| Mechanic | What the plan does | Source |
|---|---|---|
| Her four forms | The plan follows the npc row's type change (100/70/50/30) and re-points the library's boss symbol. | W:592; K boss_symbols |
| Blackstorm | Protect from Magic on; the threat is 28 a storm on the 10-tick clock (impact 5 after the aim), because mz30a took 27 through the prayer at c=6. | W:590; K auto_impact_offset 5 |
| Blood splat, seen in flight | Every projectile 1578's destination is a marked tile. If any lands within 2 of the player, the player runs 3 tiles to the floor tile nearest home, out of every in-flight splat's 5x5 and off every pool and trail. | W:595 "those standing away can react to it", W:596 (5x5 extras); K blood_flight_base (two ticks or more) |
| The T-1 step | On the tick before her predicted attack, and only when a throw can come (2 autos since the last one), the player runs the same 3 tiles. This repeats until a throw lands on the tick after such a step. | W:595 (melee distance, and the cooldown); ET 1.1 |
| Pools and trails | The pools are graphic 1579 and the trails are loc 32984 (`api_drive.loc_copies`). Standing on either moves the player to the nearest safe tile (`_play_hazard`). | W:597, W:600; ET 2.4 |
| Matomenos | When a wave is seen: the magic set in one block (5 inputs), one Ice Barrage per nylocas every 5 ticks, the ranged set back in one block (4 inputs), then any nylocas still standing is shot. Entry: each barrage killed its nylocas (16 hp). | W:594 "essentially mandatory", W:637-643; K freeze_full_bonus +140 |
| Blood spawns | The nearest one within 10 tiles is shot (they lay the trails). | W:598-600 "Kill or avoid" |
| Drain | A drained Ranged level (below 88) is restored with a super restore. | advanced guide :111 "always repot" |
| Measurements | `prove_protect`: one blackstorm is taken before Protect from Magic goes up. `flicks`: the whip goes on after her aim, at most 12 times, until one drain is seen. These exist only so the kept rows tech.protect_magic and tech.bow_flick can be read. | K rows; W:590-591 |

Measured (five names): room 227-320 ticks, against 443 for the kept run. Damage taken 169-251
(kept 525), all from the blackstorm; pools 0 and trails 0-2 (kept 175). Food 4-9 of 10 sharks,
drinks 1-6 (kept 29 eats and 11 restores). About 0 ticks with no attack (kept 63). Inputs per
tick: 1 on 72-106 ticks, 2 on 5-9, 3 on 0-1, 4+ on 6 (the two gear blocks a wave).

Open: one blackstorm hit 27 through Protect from Magic with six Matomenos leaked (mz30a t256,
hp 27, so the raw hit may have been higher). W:590's halving gives 14 at c=6. It is one sample,
with leaks the plan now prevents; the 2026-10-05 survey's spec.maiden.auto_prayed_entry failure
on svb (19 of 28 off the formula) is the same row, read by the re-author.

## Maiden, Normal trio (`tob_maiden`, mode `normal`), NOT green (raid seam32 play_tob_maiden_normal)

`seed_survey.py _play_maiden --party 3`: **0 of 5**. Every leader reaches the 30% wave; the
own-name run killed her (room 617 ticks, line "6:10") with seat 3 dead at t632; the other four
lose the tank at t652-672 with every supply spent (20 anglerfish, 16 brew doses, 13-14 restore
doses). `party_repeat.py` (the harness copied with `party = 3,`, run s32mzrep): 3 runs AGREE.
The Entry solo plan is unchanged (`seed_survey.py _play_maiden`: 5 of 5). Same plan file; a
party of one never reaches any line below (`m.R == nil`).

| Mechanic | What the plan does | Source |
|---|---|---|
| Roles | Seat 1 (the leader) is the ranger tank on 6434,94; seat 2 the freezer on 6441,94 (east of her middle row: both spawn rows within Ice Barrage's ten tiles; from 6440,89 the north spawns were out of reach); seat 3 the north ranger on 6437,100. Tiles re-based on her own tile. | 10Boot yt_4i4lv-srJkw 0:06:33 "The person closest to the boss becomes the tank", "You don't want your mage to be closest"; 0:08:48 "camping on the north side"; W:603 one freezer in a trio |
| Blackstorm overhit | The hit is settled on her LAUNCH tick against the hitpoints then (tob_maiden.rs2 `~tob_maiden_blackstorm`), so `threat` needs storm + 1 for a launch inside the horizon, judged one tick early (the client sees her animation a tick late; a bite on T-1 is eaten after her scan). The storm is max(25, the largest one-tick loss read + 2). | W:590 "cannot be tick-eaten"; s32mzn1 (12 hp at launch t227, ate t229, died t232), s32mzn4 |
| Priming | The freezer puts the magic set on while her bar is within 5% of the next threshold and holds it (at most 60 ticks), walking home if a dodge moved it; the first barrage leaves on the spawn tick. | W:643 "hover their mouse over the S1's spawn position"; W:639 "on the first tick possible" |
| Freeze target | Every 5 ticks: the crab (walking or frozen) with the most WALKING crabs at gap >= 4 in its 3x3, a walking one before a frozen anchor, nearest her among equals. Ice Barrage lands two ticks after the cast (npc_spotanim 369), so a crab nearer than gap 4 reaches her first. The freezer holds its tile (no pre-emptive dodge) while crabs walk. | W:637 "frozen on top of each other in front of Maiden", W:639 "barrage the clump"; 10Boot 0:07:40 closest first |
| Rangers on crabs | A walking crab at gap <= 5 first, then the crab frozen longest (its ice ends first), then any walking crab, then her. Seat 3 shoots blood spawns when no crab is up. | 10Boot 0:08:14 "machine gun down the crabs that aren't in the clump"; W:639 "kill the stray nylocas before getting back on Maiden"; W:598-600 |
| Tank swap | Seat 1 steps to the reserve corner 6438,101 at 4 anglerfish left; seat 3 steps onto the tank tile after seeing seat 1 within 1 of that corner (its own `api_drive.players` rows) for 3 ticks. | 10Boot 0:06:33 "You can swap out when someone gets low"; W:589 |

Measured (final survey, five names): frozen 3-4 of 6 per threshold, 2-4 reached her per
threshold, 7-10 a room (heals about 900-1300); rooms 600-700 ticks; damage taken about 2000.

Open (the first cause of each red leader, from its log):
- **Supplies run out before she dies** (four of five): the tank dies at t652-672 with nothing
  left. The room is long because 7-10 Matomenos reach her (each heals 2x its hitpoints and adds
  3.5 to every storm) and the bows deal about 6 a tick to her. The sources' tools are absent: the
  trio's freezer is one of two in the 4-scale tables, the rangers "machine gun" the strays with a
  blowpipe (10Boot 0:08:14; `toxic_blowpipe_loaded` needs darts and scales loaded,
  blowpipe_ammo.rs2, no kit verb does it), and the Defence drain at the start (W:605-626, 10Boot
  0:06:33 "everyone should drop a dragon warhammer spec") is not in the plan.
- **A new tank does not know the storm** (s32mzn9, svb, svc): `storm_seen` is the raider's own
  largest loss, so seat 3 stepping in late fight under-eats against a 40+ storm. What a person
  sees is the hitsplat on the old tank.
- The rangers' Attack on a walking crab answers `timeout` most presses (`add attack timeout` in
  every leader's detail); hits still land. Not yet read.
- Library (raid_play.lua, not this seam's file): `api_drive.players`' pid counts from 1 and the
  tick log's from 0 (s32mzn1: the leader was players() pid 1, the log's pid 0), so in a party
  `st.my_pid` names the next raider and the leader counts another raider's swings. The Maiden
  plan re-reads its own log pid by tile once (`m.pid_fixed`); the library should.

## The other four rooms: mechanics and sourced answers (for the authors)

**Nylocas** (W:702-776; ET 4). Trio roles are one mage, one melee and one ranger (W:713). Each
kills only its own style, because a wrong-style hit is reflected or punished. Pillars are
assigned per lane (`blert_nylo_pillar_assignment.json`). Melee uses a multi-hit 4-tick weapon on
the smalls and the scythe only on lined-up or big ones (W:717). Vasilias changes style, so the
plan switches loadout in one block on the switch tick (the LOADOUT skill, one `t.together`).

**Sotetseg** (W:777-821; DRIVER_NOTES seam20 "Sotetseg's ball"). Protect from Magic or Missiles
by the ball's colour, switched so it is up when the ball LANDS (the pinned exception). The big
ball is split by standing together. The maze: one runner walks the marked tiles, and every tile
stood on must be a maze tile (README technique row). In a party the role is "who runs the maze".

**Xarpus** (W:822-866; ET 1.3). Phase 1: kill the exhumeds and stand off the acid (HAZARD).
Phase 2: melee can step back one tick before the spit scan ("5-tick Xarpus", the T-1 rule).
Phase 3: do not attack while he faces you ("the stare"), and turn your back.

**Verzik** (W:867-998; ET 1.3). P1: hide behind a pillar from her auto (no auto lands while
the pillar is between), and the Dawnbringer holder (the ROLE) specs. P2: stay close except on
the bounce tick (Plank2g quote, ET 1.3), with Protect from Missiles up when the urnbomb LANDS
(the pinned exception), and kill the nylocas by style. P3: step away one tick before her
attack (Granddad Jad, ET 1.3), webs and yellows by position, and the green ball passed.
Protection prayer on hit is the pinned exception.

## Nylocas, Entry solo (`tob_nylocas`, mode `entry`), proved (raid seam31 play_tob_nylocas_green)

Plan file `raid_play_tob_nylocas.lua` (raid seam30). `E` is
`sources/wiki_Theatre_of_Blood_Entry_Mode.wikitext` (its Nylocas "Solo strategy"), `W` the
Strategies page, `NT` `encounters/nylocas.tsv`, `NB`/`NR`/`DMG` the room's own scripts
(`tob_nylocas_boss.rs2`, `tob_nylocas.rs2`, `tob_damage.rs2`). Harness `test/raids/_play_nylocas.lua`.

| Decision | Rule in the plan | Source |
|---|---|---|
| One weapon per colour | whip (4), magic shortbow on rapid (3), Ice Rush / Ice Burst (5); swap in one block on the press tick | E:155-157 "You will need all three attack styles"; Ancient Magicks + a fast ranged weapon |
| Never the wrong colour | a copy is pressed only with its colour; a flicker that turns under the press is dropped (step); from wave 16 nothing younger than 7 ticks is hit | DMG:272 (a wrong hit nulls the raider on it for good); NT flicker_first_wave 16, first_switch 5, hold 2 |
| Aggros first | the aggro hitting through the prayer, then one hitting, then other aggros, then chewers | E:160 "must be killed as fast as possible"; W:742 |
| Greens first, smalls first, oldest first | small score biases | E:162 "Focus the green (Ranged) Nylocas first"; W:746 "prioritising the smaller ones first" |
| Stay central | home 31,24 local; melee only on the platform | E:162 "stay near the centre ... unless you are cleaning up greys"; E:160 "cannot melee them until they reach said platform" |
| Let a low pillar go | chewers of the lowest support under 15% are passed over | E:171 "let one that's low die and focus on the other three" |
| Freeze chewer clumps | Ice Burst on 3+ chewing a support under 70%, centred on a blue when one is in it | E:164 "Ice barrage/burst any clumps ... Frozen nylocas cannot attack the pillars" |
| Off the blast | a copy aged 45-52 (46-53 big) within 2 tiles: run to the safe tile nearest home | NT lifetime_small 52 / big 53; E:161 "at least two tiles away"; ET 1.1 T-1 |
| Wave prayer | the colour with the most weight among aggros in reach (big 2, grey only within 2), held until another is 2 heavier | kept tob_nylocas.lua technique (tech.prayer row) |
| Vasilias prayer | by her form from the tick it is seen (spawning = melee), sent FIRST in the plan's own block | W:752; NB:136-150 first attack +2/+3 after a turn |
| Vasilias style | her form's weapon; after a turn re-press (the turn stops attacks); no arrow/spell that would land within a tick of the predicted turn (14 then 15 ticks) | W:752 "The player will stop attacking when Vasilias changes forms"; NB:176 p_stopaction; NB:436-464; DMG:278 reflect + heal |
| Vasilias magic form | Ice Burst, not Ice Rush, on her (same 5 ticks, max 22 against 18); her row is fought while it is there, whatever her bar reads (at 4 of 360 it reads 0) | wiki Ice burst / Ice rush; svhplaynyloc 2026-10-06 (bar 0 from t861, the plan dropped her and died at t1237) |
| Supplies | the plan's own `_play_nylocas_supplies`, the library's rule with two guards: a food only when at least half its heal lands under the base, a brew only when at least half its dose lands UNDER THE BASE (this server does not hold a brew's overheal); the drink goes first in the block, so a combo's food is judged after the brew; threat unchanged (2 big max hits floor + unprayed aggros + blasts + 17 a swing through her magic/ranged prayer + 40 a support under 25%) | E:171; NB:370-383; svaplaynyloc t532-538 (brew 99 -> 115, raider row 99 the same tick); svdplaynyloc t401 (brew first, the shark after it healed 0) |
| Kit: bandages and the Entry set | 7 Theatre bandages in place of the 6 sharks and one restore (sharks first in the waves, bandages first from the interlude, one bandage eaten in the interlude: "heal up and boost"); the Entry page's recommended Entry equipment worn (void melee helm, glory, elite void top/robe, void gloves, dragon boots, berserker ring (i)) | E:151 "After defeating the Pestilent Bloat ... 10 bandages"; E:33 bandages make combat/ranging potions unnecessary after the first two bosses; W:750 "heal up and boost"; E:78-91 {{Recommended equipment|style = Entry mode}} |

Library faults met in seam30 (protection toggle, death serial) are fixed in raid_play.lua by
raid seam31 play_library_faults; the plan's workarounds are removed.

Measured (2026-10-06, headless, seed_survey `_play_nylocas`): 5 of 5 on the final tree and
10 of 10 with `--names 10` (results under build/seam_state/matthew-mbp-m4-raid-b1-seam31/
ny31_final_results.tsv and ny31_s4_results.tsv). Own name: room 706 ticks mark to her death,
taken 177 (seam30's ny30i 707 / 427; the kept room 945 / 650), 6 bandages (the interlude one
at t633), 2 brew doses, lowest 51, her fight 123 ticks in 31 hits, 0 npc_heal rows on her.
Ten names: room 641-765 ticks, taken 136-282, her fight 114-194 ticks, 0 heals on her in
every name, 8-18 of the 47 heals carried used. What moved it, each measured on the five
names: the supply guards alone 1 -> 3 of 5 (svd still died in the waves, sva at her with
supplies spent); with the bandage kit and the Entry set 5 of 5, wave damage 94-238 (was
227-424); the brew cap at the base cut the drinks from 40 doses to 2-18. Still open: 2-3
support collapses a run (77-118 damage, now the largest source); bites 1290-1360 a run,
about 85% from SMALL chewers (greys 10774 most, about 600 bite animations a run; bigs about
200); freezing clumps on any support (not only under 70%) was measured (c1: support deaths
1,2,3,3,2 against 3,2,3,3,2) and not kept (taken 1116 against 961 on the same names).

## Nylocas, Normal trio (`tob_nylocas`, mode `normal`), KEPT on five names, thin margin (raid seam32 play_tob_nylocas_normal)

The harness is `test/raids/_play_nylocas.lua` run with `--party 3` (no `party` field, so the
same id stays the Entry solo harness: `seed_survey.py _play_nylocas` solo, `--party 3` trio;
`party_repeat.py` needs a copy with `party = 3,`, build/seam_state/.../_play_nylocas_trio.lua).
The plan is the Entry plan with seats (`P.roles`, used only when `st.party > 1`).

| Who / when | Intent | Source |
|---|---|---|
| seats | p1 the mage (and the tick log), p2 the ranger, p3 the meleer; each prefers its own colour by `own_colour_bonus` 12. | W:711 "Trio: x1 mager, x1 melee, x1 ranger"; trio guide (blert_guides/tob_nylocas_trio_content.mdx) Mage/Ranger/Melee Waves |
| all | Always on the attack: another colour is taken when none of the seat's own is worth more. | W:746 "always be on the attack; if your assigned nylocas are not currently near ... switch weapons" |
| all | Aggros first, every seat's, no colour penalty. | W:733 "These aggro's should be prioritised first" |
| all | NEWER copies first (score + age x 0.3; a chewer within 10 ticks of its pop +20). | W:746 "always kill newly spawned nylocas after dealing with aggro's, prioritising the smaller ones first"; trio guide :24 auto-pop |
| own colour | Considered out to reach + 14 (others reach + 6). | trio guide ranger waves 8-10 "Path west and stand 1 tile away from the west barrier" |
| blues | The mage's; p2/p3 cast only at a blue aggro within 8. Only the mage bursts (a pure-blue 3x3). | W:711; s32ny10 (a helper's Ice Rush: 13 pressed in 50 ticks, 3 covered, 4 refused) |
| p2 | A toxic blowpipe (2 ticks, reach 5, seq 5061), loaded in run() by use-on (darts, then scales). | W:717 "Rangers should use a toxic blowpipe in this room" |
| Vasilias | All three follow her form and pray by it; NO SWING on or after the predicted turn (the colour is judged at the swing): a press whose first swing falls within `turn_margin` 1 is held, a weapon swinging on its own is stopped by a step the tick before. Windows 9 then 10. | W:752 "change forms every 10 ticks"; W:733 "If player makes an attack just before the nylocas changes forms, they will still take damage from it"; tob.constant ^tob_vasilias_first_switch_ticks 9 / _switch_ticks 10 |
| kit | The Entry set and weapons, anglerfish 7 in place of the Entry chest's bandages (p2: 6 and the blowpipe's scales). | transcripts/yt_KF9y2GYTJ-A.md:114 "make sure that you eat your angler" |

**Party reads (what a member sees).** A member holds no tick log, so it read no swing of its
own (s32ny2: p2/p3 "0 swings" in 933 ticks; its turn hold and re-press ran blind): it reads
its swings from the experience paid (Hitpoints or Magic rising; the leader's XP read matched
its log swings, lag 0-1 tick, note.reads). The leader re-reads its own log pid from
player_tile (players() counts from 1, the log from 0: the library read the ranger's swings
as the mage's). A "Your attack has no effect on this Nylocas." line (tob_damage.rs2:308-310)
strikes the last-swung copy off for good. A copy is NEW only if its slot is new, unseen 8
ticks, resized, or further than it could walk: the seam30 "unseen 2 ticks" reset every age
whenever a block ran 3 ticks, so every copy stayed "young" (flicker guard) and the blast
clock never ran (s32ny6 p2: pick=none with 22-33 copies present).

**Measured (final plan, `seed_survey.py _play_nylocas --party 3`: 5 of 5 KEEP; `party_repeat.py`
on the party copy, name s32nyrep, 3 runs AGREE, tick log sha db3f4e1f33c7, 923 boundaries).**

| name | supports down before her | the one left at her landing | stall (last wave vs 236) | Vasilias heals | killed (ticks from the mark) | wrong-style wave swings |
|---|---|---|---|---|---|---|
| _play_nylocas | 3 | 36,29 at 0.11 | 139 | 0 | 859 | 11 |
| svaplaynyloc | 3 | 36,29 at 0.05 | 135 | 0 | 799 | 8 |
| svbplaynyloc | 3 | 36,29 at 0.03 | 139 | 0 | 759 | 11 |
| svcplaynyloc | 3 | 36,18 at 0.11 | 127 | 0 | 722 | 8 |
| svdplaynyloc | 3 | 36,18 at 0.01 | 147 | 0 | 761 | 7 |

Nobody died on the five names. The margin is ONE support: a sixth name (s32nyrep) reads 0
of 4 at her landing (tech.pillars_at_boss FAIL; supports t350/451/452) and a seventh
(s32nyrep2) wipes at t445 when the fourth falls. Waves stall 127-147 ticks; kills average
age 20-25 ticks (blue chewers 24-28, big blues ~40, greens 17-20).

**Open (the next pass's rows).**
- Wrong-style WAVE swings 7-11 a room (none on Vasilias): a swap while engaged keeps
  swinging at the old copy with the new weapon until the next press lands (svcplaynyloc p2:
  whip pressed on a grey t163, blowpipe on t164, darts on the grey t165/t167 for 0, 0).
  `P.swap_stop` (in the plan, OFF) sends a one-tile step toward the new copy with the swap:
  0-2 wrong-style swings a room, but survey 3 of 5 (survey8) -- the step costs the tempo the
  pillars live on. A walk click on the raider's own tile instead broke p2 (11 swings).
- The pillar margin: what was tried and NOT kept -- defending the lowest support instead of
  letting the low one go (survey7 3/5), the meleer casting at blues (survey9 3/5).
- A manual cast is followed by the player's own melee five ticks later (s32xpswing: seq 422
  at 13 and 21 after Wind Strikes at 8 and 17): a staff bash on a blue is a wrong-style hit
  if no next press comes inside five ticks.
- Driver (spell.lua `_select_row_is_held`): Ice Rush presses answer `refused` 15 of 41 for a
  helper seat (s32ny7 p2), about a third.

## Sotetseg, Entry solo (`tob_sotetseg`, mode `entry`), proved

Plan: `script/plugins/quest_driver/raid_play_tob_sotetseg.lua` (QD.raid._play_sotetseg_decide,
QD.raid._play_sotetseg_maze). Harness: `test/raids/_play_sotetseg.lua` (the kept room's kit and
entry, one `t.raid.play`, the kept technique and room-complete rows copied unchanged).
`seed_survey.py _play_sotetseg`: 5 of 5 (twice: survey1 with the first plan, survey2 with the final).
Sources: E = wiki_Theatre_of_Blood_Entry_Mode.wikitext, W = wiki_Theatre_of_Blood_Strategies.wikitext,
ET = ENCOUNTER_TIMING.md section 5, S = our tob_sotetseg.rs2 / tob.constant.

| Decision | Source |
|---|---|
| Melee him with the scythe, Piety lit, the melee kit the kept room's phase 3 wears | E:191 "it is best to attack Sotetseg with Melee" |
| Protect from Melee on every tick the raider stands in his range (footprint distance <= 1) | E:191 "praying Protect from Melee"; S: the melee's prayer is read in the swing tick's own player phase, so it cannot be reacted to; S `npc_range(coord) <= 1` |
| Protect from Magic the tick a 1606 is seen in the air, held until its landing tick has passed; Missiles for a 1607 | E:191 "switching to the other two protection prayers when you see their respective projectile"; the owner's ruling (ball read at IMPACT), S tob_sote_impact |
| Out of his range, Protect from Magic | S: no melee past range 1, so every attack there is a ball |
| ONE protection in the plan's walk_prayers per tick (decide writes slot 1); a sent press is treated as lit for 2 ticks | the protections exclude each other and a press is a toggle (nylocas fixer, seam30) |
| The death ball (1604) is not prayed; its 15 is in the supplies threat when it lands inside the horizon | E:191 "cannot be protected against ... 15 damage" solo; W:20 tick-eat it |
| Eat by the largest hit before the next chance: a prayed melee (10) per 5 ticks in range, the death ball when due, a ball and a melee unprayed only while a press was refused (prayers disabled) | the library's supplies rule; W:13 "disable protection prayers"; the first plan's standing 22 ate at 25-43 hp (survey1) |
| The maze path is the realm's `tob_sotetseg_lighttile` locs the client draws (t.world.loc_copies), walked north first, else along the row | W:25 "presented with a randomly generated path"; S tob_sote_light_path / tob_sote_path_has |
| Walk one straight run at a time, to its corner, never across a corner | ET 5.4 "an L that goes diagonal first steps off the path" |
| No step for the first 2 ticks in the realm | S p_stun 5 from the proc, realm at +3 (kept maze.stall, maze_first_move 5) |
| Wait on row 3 (index 2) until the rest of the run ends on a tick 2 mod 4, then go | W:27 "stop on the third row and wait" (the tornado spawns on row 4, ET 5.3); solo the wait times the run |
| Step off the grid north on a tick 2 mod 4 (resolves on 3) | ET 5.3 "off on 3"; kept maze_cycle 4, phase global |
| Not done: the Elder maul Defence specials | W:5-8 spec roles; left for a later plan (Entry defence 150, floor 100) |

Library change (raid_play.lua `_play_tick`): the "jump of more than 20 tiles = died" test is skipped
while `st.teleport_until` covers the tick; this plan sets it while his combat form is out of view and
in the realm. No other plan sets it. `seed_survey.py _play_smoke`: 5 of 5 after it.

Measured (survey2, final plan; mark to npc_death):

| name | kill ticks | taken | sharks | drinks | ticks no attack | inputs 1/2/3/4+ | mistakes |
|---|---|---|---|---|---|---|---|
| _play_sotetseg | 158 | 75 | 0 | 2 | ~62 | 72/4/2/0 | prayer 11 |
| svaplaysotet | 188 | 91 | 0 | 2 | ~77 | 86/3/2/0 | prayer 13 |
| svbplaysotet | 215 | 97 | 1 | 2 | ~71 | 93/4/2/0 | prayer 11, food 1 |
| svcplaysotet | 153 | 70 | 0 | 1 | ~62 | 71/4/2/0 | prayer 10 |
| svdplaysotet | 181 | 81 | 0 | 2 | ~70 | 80/5/2/0 | prayer 12 |
| kept tob_sotetseg | 468 | 343 | 11 | 1 restore | ~204 | (not recorded) | food 6, prayer 15 |

Every "prayer" mistake is a melee through Protect from Melee (Entry 1..10, unsourced: CONTENT_BUGS
seam30); 0 unprayed balls, 0 tornado hits, 0 off-path tiles, 0 missed attacks. The ticks with no
attack are the two mazes (about 36-46 each, proc to the first hit back).

## Xarpus, Entry solo (`tob_xarpus`, mode `entry`), proved

Plan: `script/plugins/quest_driver/raid_play_tob_xarpus.lua` (QD.raid._play_xarpus_decide,
QD.raid._xarpus_see, QD.raid._xarpus_quadrant). Harness: `test/raids/_play_xarpus.lua` (the kept
room's kit and entry, one `t.raid.play`, the kept technique rows technique.exhumed_cover and
technique.spit_dodge and the room-complete rows fight.done and exit.* copied unchanged).
`seed_survey.py _play_xarpus`: 5 of 5 (twice: survey1 with the grid plan, survey2 with the final).
Sources: E = wiki_Theatre_of_Blood_Entry_Mode.wikitext, W = wiki_Theatre_of_Blood_Strategies.wikitext,
A = wiki_Guide_Advanced_Theatre_of_Blood.wikitext, ET = ENCOUNTER_TIMING.md section 6,
X = encounters/xarpus.tsv, S = our tob_xarpus.rs2 / tob_damage.rs2 (read for WHEN a rule bites).

| Decision | Source |
|---|---|
| Phase 1: walk onto every exhumed the tick it is seen and stand on it until it closes; wait on the south melee tile (6434,96) between them | W:831 "stand on top of them until they return to the ground ... stand in the centre of the arena to quickly intercept"; E:204 |
| The super combat potion is drunk on the fifth exhumed (the quiet phase) | tob_xarpus.lua :120-123 (kept room's own timing) |
| Piety from the stand-up; no protection prayer at all | E:201 "Protection prayers have no effect during the fight, so prayer points can be used instead to boost damage" |
| The spit rhythm is a grid: first slot stand-up + 7, then every 4; a spit seen 0-2 ticks after its slot keeps the grid | X xarpus.p2.first_spit 7 (blert FIRST_P2_TURN_TICK), xarpus.p2.cadence 4 grade A; A:203 "you step back every 4 ticks, based on Xarpus' attack speed" |
| The dodge: one step sent on S-1 (resolves S, after his scan, so he aims at the tile just left), a second resolving S+1; the splat lands two tiles from the player; the second tile is a clean melee tile when one exists | E:207 "moving ... exactly 2 tiles at a time to avoid the poison", "moving just before you see the projectile is about to hit you"; ET 6.2 the scan reads END OF T-1; tob_xarpus.lua :445-471 (the kept recipe, unchanged, so its row copies) |
| Between dodges: stand on a clean melee tile (an edge of his 5x5) and swing on cooldown; off a pool or a landing 3x3 first | E:207 "permanent 1-tile puddle ... dealing some damage if you stand or run over it", "3x3 area"; W:836 |
| Every walk to a melee tile is routed: neither route shape (diagonal first, straight first) may end inside his footprint or cross a pool | E:212 "Do not get too close to Xarpus, or he will throw pebbles"; S ~tob_xarpus_ground_sweep (stomp + skipped spit when a tick ends inside him); measured xa30d t194 |
| The screech: a grid slot with no spit two ticks after it while the bar reads <= 25% | E:212 "screech when below 22.5% health, and will stop launching poison"; X xarpus.p3.screech_pct_entry 22.5 (the bar is 30 px) |
| Phase 3: swing only from a quadrant he does not face; only while the next swing lands before his next turn (turn + 8, one tick margin); else step one tile out in the same quadrant | W:851 "If a player attacks from a corner that Xarpus is looking at, he will retaliate"; E:212 "attacking Xarpus once with Melee when he turns to face one of the other quadrants, then click on the ground to stop attacking"; A:225-227 (22121); S tob_damage.rs2:430-437 (never before the first turn, never on a turn's own tick) |
| He turns to the player's quadrant: walk to the nearest clean melee tile of another, the quadrant he looked at last first | W:853 "Xarpus will never look in the same corner twice, so players should be moving to where he last looked" |
| Quadrant = the server's split (centre row south, centre column west) | S ~tob_xarpus_quadrant_from; conformance seam.raid_play_xarpus_quadrant |

Not copied from the kept room (deliberate measurement probes a play never makes):
technique.lag_step (holds a tile one from a landing to take the splash), technique.stomp_skip (stands
under him), technique.gaze_probe (swings into his gaze). New harness rows: play.dodge_on_spit (every
dodge's S is a real 8059 tick, or the one slot the screech cancelled -- without it the copied
spit_dodge row checks nothing when the plan's S is wrong, which xa30a showed), play.gaze_kept (0
retaliation hitsplats after the screech), play.measure.

Survey2 (mark 36 to npc_death): kill 176/176/163/160/176 ticks; taken 29/26/0/11/29; food 0 of 15
sharks; drinks 1 (the super combat); raid_report 0 long gaps, about 0 ticks with no attack, 0 missed
attacks; inputs per tick 1 on 44-50 ticks, 2 on 9-16, 3+ on 0; 0 retaliation. Kept tob_xarpus: 245
ticks, 251 taken (3 retaliations), 9 eats, about 27 ticks with no attack, 5 missed attacks. The
2026-10-05 survey: 228/263/232/223 ticks, taken 161/259/193/201, eats 7/13/8/10.

## Verzik, Entry solo (`tob_verzik`, mode `entry`), proved (raid seam30 play_tob_verzik, seam31 play_tob_verzik_green)

raid seam30 play_tob_verzik: plan `raid_play_tob_verzik.lua`, harness `test/raids/_play_verzik.lua`.

Entry, solo, the kept room's kit (K = test/raids/tob_verzik.lua :6-31: fists, the Dawnbringer, the
twisted bow, the serpentine helm, 16 anglerfish, 4 brews, 2 restores, a ranging potion). W =
wiki_Theatre_of_Blood_Strategies.wikitext, V = encounters/verzik.tsv.

| Phase | Decision | Source |
|---|---|---|
| all | Follow her one npc row through every form by its id (10831 P1, 10832, 10833 P2, 10834, 10835 P3). The client gives a new form a NEW row (P1 slot 68, P2 slot 78), so the id is the key, never the slot. | V verzik.av.npc_form_entry; s30 vz30b |
| all | Protection prayers: keep ONE in walk_prayers (slot 1 rewritten each tick). Magic-on then Missiles-off relights Missiles (a toggle), which put 23 blood spells under the wrong prayer. | nylocas/sotetseg fixers; s30 vz30g |
| P1 | Protect from Magic all phase; Piety while punching. | W:871 |
| P1 | Four punches from the start, then hide behind the pillar south-west of her (the kept tile 6426,93) for the first bolt, and out on its launch tick. | W:885 "safely attack four times with a 4-tick weapon"; W:887; V p1_verdict_tick |
| P1 | Every later bolt is TANKED under the prayer (15 max taken; 30 cap). | W:891 "tank her attacks entirely, to avoid losing out on ticks" |
| P1 | Fists until 10 punches, then 2 bow shots, then the Dawnbringer: 2 specials (75-150) and autos. The fists and the bow are the cap row's sample (10 melee, 3 ranged); the specials do the work. | W:873 caps; W:875 Dawnbringer; W:883 "use their melee weapons" |
| P1 | On her death animation (8111) leave anything within 2 of a pillar. | W:879; V pillar_collapse_range |
| T12 | One block: bow + ranging potion; then the serpentine helm; rapid style; Rigour + Protect from Missiles; heal up; walk two out of her P2 body on the west. | W:899, W:901, W:927 venom, W:943 |
| P2 | Stand two out of her 3x3 (survey3: 20-66 attacks per name with the raider two or more out on T-1, 0 slams; sva and svd were adjacent once each, a crab run, and slammed that once). Step off any tile an urnbomb, an Athanatos or a web is falling on (the projectile's destination). | W:909, W:911; V p2_scan_rule |
| P2 | Prayer: Missiles; Magic once the reds phase is SEEN (a Matomenos on the floor or a blood spell 1591 in the air; the 8117 summon is not read off her row reliably); back to Missiles while an urnbomb is in the air (read at landing, the owner's ruling). | W:931; V p2_bomb_prayer_read_tick |
| P2 | (NEW, seam31) The next summon by COUNTING her attacks: after a summon, six 8114/8116 attacks, and her next slot (+4) may be the summon (each later slot too until it comes). From her slot minus the bow's speed to the end of the absorb (+5) nothing of mine is rolled on her: a press waits, and a bow repeat that would fall in [S, S+5] is cut by a one-tile step. The bow's own repeat was rolled ON the summon tick four times in s31's red svd (t297 29, t333 46, t369 12, t405 24: 111 healed, `~tob_prepare_player_hit` rolls at the swing); 0 on every name after. | Entry_Mode.wikitext:231/:235 "DO NOT attack her while she summons them or immediately after"; V verzik.reds_first_attack_after 12, p2_cadence 4, reds_attacks_between 7 (B), reds_absorb_window 5 |
| P2 | Targets: a ranged nylocas only from 4+ tiles, the Athanatos, then the Matomenos ONLY while her summon animation plays (summon + 10), then her: a shot on her (about 15 a hit) beats a shot on a red that heals her at most the 20 it has left at the next summon. | W:928 "focus on the Matomenos until this animation ends"; V verzik.av.reds_summon.seq 8117 10.00 ticks; tob_verzik.rs2 ~tob_verzik_absorb_reds; V verzik.entry_reds_hp_1p 20 |
| P3 | Switch protection on sight of her animation/projectile (8124/1594 Magic, 8125/1593 Missiles; the green ball 1598 is not prayed, eaten for). Read on hit, the owner's ruling. | W:951; V p3_prayer_read |
| P3 | Two or more out of her (no melee on T-1). Stand on the nearest yellow pool while any is down. Nylocas as in P2. | W:953, W:969, W:948 |
| P3 | (NEW, seam31) A yellow pool counts for 14 ticks from its first sight, and only while its graphic has cycles left: the client still lists 1595 after the blast, and s31 vz31a stood on a dead pool from the yellows to its death 51 ticks later, never running from either tornado. | V verzik.p3_yellow_pool_lifetime 14 (B) |
| P3 enraged | Run from a tornado within 5. Its tile is DEAD-RECKONED: the client's npc row for 10846 stays on the spawn tile (vz31d: 6431,91 for nine ticks while the server's npc_tile rows walked it 6431..6424), so the plan walks it one tile a tick straight at the raider from where it appeared (content: `npc_walk` to the raider every tick, touch at range 1), and believes the row whenever it does move. The run scores each tile within 2 by its distance from the tornado AFTER the tornado's step toward it (npcs move first), plus the wall (cap 3) and a carry term for the last run direction; the walk goes out directly. An offline chase of this rule (20x20 floor, 1 tile a tick against 2) was touched 0 times in 600 ticks; vz31e without the carry turned back out of the 6422,79 corner and was touched 11 times. | W:981 "tracks them down"; Entry_Mode "continually moving around the room"; tob_verzik.rs2 ~tob_verzik_tornado_tick |

Measured (seam31 final survey, five names, from the mark to her npc_death): own 423 ticks, sva
488, svb 426, svc 397, svd 570, all green. Damage taken 213/289/224/158/378; food 6/10/9/5/12;
drinks 9/10/7/6/15. Tornado touches 1/1/1/1/2 (26, 37, 29, 29, 33+57). Absorb heals (damage
dealt in the summon window) 0 on every name. svd's P2: t168-334 (seam30: t168-538). Kept
tob_verzik: 679 ticks, 615 taken; seam30's best single run vz30i: 355 ticks, 211 taken.

Open: (1) the client's npc row does not follow the tornado's walk (api_drive.npcs grid_position
stays on its spawn tile); an engine row, and a visual check for later (does the tornado model
move on screen?). (2) Our server summons the reds in her 7th slot after six attacks (36 ticks a
cycle); V verzik.reds_attacks_between says 7 attacks BETWEEN summons (Blert): the plan holds for
both readings. (3) The cap row's 10 melee needs 10+ punches; the bow shots are there for the kept
row only.

## Open rows (found is not fixed)

- **A hand still lands on a pathed tile in the Normal trio** (seam29). CAUSE FOUND (raid seam32):
  the Bloat plan's tank box was one column west (local x 28..33; the tick logs say 29..34), so
  hide tiles at x 6434 were unreachable and the server's route detoured under shadows. Fixed in
  the plan. In the five-name survey no raider took any hit above 20 (no hand, no stomp: every
  hit_player row of all three pids is a fly). Solo Entry: 0 hands on
  any raider on five names. Normal trio `s29n3e`: 5 hands, all on tiles the raider moved onto
  two ticks after the shadow was drawn (t190, t264 on both members, t371, t470). The same ticks
  came back under three different `_play_safe_step` shapes (s29n3, s29n3d, s29n3e), so the walk
  rule is not the cause. The likeliest cause is that the shadow is not yet in
  `QD.world.spotanims` when the decision is made, which is a visibility lag. But the decisions
  are not in any log, so this is not proved. Closing it needs the loop's per-tick decision (the
  shadows seen, the walk sent) written to the tick log, or a member's log. That is
  raid_log_raider_state's row.
- **The Normal run-by drains only when its splat is above 0** (raid seam32). One name in five
  (`_play_bloat`) rolled 0 and played the room undrained (still green). DRIVER_NOTES' recipe swings
  again in the down (age 1-6, not halved); the plan does not, because a second swing on the walk
  cost 26 ticks in the flies. Open: a down special by p1 when the run-by splat was 0.
- **Bloat Normal: 3-4 swings per raider per down, not five** (W:689). Open: start nearer the
  down (walk with him on his side of the hide) or leave later from the north/east side, where the
  stomp's reach from the south-west tile is one tick away.
- **Members count swings** from presses and the weapon speed. They have no tick log, and no
  client read gives the local player's own animation (api_drive.players has no anim field).
- **The six-tile flinch** is the hide tile, not the guide's five; the row asks for 3 or more.
