# Ocellus: spec from RSProx

Spectated: index 4301 [875..1943] (three raiders) and 4308 [1458..2382]
(B0aty's run, four). The wiki (`sources/Ocellus.wiki`,
`sources/TFA_Strategies.wiki`) has the shape of the four phases. He is 3x3;
offsets are of his south-west tile from the arena's middle (4127,5673,1),
local (31,41) of square (64,88). Tools: `tools/ocellus_view.py` (`--boss` his
tile, seqs, spots and lines; `--list` / `--grid` the map anims, locs and map
projectiles of a window).

## 1. The room

| fact | evidence |
|---|---|
| Raiders arrive at (4126,5636,1), local (30,4), facing north up the hall; the arena is 33 tiles on | 4301 [876..892] |
| Added with the room: `tfa_ocellus_intro` ("Hooded Figure" until `varb16384_tfa_ocellus_met`) at (-1,-4) facing north, `invisible_type8_blocking` on his 9 tiles; the four censers `tfa_ocellus_artifact_heal` (11,-11), `_damage_ranged` (-11,-11), `_damage_melee` (11,11), `_spec` (-11,11) | 4301 [875], 4308 [1458] |
| Until the fight, `vfx_ocellus_sword_repair_particles_01` at the middle every 3 ticks | 4301 [878..911] |
| Talk-to (not met) / Proceed (met) start it; no recording holds his dialogue (both are spectators') | |
| The start: "Now... ascension...", he becomes `tfa_ocellus_p1`; his 9 tiles `invisible_type8_nonblocking`; the way in shut: `tfa_wallkit_archive03_entrance02_closing` (0,-13) shape 22, `_closed` +3, `inviswall_blockrange` rot 1 on (-1..1, -13) and (-1..1, -14); +1 the bar (health and the meter, extra base 96), song 893 | 4301 [913..916], 4308 [1500..1503] |

## 2. Phase 1

His health: 2500 at 1-2 raiders (3630 at three, 4680 at four). The route, the
same in both fights to the tick:

| when | recorded |
|---|---|
| start +4 | he walks to the south-east corner (sw (5,-7)), a volley every 5 ticks from his first step |
| the last volley (him 0-1 tiles from the corner) | played as `npc_ocellus_phase1_basic_projectile_end` (the others `_start`) |
| that +6 = C | the charge: `npc_ocellus_phase1_charge` and a crack line every 2 ticks, seven (C..C+12); `vfx_ocellus_p1_charge` every 2 from C+5; "Pointless..." or "Unworthy..." at C+15 (either: 4301 both, 4308 "Pointless..." x3); `npc_ocellus_phase1_explode` at C+18 with `vfx_ocellus_p1_explode_npc` |
| C+19 | the blast: `vfx_ocellus_p1_explode_floor` (a random one of `_90/_180/_270`) on every open tile beside his 3x3 at delay 0 and the next ring at delay 2 (6/6) |
| C+21 | the next corner: north-east (5,5), then (after the middle) south-west (-7,-7) |
| health at or under 66% (checked after a corner's explosion), and at a quarter (at once if he is walking: 4301 [1040]) | `npc_ocellus_phase1_teleport_out` + `vfx_ocellus_p1_teleport_out` slot 2; +4 he is at the middle (sw (-1,-1)) with `_teleport_in`, and `tfa_wallkit_archive03_center01_blockwalk` on his sw tile |
| +8, then every 8, four beats B | the charge seq and two diagonal cracks from neighbouring corners of his 3x3, clockwise from a random pair (the earlier at delay 0, the later at 60); `vfx_ocellus_p1_charge` at B+4 and B+6; the explosion at B+6, the blast at B+7 |
| the last explosion +2 | "Death... awaits..." (every recorded strike); `tfa_wallkit_archive03_center01` back on his tile; the meter emptied |
| +1 | every raider healed 4..8 (hitsplat 6) |
| +2 | `npc_ocellus_phase1_expend`; a `scriptedproj_add` of `ocellus_proj_test` at each raider (2 / 39, curve scripts 10024 / 10018 / 10017) |
| +3 | each raider hit 17..19 (13/13) |
| +8 | he walks on to the next corner |
| nothing left | "You... will disappear..."; if not at the middle: teleport out +2, at the middle +4, in +5; `npc_ocellus_phase1_transformation` 3 ticks after he is at the middle; +7 phase 2 (§3) |

**Volleys.** A shot at every raider: fire (`vfx_ocellus_fire_attack01_projanim`,
the wiki's orange, ranged) or water (`_water_`, blue, magic), one element a
walk, with `vfx_ocellus_<element>_attack01_spotanim` on him; 30 / 95 within 4
tiles, 30 / 125 beyond (46/46), angle 40, progress 0, heights 578/124 (ours
144/31).

**Cracks.** A line grows from its far end toward him: the start piece at delay
60, each tile a cycle later; it closes 5 ticks on (4 / 6 for the middle's
immediate / later line) with the `_end` pieces at delay 0, 1, ... The pieces
are named by the way the line grows: `vfx_ocellus_p1_floor_crack_01` straight
north (`_90` east, `_180` south, `_270` west), `_02` diagonal north-east
(`_90` south-east, `_180` south-west, `_270` north-west), each with `_start`
and `_end` forms. At a corner, eight long: along one of his rows or columns
from the inner side, or a diagonal to his inner corner or a tile beside it
(4301 [928..940], [965..977]). At the middle, six long, from beside one of his
corners.

**The meter (the yellow bar).** A line that closes with nobody on it adds 2
(4308 [1520], [1522], [1585], [1640], [1668]; 4301 [970]); he heals 2
(hitsplat 6) and a sound plays where the line meets him (12512 or 12513, r20
size 5). A raider on a closing line blocks it and takes 3..5 (4308 [1557..1569]:
every raider every 2 ticks). The strike empties it. The recorded meters were
0..6 at the strikes, and every strike was "Death... awaits..." with one shot
a raider for 17..19. The strike scales with the bar's height (the owner, and
the wiki's "80+ ... Embrace... annihilation..." when it is high, about five
such shots); ours: one shot more a raider for every 8 on the meter, "Embrace...
annihilation..." when there is more than one (unverified past 6).

**Damage** beyond the strike and the chip: unread (the transcripts' hits are
not attributable here); ours a volley shot 0..25 under the matching prayer, the
blast 20..40 within 2 of his 3x3 (the wiki's AoE; 4308 [1588]: 28, 30).

## 3. Phase 2

A fixed script from his change T, the same to the tick in both fights (4301
T = [1104], 4308 [1713]); the raiders' hits go to The Blade, whose damage is
his (4301: 7610 -> 7018 across it, carried on). His health from here: 5250 at
1-2 raiders (7610 at three, 9820 at four).

| T+ | recorded |
|---|---|
| 0 | `npc_ocellus_phase2_transformation` + `vfx_ocellus_p1_to_p2_transformation` slot 2, he becomes `tfa_ocellus_p2` (no ops); the meter hidden |
| 2 | 24 `tfa_ocellus_quadrant_wall_inactive` portals, two wide, on a "+" through the middle: north arm (0, 1/3/../11) rot 3, east (1/3/../11, 0) rot 0, south (0, -2/-4/../-12) rot 1, west (-2/../-12, 0) rot 2; `center01_blockwalk` under him |
| 7 | "Your time... comes..."; `tfa_the_blade1_idle0` at the middle and `tfa_ocellus_p2_sword_hitbox` ("The Blade") on his tiles |
| 11, 53, 95 | a round R (below); the quadrant the raiders stand in first, then the next clockwise (NE, SE, SW, NW): 4301 SW, NW, NE; 4308 SE, SW, NW |
| every 3 till 123 | `vfx_ocellus_sword_repair_particles_01` at the middle |
| 123 | (round 3 cut short) "This... is enough..."; `npc_ocellus_p2_to_p3_part1`, `vfx_ocellus_p2_to_p3_arm_glow01` d150, `vfx_ocellus_sword_root_fire` slot 5, `vfx_ocellus_p2_to_p3_arm01` slot 2 d150; The Blade gone; 4428 r15 s15; `invisible_type8_nonblocking` and `vfx_ocellus_sword_drop` at the middle |
| 124 | 444 r10; `tfa_arena_sword_drop_explosion` |
| 127 | the hall's walls come down: `tfa_wallkit_archive03_straight_collapse_01..04` at (-1,-13), (-13,-1), (-1,3), (3,-1); `invisible_type8_blocking` (-1,-15), `invisible_type0_nonblocking` (0,-13) |
| 131 | `npc_ocellus_p2_to_p3_part2`, `vfx_ocellus_null` h154 d300 (slots 0 and 2), he becomes `tfa_ocellus_p3` |
| 132 | the 24 portals deleted; `invisible_type8_blocking_size5` (-2,-2), `_size3` (-1,-1); each arm breaks: `tfa_arena_collapsing_explosion02/03/04/01` 3/6/9/12 out at delays 0/15/30/45; stepping stones `tfa_ocellus_platform_jump_outer_*` at 4 and 10, `_middle` at 7 |
| 135 | `inviswall_blockrange` along the chasms' edges (phase 3, §4) |

A round R, on the quadrant's two arms:

| R+ | recorded |
|---|---|
| 0, 5, .. 25 | ten of the twelve portals `_active`, a gap on each arm (the wiki's orange portals); the gaps' rows cross at the safe spot |
| 4, 9, .. 29 | each lit portal fires across the quadrant along its two rows (or columns): `vfx_ocellus_portal_projectile` 0 / 6 a tile less 6, angle 127, progress 250, heights 100 (ours 25), from a pair of invisible `tfa_invisible_npc_2x2` on it (deleted +3); the portals go dark |
| 28 | 13 `vfx_ocellus_p2_aoe_dodge` pools on the quadrant; (round 1) `tfa_the_blade1_idle2` |
| 31 | all twelve lit |
| 33 | `vfx_ocellus_p2_safe_portal_spawn` on a pool's tile; `_idle` every tick to R+39 |
| 34 | `vfx_ocellus_p2_sword_discharge` d30 on him; 4428 r15 s15 |
| 35 | 444 r10 |
| 34..39 | a raider on the safe portal is put on a random tile of the next quadrant (4301 [1153], 4308 [1761], [1763]) |
| 40 | all twelve fire (0 / 5 a tile less 5); `_despawn` |

A raider in a fired line is hit (4301 [1119..]: 42, 55; ours 40..55, so off
the safe spot, under two lines, they take ~100); on a pool 3..5 a tick (the
wiki's damaging pools; unverified).

## 4. Phase 3 (P = his change to `tfa_ocellus_p3`)

**The arena breaks** (P+4, 4301 [1239]): the hall's collapse settles
(`tfa_wallkit_archive03_straight_collapse_01..04_empty`); each arm becomes a
chasm with `inviswall_blockrange` down both sides 2..12 out (N/S arms x -1 rot 0
and x 1 rot 2, E/W arms z 1 rot 1 and z -1 rot 3; 88), `invisable_nonblocking_wall`
where its stones meet the banks (8). Each arm's three stones (4, 7, 10 out)
are the only way over: Jump (op1) from the bank onto the near stone, stone to
stone, off the far stone onto the bank it is named for; `human_yama_shortjump01`,
a teleport and a 27-cycle exact move (4301 [1327..1338]: four hops a raider, 12
a raider over the phase).

**Segments.** He jumps (P+6) to the corner of the quadrant the raiders hold;
three segments, each ending in that quadrant's destruction, then the next
counter-clockwise (4301 NE, NW, SW; 4308 NW, SW, SE). From the segment's start
S (the same in both fights to a tick or three):

| S+ | recorded |
|---|---|
| 0 | he is at the corner |
| 1 / 2 | the quadrant's barriers `tfa_ocellus_quadrant_barrier_01/02` (`_spawn`, then standing): north-east's at (8,1) r1 and (1,7) r2, each quadrant clockwise turning them a quarter |
| 6 (and 9 in the first) | void bombs, one on each standing quadrant: `vfx_ocellus_quadrant_bomb_idle_01` h255 at 0 and 30; +2 `_drop_01` and `_impact_01` and a `tfa_ocellus_void_degen_bomb` that stays |
| 4, 14 | melee triples: `npc_ocellus_phase3_melee_basic`, +3 the same, +6 `_melee_slash` (a short line of `vfx_ocellus_p3_shadow_slash_spotanim01` toward the target, delay 2 a tile); he walks after his target between |
| 24 | the spin: `npc_ocellus_phase3_prayer_in` + `vfx_ocellus_phase3_melee_prayer_in` slot 4; +1 a hit on everyone their prayer blocks (13/13 0); a loop (`_prayer`, `_prayer_loop`) for each quadrant already destroyed; `_prayer_out`; a hit of 13..23 on everyone 4 ticks in and at each loop |
| 33 (36 later) | "Your panic... is futile...": `npc_ocellus_phase3_basic_projectile_all`, `vfx_ocellus_magic_spotanim01` slot 4 d30, `vfx_ocellus_magic_projanim01` at everyone 45/60 a100 p100 435/124 (ours 109/31); a `tfa_ocellus_void_degen_aoe` patch (21..51 ticks) |
| 36 (39 later) | the void vine: "The void... will take you..." or "Await... the void..."; `tfa_ocellus_p3_void_vine` (44 hp) on a raider's tile, rooting them; 14 ticks to cut it (the wiki: 50 if not) |
| (later segments) 59 | "Insolence...": three `npc_ocellus_phase3_melee_superior_slash` d30, 5 apart, each a band 4 wide of `_superior_spotanim01` toward the target, its debris `tfa_ocellus_void_degen_aoe_insolence_1/4/7` (near, middle, far; random turns) for 16 ticks (56/56) |
| (later segments) 3, 51, 111 | one or two void rifts (`tfa_ocellus_p3_bombportal_basic` "Void Rift" / `_buff` "Void Augmenter", 44 hp), spawn seq, despawn seq +10, gone +12 |
| 53 (first) / 134 (later) = U | "Untether...": he jumps to the corner with an exact move (old, 0, new, 90, the yaw), `npc_ocellus_phase3_unleash_in`; U+2 `_unleash_loop`; pools `vfx_ocellus_p2_aoe_dodge` (10..15) on the quadrant at U+6, 10, 14, 18; U+18 `vfx_ocellus_p3_unleash_spotanim` d360; U+25 `vfx_ocellus_unleash_arena` (NE `_180`, NW `_90`, SW plain) over it; U+29 `_unleash_out` |
| U+31 | "Obliteration..." or "Run... It matters not..."; `npc_ocellus_quadrant_destroy_in`, `vfx_ocellus_phase3_arena_collapse_in` slot 2; the barriers' `_despawn` |
| U+33 | `_destroy_loop`, `_arena_collapse_loop` slot 2 (looping) |
| U+42 | "You can't... escape... the end..." or "This end... comes..."; `npc_ocellus_phase3_unleash_out_90f`, `_arena_collapse_out_01` slot 2, `_out_02` slot 4 |
| U+44 | the quadrant gone: `invisible_type0_blocking` on its 84 tiles, its wall's `tfa_wallkit_archive03_slice_collapse_<quadrant>_01..03` |
| U+48 | the next segment; after the third, U+49 "End..." (phase 4) |

His health holds at 75, 50 and 25% until each quadrant falls (4301: 74.9,
48.4, 25.8% at the destructions; 4308: 74.4, 48.5, 25.1%).

**The censers** (one per quadrant's outer corner, 11 diagonal from the middle:
NE Bandosian `_damage_melee`, NW Zarosian `_spec`, SW Armadylean
`_damage_ranged`, SE Saradomin `_heal`; "Interact"). Each is lit once, in
phase 3, while the raiders hold its quadrant (4301: NE, NW, SW in segment
order; 4308: NW, SW, SE, then NE a tick before "End..."). Nobody walks to one:
the raiders are 6-7 tiles off, across the chasm (4301 [1438] Visqo at
(4117,5677) for (4116,5684)), so it answers at range (`aploc1`). U = the use
(7/7: 4301 [1308], [1438], [1564]; 4308 [1858], [1950], [2244], [2259]):

| U+ | recorded |
|---|---|
| 0 | `loc_anim` `ornament_censer_activate` on it |
| 1 | every raider in the room: spotanim slot 1, the censer's god: `sp_attackglow_bandos` (melee), `_zaros` (spec), `_armadyl` (ranged), `_saradomin` (heal) |
| 3 | `tfa_ocellus_artifact_*_inactive` on its tile |

What each gives is the wiki's guess, to everyone: 50 health and 50 prayer;
half a special attack; the next melee or ranged hit a max hit.

## 5. Phase 4 (E = "End...")

| E+ | recorded |
|---|---|
| 0 | he is at the last quadrant's corner, his 9 tiles blocked; its barriers (`_spawn`, +1 standing); +2 the stones of the arm clockwise of it blocked |
| 3, then every 60 | `npc_ocellus_phase3_deflect_in`; +2 `_deflect_loop`; +3 `_soul_fragment_start`; +4 `vfx_ocellus_soul_fragment_ocellus_levitating_idle` slot 2 (looping) and the wind: every raider hit for half their health (4301 41/40/49, 4308 39/49/46/55) |
| +21 | `_deflect_out`; the wind given back (the wiki's orb: 41, 40) |
| +24, +52 | `npc_ocellus_phase3_melee_hybrid` with `vfx_ocellus_phase3_melee_hybrid_magic` or `_ranged` slot 2; +5 everyone hit, 5..7 under the matching prayer (4301 [1739], 4308 [2292]: 7 x7) |
| +34 | `_soul_fragment_ocellus_idle` slot 2, `_soul_fragment_start`; +1 the wind again; +13 it is given back; +16 `_soul_fragment_end` |
| +59 | void rifts: two, three, four (4301 [1769], [1829], [1889]) |

The wiki's void bursts (every 2 ticks, six times, at a raider who stands
still) and their orbs are each raider's own and never reach a spectator.

## 6. Death (K)

| K+ | recorded |
|---|---|
| 0 | "Challenge complete: Ocellus. Duration: ... Total: ..." and "The Fractured Archive: Total completion time: <col=ef1020>m:ss</col>"; his song stopped; `npc_ocellus_phase3_death`, `vfx_ocellus_death_skirt_spotanim01` d1, `vfx_ocellus_death` slot 5 d1, `vfx_ocellus_death_crack_spotanim01` slot 3 d1; ops cleared |
| 7 | seq -1, his body hidden |
| 14 | `tfa_blade_npc_vault_entry` ("The Blade": Commune, Travel) on his tile |
| 16 | he is deleted |

## Audit: phase 1, our scriptrun against 4301, 4308 (2026-10-21)

Run: `test/guthixian_archive/tfa_ocellus.lua` (solo; `::tfaroom 6 4`, up the
hall, Talk-to; `::tfaboss` to 66%, a quarter, 0). Inventory:
`tools/rsprox_tells.py --anchor 16674,16671 idx4301.txt idx4308.txt --ours
<ticklog>`.

| tell | recording | ours | verdict |
|---|---|---|---|
| room | the intro npc, 9 blocking tiles, 4 censers | same | matched |
| start | the line, the type, 9 tiles open, the entrance closing/closed, 6 blockrange walls | same | matched |
| walks | a volley every 5 from the first step, `_start` / `_end`, fire or water a walk, 30/95-125 a40 144/31 | same | matched |
| corners | charge seq + crack every 2 (7), spot from +5, the line +15, explode +18, blast +19 (rings 0 / 2, 4 turns) | same | matched |
| cracks | every piece name and turn, delays 60.. / 0.. | same names, our lines random per the rules | matched |
| middle | out, +4 in + `center01_blockwalk`, four beats 8 apart, the strike (+2 line + `center01`, +3 heal, +4 expend + shots, +5 hits) | same | matched |
| meter | +2 a missed line with heal 2 and 12512/12513 r20 s5; chip 3..5 to a blocker | same | matched |
| strike | `scriptedproj_add` (curve scripts) | a plain projectile of `ocellus_proj_test` | **open (engine: no scripted projectiles)** |
| strike size | one shot a raider at meters 0..6 | 1 + meter / 8 | unverified past 6 |
| head icon | `headicon_customisation` 440 on the start | not sent | **open (engine)** |
| music | `midi_song_v2` with fades | plain `midi_song` | **open (engine)** |
| sound -1 | `sound_area id=-1` delays 77..107 near him at some line closings | not sent | open (unexplained) |

## Audit: phases 2-4 and the death (2026-10-21)

Same run (phase 2 standing on the safe spot by `::tfaocsafe`, phase 3 shooting
him and leaving each quadrant before it falls). Inventories with
`--anchor 16675,16676` (phase 2) and `16677,16681,16678,16680` (3, 4).

| tell | recording | ours | verdict |
|---|---|---|---|
| phase 2 script | portals, toggles, volleys, pools, safe portal, discharge, the change at T+123..T+135 | same, to the tick (T = [383]: T+123 [506], T+131 [514]) | matched |
| portal volleys | from pairs of invisible 2x2 npcs, deleted +3 | same | matched |
| The Blade | its hits are his (7610 -> 7018) | forwarded | matched |
| phase 3 segments | barriers, bombs (idle, +2 drop), melee triples, spin, volley, vine, Insolence, rifts, Untether, destruction, slice collapse, floor | same schedule | matched (geometry of slashes and pools approximate) |
| chasms | 88 blockrange walls, 8 open, the `_empty` collapses | same | matched |
| stones | Jump: teleport + 27-cycle exact move | same | matched (the bank-to-stone exact move's end is the stone, not the recording's half way) |
| void cleanup | debris and patches replaced by `invisible_type0_nonblocking` (same turn) when they end | the engine's timed loc removal (`loc_del`) | **open** |
| phase 4 | barriers, the 60-tick cycle, the wind and its return, hybrids, rifts | same | matched (the bursts and orbs private: per the wiki, unverified) |
| death | the lines, the spots, +7 hidden, +14 The Blade, +16 gone | same (+7 body hide not sent) | matched but the body hide |
| loop flags | `loop=true` on the levitating and collapse loop spots | no loop byte | **open (engine)** |
| head icons | the deflect / soul split prayers as `headicon_customisation` | not sent | **open (engine)** |
| damage | mostly unreadable (hit attribution in these transcripts) | per the rows above | approximate |
| censers | Interact at range in phase 3; U `ornament_censer_activate`, U+1 the god's `sp_attackglow_*` slot 1 on every raider, U+3 the inactive form (7/7) | same ([546], [547], [549]) | matched (the gifts are the wiki's; a melee/ranged blessing makes the raider's next hit of that style their max hit through `~player_hit_npc_prepare`) |
