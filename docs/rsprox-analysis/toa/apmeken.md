# Path of Apmeken

Read from 41 visits in 19 RSProx sessions, re-transcribed with every player
shown: 21 solo (4 of them rev 234/235), 5 duos, 10 trios, one of five, three
of six and one of seven. S = the tick of "Challenge started: Path of
Apmeken." (crossing the entry barrier). Tiles are square-local, base
(3776,5248). Full inventory of the first 8 visits:
[apmeken.out](apmeken.out). Common entry/leaving: [README](README.md).

**Ours:** `minigame_toa/scripts/toa_apmeken.rs2`, compared below.

Room song: "Test of Companionship". At entry the raiders take supplies:
potions from `toa_path_apmeken_potions` (19,37), (22,25), (42,39) ("You take
some potions.") and a hammer from `toa_path_apmeken_hammers` (17,36), (22,39),
(42,25) ("You take a hammer."). The middle, 31..33 x 31..33, is the statue:
no recorded raider stands on it.

## Baboons

| Npc | Id | Hitpoints | Attack, every | Projectile (start, end, angle, slope, height) |
|---|---|---|---|---|
| Brawler | `toa_path_apmeken_baboon_melee_1` (11709) / `_melee_2` (11712) | 25 / 30 | `npc_mandrill_attack_melee` (9742), 4, beside the raider | — |
| Thrower | `_ranged_1` (11710) / `_ranged_2` (11713) | 30 / 35 | `npc_mandrill_attack_ranged_02` (9745), 4, from up to 5 | `toa_baboon_ranged_travel_1` (2242) / `_2` (2243): 67, max(80, 67 + 4d), 14, 32, 17 → 36 |
| Mage | `_magic_1` (11711) / `_magic_2` (11714) | 20 / 25 | `npc_mandrill_attack_magic` (9746), 4, from up to 5 | both `toa_baboon_magic_travel_1` (2247) (10.02 a room = 4.80 + 5.22 casts): 30, max(50, 37 + 5d), 15, 50, 20 → 34; on the raider's tile as it lands `firebolt_hit` range 4, or `spellfail` (227) when nothing gets through |
| Shaman | `_shaman` (11715) | 16 | every 8, from up to 7: a summon (`npc_mandrill_attack_magic_02` (9747), `bind_all` range 6) while it has fewer than 4 thralls (107 of 111), else a magic attack (12 of 13) | as the mage's, but `toa_baboon_magic_travel_2` (2248), its own (0.41 a room = its 0.41 casts) |
| Thrall | `_thrall` (11718) | 2 | melee, 4 | added the tick after the summon within 3 tiles, with `nex_summon` (2017) and `toa_amp_baboon_circle_03` range 4 |
| Volatile | `_zombie` (11716) | 8 | none: beside its raider at A, `npc_mandrill_explode` (9756) at A + 2, the blast and its removal at A + 4 | `fireblast_impact` (131) on its tile, `fireblast_hit` delay 5 range 6, and a 3x3 `spotanim_zebak_jugbreak_npc` (11745) one tile south-west (centred on it) facing where the volatile faced, with `spotanim_mandrill_explode` (2249), gone 2 later; baboons within a tile lose what they have left |
| Cursed | `_cursed` (11717) | 10 | none: wanders and leaves venom | |

Every baboon walks a tile a tick, faces its raider on spawn, and the ranged
and magic ones start 2 after spawning. Death: `npc_mandrill_despawn02` (9755)
and an area sound -1 range 8 on its tile (its empty death sound, every death),
then removal 3 later (1,893 of 1,893), or 1 later for a killed volatile (189
of 189). Hit by a raider, `toa_mandrill_defend_01` (5977) to that raider
(46.2 a room).

## Waves

The rev 234+ room has **eight waves** (4 of 4 rev 234/235 raids); rev 223/227
had two more in front (brawlers x c, then throwers x (c - 1) and a mage),
otherwise the same. With c = 2 solo and 3 for any party (2 to 7 seen), in
spawn order:

| Wave | Spawns |
|---|---|
| 1 | brawler x c, shaman |
| 2 | thrower x c, volatile |
| 3 | mage x c, cursed |
| 4 | large thrower x c, cursed, volatile, R |
| 5 | large mage x c, volatile, shaman, R |
| 6 | large brawler x c, shaman, cursed, R |
| 7 | large brawler, large thrower, shaman x (c - 1), cursed, cursed |
| 8 | shaman, volatile x c, R, R |

R is a shaman, volatile or cursed, evenly (185 draws: 58 / 68 / 59; all nine
pairs seen in wave 8). Each baboon takes a hole of its own at random; the six
holes are x 24 / 32 / 40 on z 24 and z 40. 370 of 370 recorded waves.

**When.** Wave 1 on S + 16 (4 of 4). Wave k (k ≥ 2) comes on the first tick
at least 4 after the last wave on which either

- the uncleared count (wave baboons added and not yet removed; thralls do not
  count, the cursed do) is at most MIN, or
- it is at least 8 after the last wave and fewer combat baboons than MAX are
  left,

with p raiders, in integer division: MIN = 2 + (p - 1) / 3 for waves 2 to 6,
2 + (p - 1) / 2 for wave 7, 3 + 2(p - 1) / 5 for wave 8; MAX = 3 for seven
raiders and above, else 0 (the second branch is seen only in the seven).
Recorded on that tick or the one after it: 352 of 352 for one to six raiders,
360 of 361 in all (the miss is the seven's).

The room completes on the tick the last baboon of wave 8 (thralls too) is
removed: "Challenge complete", the exit barrier (44, 31..33) to
`invisible_type8_nonblocking` (29733) rotation 1, every venom pool still down
to 29733 rotation 0, and every raider's tint reset (every visit).

## Venom pools (the cursed baboon)

Each tick the cursed baboon leaves `toa_path_apmeken_venom_pool` (45493),
shape 10, rotation random 0-2, with `barbassault_splat` range 4, on the tile
it stood on at the start of the previous tick (683 of 763), never on a tile
still pooled (2,910 of 2,910 pools fresh). A pool lasts 30 ticks (649 of 763;
the rest were cut short when the room completed), then the tile gets
`invisible_type8_nonblocking` (29733) shape 10 rotation 0 (763 of 763).

A raider in a pool on tick t takes a venom splat on t + 1, one for every tick
in it (18 of 18, 6 to 16). "You have been poisoned by venom!" comes only on
the first tick of a stay (10 of 11; none of 7 on a tick that followed one in a
pool).

## Apmeken's Sight

Granted on S + 2 to the raider who started the room ("You have been granted
Apmeken's Sight." / "<col=3366ff>X has been granted Apmeken's Sight.", with
`toa_amp_skull_appears_01` to everyone, 30 of 30); with a
party it passes to another raider 120 after it was given, and the next warning
then comes 10 after the pass. The first warning is on S + 30, each next one 50
after the last less 2 for every two waves out (waves 1-2: 50, 3-4: 48, 5-6:
46, 7-8: 44), counting the waves spawned before the warning: 111 of 111. The
answer comes 20 after the warning.

With a party only the holder is told what is wrong and sees where; the others
get "<col=a53fff>You sense an issue somewhere in the room."

| Event | Holder's warning | Markers | Fixed by | Missed, 20 after the warning |
|---|---|---|---|---|
| Fumes | "<col=a53fff>You sense some strange fumes coming from holes in the floor." | `toa_path_apmeken_sight_warning02` (2135) at (24,28), (40,28) and `_warning` (2134) at (24,36), (40,36), height 46 | pouring a potion on a vent: "<col=229628>You neutralise the fumes coming from the hole." ("The fumes from this hole have already been neutralised."; off a vent "You pour the potion on the floor, but nothing happens."); min(p, 4) vents | "<col=ff3045>The fumes filling the room suddenly ignite!", `toa_path_apmeken_gas` (2138) at each vent left |
| Roof | "<col=a53fff>You sense an issue with the roof supports." | 2134 at (28,40), (36,40), 2135 at (28,24), (36,24), height 96 | the hammer on a pillar: "<col=229628>You repair the damaged roof support." ("This roof support has already been repaired."); p pillars, at most 4 | "<col=ff3045>Damaged roof supports cause some debris to fall on you!", the pillars left to `toa_path_apmeken_pillar_shaking` (45495) rotation 3 and back to 45494 4 later, `toa_path_apmeken_rubble` (2139) on each raider |
| Corruption (party only) | "<col=a53fff>You sense Amascut's corruption beginning to take hold."; every other raider tinted (0, 600, 0, 6, 24, 112) | | the holder pours where a raider stands: they get "<col=229628>X neutralises your corruption." and the tint cleared (0, 0, -1, -1, -1, 0), the holder "<col=229628>You neutralise X's corruption." | "<col=ff3045>Your group is overwhelmed by Amascut's corruption!" and every raider tinted (0, 50, 0, 6, 36, 112) (9 of 9) |

Answered: "<col=3366ff>Apmeken's Sight guides you|your group into
neutralising some dangerous fumes." / "...repairing the roof supports." /
"...overcoming Amascut's corruption.", with `found_gem` to everyone. A repair
plays `poh_build_wood`. A wrong fix (a vent when the roof is
asked, a pillar when the fumes are) sets that one off on its own. The warning
plays `toa_amp_skull_appears_01` to everyone. A miss cost 20 at Entry solo
(fumes, 1 of 1) and 12 at Expert with six (1 of 4).

## Ours vs recorded

Rebuilt 2026-10-19: `toa_apmeken.rs2` (a ticking room from `~toa_room_tick`,
latched once a tick), the Apmeken blocks of `toa.constant`, the baboons in
`toa.npc` (defend and death sounds), and `varp7528_toa_apm_pool_at` in
`toa.varp`. The engine gained what the room needed to send what Jagex sends:
`spotanim_map_specific` (MAP_ANIM_SPECIFIC to one player), `p_tint` (the player
tinting block, drawn by the client), `loc_add_revert` (a timed loc that ends on
another loc), and npc `defend_sound_range` / `death_sound_range` (area sounds,
-1 included). Checked with `test/raids/toa_apmeken_tells.lua` (scriptrun,
`--fixture test/raids/fixtures/fresh_lumbridge.ini`, solo, raid level 0: the
supplies, one pour off a vent, all eight waves with `::toaapmkill` taking one
baboon every 7 ticks, every event answered but the second) against the rules
above with a replay checker: 974 of 974; and every tell kind diffed against
the 41 recorded visits with `tools/rsprox_tells.py --anchor 11709-11718
--ours`. The wave table and MIN/MAX are also checked by the selftest (`toarun`).

| | Status |
|---|---|
| Waves | **fixed**: the eight-wave table with R even, wave 1 S + 16, each next on the tick the room thins to MIN. It was a fixed timer of ten waves |
| Baboons | **fixed**: reach and cadence, projectiles (both mages `_travel_1`, the shaman `_travel_2`), `firebolt_hit` or `spellfail` by what lands, summons and thralls, the volatile's fuse, blast and jugbreak burst, the deaths with their -1 death sound, the defend synth to the hitter |
| Venom | **fixed**: a pool a tick behind the cursed baboon, never on a pooled tile, 30 ticks, ending as 29733 rotation 0; a splat the tick after each tick in one, the message only on the first |
| Sight | **fixed**: grant S + 2 with its skull synth, warnings S + 30 then 50 less 2 a pair of waves, the markers to the holder alone (map_anim_specific), answer 20 later with `found_gem`, the miss, the repair's `poh_build_wood`, the pass with a party |
| Corruption | **fixed**: the holder's line ("beginning to take hold"), the tint on the corrupted, cleared on a cure with the holder's "You neutralise X's corruption.", the miss's tint on everyone |
| Completion | **fixed**: on the last removal, the exit to 29733 rotation 1, the pools cleared as they would end, every tint reset |
| Rates that differ with the test, not the room | the test keeps thralls alive (killing one baboon every 7 ticks) so its shamans reach 4 thralls and cast more often than the recorded (where raiders kill thralls at once); the test raider prays nothing, so `spellfail` is rarer; per-window counts compare one long run with 41 short visits |
| Open: seven raiders | MIN/MAX miss one recorded wave of the seven (360 of 361) |
| Open: party | the pass, the corruption event and MIN/MAX above one raider run only on the recordings, not on a party test |
| Open: damages | the baboons' hits, the venom splat and the misses keep scaled constants; the recorded hits are few and at unknown raid levels |
| Open: hammer message | "You need a hammer to repair this." is ours; no recording clicks a pillar without one |
| Open: venom message | the first-tick rule is in the script; the tick log does not carry chat, so the checker cannot see it |
| Open: tint on screen | the client draws the tint (ported from the rev-239 deob, unit-tested); not yet seen in a renderer crop |
