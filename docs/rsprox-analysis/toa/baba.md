# Ba-Ba

Read from 41 kills in 19 RSProx sessions, re-transcribed with every player
shown (21 solo up to a seven), packet by packet. S = the tick the room starts
(the teleport crystal); tiles are square-local, base (3776,5376). Each kill's
invocations are read from the party panel the server last sent before it
(clientscript 6729's masks). Full inventory of the first 8 kills:
[baba.out](baba.out). Common entry/leaving: [README](README.md).

**Ours:** `minigame_toa/scripts/toa_baba.rs2`, compared below. Where no
recording shows a thing, the wiki or Near-Reality's server (NR,
`BabaEncounter`/`Baba`/`RollingBoulder`) is used and named.

## Npcs

| Npc | Id | |
|---|---|---|
| Ba-Ba | `toa_baba` (11778), 5x5 | added on S on her home (39,30) with `npc_mandrill_spawn01` (9752) and six synths to each raider: `toa_baba_spawn_boss_roar_01` d9, `_first_movement_01` d11, `_second_movement_01` d31, `_footstep_03` d70, `_footstep_01` d102, `_footstep_02` d109 |
| Digging | `toa_baba_digging` (11780) | |
| After the second dig | `toa_baba_coffin` (11779) | she fights on in this form and dies in it: `npc_mandrill_despawn01` (9754) |
| Boulder | `toa_baba_boulder` (11782); breakable `toa_baba_boulder_weak` (11783), 3x3 | added facing west (angle 512) |
| Rubble | `toa_baba_rubble` (11784), `toa_baba_rubble_2` (11785) at half or less, 3x3, on `invisible_type8_blocking_size3` (42838) | |
| Baboons | `toa_baba_baboon` (11781) | |
| Sarcophagi | `toa_baba_sarc_npc` (11788), eight added on S on the coffins at x 27 / 32 / 37 / 42, z 24 / 40 | the coffin loc `toa_baba_coffin_closed` / `_open` |

Her defend sound is `toa_baba_defend_01` as an area sound, range 9, from her
middle tile; every death (hers, a baboon's) an area sound -1 range 8 on the
npc's tile; rubble and a coffin defend with area -1 range 10.

## Her fight

She faces her target and starts to move on S + 6 (41 of 41 faces, 39 of 41
first moves), and **runs**: two tiles a tick on 312 of the 490 ticks she moves.
Her steps follow her target's tile a tick late. She swings
(`npc_baba_attack_melee`, 9743) every 6 ticks from a side of her 5x5 (96 of
115 recorded swings; the rest are her rockfall swings). Rubble blocks her (the
wiki: rubble used to be a safespot, fixed in 2022), so she goes round it.

Her attack turns run a cycle: after her first swing, the rocks with a swing,
the shockwave, a swing, the boulder throw, a swing, and again (NR counts a
shockwave every fifth attack; recorded 3-5 swings between shockwaves, mode 4).
The target: NR's, the raider lowest on hitpoints, the nearest her middle on a
tie.

## The shockwave

On her target's tile, beside her (58 of 58): a cross of shadows on open floor,
and `darkbow_shadow_impact` (3737) area range 10 at delays 0, 30, 60 (and 90).

| | Without Shaking Things Up | With it |
|---|---|---|
| Shadows | arms of 2: `gargboss_debris_shadow_120` (1447) d0, `_90` (1446) d30, `_60` (2111) d60 | arms of 3: `_150` (1448) d0, `_120` d30, `_90` d60, `_60` d90 |
| Slam | 4 later (6 of 7) | 5 later (55 of 63) |
| Area | the 3x3 and the four tiles 2 out | the 5x5 and the four tiles 3 out |

The slam: her swing, `darkbow_smoke_arrow_impact` (1103) on every open tile of
the area with delay 6 x the distance from the centre, and
`toa_baba_shockwave_gas_01` area range 7 (61 of 67). The **stomp**: a raider
under her 2 ticks (6 of 6 stomps had one under her 1-3 ticks before) brings it
on her middle tile with every shadow `_60` and the slam 2 later on the larger
area. Its hit falls 10 % a tile from the centre (NR).

## The rocks and the rubble

Her swing facing 10 south of her middle, and `toa_baba_rock_fall` (2250),
`_faster` (2251) or `_fastest` (2252) delay 20 on **every raider's tile** (69 of
79 on the recorder's own) and on two more where rubble will land. To every
raider `toa_baba_circular_rocks_falling_01` and `_impact_01` delay 150 - 60 x
speed. The speed is 0 at Apmeken levels 0-1 and 1 from 2: level 0 shows 2250
(48 of 48), levels 3, 4 (five fights whose health is level 4's) and the
seasonal 5-6 all 2251; `_fastest` (2252) is in no recording, so NR's min(2,
level / 2) is wrong at 4. The rocks land 6 - 2 x speed later (recorded 4 at
speed 1).

Rubble: the npc and its loc, no animation, its bar re-sent every tick. 20
hitpoints a raider between the two, the second at half when they differ (solo
10/10, duo 20/20, trio 40/20, five 60/40, six 60/60: the implied pools of 47
hits). One at half or less is `toa_baba_rubble_2`, its transformation re-sent
every tick. A raider under landing rubble says "Ouch!", plays
`agilityarena_player_spikedback` (1114) and is moved a tile out (exactmove
delay2 29) with "You are knocked back from the rubble." (spam; 101 and RSProx
4292 [1004]: both stood on the rubble's east column, middle row, and went to
(+1,-1), angle 1791, unhurt; 4292 also sends `busy` 1 that tick, 0 the next). When her
pool crosses a dig's threshold the rubble takes a hit for what it has left
that tick and is gone the next (84 of 98); its tile becomes
`invisible_type8_nonblocking` (29733) shape 10 rotation 0.

## The boulder throw

`npc_mandrill_attack_ranged_01` (9744), "Ba-Ba throws a large boulder at
you." (`ff289d` solo, `ef0083` with a party), `toa_baba_throw_junk_woosh_01`
delay 10, and one `toa_baba_ranged_travel` (2244) a raider from the tile of her
5x5 nearest them (135 of 135): start 90, end 210 (180 in the form after the
second dig: 137 of 137, 23 of 24), angle 40, slope 64, height 100 -> 31. It
lands end / 30 later on wherever they stand: `earthsurge_impact` (1463) height
50 on every raider, `tob_sotetseg_large_fireball_player_hit` and
`toa_baba_throw_junk_impact_01` to each, and the hit.

| Standing beside (a side, not a corner) | Hit | Recorded |
|---|---|---|
| nothing | whole (58-60 at Expert) | 5 of 5 |
| rubble | an eighth (7 at Expert, 19 of 28); the rubble takes 20 and the impact | 22 of 30 broke the tick after |
| a coffin | half (28/58, 29/59) and the coffin npc takes the impact | 3 of 3 |
| a baboon, another raider | half (wiki "split", NR 45 %) | none |

## The digs

At **66 %** and **33 %** of her pool. She leaps 2 ticks after the crossing (25
of 25 where the bar shows the tick): `npc_mandrill_attack_special_jump01`
(9748), a face_coord at her throne, and an exactmove from where she was to her
home (delay 30) whose angle is ceil(atan2(-dx, -dz)) in 2048ths (82 of 82).

| Tick | |
|---|---|
| L + 2 | the digging form; the scream: "Ba-Ba screams and knocks you back!", `human_troll_flyback_merge` (9799), `stunned_thieving` (245) h124, `myarm_flyback` (3201), and an exactmove west to x 22 on the raider's row facing east, delay1 5, delay2 30 for every 2.5 tiles to 120 (59 of 59) |
| L + 3 | she faces east |
| L + 4 | the first volley, then one every 8 (6 with Boulderdash: 90 of 90 gaps), seven |
| L + 60 | up (46 with Boulderdash), `npc_mandrill_attack_special_end01` (9751), 52 of 82 digs; the others end early on a melee hit or her death |

A volley: `npc_mandrill_attack_special_ranged01` (9749), five
`toa_baba_balls_travel` (2245) from (40,33) to x 39 on the five rows (start
30, end 90, angle 35, height 50 -> 5), `toa_baba_falling_small_grey_rocks_01`
and `toa_big_baba_boulder_travel_01` delay 90. 2 later a boulder on each row at
x 37: four `toa_baba_boulder` and one `_weak` (516 of 516), its row first in
the middle three (58 of 58) and moving at most two rows after (452 of 458).
Each comes with an exactmove to the next tile and `toa_boulder_rolling` (9518)
and rolls a tile west a tick (teleport + exactmove, yaw 512), gone at x 22 16
after it came (1,921 of 1,921).

A boulder that reaches a raider (its front two columns) knocks them west with
`human_knockdown` (7210) delay 20 and `toa_human_boulder_impact_01` (6033)
(1.83 a kill). RSProx 4292 (rev 241, 7 of 7): the knock is a plain teleport
with no exactmove and no turn, 6 tiles (x 29 -> 23 at [1045]); a raider
already at x 22 is not moved at all (6 of 6, [1052..1100]), the knockdown
coming on the tick the boulder on their tile is removed (A + 16); the hit is
on the next tick every time. The steps after a knockdown there are the
raider's own path to the cracked boulder they clicked (`opnpc2`), not a
push. Still unrecorded: Boulderdash's distance (5) and the hit (14 scaled).
A baboon in its path dies (wiki).

## Her baboons and the sarcophagi

From 75 % (NR; recorded first summons with her bar at 75-85 %), then every
~200 (197, 206, 250): 2 solo or for two, 3 for three, 4 for four to six, 6 for
seven, with `npc_mandrill_spawn02`. Each throws at a raider 4-5 after it comes
and every 3 (`npc_mandrill_attack_ranged_02`, `toa_baboon_ranged_travel_2`
start 67, end 80 + 3 x distance, angle 8, slope 128, height 36,
`troll_with_rock_attack` area range 5). After three throws the one nearest a
sarcophagus (10 of 13) goes for the nearest closed one and whacks it
(`npc_mandrill_attack_melee`, area -1 range 12) every 4; the coffin's bar drops
a third the tick after each (area -1 range 10) and it opens 2 after the third
(`toa_baba_coffin_open`, rotation 1 on the north row, 3 on the south).

An open coffin, from 5 later (3 of 7) every 3 (219 of 222): 2 or 3
`toa_baba_skull_proj` (2246; start 0, end 90, angle 40-50, height 37 -> 0) to
open floor in front of it, `firebolt_impact` (128) height 150 on the coffin a
skull, `fireblast_cast_and_fire` area delay 51 range 8 a tile in front. 3 later
each lands: `wild_falloff_meteor_blast_red` (2265) and `fireblast_hit` area
range 4 with the same delay, 0-6. The coffins close again
(`toa_baba_coffin_closed`) when she dies. The skulls' box and their hit are
NR's (no recorded raider was hit); Gotta Have Faith adds a share of the prayer
the raider is missing (NR).

## Invocations

| Invocation | Recorded / source | |
|---|---|---|
| Boulderdash | 6 kills | volleys every 6, up at L + 46; boulders knock back 5 (NR) |
| Shaking Things Up | 36 kills (5 without) | the larger shockwave, slam 5 later |
| Jungle Japes | 23 deaths with it: a peel on the dying baboon's tile on its death tick (0 of 14 without), rotation 0-3; RSProx 4292 (rev 241, raid 160): **two**, on its tile and the one north, one rotation (2 of 2 deaths) | stepped on at T, the peel goes at T + 1 (29733, its rotation) and the slip shows at T + 2; 4292 [1041..1042]: the raider stood where the baboon died, so the peel went the tick it came and the slip came the next: spam "You slip and fall on a banana skin!", `[facing]` kept, `tob_bloat_stunned` (1575) h124, `royal_human_slip_fall` (4030) delay 20, synth `stunned` (2727) and a hit of 16 (10 x 1.64, path 0). The 5-tick stun is NR's: the raider stood still the 3 ticks to the next boulder |
| Mind the Gap | 2 kills, nobody fell | wiki + NR: a scream or boulder knockback past the wall on rows z 30-34 (the entrance corridor's rows) drops the raider in the pit, dead |
| Gotta Have Faith | 38 kills | the skulls' extra damage (NR) |

## The end

Her death animation in the form after the second dig (or while digging),
removal 4 later and "Challenge complete" on that tick with the sarcophagi's npcs
and everything she left cleared (41 of 41), `rumbling` loops 20 delay 90 to
every raider, and the opened coffins closed.

## Ours vs recorded

Rebuilt 2026-10-19 (second pass): `toa_baba.rs2`, the Ba-Ba blocks of
`toa.constant`, her forms, boulders, rubble, sarcophagi and baboons in
`toa.npc` (sounds, facing, no wander, boulders `blockwalk=none`), the yaw
table `toa.enum`, and `varp7529_toa_baba_slip_at`. The engine gained
`npc_routeapproach` (a sized npc routed round locs and other npcs at its move
speed), `loc_add_revert`, `sound_area(null, ...)` and npc
`defend_sound_range` / `death_sound_range`. Checked with
`test/raids/toa_baba_tells.lua` (no invocations) and
`test/raids/toa_baba_inv_tells.lua` (all five on), scriptrun, `--fixture
test/raids/fixtures/fresh_lumbridge.ini`, solo, raid level 0: the raider stands
off, takes the first throw beside rubble, `::toababahit` takes her to each dig,
50 ticks between them for her baboons to open coffins; a replay checker 434 of
434 and 515 of 515; and every tell kind diffed against the 41 recorded kills
with `tools/rsprox_tells.py --anchor 11778-11788 --ours` (92 rows on both
sides; the only row ours alone sends is the every-room lobby song).

| | Status |
|---|---|
| Pursuit | **fixed**: from S + 6, running, held target, routed round rubble (she cannot walk over it) and npcs, swings from a side. She used to walk at the nearest raider with her south-west corner aimed at them, and stand behind rubble |
| Pursuit, 2026-10-19 later | the chase runs in her `[ai_timer]` (npc phase), from the raider's tile at the end of last tick, walked the same phase. Run from the room tick (a raider's queue, before that raider walks) she chased where the raider had been a tick earlier and the raider chased where she had been: with run on the two swapped two tiles every tick for good. Now she closes and stands |
| Boulders, 2026-10-19 later | a boulder's baboon check reads its three front tiles with `npc_find`; its own `npc_findallany` reset the boulder loop's search, so every boulder after one that struck stood still that tick and the line broke up. A baboon summoned into a lane during a dig now dies to it, goer or not (the goer is not replaced) |
| Knockbacks, 2026-10-19 later | the scream and a boulder drop the raider's attack and hold them (`p_stun`): the first recorded step after a scream is +6 (every recorded scream), after a boulder +2 with its hit on +1 (3 of 3). Unheld, a raider who was hitting her ran straight back at her |
| Attack cycle | **fixed** to the recorded order: rocks, shockwave, swing, throw, swing |
| Shockwave | **new**: both sizes by Shaking Things Up, the stomp, shadows on open floor, the slam's tiles, delays and sounds |
| Rocks and rubble | **fixed**: rocks on every raider's tile, speed by path level, rubble hitpoints by party with `rubble_2`, bars and transformations re-sent every tick, "Ouch!" knockback, cleared on the crossing. They used to fall on random tiles with an Osmumten summon animation on the rubble |
| Throw | **fixed**: source tile, end by form, landing graphics and sounds, the shields (rubble an eighth, coffin and baboon or raider half), rubble absorbing 20 |
| Digs | **fixed**: the leap 2 after the crossing at the recorded yaw, the scream's animation, spotanim and sound, the east face, volleys every 8 / 6 with Boulderdash, up at 60 / 46. The boulders now face west (the script passed a direction where the engine wants a yaw, which turned them south), roll from the tick they come, knock raiders back and kill baboons |
| Baboons and sarcophagi | **new**: summons from 75 %, three throws, the goer's whacks, the coffin's bar and opening, the skull volleys and blasts, the coffins closed at the end; Jungle Japes' peels and the slip |
| Mind the Gap | **new**, unrecorded (nobody fell in the 2 recorded kills): a scream or boulder knockback into the pit rows kills |
| Rates that differ with the test | her defend sound is per raider hit and the test hurts her by cheat; the coffins fire for long stretches because nobody kills the goer; path level 0 keeps her rocks at the slow speed |
| Open: special timing | the cycle's order is recorded; the throw's gap after the rocks varies 11-23 in the recordings and ours follows her attack turns |
| Open: spawn tiles | the summons' tiles are fitted (one near her, the rest on two side tiles two times in three) |
| Open: damages | the throw (the shields' fractions fitted to Expert), rocks, shockwave, boulders, skulls, peels and her swing keep scaled constants; NR's where named |
| Second peel, 2026-10-20 | **fixed** (RSProx 4292, 2 of 2 deaths): a peel on the dying baboon's tile and one on the tile north, one rotation; the slip's hit 10 scaled (16 at 160; was 8) and the line as spam |
| Every room | **fixed** 2026-10-19: Osmumten after the boss and "Proceed", the "Challenge complete" times and song (README, "Leaving") |
