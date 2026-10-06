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

## Maiden, Normal trio (`tob_maiden`, mode `normal`), NOT green: she dies on 5 of 5 names, rows red (raid seam32, seam33 play_tob_maiden_normal_green, seam35m play_tob_maiden_normal_rows)

`seed_survey.py _play_maiden --party 3` (seam33 final plan, QUEST_BINARY with `::blowpipe`):
**0 of 5 green, and she DIES on 4 of 5**. On `_play_maiden`, `sva`, `svb` and `svd` the room is
cleared ("Wave ... (Normal Mode) complete!", `room.cleared` PASS) in 390-470 ticks, and no
raider dies. Only the technique rows are red: `tech.freeze` and `tech.crabs_killed` need at most
one leak per threshold; `svb` also fails `tech.tank` (the freezer took a storm). On `svc` the
leader dies at t406, after two pools (30 + 30) next to her and then a 35 storm at 19 hitpoints.
Seam32 had the same survey at 0 of 5 with 600-700 tick rooms, and 4 of 5 there died with
every supply spent. `party_repeat.py --script <harness + party = 3,> --name s33mrep2 --runs 3`:
AGREE (tick log sha 71740ae402e4, 532 boundaries, lockstep PASS each). The Entry solo plan
does not move: `seed_survey.py _play_maiden` is 5 of 5, and all five ledgers are
byte-identical to seam33's post-chinchompa baseline (`seam33/chin/pm_after`). Every line below
is under `m.R ~= nil` (a party).

| Mechanic | What the plan does | Source |
|---|---|---|
| Roles | Seat 1, the leader, is the TANK, on 6434,98, north of her middle and still the nearest raider. Seat 2 is the freezer on 6441,94. Seat 3 is the north ranger on 6437,100. Tiles are re-based on her own tile. | 10Boot 0:08:48 "If you're the range or melee role, camping on the north side ... gives the mage space to hit the south freezes and allows you to get the best access to the crab that doesn't get frozen"; 0:06:33 "The person closest to the boss becomes the tank" |
| THE OPENER | Every seat runs the same steps. The Dragon warhammer goes on in one block (`intent.gear`). The special is armed from the orb with the attack press the next tick (`intent.spec`, under Piety), and the swing is seen as varp300 falling by 500. A 0 splat is swung again while energy lasts (at most 2 swings), and then the bow goes back on. Measured in s33m1: t63-69, splats 0, 35, 33, 29, three drains (200 to 140 to 98 to 69). | 10Boot 0:06:33 "everyone should drop a dragon warhammer spec, then switch to range gear"; W:624 "Instantly hammer Maiden"; W:630 "Elder maul special attacks until two hit"; wiki_Dragon_warhammer.wikitext:59 "30% ... on successful hit" |
| Rapid, Rigour | The harness presses combat style slot 1 (varp43 = 1), so the bow fires every 5 ticks and the pipe every 2. Rigour is lit beside Protect from Magic outside the opener and the magic set (`down_prayers = { "piety", "rigour" }`, the library's managed list; the Entry plan never wants either). | wiki_Twisted_bow.wikitext:51; wiki_Toxic_blowpipe.wikitext:41, :108; 10Boot 0:02:45 "77 prayer for rigour, augury, and piety" |
| The pipe | The rangers (seats 1 and 3) put the loaded toxic blowpipe on in one block while Matomenos are up, and put the bow back on when none is left. They shoot crabs within the pipe's reach + 2 of their own tile (s33m2: a tank that chased a south crab died on a pool). A FROZEN crab may be up to 10 away once nothing walks. | 10Boot 0:08:14 "Everyone else should machine gun down the crabs that aren't in the clump with their blowpipe"; W:639 "DPS roles should kill the stray nylocas before getting back on Maiden" |
| Ranger order | North walkers first, then south walkers, nearest her first. Frozen crabs come last, longest-frozen first. The walker already being shot stays the target while it walks in reach. | 10Boot 0:08:48 (north camp, "the crab that doesn't get frozen"), 0:08:14 (the clump is the freezer's) |
| Freezer order | Every 5 ticks it barrages the crab with the most walkers at gap >= 4 in its 3x3. A walker beats a frozen anchor; ties go to the nearest her (arrival), then the south one. It never targets a walking N1 (the north spawn nearest her, ten tiles east of her spawn tile). | 10Boot 0:07:40-0:08:14 "freeze the crabs that spawn closest to Maiden first. Prioritizing the south side ... since those crabs reach the boss first"; 0:08:48 "this crab should not get frozen"; W:637 "N1, N2, and S1 cannot be clumped" |
| The halt | A weapon swap does not end an attack. The freezer's primed wand swap kept its bow attack on her, so it walked to her side and stood there 27 ticks (svdplaymaide t175-202), as the closest raider and out of barrage reach of the north spawns. On the swap tick it now steps one tile east. | (the log) |
| Leaks seen | The plan counts a crab that was at gap <= 1 with health on its bar and is gone the next tick (a kill shows an empty bar first). The storm a raider eats against is max(mode figure, own largest loss + 2, `floor(floor(36.5 + 3.5c)/2) + 2`). A party raider looks 6 ticks ahead for a launch (two doses), not the library's 2 ticks between swings: svdplaymaide's tank, out of anglerfish at 8 hitpoints, saw the t365 launch at t363 and died with 22 brew doses left. | W:590 "36.5 + (3.5 * c) ... halved by Protect from Magic" |
| Supplies | Each ranger carries 8 Saradomin brews, 6 super restores and 12 anglerfish, plus the hammer and the pipe. When one brew dose covers what the library asked of a bite, the ranger drinks the dose instead (no attack delay), and restores Ranged at 76 rather than 88. The freezer carries 13 anglerfish, 4 restores and 2 brews, and never takes a brew first (Magic must stay at 94). | 10Boot 0:04:23 "eight brews, four restores, and three anglers"; consume_shared.rs2:49 |
| Tank swap | Seat 1 steps out at 8 bites left (anglerfish + brew doses), on the tick after one of her launches. Seat 3 steps in on the first tick it sees seat 1 within 1 of the reserve corner. The swap walks are allowed during a wave. | 10Boot 0:06:33; W:589; s33m1 t406 (a mid-cycle step-out left the freezer the closest at the t411 launch) |

(e) The rangers' `timeout` on an add press, read: `QD.player.attack`'s `timeout` means "the row
WAS pressed and no hit showed inside the ONE tick the settle waits" (combat.lua's banner;
raid_play.lua `_play_press`). A dart or an arrow cannot land inside one tick, so seam32's
`timeout`s were presses that landed. The party's add press now goes through the library's
`_play_press`, which answers `pressed` and then `ok` when the hitsplat shows (seam33 leaders:
`presses [ok 60]`, effect lag +2..+9 ticks).

Measured (sv3, five names): rooms 390-470 ticks (seam32: 600-700). Damage taken by the party is
636-1076 (seam32: about 2000). Per threshold, 3-5 of 6 crabs are frozen and 1-3 reach her: own
1/3/2, sva 2/2/1, svb 2/2/3, svd 2/3/3.

Open (the first cause of each red leader, from its log):
- **Leaks above one a threshold** (all five; `tech.freeze`, `tech.crabs_killed`). Every
  threshold leaks N1, as the source says it will. The second and third leaks are the far 3s and
  4s (spawn x 6444/6448) that walk 12-16 ticks unfrozen. One freezer at one cast per 5 ticks
  (landing 2 ticks later) freezes 3-4. The darts land but reach only 50-60 of the 75 hitpoints
  before the crab arrives (svdplaymaide w2: N3 reached her with 21 left). The source's other
  answers are not in the plan: W:637/639 clumping the 3s and 4s "on top of each other in front of
  Maiden" by freezing the lead crab and barraging the anchor as the followers walk in (the
  anchor rule exists, but it counts only walkers already in the 3x3, not ones about to arrive),
  and W:649's chinchompas on the stack.
- **svc: the tank in blood next to her**. Its dodges ended on 6433,97 / 6432,96, beside her,
  where a throw cannot be out-walked ("players in melee distance will need to move before she
  attacks", W:595). Normal has `presteps = 0`.
- **svb `tech.tank`**: the freezer took storms on some ticks of the swap. Not read further.
- Library (raid_play.lua, not this seam's file): the pid base (seam32 note) stands, and the
  supplies horizon between swings is two ticks, too short for a hit settled at the launch. The
  Maiden plan widens its own threat function (`reach_h`).

### seam35m (camera lane): the freezer's plan, the arrival rectangle, NOT green

`seed_survey.py _play_maiden --party 3` (final code): **0 of 5 green, she DIES on 5 of 5**
(`room.cleared` PASS on `_play_maiden`, `sva`, `svb`, `svc`, `svd`, rooms 320-427 ticks; seam33: 4 of 5).
Red rows: `tech.freeze` on all five, `tech.crabs_killed` on four (green on `svb`: 3 reached, 1/0/2),
`tech.tank` on `svb` (the freezer took one storm), and `sva`'s north ranger (p3) dies. Entry solo
`seed_survey.py _play_maiden` stays 5 of 5. `party_repeat.py --script <harness + party = 3,> --name m35rep
--runs 3`: AGREE (sha ae77e7d8ff8d, 307 boundaries, lockstep PASS) -- but on that name the leader dies
at t306 to a 60 blackstorm (the unprotected size at c = 7, through a lit Protect from Magic: not read
further).

| Mechanic | What the plan does now | Source |
|---|---|---|
| Reached her | A crab is absorbed when its SW anchor stood, at the end of a tick, in the content's rectangle: two tiles out on her west and south faces, one on her north and east (a size-2 crab's footprint within 1 of hers), frozen or not. The plan (`_play_maiden_crab_in`) and the harness (`arrived_at`) both use it; seam33's 1x1 `gap <= 1` counted every south absorb at gap 2 as a kill. | tob.constant:480-494; tob_maiden.rs2 `[proc,tob_maiden_crab_tick]` |
| The walk | One tile a tick toward her footprint clamped to the crab's own tile: diagonal until level with her top or bottom row, then straight along it. Every unfrozen crab of the surveys matched it (N3 19,12 to 6,5 in 13 ticks; S3 19,-8 to 6,0 in 13). | tob_maiden.rs2 `[proc,tob_maiden_crab_goal]`; ET 2.3 |
| Ice Barrage | Freezes the target and every crab in its 3x3 on the tick it resolves; the crab does not step again. A re-cast on a frozen crab does NOT extend the freeze (S4 frozen t127, re-iced t132/t137, walked at t159). Held 32 ticks. | m35a/m35c tick logs |
| THE FREEZER'S PLAN | `_play_maiden_ice_plan`: every order of the next four barrages (each on any crab in its ten tiles, a frozen anchor included), simulated on the walk above; keeps the order that freezes the most before they reach her, then the one whose LEFT crabs walk longest (the DPS's time). A crab counts only if it is in the target's 3x3 on the tick the cast resolves -- measured per wave from the target's first still view (casts resolved one tick late through all of svc's wave 1, on time in waves 2-3) -- or, unmeasured, on both candidate ticks. | W:637 "the other spawns can all be frozen on top of each other in front of Maiden"; W:643 "Clumping 3s and 4s should be prioritised over freezing a single S2 or N2 spawn; if it is necessary to leave a single nylocas, the DPS roles should attack it" |
| Cadence over a dodge | The freezer casts on its 5-tick cadence even mid-dodge while a crab still walks (svb w2: a dodge slipped a cast from t211 to t212 and lost a pair). | (the log) |
| Refresh / clump | With nothing walking: a crab frozen 24+ ticks first, then the biggest clump, longest frozen among equals. | W:639 "barrage the clump until it is dead" |
| Rangers | Run the same plan on what they see (the freezer's tile, its cadence from the last freeze they saw) and shoot only what it LEAVES, nearest her first; never a walker it is about to freeze (unless at gap 2), then the frozen ones. | W:643; 10Boot 0:08:14 |
| Still | A crab is "frozen" after two views of consecutive ticks on one tile, never counting the view after its first (that first view already shows its first step). | (the log: svc w2's plan saw six spawns "frozen") |
| Pool threat | A party raider's eat threat counts the pool as 10 + 2c from its own leak count (sva's tank, c = 11, ate nothing at 41 and took 32 + 32). | W:597 |

Measured, the plan's own best at each wave's first cast (`p2:play.ice_plan`): 3, 4 or 5 of 6, and
6 of 6 only once. **`tech.freeze` (all but one frozen every threshold) is not reachable with one
freezer on a 5-tick cast for most spawn sets**: e.g. N1, N2, N4, S1, S2, S4 -- N1 arrives 6 ticks after
the spawn, S1 7, N2/S2 9, and N1/N2/S1 cannot be clumped (W:637), so at most two of those four are
frozen by the casts at +1 and +6, and the plan's best is 4. Changing the row is the orchestrator's
call (the source's own words are "if it is necessary to leave a single nylocas"); this seam did not
loosen it.

Open (first cause of each red name, from its log):
- **Leftovers cannot be killed in time.** A left crab reaches her 6-9 ticks after it spawns. The
  rangers' darts land 0-15 a hit on a Matomenos (`_play_maiden` w2: N1 8 and 0 then absorbed at 67;
  N2 four darts and no hitsplat), so two pipes take 8+ ticks for 75 hitpoints. Rangers keeping the bow
  (seam35m sv6) was no better (reached 5/9/6/6/7 against 6/7/3/7/7).
- **Content row (for the content pass, not this seam):** a Normal trio Matomenos has 75 hitpoints
  here (a fresh crab's absorb heals 150 = 2 x 75); W:593 says "200 Hitpoints (175 in 4-man and 150
  in trios and below)".
- `sva`: the north ranger (p3) died; `svb`: the freezer took one storm (tech.tank). Not read further.
- Preferring to leave NORTH crabs (10Boot 0:08:48, the rangers camp north) was tried (sv5) and was
  worse; reverted.

### Maiden, Normal trio, following the Blert reference (raid seam40)

Reference: `docs/minigames/theater_of_blood/sources/blert_api/reference/maiden_normal_3.json`
(24 death-free Normal scale-3 rooms). Compare with `raid_report.py RUN --against <that file>`.

| What the real trios do (reference) | What the plan does now |
|---|---|
| Both dps on her with the SCYTHE in every phase (melee_pct 88.9 / 89.3), dps1 at (5,6) (4,6) (3,6) and dps2 at (6,5) (6,4) (6,2) from her SW tile, dist_boss 1 | Seats 1 and 3 are `melee` scythe seats (`::maxmelee`, super combat at the door), homes (5,6) and (6,5), `side` north / east. They dodge along her edge and walk back to their side. The library's press goes through `_play_reach` while blood is down. |
| First attack: TWISTED_BOW at tick 5-6 from 8 tiles out, then a special, then the scythe from tick 16 | The tbow is worn at the door and shot on the run in. Then the hammer (scythe seats only, one swing, no retry), then the scythe on the tick the energy is spent. Room ticks 6 / 11 / 17. |
| Freezer: TWISTED_BOW + barrage from (15,-1)..(15,2); no hammer | The freezer opens with the bow (no opener) and is otherwise unchanged. |
| Storms split between the dps (boss_targeted_pct 38 / 41.45) | THE SHARED TANK: on every other attack, seat 1 steps one tile north (4 from her centre) on next_attack-1, so seat 3 takes the storm. Measured split 27-46 / 54-73 %. |
| dps attack 1-3 adds a phase, first at +4 ticks; none at 30 percent | The scythe seats take every NORTH walker within 12 of her edge, N1 first, and stay on it (sticky). Frozen ones and the south lanes are the freezer's. None in her last form. |
| no eats or drinks per phase (n=10 / 5) | The super combat is drunk again when Attack or Strength is under 108. Super restore under 90. Food before brews for a scythe seat (no brew-first). |

Tried and turned OFF (measured):
- **Freezer melee at 30 percent.** The reference shows the freezer with SCYTHE in 19/24 rooms. Here, the scythe after four barrages made 30 percent leaks 21 / 9 / 7 (3 / 3 / 2 without it) and the room 338 / 305 / 349. `freezer_melee30 = nil` keeps the code and the reason.
- **Freezer planning over the south lanes only.** The north trio then walked in unfrozen (m40j). The option `P.south_first` is unset.
- **Scythe seats killing blood spawns within 3 tiles.** This produced 2 deaths in 3 names (survey11). Reverted.

Entry solo: the reference is ONE room (`maiden_entry_1.json`, 82 ticks, all scythe). The solo now puts the scythe on (`::fullscythe`, one shark fewer) once its kept technique rows are measured: `prove_protect`, `presteps` (or prestep_seen) and `flicks` (or drain_seen). That happens at room tick 117-146. Survey 5/5 KEEP, room 219-236 (was 223-268). The kept rows set the switch tick.

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

## Nylocas, Normal trio (`tob_nylocas`, mode `normal`), NOT KEPT under the sourced bar (seam35m): she dies, nobody dies, the supports do not hold (raid seam32 play_tob_nylocas_normal, seam33 play_tob_nylocas_normal_green, seam35m play_tob_nylocas_normal_supports)

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
| p1 (seam33) | An Eye of Ayak (powered, 3 ticks, reach 6, seq 12397), given uncharged and charged in run() by its Charge op with demon tears (setup.ayak); no freeze. Starts with it on. | W:719 "Mages should use an eye of ayak, as its 3 tick speed ... Barrages should only be used when the fight becomes hectic"; wiki Eye of Ayak :22, :69-70 |
| p2, p3 (seam33) | The helpers' blues: p2 an Eye of Ayak, p3 a Sanguinesti staff (powered, 4 ticks, reach 7, seq 1167, charged with blood runes: setup.sang) in place of the battlestaff's Ice Rush; a powered-staff seat takes blues like any colour. p3 starts with the whip on. | trio guide :216, :251-255, :289, :334 (the ranger Ayaks mage bigs), :386-387, :434, :453 (the meleer Sangs); wiki Powered staff :6, :10 |
| seats (seam33) | OWN COLOUR FIRST as a rule (`own_first`): another seat's colour only while none of the seat's own can be pressed; aggros stay every seat's. | W:706 "Each player should be assigned a style"; W:746 "if your assigned nylocas are not currently near or in the room, switch weapons"; W:733 |
| swaps (seam33) | THE SWAP, TIMED (`swap_timed`, a party only): a swap and its press go out only when the old weapon's next swing is two or more ticks off while it is engaged on a copy that still stands. | W:731-733 a wrong-style hit nulls the raider on the copy; seam32's "a swap keeps swinging at the old copy" |

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

**Seam33 (play_tob_nylocas_normal_green): the powered staves and the rules above, measured.**
The KEPT bar is now every support standing and above half its bar when she lands
(`tech.pillars_at_boss`, 4 of 4, each > 0.50). `seed_survey.py _play_nylocas --party 3
--names 7` on the final plan: 0 of 7 under that row. She died and nobody died on all seven;
under seam32's ">= 1 standing" row it would be 5 of 7 (`tech.prayer` red on two). The two
names that failed seam32 (sve, svf) now survive, and svf has all four standing.
`party_repeat.py` on the trio copy (s33nyrepG): 3 runs AGREE, sha e8bf33f48acb, 788
boundaries. The Entry solo survey: 5 of 5, ledgers byte-identical.

| name | stall (last wave vs 236) | standing at her landing | bars |
|---|---|---|---|
| _play_nylocas | 99 | 1 | 36,29:0.21 |
| svaplaynyloc | 83 | 2 | 36,29:0.17 36,18:0.05 |
| svbplaynyloc | 91 | 1 | 36,29:0.19 |
| svcplaynyloc | 103 | 2 | 36,29:0.33 25,29:0.04 |
| svdplaynyloc | 95 | 1 | 36,29:0.12 |
| sveplaynyloc | 111 | 1 | 36,29:0.30 |
| svfplaynyloc | 83 | 4 | 0.28 0.15 0.16 0.03 |

The step by step, on one seed (s33nyseedone*; nylo/nyan.py over the leader's tick log):
- seam32 plan: 129 of 206 copies killed, 119 ticks of stall, a raider died.
- (1) Ayak mage + (2) own colour first + own weapon at the start: 163 killed, stall 115.
- (3) the timed swap: 172 killed, stall 99, wrong-style wave swings 11 -> 3 of the matched
  swings, 3 of 4 standing (0.20 / 0.07 / 0.03).
- (6) helper staves: ranger swings 118 -> 131-142 in the waves (27-29 of them Ayak).

Tried and NOT kept, each measured on two seeds (behind a flag or reverted):
- `big_cost` 8 ("prioritising the smaller ones first", W:746): stall 87 / 115.
- `own_wait` (the meleer waits for and meets its tunnel greys): identical on one seed, no gain
  on the other.
- `burst_clump` 3 (the Ayak mage bursts a pure-blue 3x3): only 3 casts a room.

**Why it is red (the first cause, every red leader).** The trio does not kill fast enough.
- Stall: 83-111 ticks before wave 31, where real trios have none before wave 28 (trio guide
  :21-24).
- Unkilled copies: 34-44 of 206 per room, mostly greys (16-24 of 63-69) and blues (12-21 of
  75-80). They chew for their whole 51 ticks and then pop, which is about half of the
  supports' damage.
- Late surge: 23-30 copies are alive at t350-450 (the cap is 24 from wave 20). The supports
  lose 170-270 hp per 50 ticks there, and the first support falls at t434-484.
- The meleer is the bottleneck. It swings 66-77 times in about 390 wave ticks (one every 5-6
  ticks on a 4-tick whip) and loses 79-113 ticks to gaps over 4 ticks, walking between the
  pillars' grey clumps.
- Grey and blue hits read about 20 percent zero. With stat1 (Defence) 200 on every copy
  (cache_npc_nylocas.txt), the Entry void set is short of accuracy.

The trio guide's own levers are the next rows:
- chinchompas on green and grey doubles (seam33 chinchompa_multi_target landed the splash);
- a scythe on grey doubles and bigs (trio guide :232-239, :415);
- the 3-tick swift blade (:386);
- the per-wave lane assignment (blert_nylo_pillar_assignment.json).

`tech.prayer`'s "blocks >= 20" was written for Entry solo. In a party the leader is hit less
(11-18 blocks against 1-12 that landed on svb/svd/sve, in the survey of the plan before the
helper staves), so it reads red on a plan that prays
right. It needs a party form (blocks against landed) from the owner, not a lower number.

**Seam35m (play_tob_nylocas_normal_supports): the bar from the sources, then the plan measured
against it.** Seam33's "each above 0.50" was never sourced. Real trios were read for it:
34 completed Regular trio Nylocas rooms on blert (30 harvested with
`tools/measure_tob_pillar_damage.py harvest 3` -- mode 11, scale 3 -- and the four trio
streams of the spec pass; uuids in build/seam_state/matthew-mbp-m4-camera-b1-seam35m/nylo/
trio_blert_uuids.txt), that tool's own bite model (a copy cardinally adjacent to a 2x2
support and not moving, first bite on its third parked tick, then every 3) counted up to
Vasilias' first event, 0.90 a bite (ENCOUNTER_TIMING 4.5, the ceiling the collapses give)
over 230 hitpoints (380 - 50 x 3, the 21 June 2018 newspost):

| measure, 34 real trio rooms | value |
|---|---|
| all four standing at her landing | 34 of 34 |
| the weakest support at her landing | median 0.31, range 0.10..0.54 |
| rooms with every support above 0.50 | 3 of 34 at 0.90 a bite, 0 of 34 at 1.0 |
| one support, all 136 | median 0.44, tenth percentile 0.23 |
| the combined bar | median 0.43, range 0.22..0.63 |
| her landing, ticks from the room start | median 304, range 294..353 |
| wave 31 out | median 257.5, range 245..286 (spec 236: stall 9..50) |
| copies that live 50 ticks (auto-pop) | median 23.5 a room, range 11..30, of 206 |

So `tech.pillars_at_boss` now asks **four standing, the weakest at or above 0.10** (the
weakest real trio), citing the table. Under it the plan is still red: the final survey
(seed_survey --party 3, the seam33 plan unchanged) stood 1, 2, 1, 2, 1 of 4 at her landing,
stalls 99/83/91/103/95 and landings 396-424 ticks from the mark, nobody died, she died on
all five (byte-for-byte seam33's table). party_repeat on the trio copy (s35nyrep): 3 runs
AGREE, sha 255d93f3f4c8, 839 boundaries.

**Where ours and real trios part** (s35ny0, the leader's tick log): 920 support bites in the
room against a real median of about 580; her landing about 110 ticks later than a real
trio; the late surge (25-28 live copies at t375-425, 385 support damage in those 100 ticks).
The meleer is the narrowest seat: 66 swings in about 400 wave ticks on a 4-tick whip, 123
ticks lost to gaps over 4 while it runs between greys at all four supports (the trace:
picks at d3-d8). Real trios clear the same 206 copies at about 0.70 kills a tick against our
0.42, with tools this plan does not have: chinchompas on doubles (trio guide :211-226,
:300-307, :352-359), the scythe on doubles and bigs (:415, :460-476), a 3-tick Swift blade
(:386, :420), claws (:393).

Tried and NOT kept: `keep_all` (a trio never lets the low support go: the Entry guide's
"let one that's low die", E :171, is a solo's rule): survey1 stood 3, 2, 1, 2, 2 against
2, 2, 2, 2, 2 on the same names -- the supports fall for the copies not killed, not for
being let go. Left OFF in the plan behind the flag.

Next levers, in order: the ranger's chins on pure-green doubles (the chin is multi-target
since seam33; the plan's loadout is one weapon a style, so a chin needs its own weapon key
beside the blowpipe), the meleer's scythe on grey doubles (CONTENT: scythe_of_vitur.rs2:38
implements the arc only on 2x2 targets), a 3-tick melee weapon (Swift blade: no bonus params
in all.obj).

### Nylocas follows Blert (seam40, 2026-10-06)

Reference: `docs/minigames/theater_of_blood/sources/blert_api/reference/nylocas_normal_3.json`
(blert_reference.py nylocas --mode normal --scale 3 --rooms 30: 27 death-free rooms of 31).
Room 412 [372-472]; boss phase starts 308 [296-357]; boss phase 95 ticks [75-123];
wave 31 at 260 [244-293]; kill cadence 0.71 kills/tick [0.64-0.75]; small death age median 15.

What the trio plan now copies from Blert (recorder equipmentDeltas, prayerSet, levels):
- Weapon per seat and target colour (QD.raid._play_nylocas_key -> `<style>`, `<style>_big`, `<style>_boss`):
  mage blues Ayak / greens blowpipe / greys scythe; ranger greens blowpipe / blues Ayak / greys scythe;
  meleer greys whip (Sulphur blades are not in content) with scythe on bigs and on Vasilias, blues Ayak (no Sang), greens blowpipe.
  On Vasilias: melee form scythe (all three), magic form Ayak (all), ranged form tbow/blowpipe.
- Gear: meleer ::maxmelee; ranger and mage void with the RANGE helm, rupture / occult.
- Offensive prayer per seat colour in the waves (mage augury, meleer piety, ranger rigour), by form on her.
  Per-swing switching was tried (ny40e) and cost swings: dropped.
- Boosts drunk at the door: 4dose2combat + 4doserangerspotion (+ magic potion for the mage); t.ticks(3) between drinks.
- Lanes: every Blert role's median tile is the centre (31-32,24); ours match.

Measured (party 3, seed_survey --names 5): BEFORE boss phase ~240, pillars standing 1-2 of 4;
AFTER boss phase 143-188, standing 3,3,3,1,2 of 4, w31 299-335, landing 380-404. 0 of 5 green against
the reference (boss phase still 1.5-2x Blert's 95). Solo Entry _play_nylocas 5 of 5 green; party_repeat 3 runs AGREE.

Where the gap is (diagnosis scripts under build/seam_state/matthew-mbp-m4-raid-b1-seam40/ny40/):
- Waves 1-10 land on Blert's ticks exactly; from wave 11 the content's alive cap (12) stalls us
  (w12 +11, w17 +31, w20 +47, w31 +67) while Blert trios run 14 alive after a spawn with no stall until w28.
- Attacks per killed small: Blert 1.13 (71% one attack), ours 1.34-1.40. Greys are the gap:
  small grey death age 23 vs 16, popped 15.8/room vs 9.9, big greys popped 4.6 vs 0.3.
- Her phase: 12.5 hp/tick vs 20.3; 24.7 a swing vs ~34.
- One traced run (ny40j, P.trace_seat = 3, restored to nil): the meleer whipped one grey 4 times for 0
  (t170-183) while it chewed pillar 36,29 -- nulled or blocked, unresolved.

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

## Sotetseg, Normal trio (`tob_sotetseg`, mode `normal`), proved (raid seam33 play_tob_sotetseg_normal)

Plan: `QD.raid._play_sotetseg_trio` (the decide dispatches to it when `st.party > 1`; the solo
Entry decide is unchanged), `QD.raid._play_sotetseg_follow` (the raiders reading the glow),
`QD.raid._play_sotetseg_seat`, and two party lines in the runner's `QD.raid._play_sotetseg_maze`.
Harness: `test/raids/_play_sotetseg.lua` with `--party 3` (`trio_kit` / `trio_run`; solo is as
before). Every raider swings the scythe. T = transcripts/yt_4i4lv-srJkw.md, K =
transcripts/yt_KF9y2GYTJ-A.md.

| Who / when | Intent | Source |
|---|---|---|
| all, seats | p1 east (his east face, middle), p2 west (south end of the west face), p3 north-west (the west face's z+3: z+4 is not standable, s33soa). Each seat shares an edge with him and is 3+ tiles from his centre and from the other seats, so no ball or ricochet flies under 2 ticks (S tob_sote_ricochet: 5 + 36 + 8 per tile cycles) | W:791 "In a trio encounter, players will stand to the east, west and north-west respectively" |
| all, prayer | Protect from Magic from the barrier; Protect from Melee in his range; the ball's colour from the tick a red/grey shot is seen with its dst within a tile of the raider until it has landed (a homing dst lags a walking raider a tick) | K:151 "to start the room pray magic and piety"; E:191 (the Entry switch); the owner's ruling (ball read at impact) |
| all, death ball | On a 1604 in the air, from 5 ticks before it lands to the tick after, everyone stands on the tile in front of his south face; the share is S tob_sote_ball_impact's radius 1 | W:794 "This damage can be split among other players"; T:95 "all gather together on the tile directly in front"; K:151 "group up at the center tile in front of the boss" |
| runner (the room picks: S tob_sote_send_party hunts and sends the first; p1 on every name) | The Entry runner, with no row-3 wait (no tornado for the runner in a party) and the last tile held 6 ticks so the others are on the arena grid before the 4-tick check | W:803 "This tornado will not appear for the maze runner (unless they are the only player)"; S tob_sote_runner_tick; S tob_sote_grid_occupied (both grids) |
| the other two | Read the ONE lit tile on the arena grid each tick (S tob_sote_mirror), join two glows on a straight run, wait off the grid's south edge until the glow reaches the last row, check the maze's shape (even row one tile, odd row a run), then walk it corner to corner and step off north. A path that fails the check is never walked (the maze still ends when the runner leaves) | W:799 "forcibly teleported to the other end"; W:801 "a red glow"; T:97 "Everyone else just needs to follow the path ... only use straight line movement"; W:803 the wrong tile's blast |
| kit | `::maxmelee`, prayer 99, Agility 99, 16 anglerfish, 3 restores, 1 super combat, 4 brews | K:114 anglerfish; T:93-95 eat/brew; the Bloat trio's run energy finding |

**Measured, `seed_survey.py _play_sotetseg --party 3` 5 of 5 green (twice, byte-identical
measures), `party_repeat.py` on a `party = 3` copy 3 runs AGREE** (tick log sha ff3749ba10c8,
466 boundaries). Room ticks from the mark; pids as the log counts them (p0 = the leader).

| Leader name | Room ticks | Taken p0 / p1 / p2 | Maze 1 (proc-react) | Maze 2 | Death balls: share each (all 3 stacked) | His melee | Balls blocked |
|---|---|---|---|---|---|---|---|
| _play_sotetseg | 428 | 291 / 219 / 263 | 44 | 41 | 37, 1, (t456 after his death) | 33 | 78 |
| svaplaysotet | 419 | 153 / 181 / 247 | 42 | 53 | 7 (one more nulled by a maze, W:799) | 31 | 83 |
| svbplaysotet | 424 | 216 / 211 / 339 | 43 | 46 | 25, 18 | 37 | 68 |
| svcplaysotet | 397 | 293 / 248 / 305 | 44 | 40 | 9, 4 | 31 | 67 |
| svdplaysotet | 438 | 256 / 285 / 267 | 47 | 47 | 18, 15 | 38 | 71 |

Every maze: one raider in the realm (p0), the other two on the arena grid 16-23 ticks each, 0 off
the path, 0 gaps in the glow, 0 hits over 3 while a maze was on. No deaths. raid_report: 69-85
mistakes a name (food 30-36: eats over 90 hitpoints while the disabled-prayer threat counts 95;
prayer 39-51, of which about 18 are melee through Protect from Melee, W:787's 22, and 6-10 are
ricochets or melee taken while switching: t37-39 on the walk in, t165 the runner's walk back).

**Not done (tried, red).** The Defence drain (W:781-784 "1-2, 1-3, 2-3"; T:91 the trio's order;
K:149 "your Dragon warhammers") with the Bloat run-by's machine: s33sob, all three died (p3 t500,
p2 t555, p1 t600), boss alive. First causes from the log: p3's special never spent energy
(gave up phases 2 and 3: the hammer stayed worn), p2's attack press `not_visible` from t350, and
damage taken 565 / 424 / 448 against about 250. Reverted; the code is kept at
build/seam_state/matthew-mbp-m4-raid-b1-seam33/sote/raid_play_tob_sotetseg.hammer_red.lua.

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

## Xarpus, Normal trio (`tob_xarpus`, mode `normal`), proved (raid seam34x play_tob_xarpus_normal)

Harness: `test/raids/_play_xarpus.lua` under `--party 3` (`QD_PARTY.size` picks the trio's kit,
entry and rows; the solo run is unchanged). Plan: `raid_play_tob_xarpus.lua`
(`QD.raid._xarpus_p1_trio`, `QD.raid._xarpus_p2_trio`; phase 3 is the Entry plan's, per raider).
`seed_survey.py _play_xarpus --party 3`: 5 of 5. Repeat: `party_repeat.py` has no `--party`, so
the proof ran on a copy with `party = 3,` added (`build/xn34_repeat/_play_xarpus_trio.lua`,
`--name _play_xarpus --runs 3`): AGREE, tick log sha e94bf891a445, 360 boundaries.
`seed_survey.py _play_xarpus` (Entry solo): still 5 of 5.

| Who / when | Intent | Source |
|---|---|---|
| kit | `::maxmelee` (scythe), prayer 99, Agility 99, 16 anglerfish, 3 restores, 2 super combats, a Dragon warhammer each; no brews | W:842 "if a scythe is in the player's possession"; W:839 "at least two successful hammer/maul specials"; _play_bloat.lua's party kit |
| entry | p1 starts the fight at the barrier; p2/p3 cross after `started` | _play_bloat.lua's entry |
| phase 1 | The k-th exhumed to rise is raider ((k-1) mod 3)+1's (two on one tick: by tile), so every raider has 24 ticks between its own and two never run for one; between them each waits on its own melee tile of the 5x5 he stands up into (34,32 / 31,35 / 37,35 local) | W:831 "stand on top of them until they return to the ground ... stand in the centre of the arena to quickly intercept"; W:829 "12 exhumed in trios"; X exhumed_count.normal 12, spawn_gap.normal 8, open_ticks 11 |
| phase 2, the stack | All three stand on ONE tile and move as one: every raider runs the same rule on the same view (ties by tile), so whoever he aims at, the spit, both chains and the next spit land on the stack's step-back tile: one puddle a spit, never on a melee tile | W:836 (the first splatters to the next player, the rest to the next two); W:844 "If on the correct timing, no poison will splatter next to the boss ... especially the marked tiles"; our server: S ~tob_xarpus_land_splat / ~tob_xarpus_chain_to |
| phase 2, the step back | Two ticks long, the ends of S+2 and S+3 = S'-1 (the chains are aimed at the first, the next spit at the second), on a tile three out from his footprint (two from every melee tile) and two from the last step-back tile; a tile ringed by old puddles scores worse, so the stack walks round him; the press on S'-1 runs back in and swings | A:203 "you step back every 4 ticks"; A:211 "you swing while running in"; measured (xn34c): the spit lands S+3, the chains are thrown on S+3 at the end-of-S+2 tiles and land 2-3 ticks later |
| phase 2, specials | The hammer goes on with a step back, its special rides the press back in, the scythe comes back with the next step back; two per raider (the first two cycles) | W:831 "All players should have their defence-draining weapon equipped"; W:839 |
| phase 2, a press that did not land | (a click "covered" by the stack's own models) the raider presses again on the next tick from the step-back tile, so the server's run in puts it back on the stack's tile | measured xn34g t161 |
| phase 3 | The Entry plan per raider: swing only from a quadrant he does not face, inside the turn window | W:851-853; A:225-227 |

**What was wrong before (read from the logs, never replayed).**
- `xn34b` (each raider on its own side, the Entry dodge): pools on melee tiles; a press with a bite in
  the same tick took two ticks, so the pattern slipped; all died by t191.
- `xn34d`/`xn34e` (own sides, two-tick step back): three puddles a spit (the target's and both chains'),
  raiders stood on ring puddles (no step-off in melee) and died t212-228. Phase 1 was already 12 of 12.
- `xn34f` (the stack): held together, but its step-back tiles were two out (one from the melee tile the
  press runs back to), so every chain splashed the stack in melee (about 22 a cycle); died t357.
- `xn34g`/`xn34h`: first kills. One press "covered" left p3 a tick behind and it walked to a different
  melee tile (25 ticks apart); the S row and W column of three-out tiles filled and the fallback put two
  puddles on melee tiles.

**Measured, five names, leader = p1 (pid 0..2 are the raiders):**

| Leader name | Kill (ticks from the mark) | Screech | Taken pid0 / pid1 / pid2 (hits) | Swings | Ring puddles | Stacked (apart) | Retaliations | p1 food | p1 inputs/tick (1 / 2 / 3+) |
|---|---|---|---|---|---|---|---|---|---|
| _play_xarpus | t326 (304) | t280 | 148 (15) / 168 (18) / 167 (18) | 27/27/27 | 0 of 36 | 123 (10) | 0 | 6 | 112 / 13 / 0 |
| svaplayxarpu | t312 (290) | t272 | 140 (14) / 149 (16) / 148 (16) | 26/26/27 | 0 of 32 | 123 (2) | 0 | 5 | 102 / 12 / 0 |
| svbplayxarpu | t312 (290) | t272 | 140 (14) / 149 (16) / 148 (16) | 26/26/27 | 0 of 32 | 123 (2) | 0 | 5 | 102 / 12 / 0 |
| svcplayxarpu | t291 (269) | t256 | 100 (10) / 109 (12) / 108 (12) | 22/23/23 | 0 of 28 | 109 (0) | 0 | 4 | 94 / 11 / 0 |
| svdplayxarpu | t320 (298) | t280 | 148 (15) / 168 (18) / 167 (18) | 26/26/26 | 0 of 36 | 123 (10) | 0 | 6 | 107 / 13 / 0 |

Every name: 12 of 12 exhumed stood on, 17 heal orbs (all before the owner arrived, +2 to +5 ticks after
the rise), healed 204; stand-up U t139; dealt 3016 in 203-238 hits (18-34 zeros); p1's specials at t145
(energy 1000) and t153 (500); no death. Damage taken is all poison (hitsplat 28): splashes and the
puddles a run crosses; none after the screech is 30 or more.

**Not done.** Phase 1's heal orbs: the owner arrives 2-5 ticks after the rise (X heal_delay 3), so
17 orbs (204) land; a nearer-raider split would cut that, at the price of a coordination rule the
raiders cannot check. The specials' drain is not read back (no Defence row). `party_repeat.py`
gains a `--party` (or `_play_xarpus` a trio id) before the repeat can run on the harness itself.

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

## Verzik, Normal trio (`tob_verzik`, mode `normal`), proved (raid seam34v play_tob_verzik_normal)

The harness is `test/raids/_play_verzik.lua`'s party branch: a run with `--party 3` (`seed_survey.py
_play_verzik --party 3`) plays Normal, three raiders; without it the file is the Entry solo, unchanged
(no `party` field, so run.py does not make every run of the id a party run). `party_repeat.py` reads
only a declared party, so the repeat gate runs a copy with `party = 3,` added (`--script`).

The Dawnbringer: one per raid, from the skeleton after Xarpus (tob_xarpus.rs2
`[proc,tob_dawnbringer_take]`). The room before Verzik is not played, so p1 is handed the one copy
(`::give`, p1 only), as that search would. p2 and p3 keep one backpack slot free for it.

| Who / when | Intent | Source |
|---|---|---|
| kit | `::maxrange` (twisted bow, Masori) plus insulated boots; carried: the scythe (`::fullscythe`, wielded at the door), a charged serpentine helm, 4 brews, 3 restores, a ranging potion, 16 anglerfish | W:891 "Scythe of vitur"; W:881; W:923 venom; V p2_zap_max (25 with the boots) |
| all, P1 | Protect from Magic and Piety. Scythe until the bolt; on the cover tile by the tick before the launch; out on the launch tick | W:871; W:883; V p1_cadence 14, p1_launch 3 |
| all, P1 cover | The pillar shadow nearest her (the content's boxes, `~tob_verzik_behind_pillar`). A pillar with 60 or less left (its lowest bar seen, remembered) is hidden behind only from 3+ from its centre, outside its fall, and scores worse; ties go to the west pillar, so the trio hides together | W:877, W:885; `^tob_verzik_pillar_collapse_range` 2 |
| all, P1 past the near row | A shadow more than 7 from her is no cover: the bolts are tanked under Protect from Magic | W:887 "even tank her attacks entirely" |
| p1, p2, p3, P1 | THE DAWNBRINGER SHARED: the holder wields it, arms the special from the orb with the press, and sees each special as the 350 energy it spends. With two spent it puts the scythe back on and drops the sword on the cover tile. p(r) takes it the (r-1)th time it lies on the floor | W:875 "drop the Dawnbringer for the next player (in orb order)"; W:887 "pillar drop"; the owner, 2026-10-05; special_attack.obj sa_energy 350 |
| all, P2 | Rigour + Missiles, Magic for the reds; p1 two out west, p2 east, p3 south of her body | W:901; W:904 "Trios: one goes south, one east, and one west" |
| all, P2 reds | The Entry rule is kept: a red is shot only in her 10-tick summon animation | vzn2: shooting every red (150 each, a fresh pair every 36 ticks) put 528 hits into 30 reds and P2 never ended |
| all, P3 | Each raider takes the r-th yellow pool (x, then z), deduplicated (each pool's graphic is listed three times) and locked for the charge. No press while it charges, and the bow is stopped on the pool | W:968; W:968 "Verzik is invulnerable while charging" |
| all, P3 | A web on a teammate's tile is shot first. A raider beside another steps apart, and a raider off the floor steps back on | W:955; W:975 the ball bounces to a neighbour |
| all, P3 threats | Her melee 63 within 2; a nylocas 63 within 4 (`^tob_verzik_p2_nylo_blast_near`), outside the enrage band | W:942; tob.constant |
| all, enrage | POWER THROUGH: keep shooting at the 45 band; run from a tornado only with the green ball in the air or above the band + 15; with yellows charging, step on the pool at the last moment | W:983 "keep their health low so the tornado heals little"; W:981 |

**What was wrong before (read from the logs, never replayed).**
- `vzn1`: P1 threat 0, so no raider ate. Two died to a pillar's collapse while walking out (npc 8377, t119/t175).
- `vzn2`: the Dawnbringer was shared, but P2 never ended (all shots went into reds).
- `vzn3`: the trio stood on ONE yellow pool (6430,93) and took the blast.
- `sv3a`/`sv3b`: a fading health bar read as a whole pillar. The trio split between the two near pillars and both fell on them (t119).
- `sv3c`: the bow's repeat swing pathed the leader off its pool the tick before the blast (t578).
- `sv3l`: the leader stood off the floor (6421,84) and pressed Attack for 20+ ticks with no swing.
- Tried and reverted, all worse (0-1 of 3): dead-reckoning only one's "own" tornado (16-22 touches a room), P3 region homes, an open-ground rule, and a run judged on two tornado steps.

**Measured, `seed_survey.py _play_verzik --party 3` 5 of 5 green; `party_repeat.py --script
<declared copy> --name vzrepeat --runs 3` AGREE** (tick log sha 4f1562e0507b, 798 boundaries). Entry
solo `seed_survey.py _play_verzik` 5 of 5 green: svd's tick log is byte-identical to HEAD's. Leader =
pid 0.

| Leader name | Room (P1 / P2 / P3) | Taken pid0 / pid1 / pid2 | Bolts on pillars / at raiders | Tornado touches | Food p0/p1/p2 |
|---|---|---|---|---|---|
| _play_verzik | 660 (128 / 264 / 251) | 464 / 428 / 450 | 10 / 2 | 8 (218) | 16/16/16 |
| svaplayverzi | 599 (136 / 258 / 188) | 256 / 323 / 426 | 12 / 3 | 5 (123) | 10/14/17 |
| svbplayverzi | 679 (157 / 322 / 183) | 276 / 387 / 382 | 14 / 4 | 4 (103) | 8/14/13 |
| svcplayverzi | 567 (124 / 238 / 188) | 271 / 311 / 322 | 10 / 2 | 7 (180) | 11/12/12 |
| svdplayverzi | 702 (136 / 280 / 269) | 466 / 426 / 608 | 12 / 3 | 12 (326) | 17/17/17 |

The Dawnbringer: 6 specials on every name, two per raider (energy spent at t43/t50, t72/t76,
t102/t110), 671-758 of P1's ~1,530. Deaths 0 (`::tobjail deaths=0`). Bombs on a raider's held tile:
2-3 per member per room, against 78-90 thrown. Her P2 body never hit anyone for more than 30 (no
slam, no stomp). The green ball was tanked, never split (W:975 "most teams will opt to simply take
the hit"); 0-1 per room.

**Not done.** The supplies margin is thin: on `_play_verzik` and svd every raider ate all 16 fish.
P1 still costs 124-157 ticks against the wiki's "before the second pillar collapses" (W:885).
Members do not see their own swings, so a member's out-window can still waste a swing. The client's
tornado row standing still is the Entry section's open row (1).
## The whole raid, Entry solo (`_play_entry`), proved (raid seam35e play_tob_entry_relay)

`test/raids/_play_entry.lua` plays the Theatre end to end as a player does. It walks to the
notice board in Ver Sinhaza, forms an Entry party of one (`t.party.form`) and takes the door's
ready check (`t.party.ready`). It never calls `t.raid.enter`. Each room is its own harness's
pre-fight (the barrier, the prayer, the loadout, re-based on the room square it arrives in)
and then ONE `t.raid.play`. After each room it turns the prayers off and walks out: the
cleared barrier is a gate, and the passage is `tob_dungeon_walkway_exit_clickbox`
(`tob_dungeon_xarpus_arena_door_exit` at Xarpus). It takes the supply chest after Bloat and
after Sotetseg, the Dawnbringer from the skeleton after Xarpus, Verzik's trapdoor into the
reward room, and that room's chest.

| Line | Rule | Source |
|---|---|---|
| Kit | Gear for all three styles, Ancient Magicks, a venom source, at least 6 brews and a restore per three brews, the rest food. The weapons and switch sets are the ones each plan names. ONE ranged armour set (the Maiden plan's `ranged_set`) is worn throughout. Insulated boots (`slayer_boots`) are worn from the start. Supplies are 6 brews and 6 restores, no fish. | E:24-31, E:94; raid_play_tob_<room>.lua loadouts |
| Six restores, not two | This content's bandages restore no prayer (CONTENT_BUGS "From seam13": open, the item page gives no figure), so prayer is the budget. Over the first four rooms the run drank 16 restore doses: Maiden 7, Bloat 1, Nylocas 6, Sotetseg 2. | measured, the relay's fourth run |
| Spent switches dropped | After Maiden: the magic set, the arcane and the blood runes. After the Nylocas: the shortbow, the staff, the whip and the chaos, water and death runes. The chest gives only as many bandages as fit: 4 with nothing dropped, 6 after Bloat with the drop, 10 after Sotetseg. | E:151 "always contain 10 bandages"; measured |
| Super combat | One dose before Bloat, one before Sotetseg, one before Xarpus. | E:33 |
| Prayers off after each kill | A plan lights what its room needs and never turns it off. Bloat's Piety drained through the corridor and the whole Nylocas (76 at the mark, 0 by t950), and Vasilias was fought unprayed. | the relay's second run |
| Way out | Maiden: her entry barrier, then the passage. Bloat: the west barrier at local x 23 (pressed only while still east of it: a second press stepped one name back into the arena), the corridor, the chest. Nylocas: back through her entry barrier, then up the walkway to (38,51); from the platform a support covers the clickbox. Sotetseg: barrier, chest, passage. Xarpus: the north gate (34,43), the skeleton, the door. A covered click falls back to walking into the passage, which the content treats as the same thing (tob_raid.rs2 `~tob_exit_walked`). | tob_party.rs2 "The way out"; tob_raid.rs2 |

**Measured** (`seed_survey.py _play_entry --names 5`: 5 of 5; the KEPT survey, `--names 3`:
3 of 3). Per room, own name / sva / svb: ticks from the barrier to the death; damage taken; what
was used (brew and restore doses, bandages).

| Room | Ticks | Taken | Used |
|---|---|---|---|
| Maiden | 241 / 263 / 280 | 180 / 191 / 221 | 8-9 brew, 5-7 restore |
| Bloat | 190 / 188 / 187 | 106 / 65 / 81 | 2-4 brew, 1 restore, 1 combat; chest 6 |
| Nylocas | 670 / 721 / 708 | 168 / 193 / 180 | 6 bandages, 4-6 brew, 5-6 restore |
| Sotetseg | 208 / 239 / 228 | 59 / 94 / 84 | 0-1 brew, 2-3 restore, 1 combat; chest 10 |
| Xarpus | 230 / 224 / 176 | 162 / 168 / 17 | 0-5 bandages, 0-1 restore, 1 combat |
| Verzik | 428 / 272 / 376 | 258 / 98 / 272 | 2-8 bandages, 1-6 brew, 4-6 restore |
| Raid | 2291 / 2253 / 2304 ticks, lobby door to Verzik's death | | left: 0-8 brew doses, 1-5 restore doses, 0-3 bandages |

The in-game line reads "Theatre of Blood completion time: 19:26-20:02". The Entry Mode page
gives no typical time. The own name ends with 0 brew doses and 2 restore doses left, so the
margin is thin.

**What failed inside the relay and no room test showed.** Each was fixed where it lives:

1. **Maiden's solo plan used absolute tiles** (raid_play_tob_maiden.lua). The floor and home
   were written in the room test's instance (6426,92). The door builds her in the next free
   instance (6426,156), so every dodge tile was 64 rows away and the raider died in her blood
   at t158. The solo plan now re-bases on her body tile, as a party already did. The offset is
   0 in the room test.
2. **Bandages were not food to the library** (raid_play.lua `QD.RAID_PLAY_FOOD`). The relay's
   Xarpus died at 14 hitpoints with ten in the pack. They are now the table's last row (heal 20;
   E:151, tob_spectate.rs2).
3. **Xarpus P2 never ate** (raid_play_tob_xarpus.lua). The "no bite before a dodge" gate was
   `v.tick >= S - 2`, and the catch-up keeps `S >= tick - 1`, so the gate was always true. The
   branch also runs once per cycle, on S. A supply trace showed the bandage chosen at 24, 13 and
   2 hitpoints and dropped each time. Only the dodge's own tick (S-1) is gated now.
4. **Xarpus P3 stalled** (same file). After a 95-tick P2 every clean melee tile lay behind a
   pool on both route shapes, `nearest_edge` returned nil, and the raider stood still for 700
   ticks. P3 now retries without the route test, and only when the strict answer is nil.
5. **The first `_play_see` counted earlier rooms' swings** (raid_play.lua `_play_state`). A
   room test's tick log starts at its room; the relay's starts in the lobby. Verzik's first read
   took the 60 scythe swings from earlier rooms as punches and swapped to the bow fifteen ticks
   in. `st.anim_serial` now starts at the newest `player_anim` row.
6. **Verzik's P1 bolt is settled at its launch** (raid_play_tob_verzik.lua). "NOT tick-eatable:
   the verdict is settled here, on the launch tick" (tob_verzik.rs2). The plan counted
   landings, ate on the launch tick at 10 hitpoints, and died. `bolt_lands` now counts launches.
7. **Verzik's P3 yellow pool** (same file). The bow's press paths to its own range: from the
   pool at 6428,207 that meant 6428,209, so the raider left the pool on every swing, and the
   blast (judged on T-1) found him off it. No press is made from the pool in the last 4 ticks of
   its life.

Each plan change was re-proved on its kept harness (`seed_survey --names 5`: _play_maiden,
_play_xarpus, _play_verzik, _play_smoke, _play_nylocas, _play_sotetseg all 5 of 5 solo;
_play_xarpus, _play_bloat, _play_sotetseg `--party 3` 5 of 5).

## The whole raid, Normal trio (`_play_normal`), NOT KEPT (raid seam39 play_tob_normal_relay)

`test/raids/_play_normal.lua` is the Entry relay for three raiders in lockstep (README "The
whole raid, Normal trio"). Each room is played by its own trio harness's pre-fight and ONE
`t.raid.play` per raider. The entry is the notice board and the Normal party, the way
`_party_smoke` phase B forms it. The leader walks every way out, and its passage carries the
party (`tob_raid.rs2 ~tob_carry_party`). The lobby, the lockstep and the room-to-room walk
work on every name: Maiden is reached on 3 of 3 and Bloat on 2 of 3, and `party.lockstep` passes.
`party_repeat.py --runs 2 --allow-red` AGREEs (s39rep: sha c7d9f0b64027, 1106 boundaries;
s39rep2, on the final file: sha b36f4fc7c52c, 3698 boundaries).

**Result (`seed_survey.py _play_normal --party 3 --names 3`, final file): 0 of 3.**

| Name | Maiden | Bloat | First cause (leader's tick log) |
|---|---|---|---|
| `_play_normal` | cleared at 392 ticks, nobody died | Bloat at 1213 of 1500 when the leader died (t1025) | The leader, Maiden's tank, left her with 0 fish and 5 brew doses (7 eats, 39 drinks). Under Bloat (role 3) it had nothing to eat. |
| `svaplaynorma` | north ranger p3 died t698, then the leader t701 | not reached | p3 left Maiden with 0 fish, 1 brew dose, 1 restore dose (5 eats, 40 drinks). The leader took 24 + 21 from her at 45 hitpoints. |
| `svbplaynorma` | cleared at 406 ticks, nobody died | the starter (p3, role 1) died 9 ticks in; the leader died t1149 on a 45 at 45 hitpoints, prayer off | p3 had 2 fish after Maiden. The opener costs about 120 hitpoints in ten ticks. |

**The first cause is the supply budget, and it is the same on every config tried.** The six
room harnesses each give a raider a whole pack of food for ONE room. A trio carrying the
whole raid's switches cannot carry that.

| Room harness (Normal trio) | What one raider ate and drank |
|---|---|
| Maiden (`_play_maiden`) | tank 12 eats + 28 drinks, rangers' packs 26 supply slots |
| Bloat (`s39bloat`, run here) | role 1: 9 eats + 11 drinks on 14 fish. Its opener took 19, 20, 10, 8, 10, 7, 12, 7, 9, 8, 12 in ten ticks |
| Nylocas (`_play_nylocas`) | p1 6 eats + 23 drinks |
| Verzik (`_play_verzik`) | 16 eats + 17-18 drinks EACH |

In the relay, Maiden alone empties every pack except the freezer's: the tank uses 7-9 eats and
39-48 drinks. The supply chest is a points store (10-13 points a deathless chest; a shark 1, a
brew 3), and it stands after Bloat. So nothing refills a raider for Bloat. The arithmetic for
Verzik alone (about 34 consumables a raider) is out of reach of a 28-slot pack that also holds
the bow, the scythe, the Dawnbringer slot and the helm.

**What was tried, measured (scratch names n1..n6, surveys 1-7 in the seam39 state dir).**

| Config | Change | Outcome |
|---|---|---|
| n1 | seat = role, 13-14 supply slots a seat | all three died at Maiden; the tank ran out of food at t576 |
| n2, survey 1 | seat 3 tanks (`opts.role`) | Maiden deathless on 2 of 3. She cast at seat 1 as often as at the tank (pid order), so two raiders ate like tanks. Every pack left her with 0 fish; Bloat killed the leader with 0 food |
| n4 | brews for fish (11 brews, 1 fish) | worse. The library eats only under the plan's threat, so the tank sat at 26 hitpoints for six ticks, then took a 42 (`_play_supplies`: brews alone heal 16 per 3 ticks) |
| survey 2 (config B) | seat 1 tanks and is the Nylocas ranger (21 supply slots) | Maiden deathless on 2 of 3; the leader as Bloat role 1 died on its walk with 0 fish |
| survey 3-4 | Bloat role 1 = seat 3; members eat up between rooms | p3 reached Bloat on 34 hitpoints: `~tob_restore` heals only the watchdog's raider. Topped up, it still died 9 ticks in |
| survey 5 (config C) | the leader is Maiden's freezer and Bloat role 1 | the freezer-leader died in her blood on 2 of 3 (the run ends with the leader). On the third, Bloat reached 1343 of 1500 before all three, out of run energy, walked one tile a tick and took a fly a tick |
| survey 6 (config D) | C's roles back to B, plus a stamina potion each | the stamina press answered `timeout` inside the combat dose's delay (now re-pressed). The seat-2 starter in Masori died 8 ticks in |
| survey 7 (config E) | Torva worn, Masori carried | Maiden worse (the tank had 17 slots); the starter in Torva still died 9 ticks in |
| final (config D + seat-3 starter + stamina re-press) | | the table above |

**Kept in the file.** Seat 1 is Maiden's tank and the Nylocas ranger, and it carries the most
food. Seat 2 is the freezer and the Nylocas meleer. Seat 3 is the north ranger, the Nylocas mage
and Bloat's role 1. Every seat carries a stamina potion (10Boot 0:19:24), and the members eat
up between rooms (`top_up`).

**What would move it (not done here: other rows' files).**
1. The room plans' intake. The relay's Maiden took 1,050 (n2) and 1,429 (n1) against her
   harness's 625. Her blood (`-1`, 18-28 a tick and half of it from prayer, `~tob_maiden_blood_sweep`)
   killed the freezer at its home tile in 4 runs, while it held for a cast
   (`raid_play_tob_maiden.lua` `holding`).
2. Bloat's opener at about 12 a tick.
3. The supply chest store (`tob_midway_stores`, after Bloat and Sotetseg) bought from in the
   relay, for the rooms after Bloat.
4. The content's `~tob_restore`: one raider in a party.

Sources: E = wiki_Theatre_of_Blood_Entry_Mode.wikitext, W = wiki_Theatre_of_Blood_Strategies.wikitext,
10Boot = transcripts/yt_4i4lv-srJkw.md, store = wiki_Chest__Theatre_of_Blood_.wikitext :24-36.

## Open rows (found is not fixed)

- **The whole-raid relay's supply margin is thin** (raid seam35e). Under the own name the
  relay ends with 0 brew doses and 2 restore doses. Prayer is the budget because the content's
  bandages restore no prayer (CONTENT_BUGS "From seam13", open with no figure). Once that is
  sourced and modelled, the relay's six restores can go back to the Entry page's ratio (E:30).
  The Bloat chest gives 6 of its 10, because only 6 slots are free after the Maiden drop.

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
- **The Normal relay's supply wall** (raid seam39). No seat survives Maiden + Bloat on one
  pack. Maiden empties every pack but the freezer's. Bloat's role 1 opener costs about 120 in ten
  ticks, the room harness's too. Verzik's harness eats 16 + 18 a raider. Open: buy from the
  Normal store (`tob_midway_stores`) in the relay; lower the plans' intake (Maiden blood under a
  holding freezer, Bloat's opener).
- **`~tob_restore` heals one raider of a party** (raid seam39, CONTENT). `~tob_room_cleared`
  calls it once, in the watchdog's raider. A member reaches the next room on what it had left
  (svbplaynorma p3: 34 hitpoints at Bloat), with its run and special energy unrestored. The
  wiki says restore is Entry's ("your Hitpoints and Prayer are replenished after defeating each
  boss", E:17), so Normal should restore nobody and Entry everybody. Not changed here: no seam
  row names tob_raid.rs2.

## The four rooms against Blert, Normal trio (raid seam42)

Closer survey on HEAD 407dc4a25: Xarpus 3 of 3 (fixer: 5 of 5, party_repeat AGREE), LANDED;
Bloat 0 of 3 on blert.room_ticks and blert.downs only (every technique row passes), LANDED as
the measured step toward the reference; Sotetseg NOT LANDED (0 of 3 against HEAD's 2 of 3);
Verzik unchanged (the gap is the kit). Entry solo 5 of 5 on _play_smoke, _play_xarpus,
_play_sotetseg.

### Bloat, Normal trio -- follows Blert (seam42)
Reference: docs/minigames/theater_of_blood/sources/blert_api/reference/bloat_normal_3.json (blert_reference.py
bloat --mode normal --scale 3 --offline; 19 death-free of 30 cached rooms): room 137 [75-195], downs 2 (3 in 2 of 19),
down1 starts at 42 [39-47] and lasts 33, swings per raider in down1 6 [5-7], first swing age 3, last swing age 26.5,
down1 party damage 27 hp/tick [18.7-35.1], down2 22.8 [19.9-26.4], eat at ~25% hitpoints, hp lost per raider ~95.
What the plan now does like the recorded trios: all three enter with the leader (15 of 22 death-free rooms; the
wiki's "rest enter on the down" is 3 of 22); NO Dragon warhammer run-by (no DWH special in 30 rooms, one BGS);
hide hugging the tank, one tile off it ("Hug the pillar", W:687; Blert walk distance mode 7; the mirror tile was 9);
the last swing as late as the raider's own distance out of the stomp allows (leave_from_here) and no stomp threat
counted while it will leave in time (no more bites at 85-99 hitpoints on a down's last swings).
Before -> after (_play_bloat leader; after = 3 names): room 332 -> 259/340/269; downs 5 -> 4/5/4; down1 hp/tick
11.6 -> 16.6/13.8/12.4; down1 swings p1 4 p2 2 -> 4/4/4 each; first swing age 7-9 -> 5-7; hp lost 269/184/215 ->
131/36/56 (inside the reference); stomp hits 0; PfM'd fly max 15.
Still outside: room ticks, downs, swings per down (4 vs 6), damage per tick (half), eat threshold (81-99% vs 25%).
Where the rest is: (1) followers have no tick log, so raid_play.lua counts a swing every 5 ticks once engaged and
never re-presses when the server stops swinging: p3 stood in reach 20 ticks of down2 and down3 with no input
(_play_bloat t170-190, t242-262); (2) the 5th swing (age 25) is lost to the stomp margin (a -1 margin put the
stomp on 2-3 raiders, survey_5); (3) the real trios add a crystal halberd special on down1 (17 of 19) and claws on
down2 (12 of 19) and ZCB/tbow on walk1 (11 of 19), none in our kit.

### Xarpus, Normal trio, follows Blert (raid seam42 play_tob_xarpus_follows_blert)

Reference: `docs/minigames/theater_of_blood/sources/blert_api/reference/xarpus_normal_3.json`
(`blert_reference.py xarpus --mode normal --scale 3 --rooms 20 --offline`: 13 death-free rooms of 20
cached Regular trio rooms under build/blert/xarpus/; plus `extra_seam42` from
build/seam_state/matthew-mbp-m4-raid-b1-seam42/xarpus_blert/analyse.py). Comparison:
xarpus_blert/xcompare.py (raid_report.py --against knows only Maiden in this tree).

What real trios do that the stack did not: they never stack in phase 2 (0% of ticks; each raider's
nearest other raider 5 [3-6] tiles away) and swing about every 5.2 ticks (21.5 scythe swings in a
112-tick phase; W:844 "With a 5 tick weapon ... players will only delay an attack once in their
cycle"). The stack has to step out on S+2 (the chains' aim) and S+3 (the next spit's aim) every
cycle, so it swung every ~7.2 ticks and phase 2 ran 117-141 ticks.

| Who / when | Intent (changed rows only) | Source |
|---|---|---|
| phase 1 | The exhumed goes to the raider whose wait tile is nearest, unless that raider holds one still up (then the nearest free one) | reference outcome.phase.start.boss_heal 96 [50-182] (ours was 204); role.melee*.phase.start.dist_raider 5 |
| phase 2, the spread | Each raider holds its own side: the melee and step-back tiles whose nearest wait tile is its own (Voronoi of 34,32 / 31,35 / 37,35) | reference role.melee*.phase.phase1.dist_raider 5 [3-6]; extra_seam42 p2_all_three_on_one_tile_pct 0 |
| phase 2, the target stays in | The raider the spit in flight was aimed at (acid thrown from inside his footprint, destination = its own step-back tile) stays in melee that cycle; the other two step out on S+2 and S+3 as before | S ~tob_xarpus_spit (never the last target twice running, Near-Reality validTargets) and ~tob_xarpus_chain_to (a chain never goes to the spit's target); W:844 |
| phase 2, a used-up side | A melee tile with no clean step-back tile of its own side in reach is left for one that has one (also at the press: the run back in goes there); with none, any clean step-back tile in reach | measured: late-phase chains on melee tiles (t248) without it |
| exit | The leader eats one anglerfish if the pack is full before searching the skeleton | S ~tob_dawnbringer_take ("You don't have enough inventory space to take that"); measured svb (free 0) |

Rows: `trio.stacked` is gone; `trio.spread` (each raider's median nearest-other distance in [3,6]);
`trio.ring_clean` is bounded by the real rooms (<= 7: 13 death-free rooms leave 2-7, median 4,
distinct P2 splats on melee tiles; was == 0).

**Measured, five names (leader p1), `seed_survey.py _play_xarpus --party 3 --names 5`: 5 of 5.**

| Leader | Room (ref 276 [257-298]) | P1 heal (96 [50-182]) | P2 ticks (112 [96-124]) | P3 ticks (51 [45-70]) | spits (27 [23-30]) | P2 swings/raider (21.5 [19-29]) | P2 dist_raider (5 [3-6]) | ring puddles (2-7) |
|---|---|---|---|---|---|---|---|---|
| before (_play_xarpus, stack) | 297 | 204 | 139 | 41 | 33 | ~20 over P2+P3 27 | 0 | 0 |
| _play_xarpus | 273 | 168 | 119 | 37 | 28 | 20/20/20 | 5/5/5 | 2 |
| svaplayxarpu | 266 | 168 | 111 | 38 | 26 | 18/18/19 | 5/5/5 | 2 |
| svbplayxarpu | 264 | 168 | 103 | 44 | 24 | 17/17/18 | 5/5/5 | 0 |
| svcplayxarpu | 252 | 168 | 99 | 36 | 23 | 17/16/17 | 5/5/5 | 0 |
| svdplayxarpu | 278 | 168 | 123 | 38 | 29 | 20/20/21 | 5/5/5 | 2 |

Heal orbs 14 (was 17); no deaths. Repeat: `party_repeat.py --script build/xn42_repeat/_play_xarpus_trio.lua
--name xn42rep --runs 3`: AGREE (tick log sha ccbb1f5144c5, 342 boundaries). Entry solo
`seed_survey.py _play_xarpus`: 5 of 5.

**Still outside the reference.** Phase 3 is 36-44 ticks on every name (ref 45-70) with 7-9 swings
a raider (ref 9-12): ours kills the last 25% faster on FEWER swings, so the gap is the damage per
swing or the hit points left at the screech, not the plan (phase 3 is the Entry per-raider plan,
unchanged). svc's room (252) is 5 under the reference's minimum for the same reason. P3 raiders
sit 0-6 apart (ref 2 [0-4]).

NOT LANDED. The closer's survey on HEAD 407dc4a25 (after the follower-prayer fix) put the plan below at 0 of 3 (two deaths, one unfinished run) against HEAD's own plan at 2 of 3; the files were restored from HEAD and the work is kept at build/seam_state/matthew-mbp-m4-raid-b1-seam42/close/sotetseg_unproved.patch. The measurements stand.

### Sotetseg, Normal trio, against Blert (seam42 play_tob_sotetseg_follows_blert)

Reference: `docs/minigames/theater_of_blood/sources/blert_api/reference/sotetseg_normal_3.json`
(`blert_reference.py sotetseg --mode normal --scale 3 --rooms 20`; 20 death-free rooms of 29; roles melee x3 in 19).
`raid_report.py --against` cannot read a Sotetseg run (`blert_reference.BOSS_IDS` holds only Maiden: run_room -> None);
the comparison is `build/seam_state/matthew-mbp-m4-raid-b1-seam42/sotetseg/compare.py`, the real maze timings
`.../sotetseg/maze_times.py` over the cached streams in build/blert/sotetseg (58 mazes).

| number | Blert median [range] | before (seam33 plan) | after (s4, last clean party run) |
|---|---|---|---|
| room ticks | 212.5 [164-262] | 418 / 460 / died | 333 / 350 / 354 |
| start phase ticks (to maze 1) | 52.5 [42-67] | 92 / 83 | 63 / 73 / 88 |
| maze, proc to combat form back | 28 [15-49] | 41, 47 | 31-36 |
| scythe accuracy before maze 1 | (~30 dmg a swing) | 0.47 (22 a swing) | 0.85-0.87 |
| hp lost per raider | 92.5-108 [3-225] | 247-327 | 199-383 |

What the real trios do that ours did not:
* ELDER_MAUL once a phase per raider (weapons table: 15/13/12 of 19 rooms by phase). Defence 200 untouched left
  the scythe at 47% accuracy; one maul special each (`pvm_elder_maul.rs2`: -35% of current Defence) took it to 87%.
  The maul goes on in the SAME block as the orb press, the tick after a scythe swing; a tick earlier and the
  auto-attack swings it plain first (seq 7516 then 11124 six ticks later).
* Followers walk BEHIND the glow while it is lit (two tiles back), not after the last tile: maze 41/47 -> 31-36.
  Rules that made it safe: follow a neighbour chain built from the lit tiles each tick (the order tiles were lit in
  cut a corner diagonally onto a dark tile); never retarget mid-run on the grid (the server routes the new target
  from wherever the raider got to); never walk a tile guessed across a 3+ tile glow gap (a glow missed while the
  runner ran two a tick crosses two lateral rows, and either can hold the turn).
* Eat for ONE attack of his, not one per five ticks of the horizon: two worst-case melees made every raider eat
  and brew at 80-115 of 99, and the brews' Attack drain took accuracy to 0.29 by the last phase.

Still outside the reference: room 333-354 (> 262); hp lost; the post-maze phases (defence back, fewer specs: the
energy allows two mauls, the real teams three - a lightbearer or spec restore is the next lever, unsourced here).

### Verzik, Normal trio, against Blert (seam42 play_tob_verzik_follows_blert) -- NOT closed

Reference: `docs/minigames/theater_of_blood/sources/blert_api/reference/verzik_normal_3.json`
(blert_reference.py verzik --mode normal --scale 3 --rooms 20: 20 death-free rooms of 27).
`raid_report.py --against` refuses Verzik (BOSS_IDS knows only Maiden), so the comparison was
`build/seam_state/matthew-mbp-m4-raid-b1-seam42/verzik/compare_verzik.py` (phase bounds from her retypes 8371/8373).

| Number | Blert median [range] | Ours at HEAD 7f942f915: _play_verzik / sva / svb |
|---|---|---|
| room ticks | 438.5 [359-607] | 833 (two died) / 638 / 591 |
| P1 ticks | 85.5 [60-152] | 127 / 135 / 156 |
| P2 ticks | 210 [170-261] | 299 / 296 / 289 |
| P3 ticks | 134.5 [122-200] | 407 / 207 / 146 |
| P2 heal on her | 198 [65-313] | 802-848 (three red absorbs of 206-228) |
| hp lost per raider | 130-173 [69-259] | 268-715 |
| P2 weapon | SCYTHE (16-18 of 20 rooms per role; melee 83-92%) | twisted bow 47-51 shots |
| P3 weapon | SCYTHE (21-23 swings per role) | twisted bow 26-66 shots |
| P1 | DAWN 4 + SCYTHE 8-10 per role | dawn 3-4 + scythe 14-21 per role |

Every real Normal trio plays Verzik ALL MELEE. Ours is a ranged kit (`::maxrange`, Rigour). Tried and
reverted: the scythe on the reds in P2 with the ranged kit -- 8.0 a hit on a red (729 hits), P2 never
ended (0 of 3). Next: the melee kit (`::maxmelee` exists, cheat_max_gear.rs2:31) with Piety, P2 melee with
the out-on-T-1 step against the bounce (V verzik.p2_scan_rule), P3 melee with the under-her step on T-1
(V verzik.p3_melee_predicate). At HEAD the survey is 2 of 3 (the leader's own name dies in a 407-tick P3),
where seam34v measured 5 of 5 before the content merge bf3dabef7c.
