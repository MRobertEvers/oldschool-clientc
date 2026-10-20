# Skeletal Mystics -- solver plan (phase 4)

Target shape: `script/plugins/quest_driver/cox2_olm.lua` (a `QD.T2.register` room) and
`test/raids/cox_mystics.lua`, entered with `::coxtest 9 <n>`. Spec: `mystics_spec.md`.
No code this round.

## The strategy a strong solo human plays

From V1 (Synq 0:31:53), W2 (CoX/Strategies), V2 (Zulerah's luring guide), V3 (Extile, Gnomonkey, edb0ys, Tubby):

1. Gear: ranged with the **salve amulet (ei)** (they are undead; W2 "A salve amulet(ei) will help"), **Protect from Magic** for the whole room, Rigour/Eagle Eye. Weapon: toxic blowpipe (fast, low defence is not their case but it is the WDR minimum's ranged weapon) or the dragon crossbow with ruby bolts (e) at range; Synq: "the bofa and blowpipe are also commonly used".
2. **One mystic at a time.** Their aggro is by sight within a large radius: approach so only one sees you (Synq "Sometimes you can isolate the first mystic without having to aggro the others"; Tubby "try to attack one mystic without aggroing the other two ... fairly large aggro radius").
3. **Corner trap** where the layout allows: come close in the open so the mystic switches to melee (a straight walk to you exists; 50% after each swing), then put an obstacle between you so its naive step is blocked east or west of it (Synq "They'll get trapped on objects either east or west of them"); stand diagonal to it, never orthogonally beside it (Synq "Stand diagonal of the mystics so you don't take any melee damage"). A trapped melee mystic does not swing (Extile). It flips back to magic eventually (8 ticks out of its reach box, or after a cast): re-prompt it. "Losing DPS while luring them isn't worth it" (Synq) -- keep shooting.
4. Without a trap: shoot from range under PfM; the cast is halved and only one mystic shoots.
5. On the last kill the portals appear; take the end portal (Travel-End from the start/middle portal) or walk out.

## Perception (api_drive, both lanes)

| fact | verb | notes |
|---|---|---|
| mystics (7604-7606), tiles, HP | `api_drive.npcs(0)`: `npc_id`, `server_x/z` (2x2 SW), `health_ratio` | |
| who is aggroed on me | npcs row `face_x/face_z` + first-seen casts/walks | there is no target field on the row |
| a cast and its kind | npc `seq_id` 5523 + its graphic (`spotanim_sent_id` 1322 fire / 1321 vulnerability) | landing: projectile 130/168 `cycles_left` |
| incoming magic | `api_drive.projectiles(0)`: 130 / 168 with `target` = me, `cycles_left` | lands on the hitsplat tick |
| a punch | npc `seq_id` 5485 | lands that tick |
| melee style | inferred: a mystic that walks toward me instead of casting | the style is not sent; infer from movement + no cast for >= 4 ticks |
| obstacles | `tiles_nearest`, `route`, collision via `aplan` | for the corner-trap tile search |
| exit / portals | `api_drive.locs`: 29741 Mark of Power gone, 49996-49998 appear | |

Missing / engine work:

- **E2 line of sight / line of walk on both lanes**: the trap and the "isolate one mystic" both need "can mystic M see me from tile X" (LOS, its aggro) and "is there a straight line of walk" (its melee reroll). `server_los` exists on the live plugin only; scriptrun has no such bind. Needs one C binding registered on both lanes: `api_drive.los(x1,z1,x2,z2,level,size,kind)` with kind = sight | walk.
- **E3 npc naive step**: `api_drive.npc_step(slot, toward_x, toward_z)` -> the tile a size-N npc's naive one-tile step lands on (or itself when blocked), the same `findNaivePath` the server mover uses. The corner-trap search is "a tile T, diagonal to the mystic, where its step toward T is blocked"; the bot must ask the server's own rule rather than reimplement it in Lua.
- E1 (npc phase order, see `guardians_solver.md`) affects the punch's flinch timing only marginally here.

## Threat field

| term | window | where | E[dmg] / max | prayer |
|---|---|---|---|---|
| fire blast | cast T (seen), lands T + 1 + floor((1+d)/3) | anywhere in its LOS within 10 | acc x 12.5 -> halved under PfM / 25 (12 PfM) | PfM at the cast |
| vulnerability | as fire, 1 in 5; a landed one drains 4-8 Defence | as fire | as fire | PfM |
| punch | swing T when orthogonally beside its 2x2 at end T-1 | its 4-neighbourhood | acc x 12.5 / 25 (12 PfM) | PfM (melee) at the swing -- the bot keeps PfMagic, so punches are avoided by position, not prayer |
| extra aggro | a second mystic that sees me | its LOS within 15 | doubles the field | -- |

## Mode machine

`ENTER` (prayer PfM, Rigour, salve on, weapon) -> `PICK` (the nearest mystic that can be engaged while at most one other can see me; score tiles by how many mystics see them via E2) -> `LURE` (step into its open line until it walks at me = melee) -> `TRAP` (move to a diagonal tile behind an obstacle per E3; hold) or `KITE` (no trap tile: shoot from max range out of the others' sight) -> `SHOOT` (every weapon cycle, from the trap or kite tile) -> next `PICK` -> `EXIT` (portal or walk). `RESCUE`: HP threshold -> step out of every LOS, Xeric's aid (+); Defence drained by vulnerability -> revitalisation (+) only if the drain is large.

## Kit (start from `test/raids/cox_olm_solo.lua`)

The Olm kit already holds what this room needs: **salve amulet (ei)** (in the We Do Raids minimum), toxic blowpipe with dragon darts, dragon crossbow + ruby dragon bolts (e), black d'hide body/chaps, CoX (+) potions. Wear the salve instead of the fury for this room (one swap). No new items.

## Probes

- P1 per mystic per tick: style inferred vs actual (cast / walk / punch rows), and whether it is trapped (tile unchanged while not adjacent).
- P2 cast landing tick vs predicted `1 + floor((1+d)/3)`.
- P3 damage under PfM vs without (halving).
- P4 how many mystics see the bot each tick (aggro discipline).
- P5 portals appear on the last death; Travel moves the bot.

## PASS

All mystics dead and the player alive on 48 seeds, with **0 punch damage** (a strong human never stands beside one), damage taken per mystic reported (the field's expected cost is the yardstick), and no `abort` / `no primary player bound` in stdout. First probe (2026-10-18, no armour, no prayer, `scratchpad/probe/after_m`): casts at 31-33 landed 4 ticks later at range, accuracy rolls splashed several, punches only when beside one (5485 at 39/43/47), a melee-style mystic walked one tile a tick at the player; the unarmoured player died at tick 57 to three mystics at once -- the case the strategy avoids.

## Round 2 (2026-10-18): built and swept

`script/plugins/quest_driver/cox2_mystics.lua` (QD.T2 room `cox_mystics`) and
`test/raids/cox_mystics.lua`. **48/48** (`--name my0..my47`): every mystic dead,
the Mark of Power seen gone and the portals up, the exit Passage taken, 0 punch
damage (0 punches in 48 runs); damage taken 157-369 (median 244, the overload's
50 included).

As built (it differs from the plan above where the probes said so):
- Watchers (`B.watcher`, the planner's line-of-sight rule) price every tile a
  mystic sees: an awake one 0.8 a tick (max 3), a sleeping one 1.5 (waking it);
  the 2x2's side is forbidden, two tiles when that mystic stepped last tick (a
  melee mystic walks in and punches on the next read).
- THE CLICK: from out of the blowpipe's reach (5) and more than three tiles from
  every mystic, an Attack on the target (`intents.world_op`), latched until I
  swing or a mystic comes within three tiles; then the plan shoots. The plan
  alone found no shot in the maze for hundreds of ticks; toggling the click tick
  by tick shot 3 times in 300 ticks.
- The target: a mystic within three tiles first, then the awake one with the
  fewest hitpoints. Going past a close one for another walked me beside it.
- Xeric's aid drains Ranged: a revitalisation (+) when Ranged is 5+ under its
  base, or under it with the overload's re-boost more than 8 ticks off (the
  first probe landed 15 of 90 swings without it).
- No corner trap and no new drive verb: E2 (line of sight on scriptrun) was not
  needed -- the watchers are the C planner's own sight test.

## Round 3 (2026-10-18): the room to RSProx

The content now follows the recordings (`docs/rsprox-analysis/cox/mystics.md`). The solver changes that this needed:

- The cast seq is 5528. A step no longer marks a mystic awake, because idle ones wander (up to 7) and the trap held a wanderer for 1150 ticks (myst1). A sleeping one's waking is priced over its hunt box (7 round its south-west tile), not everywhere it sees.
- The target holds while it is awake. In the cw room's cluster the pick flipped on the 3/4 line every step (myst2: 300 ticks swapping two tiles).
- A fight tile not reached after 10 ticks without a swing hands over to the click (myst7: 523 ticks at gap 6).
- Danger is the four punch tiles, not a diagonal: a mystic trapped on my corner is shot (myst3).
- After the clear, a mark still listed is a zone outside my 7x7 window. I walk at it (myst3: 1800 ticks 27 tiles off it).
- `api_drive.npc_stall` walks the pursuit's line (the naive path beside the tile), the one `npc_approach` walks. Traps now happen: 6 of 8 seeds, where none did before.

`cox_mystics.lua` plays a party too (`--bots N`): seat 1 builds the raid and the others join. 8 of 8 solo (myst1-8)
and 4 of 4 party (2 duo, 2 trio), 0 punches.

## Round 4 (2026-10-19): the budget kit -- measured, then the plan

The relay plays this room on `QD.COX.SOLO_KIT` (no raid potion: 7 Saradomin brews, 2 super restores +
a sanfew). Harness `test/raids/_cox_room_budget.lua`, `mystics0..11` before this round
(`scratchpad/sweeps/b_mystics`): 10/12, mean 16.6 heal and 7.6 restore doses, 150-550 taken.

Measured off the tick logs (casts are seq 5528 rows, punches 5485, hits `hit_player`):

- **Casts are the whole cost**: 91-169 casts a seed, 3.4 damage a cast on average under Protect
  from Magic. A mystic that sees me for its whole life (about 50-100 ticks) costs 45-85.
- **The trap works when it holds**: mystics0's first mystic cast 3 times, then stood silent 82 ticks
  (it walked 7 tiles at me and stuck) until it died.
- **Why it did not hold**: (a) mystics2: I alternated between two corner tiles of one mystic and
  passed its north side each time -- 7 punches, each punch a re-roll back to magic; (b) 9-tick
  cycles (mystics2, mystics9): melee for 8 ticks with me out of its 2-tile box (from its SW tile,
  `huntall(npc_coord, 2)`), back to magic, a cast, a re-roll; (c) the trap was searched only after
  a step was seen, and dropped and re-found every tick.
- **The other mystics**: in the cw cluster rooms all three wake together and two or three cast on
  most 4-tick windows while I fight one.
- **Drained Ranged**: 28 brew doses drained Ranged to 2 landed in 58 swings (mystics2's last) and
  19 in 207 (mystics9's last): every brew makes the fight longer, so more casts, so more brews.
- Prayer: Protect from Magic + Rigour the whole room drains a dose of restore every ~80 ticks.

The plan (the guides' cheap method, V1/V3/W2):

1. **A trap tile T for the target, chosen once and held**: T with the mystic's naive walk toward it
   (`api_drive.npc_stall`, its current tile, up to 12 steps) stalled at P, T inside P's 2-tile box,
   never on P's sides, P sees T (my shot). Scored by: other mystics that see T (awake: casters;
   asleep within 7: a wake), my distance, and -- while the target is still MAGIC -- whether a
   1x1 walk from T reaches the tile it will cast from (its 50% re-roll to melee needs a line of
   walk). The route there must not end a tick on any mystic's side.
2. **Prompt, then wait**: on T a magic mystic casts (halved), re-rolls melee within ~2 casts, walks
   at me and stops at P; then it never swings while I stay in its box. I shoot it from T.
3. **Blacklist, do not dither**: T is dropped only when the mystic leaves the predicted walk (my
   model was wrong: never that T again), or casts 4 times while I stand on T without ever walking
   (no line of walk), or I do not swing for 12 ticks on T (no line of sight for me).
4. **Prayer only when it pays**: with the target stuck and silent and no other awake mystic seeing
   me, Protect from Magic and Rigour go off; they come back the tick that changes.
5. Without a trap tile: the fight tile / click of round 2-3, unchanged.

### Round 4 as built and swept

`cox2_mystics.lua`: `QD.COXM.trap` (the held trap, re-read from where the mystic stands while I walk
to it -- it walks at where I AM, not at T -- and blacklisted only on evidence once I stand on it),
`QD.COXM.lure` (a magic target with no trap tile it can re-roll on: a tile in reach, in its sight,
with a line of walk; blacklisted after 4 casts with no step), `QD.COXM.walk_line` (the re-roll's
line of walk, `collision_map_line_of_walk`'s ray stepped one axis at a time over `npc_stall`),
`QD.COXM.quiet` (prayers off, brew floor 35), the trapped mystic held as the target over a close one,
the trap's pull 10. `QD.raid.cox_mystics_map()` is a probe: the room's walk map as text.

Sweeps (`scratchpad/mystics_agent/r5`, harness `mystics0..23`; `t5`, `cox_mystics.lua my0..23`):

| | green | heal doses med / worst | restore med / worst | taken med / worst |
|---|---|---|---|---|
| before (harness 0..11) | 10/12 | ~16 / 28 | ~8 / 8 | ~300 / 547 |
| harness 0..23 | 22/24 | 12 / 28 | 8 / 8 | 241 / 514 |
| cox_mystics.lua 0..23 | 22/24 | 11.5 / 28 | 8 / 8 | 203 / 544 |

Still not cheap. What is left:

- **The cluster rooms**: three spawns within a few tiles wake together; with one trapped the other
  two cast on most 4-tick windows (mystics2, mystics12, my3, my8: 460-544 taken). The next step is
  a trap tile that two mystics' stalls share, or pulling one out of the others' sight before
  waking the rest.
- **No trap or lure tile reachable** for some spawns (mystics12: 0 traps): the fight-tile fallback.
- **Punches** (0-18 a seed): a lure tile in the open lets a melee mystic reach me before a trap
  tile is found; `npc_stall` and the server disagree on some steps (the "walk left the model" drops).
- **The re-roll**: lure tiles my ray says have a line of walk sometimes never convert (r3 mystics1:
  163 casts from a straight open row at gap 4). Either my emulation of the ray is wrong or
  `~cox_mystic_can_walk_to_target` answers no there; not proven either way (no line-of-walk verb on
  scriptrun: `server_los` is client-only).
- mystics21: timeout -- a melee mystic walked 973 tiles after me out of my npc window while I
  searched the centre.
- Restores: all 8 doses go in most seeds (brews drain Ranged; the restore gap is 8).

## Round 5 (2026-10-19): the relay's room -- one mystic at a time

The whole-raid relay (`test/raids/cox_raid_solo.lua`, sweep r29) played this room 21 times on
`QD.COX.SOLO_KIT` and passed 12. All nine losses are deaths with doses left on arrival (mean 24
heal, 10.6 restore). Read off each seed's tick log (`scratchpad/mystics2_agent/an2.py`: casts
5528, punches 5485, a mystic awake from its first cast to its death):

| seed (room) | first wrong decision |
|---|---|
| raid13 (9) | Full pack: the blowpipe's wield refused ("two-handed, no free slot") for 260 ticks with Protect from Magic + Rigour lit on no mystic. Then the lure walked into a 2/4/2-tile cluster and woke two in two ticks, the third 22 later: 272 ticks with three casting. |
| raid24 (8) | The same refusal for 546 ticks; four revitalisation doses spent on the idle prayer. Then a trap walk woke two in one tick (t2983). |
| raid43 (9) | The same refusal for 536 ticks; then the trap woke the second and third while the first died. |
| raid7 (4), raid29 (4), raid34 (5), raid35 (3), raid11 (11) | Trap search for the first mystic took me inside the others' 7-tile hunt (in their sight): two or three awake within 1-9 ticks of the first; 150-250 ticks with two or three casting. (raid11 arrived with 0 heal doses.) |
| raid4 (11) | The last mystic's lure tile (6443,115) was never walked to: I stood at gap 6 (out of the blowpipe's 5) for 472 ticks, no swing, 122 casts at me. |

The method (the owner, 2026-10-19: "pray mage and rigour and the blowpipe and should avoid tanking
multiple mystics at a time. Try to kill them one at a time"):

1. **The pack**: the blowpipe goes on at once. A full pack lends a spare raid item to the floor
   (`QD.COX.spare`: an empty vial, else a raid dose) and takes it back once a slot frees.
2. **Prayer**: Protect from Magic + Rigour while any mystic is awake or I stand within 3 of a
   sleeper's hunt; off before that. Rigour is pressed by the room itself (the raid's pool gate
   would click it off). No overload: it is saved for Olm.
3. **Wake masks**: per sleeping mystic, the tiles within 8 of its south-west tile that it sees
   (`npc_sees`; RM-W1's hunt, one tile of wander). The plan pays heavily for ending a tick in the
   mask of any sleeper but the one I mean to open on.
4. **One target**: an awake one first (held); with none awake, the sleeper whose best fight tile
   wakes no one else. **The fight tile**: gap 2..5 of the target, in its sight, scored by sleepers
   it wakes (1000 each), other awake mystics that see it (300), within 2 of another (400), a line
   of walk from the target (25: no line of walk keeps it on magic and still, RM-S2), the distance.
5. **No swing for 12 ticks** with the target awake: the click (the server walks me into reach).
6. The trap, the lure and the prayer-off "quiet" go (the trap kept the first mystic cheap but its
   walks woke the rest in 6 of 9 losses).

### Round 5 as built and swept

`cox2_mystics.lua`: `QD.COXM.mask` (wake masks, priced 25 a tick in the plan: at 6, under a swing's
12, the plan stood and shot while a wanderer's mask came over me), `QD.COXM.tiles` / `best` /
`fight_tile` (the scored fight tile, its route's ticks priced against the masks, the tile I stand on
kept while it is clean, a tile search cached per mystic per tick -- two a tick ran out the 400000
instruction budget), `QD.COXM.pick` (an awake one held, else the cheapest sleeper), the no-swing
watchdog (ban the tile; the third ban in a row clicks), the door: the blowpipe wielded in `enter`
(a spare raid item lent to the floor when the pack is full) **before** Rapid is pressed. Trap, lure,
quiet and the overload are gone. The ticklog carries a `myst stat` mark every 20 ticks (Ranged,
prayer, PfM/Rigour, awake count, com_mode, damagestyle).

Two findings beyond the plan:

- **Rapid was never on in the relay.** `enter` pressed style slot 1 on the tentacle; the blowpipe went
  on later in the loop and the fight ran at com_mode 0 (Accurate): a swing every 3 ticks, not 2
  (every relay seed and the room tests, `player_anim 5061` gaps). Wielding first fixes it (gap 2).
- **The pick flipped between two awake mystics every tick**: `health_ratio` is -1 before a headbar
  and outranked the held target's -1 (raid13 t1926-1939).

Sweeps (`scratchpad/mystics2_agent/`): relay seeds `w6`, room test `rt` (my0..23), harness `hb`
(mystics0..23). The relay's arrivals are identical to r29 per seed (deterministic), so the
comparison is fair.

| | before (round 4) | after (round 5) |
|---|---|---|
| relay, 21 seeds holding the room | 12/21 (r29) | 14/21 (w6) |
| cox_mystics.lua 0..23 | 22/24, heal med 11.5 / worst 28 | 24/24, heal med 10 / worst 20, restore 8 / 8, taken med ~200 / worst 351 |
| harness 0..23 | 22/24, heal med 12 / worst 28 | 24/24, heal med 9.5 / worst 19, restore 8 / 8, taken med ~205 / worst 382 |

Relay: fixed raid4, 7, 13, 24, 29, 35, 43 (7 of the 9 losses); lost raid5, 17, 27, 39, 48 (all
passes under round 4). Still failing:

- **Cluster rooms** (raid5 spawns 1/2/2 apart, raid39 5/1/6, raid27, raid48): no tile in the
  blowpipe's reach of one is out of the others' hunt, so two or three wake within 40 ticks and cast
  together. Round 4's trap kept the first one silent in these rooms, which is why they passed then.
  Next: in a cluster, wake one and walk out of the others' sight so it follows (its magic pursuit),
  or trap the first as round 4 did, with its walk priced by the masks.
- **Restores run out, then Ranged drains to 0** (raid34 arrived with 7, raid17 with 1): the shared
  prayer sips and the Ranged restores share the doses, and after they are gone every brew takes 10%
  of Ranged off (raid34: 81 of 85 swings missed). A kill takes 40-45 swings at ~35% accuracy.
- **raid11**: arrived with 0 heal doses at 89 hp (supply-starved).
- A wanderer still walks into range sometimes (raid34 t1823): the plan moves a tick late.
