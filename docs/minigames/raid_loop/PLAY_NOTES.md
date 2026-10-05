# The play library: skills and room plans (raid seam27)

The owner, 2026-10-05: "I noticed that the driver is not very fast or good. That is not going
to work in normal mode. You will need to code up the agents a lot smarter using the actual
strategies." This file is the strategy half of `t.raid.play`. The code is at the end of
`script/plugins/quest_driver/raid.lua`, under the banner `SEAM raid_play_by_tick_intent`. It is
there and not in a separate `raid_play.lua` because the driver is one chunk built from
`DRIVE_SCRIPT_PARTS` (src/plugin/torirs_plugin_drive.c:98, C), and `dofile`, `loadfile` and
`load` are removed from the plugin sandbox (torirs_plugin_lua.c:3980). A new part would need a
C edit and a rebuild of every binary. That is a one-line move for a later pass if it is wanted.

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

## Skills (each a function in raid.lua)

| Skill | Function | Rule | Source |
|---|---|---|---|
| Attack on cooldown | `_play_attack`, `_play_next_swing` | A click only STARTS a fight. After that the weapon swings every `speed` ticks by itself, so the library presses Attack only when it is not engaged (after a step), or when no swing has been seen for speed + 2 ticks. | wiki Attack speed; scythe speed 5; swing seq 8056 measured in build/quest_gate/tob_bloat/ticklog.tsv |
| Pray by the telegraph | `_play_pray` | The plan names the set wanted on the NEXT tick, because a press made between ticks T-1 and T is in force for T. Bloat: Protect from Missiles from age 32 of the down onwards (the first fly is at T+33), and Piety for the attackable window. Pinned exceptions from DRIVER_NOTES seam20 are carried by those rooms' plans: Verzik P3 on hit, Sotetseg's ball at impact, urn bombs at landing. | ET 1.1; DRIVER_NOTES "Several inputs in one tick", "Which ToB attacks read the prayer on the send tick" |
| Supplies | `_play_supplies` | Eat when hitpoints <= the largest total the plan's `threat(h)` says can land before the next chance to eat. On a FREE tick (not attacking, or the weapon is ready, so eating adds no delay) h is the gap to the next free tick + `QD.TOGETHER_CONFIRM_TICKS` + 1. Between swings h = 2, so a bite there costs the attack 3 ticks and only an imminent hit forces it. If the best food alone is short, a brew rides in the same tick. Restores are drunk when the missing prayer is at least one dose (32), or prayer is at 2 or less. | wiki Food ("If your weapon is ready ... eating does not add any new delay"; 3-tick eat delay), Potions ("do not incur the standard 3 tick attack or eat delay"), Food/Fast foods (combo), Super restore (8 + 25%); all quoted at consume_shared.rs2:28-49 |
| Hazards | `_play_hazard` | A marked tile is avoided from the tick it is seen. The library moves to the safe tile nearest the wanted tile, and the shortest step from the player breaks ties. It never stands on a tank or off the floor. | ET 1.3 / 3.4 (judged on the previous tick's tile); "simply don't stand on the shadows" (transcripts/yt_4i4lv-srJkw.md:71) |
| Loadouts | the plan's `gear` list in the same `t.together` (ten inputs a block) | A gear swap is one tick. Bloat's plan swaps nothing; see "Not done" below. | DRIVER_NOTES "Several inputs in one tick" |
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

## Bloat, Normal trio (`tob_bloat`, mode `normal`), measured, not green

The code for the roles today: p1 crosses and hides (W:687). p2 and p3 wait at the barrier and
cross on the first down (W:689 "the rest of the team should enter"). All three then run the
same plan with `stomp_plan = "leave"`. That means attacking until age 24, then running to the
hide tile, which is beyond the stomp's 6-tile reach by age 28 (W:689 "run away after the last
attack").

**One run, `--party 3`, name `playn3`** (build/quest_gate/playn3). All three raiders died.
- p3 died at tick 440: two hands (31 + 18) landed on the third tick of the sixth down, on a tile
  it had pathed through to Bloat. Hands dropped while Bloat walked land after it goes down.
  `_play_hazard` is applied to walks, but not to the attack press's own path or to standing on a
  shadow during a down. This is a library gap, open below.
- p2 died at tick 478 and p1 at tick 501: flies after the sixth rise, with the supplies spent
  (each ate 14 anglerfish and drank 18 doses).
- The cause under both deaths is too little damage. Bloat took 666 of 1500 in 183 hits, 108 of
  them zeros, over six downs. The sourced Defence drain (W:687 "one or two players should do a
  run-by on the boss with a Bandos godsword special to lower its Defence") is not in the Normal
  plan. The kept Normal attempt carries a Dragon warhammer for p1 (tob_bloat_normal.lua:5).

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

## Open rows (found is not fixed)

- **The hazard on the attack path.** `_play_hazard` covers walks only. A down approach (the
  attack press pathing) and standing on a shadow during a down are not covered (playn3 p3,
  tick 440).
- **The Normal plan has no Defence drain.** W:687's run-by is the sourced answer. It needs the
  LOADOUT block (warhammer in, special, scythe back) on the first down, and in a trio the role
  of whoever does it.
- **Members count swings** from presses and the weapon speed. They have no tick log, and no
  client read gives the local player's own animation (api_drive.players has no anim field).
- **The six-tile flinch** is the hide tile, not the guide's five; the row asks for 3 or more.
