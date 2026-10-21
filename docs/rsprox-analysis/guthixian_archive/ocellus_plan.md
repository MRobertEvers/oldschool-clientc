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

His health: 2500 at 1-2 raiders (3630 at three, 4680 at four). The timetable
(the route itself is random, below):

| when | recorded |
|---|---|
| start +4 | he walks to his first corner (SE (5,-7) in 4301, 4308, 4312; NW (-7,5) in 4309, 4312, 4316; SW in 4315), a volley every 5 ticks from his first step |
| the volley his step reaches the corner on (or 1 short) | played as `npc_ocellus_phase1_basic_projectile_end` (the others `_start`), shots and spot as usual; the middle faced +1 and +5; C = +6 (16/16) |
| a volley's tick with him already standing at the corner (a walk of 8 or 12 tiles arrives 2 or 4 ticks before it) | the `_end` seq alone (no shots, no spot), the middle faced and his overhead cleared on it; C = +1 (4309 [1386] [1423], 4312 [3591] [3628], 4315 [5975], 4316 [1913] [1950]: 7/7) |
| C | the charge: `npc_ocellus_phase1_charge` and a crack line every 2 ticks, seven (C..C+12); `vfx_ocellus_p1_charge` every 2 from C+5; one of "Pointless..." (9), "You... fail..." (8), "Unworthy..." (4) at C+15 (21 corners); `npc_ocellus_phase1_explode` at C+18 with `vfx_ocellus_p1_explode_npc` |
| C+19 | the blast: `vfx_ocellus_p1_explode_floor` (a random one of `_90/_180/_270`) on every open tile beside his 3x3 at delay 0 and the next ring at delay 2 (6/6) |
| C+21 | the next corner: one of the other three (19 recorded: the next anticlockwise 10, clockwise 3, across 6; the four that went by way of the middle were all across); he keeps it through a visit to the middle |
| health at or under 66% or a quarter: checked after a corner's explosion, and while he walks (or waits at the corner) on each volley's tick, in its place (4301 [1040], 4309 [1546], 4312 [1508] [3660], 4315 [6088], 4316 [1982] [2073]: 7/7) | `npc_ocellus_phase1_teleport_out` + `vfx_ocellus_p1_teleport_out` slot 2; +4 he is at the middle (sw (-1,-1)) with `_teleport_in`, facing the side of his first cracks with `instant=true` (16/16), and `tfa_wallkit_archive03_center01_blockwalk` on his sw tile |
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
and `_end` forms.

**A corner's seven are three tendrils**, each growing from one of his three
tiles on the middle's side, taken in turn: line n comes from the same tendril
as line n-3 (21/21 charges cycle the three; 0/129 lines repeat either of the
two before, 40/91 repeat the one three before). In his corner's frame:

| tendril | grows from | its lines (recorded) |
|---|---|---|
| x face | the middle tile of his face toward the middle's x | straight along his middle row, or diagonal past his inner corner ending beside it on that face |
| z face | the same toward the middle's z | straight along his middle column, or diagonal ending beside the corner on that face |
| corner | his inner corner | along the inner row, the inner column, or diagonal past the corner |

The faces are 62 straight / 45 diagonal, re-rolled each time (a tendril's next
does not depend on its last: 12/18 and 8/13 straight after straight and after
diagonal); the corner 20 diagonal, 13 / 9 along (taken as even). The first
line is always the same face of one pattern turned with his corner: the z face
at NE and SW, the x face at SE and NW (21/21; the outer row or column is never
used because no tendril grows from there). Then the corner and the other face,
either order (10 / 11). They are not aimed (a raider stood on 70 of 149 the
tick before their add; no rule over t-2..t).

**At the middle, the same pattern every time**, two six-long diagonals on one
face of his 3x3 a beat, a quarter clockwise each beat, the first at delay 0
and the second at 60 (every beat of six sessions: 64/64), ending at (of his sw):

| face | first (delay 0) | second (60) |
|---|---|---|
| east | (3,2) | (3,0) |
| south | (2,-1) | (0,-1) |
| west | (-1,0) | (-1,2) |
| north | (0,3) | (2,3) |

The face he starts on varies (east 4, north 4, west 3, south 3 over 14 visits;
not the raiders' side); his landing face says which.

**The meter (the yellow bar).** A line that closes with nobody on it adds 2
(4308 [1520], [1522], [1585], [1640], [1668]; 4301 [970]); he heals 2
(hitsplat 6) and a sound plays where the line meets him (12512 or 12513, r20
size 5). A raider on a closing line blocks it and takes 3..5 (4308 [1557..1569]:
every raider every 2 ticks). The strike empties it. The recorded meters were
0..10 of 96 at the strikes (`hpbar_hud_extra_current` read off the bar: 4309
[1490] 10, 4312 [3700] 8; 13 strikes), and every strike was "Death... awaits..."
with one shot a raider for 17..19. Ours had a second shot and "Embrace..." from
8; now one shot more for every 24, five at a full bar (the wiki's "80+ ...
Embrace... annihilation..." when it is full), "Embrace..." when there is more
than one (unverified above 10: never recorded).

**The smash** (the blast on the explosion's floor tick E+1): it hits who was
within 2 of his 3x3 on the explosion's tick E, on E+1, for 28..30 recorded
(4308 [1588] B0aty at 2 and Gnomonkey at 1, [1669] Woox at 1); raiders step out
to 3 on E and back on E+1, and every one who did took nothing (21 corner and
middle smashes, 4301 and 4308). Its floor: every open tile of the two rings
(40 at the middle, 37 at a corner, whose 3 wall tiles stay bare), the inner at
delay 0 and the outer at 2, each a random turn (6/6). Ours: the same, 25..35.

**His facing at the start.** The Hooded Figure is added facing north
(`spawnangle=north`, 4/4) and turned to the tile 2 north of the middle on the
same tick (4301 [875]); ours was added facing south (the npc's own `facing`).
Phase 1's faces otherwise match ours to the tick (`oc_stream.py --phase 1`:
the middle at C-5 and C-1, the beats' quarter turns clockwise from a random
side, his target at the strike). The many recorded `[facing] player=` rows in
phases 3 and 4 are rev 241 re-sending the same target face with a new
`fallbackangle` as the target moves (4301 [1710..1730] Visqo), not turns.

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
| (later segments) 3, 51, 111 | void rifts, 44 hp: the second segment's are `tfa_ocellus_p3_bombportal_buff` "Void Augmenter", the third's `_basic` "Void Rift" (2/2); one at three raiders (12/12), two at four (14/14): max(1, living / 2); spawn seq, despawn seq +10, gone +12 |
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
| +21 | `_deflect_out`; slot 2 let go |
| +24, +52 | `npc_ocellus_phase3_melee_hybrid` with `vfx_ocellus_phase3_melee_hybrid_magic`, `_ranged` or the plain `vfx_ocellus_phase3_melee_hybrid` (melee) slot 2 (16, 13, 4 of 33; the melee only from 4312's second fight on); +5 everyone hit, 5..10 under the matching prayer |
| +34 | `_soul_fragment_ocellus_idle` slot 2, `_soul_fragment_start`; +1 the wind again; +16 `_soul_fragment_end` |
| +59 | void rifts: two, three, four (4301 [1769], [1829], [1889]; 4308 [2322] two at four raiders: not the party's) |

His overheads, the void floor, the explosions, the orbs and the 2-tick
window are in "Phase 4's private mechanics and the wiki check" below.

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

## Audit: phase 1's roots, route and waits, all six sessions (2026-10-21, later)

The owner's live run (`./launch run osrs241`, pack built after 6fb95167f8)
showed the roots wrong. 6fb95167f8 had matched the seven roots' *marginal*
weights and drawn each independently; the recordings are structured. Tools:
the per-line classifier over `--all-players` transcripts of 4301, 4308, 4309,
4312, 4315, 4316 (149 corner lines, 64 middle beats), our tick log's
`map_spotanim` rows decoded the same way, `oc_stream.py` against 4309 (whose
first corner, NW, is a waiting one).

| rule | recorded | ours before | ours now |
|---|---|---|---|
| middle roots | one face a beat, fixed tiles (64/64) | each root beside its corner on a random side (half on the wrong face) | the fixed pairs (4 seeds: 32/32) |
| corner roots | three tendrils in turn (21/21), first face by corner (21/21) | 7 roots drawn independently (repeats in a row) | the tendrils (4 seeds: 16/16 cycle, 16/16 first face) |
| route | random corners | SE, NE, SW, NW always | first random, then one of the other three |
| waiting at a corner | `_end` alone, face, charge +1 (7/7) | a full volley, charge +6: 6 more ticks stood still | as recorded (tfa1 vs 4309: to the tick) |
| threshold while walking | on a volley's tick (7/7) | any tick | on the volley's tick |
| landing face | `instant=true` (16/16) | turned | instant |
| corner line | three lines (21) | two | three |

Open: the route's weights (uniform among the other three here; recorded 10
anticlockwise / 3 clockwise / 6 across of 19); the first walk's path (east
first in the recording, diagonal first here); the explode's npc spot, absent
at 4309's corners but present in the other sessions' (kept).

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

## Audit: his npc info, block by block (2026-10-21)

The audits above were tells inventories: they counted seqs and spots per
window and never compared **facing**, which is in npc info like everything
else he does. Ocellus shipped with no facing at all (0 of 28 recorded faces in
phase 1, 0 of 72 in phase 4) and half his phase-1 seqs off by ticks.
`tools/oc_stream.py <transcript> <ticklog>` now diffs his whole per-tick
stream (seq + delay, spotanim, facing, say, form) against ours, each phase
aligned on its own start and re-aligned on events both fights share (the
teleport to the middle; Untether; "End..."). Our tick log carries every face:
`npc_face` x = -1 for an entity face (z = target, -1 a reset), x = -2 for an
angle; two engine commands send the blocks the content needs,
`npc_faceangle(angle, instant)` (`[facing] angle=`) and `npc_facereset`
(`[facing] type=reset`).

**The rules** (both fights, the counts over 4301 and 4308):

| when | facing | count |
|---|---|---|
| phase 1, a walk's first volley | `reset` | 8/8 |
| phase 1, at a corner | the arena's middle the tick after his last step, and the tick before the charge | 8/8 |
| phase 1, landing at the middle | the tile 10 out on the side of his first two cracks | 4/4 |
| phase 1, each beat | `angle=` that side, a quarter clockwise each beat (with its cracks) | 16/16 |
| phase 1, the strike | his target (`player=`) on the expend's tick | 4/4 |
| the change to phase 2 | `reset` with the line; north (the tile 10 north) as he lands, `seq -1` | 2/2 |
| phase 3, each segment's jump | the middle; his target the tick after | 6/6 |
| phase 3, a slash | the tile 5 out from his middle toward his target; his target 2 ticks on | 24/24 |
| phase 3, a spin | the same toward his target; his target 2 ticks after the spin ends | 10/10 |
| phase 3, "Insolence..." | `reset`; each superior slash his target's own tile; his target 16 ticks after the line | 4/4 |
| phase 3, Untether | `reset` and his target the tick after; `reset` at U+30 | 5/5 |
| phase 4, each deflect | his target, `reset` as it ends | 3/3 |
| death | `angle=` (4301 north, 4308 north-west; ours toward his target, unverified), The Blade at the same | 2/2 |

The rest of the recorded faces are a party re-facing whichever raider he
turns to (4301 phase 3: 64 entity faces a solo run has no second raider for).

**The timings fixed with them** (seqs and spots, same tool):

| | before | now (4301) |
|---|---|---|
| phase 1 seqs | 23/48 | 47/48 (the last: the test's health cheats change his course) |
| the change to phase 2 | transformation a tick early, no `seq -1` | to the tick |
| phase 3 seqs | 90/101 | 101/101 |
| phase 3 spots | 31/33 | 33/33 |
| phase 4 spots | 8/20 | 14/20 (the rest: the hybrid's magic or ranged, a roll) |

Phase 3's timetable, from both fights: the first segment has no volley (two
more triples after the spin, S+33 and S+43; the vine at S+36); the later
ones run triples at S+4, S+14, S+39, S+49, S+79, S+89, S+114, S+124, spins
at S+24 and S+99, "Insolence..." at S+59, Untether at S+134; a volley 3 ticks
before a triple takes its first swing (4301's second segment at S+36 and
S+111, 4308's at S+76: when he casts it is open). Debris falls on every
raider 3 ticks into the first segment's spin and 6 into each later one, and
leaves a void patch 5 ticks on (10/10). The vine's line comes 3 ticks before
it, in the later segments only (3/4). Phase 4 lets slot 2's levitation go
(`id=-1`) as each deflect and each second fragment ends.

Open: the first walk's path (the recording steps east twice and then
diagonally; ours diagonally first: the engine's route order); when the volley
is cast; a party's re-faces; the death angle's rule.

## NPC info audit (2026-10-21)

Every NPC_INFO block of every npc of the room, recording against our tick log, per life:
`tools/rsprox/npc_stream.py --match <type> --summary <transcript> <ticklog>` (counts per
life), `tools/rsprox/npc_timeline.py` (the recording's blocks) and
`tools/rsprox/ticklog_npc_timeline.py` (ours, with the file-only `npc_info` rows:
spawn angle, ops, level, transparency, exact move, timed bar). Facing is part of npc info
and is audited block by block (the entity face, the coord face and its `instant`, the angle,
the reset, the walk type).

| npc | block | recorded | ours now | verdict |
|---|---|---|---|---|
| void rifts | add | a random one of eight ways (27 lives, all eight), `angle=` that way, ops none; +1 ops 0b00010; a timed bar `healthbar_75` 0..30 over 840 cycles | ours: south, no angle, no ops, no bar | fixed |
| void rifts | end | killed in all 33 lives (6..27 ticks): the despawn seq + ops none on the kill, del +2 | ours: a fixed 10-tick despawn, a kill deleted it at once | fixed |
| rifts, vine | health, bars | 44 health; vine healthbar 0, rifts 2; never face an attacker (0/36) | ours had no server config: 10 health, retaliation latched a face | fixed (`tfa_ocellus.npc`) |
| rifts, vine | headbar | re-sent every tick while shown (13/life, 18/life) | sent on a hit only | **open (engine)** |
| deflect arena | phase 4 | a `tfa_invisible_npc` at the last quadrant's middle with the first deflect, kept to the end: range dome D+0 start, D+1 and every 3 to D+19 loop, D+21 end; melee dome D+31, D+32..D+47, D+50 (4301 `_270`, 4308 `_180`) | not built at all | fixed (SW plain and NW `_90` inferred) |
| Blade (vault entry) | add | his death angle, `angle=` it | same | matched |
| Ocellus | party re-faces | 72 entity re-sends in phase 4 (5 real changes) | not built | **open** (as before) |

## Damage audit (2026-10-21)

Every splat on every raider of the room, put down to its mechanic by
`tools/tfa_mech.py --room ocellus1..4` over the `--all-players` transcripts of index
4301..4308 (the party runs are spectated, so the raiders' prayers are not on the
wire: a mechanic's zeros are its prayed hits, its nonzero tail mostly the unprayed
ones). Each mechanic is matched by its own geometry: a projectile by its target and
landing tick, a tile hazard by the map anim or loc on the raider's tile, a swing by
the seq beside the raider.

| mechanic | recorded | ours before | ours now | verdict |
|---|---|---|---|---|
| p1 line chip | 3..5 (232) | 3..5 | same | matched |
| p1 strike | 17..19 (13/13) | 17..19 | same | matched |
| p1 volley | under the matching prayer 0 (46/47; one 10) | 0..25, prayed 0 | same | matched |
| p1 explode near him | 28, 28, 30 | 20..40 | 25..35 | fixed |
| p1 routine | three volleys on the walk, the corner charge (7 lines), the explode; at 66% the middle's four beats and the strike | same (the stream's apparent extra corner was our bot's slower damage) | same | matched |
| p2 fired line | 19..25 (10; one 29 unattributed) | 40..55 (from 42, 55 that were The Blade's) | 19..25 | fixed |
| p2 pools | no raider hit | 3..5 a tick | same | unverified |
| p3 melee triples | 1..7 under protect from melee (87/125 nonzero); 12, 19 | 0..30, prayed 0 | 0..28, 25% through | fixed |
| p3 spin | on each loop seq and the out seq (not a tick before), every one blocked by protect from melee (57/78, 31/35 0); 15..27 | 13..23 at k = 4, 7, .., not prayable | 15..27 at k = 5, 8, .., prayable | fixed |
| p3 slash | 25..28 on the spot's tick | 20..40 | 25..28 | fixed |
| p3 superior slash | 39..41 | 20..40 | 39..41 | fixed |
| p3 unleash out | 34..39 on the seq, 18..19 the tick after (3/3 raiders); two others 34 at +1 only | 40..50 once | 34..39 then 18..19 | fixed (the +1-only case open) |
| p3 void bomb | 6..11 on its tile | none | 6..11 | fixed |
| p3 void patch | 6, 7 every 2 ticks | 5 | 6..7 | fixed |
| p3 unleash pools | 39 zeros, one 28 | 20..30 | same | unverified |
| p4 wind | half the raider's health (both winds) | same | same | matched |
| p4 hybrid | 5..10 under the matching prayer (37) | 5..7 | 5..10 | fixed |
| p4 bursts | 14..16 in the range dome (18); 7..17 in the melee dome | 10..15 | 14..16 | fixed (the melee dome's lower ones open) |
| p4 explosions | 14..16, the tick after it starts on a raider who stayed (38/43; moved 2/652) | none | 14..16 | fixed (below) |
| p4 void floor | 10 (4309 [2210], [2143]) | none | 10 a tick on it | fixed (below; the rate unverified) |
| p4 hybrid, melee kind | 6..7 (13) | not built | 5..10 under protect from melee | fixed |

## Phase 4's private mechanics and the wiki check (2026-10-21)

The wiki's Ocellus page and the strategy guide's Ocellus section
(`The_Fractured_Archive/Strategies`), each mechanic set against the
`--all-players` transcripts of index 4301, 4308, 4309, 4312 (two fights), 4315
and 4316. D = the cycle's tick (D+0 his `deflect_in`, every 60); W = a wind
(D+4 and D+35). Tools: `tools/tfa_mech.py --room ocellus4` and one-off counters
over the same transcripts.

### His prayers (headicon customisation, sprite group 440, slot 0)

An empty `[headicon_customisation]` block takes the overhead off.

| phase | recorded | ours |
|---|---|---|
| 1 | 0 (Protect from Melee) from the fight's start, each corner walk's first volley and the middle strike's first shot; off the tick before each charge, with each teleport out and at the phase's end (4301 [913]..[927], [949]..[964], [1029]..[1040], [1083]..[1090]; 19/19 over 4301, 4308, 4309, 4312). The strategy guide: "While walking to a quadrant, he will have Protect from Melee active" | the same ticks; melee does nothing while it is up |
| 2 | none | none |
| 3 | an EMPTY block at each "Untether..." and two at U+48..49 (11 windows, 5 sessions): the 21 October hotfix, "Ocellus's Protect from Melee overhead icon will now properly appear in phase 3". Raiders' melee outside those windows landed (288/323 swings) | Protect from Melee U..U+48, shown (0) |
| 4 | 15 at D+0, 11 at D+25, off at D+30, 16 at D+31, 11 at D+53, off at D+59, every cycle in every session; off at his death | the same |

What 15 and 16 do: under 15 the raiders only ranged (334 shots, 0 melee
swings), under 16 they only meleed (513 swings, 1 shot), so 15 is the deflect
against melee and 16 the one against missiles and magic (the guide: Deflect
Melee, then Deflect Range). Ours makes the prayed style deal nothing; what a
deflect sends back is unrecorded (no raider used the prayed style) and not
built. 11 is Soul Split: all 18 of his phase-4 heals are the unclaimed orbs
(below), none per hit.

### The void floor

The strategy guide: from D+0 "all tiles below him and one tile away from him"
are void; from D+31 "the entire quadrant ... except for the tiles directly below
him and within 1 tile of him". There is no loc for it; the `deflect_arena_*`
spots on the `tfa_invisible_npc` draw it. The raiders' distance from his 3x3:

| D | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| 0..21 (4301, 4308, 4312, 4316) | 0 | 227 | 468 | 55 | 26 |
| 31..50 | 619 | 6 | 9 | | |

The 2s and 3s at D+31..50 are raiders still walking in at D+31..33, who took
nothing (4301 [1742..1744], 3/3). One raider stood on it: 4309 [2209] Woox, 1
from him, hit 10 at [2210]; another 10 at [2143]. Ours: 10 every tick on a void
tile, D+1..21 (his ring) and D+35..50 (the rest). How often it really hits is
unverified (one tick on it recorded).

### The explosions, the orbs, the 2-tick window

| mechanic | recorded | ours |
|---|---|---|
| explosions | six after each wind, 2 ticks apart, starting on each raider's tile at D+9, 11, .., 19 and D+40, 42, .., 50, landing the tick after on whoever stayed: 14..16 (38/43 who stood still, 2/652 who moved). Private (`vfx_void_chain_explosion`, `spotanim_map_specific`) | the same |
| telegraph | `vfx_void_chain_player_telegraph` slot 2 at W+2 (24/24) | the same |
| fragment | `vfx_ocellus_soul_fragment_player_idle` slot 2 at W-1 (24/24) | the same |
| orb ready | W+12 after the first wind (earliest takings 12), W+6 after the second (earliest 6); first orbs 2..5 from his 3x3, second 1 from it, in the quadrant | the same (`_spawn_12_ticks` / `_spawn_6_ticks`, private) |
| orb taken | heals the wind's hit (+8 on its first ready tick), `vfx_ocellus_soul_fragment_player_regain` slot 2; latest taking ready+11 | the same |
| orb NOT taken | at ready+13 (W+25, W+19) he is healed that raider's wind hit, a heal splat each, with `nex2021_nex_heal` (5281) once at his middle, range 9: 18 heals, each exactly an unclaimed raider's wind (4308 [2377] 26, 29, 12 = Gnomonkey, B0aty, Saxerpillar), 13 heal ticks, 13 sounds | new: the same |
| 2-tick window | every raider's attacks 2 apart, any weapon: D+12..20 (155/177) and D+41..52 (60/77) | the same |

The wiki's "the longer you wait to collect the orb, the larger the heal" is
not what the recordings show (the bonus is for the FIRST tick).

### The hybrid's three kinds

16 `_magic`, 13 `_ranged`, 4 the plain `vfx_ocellus_phase3_melee_hybrid`
(4312 [4418], 4315 [6733], 4316 [2725], [2757]), the plain one only from
4312's second fight on (0 of 15 before, 4 of 18 after). It hits everyone at
+5 for 6..7 (13), as the others do under the matching prayer. Ours: one of the
three at random.

### Phase 3's rifts: three kinds, one a segment

The second and third segments each get one kind, consecutive in the order
Augmenter (`_buff`) -> Void Rift (`_basic`) -> healing rift (`_heal`) ->
Augmenter: buff then basic in 4301, 4308, 4312 (twice), 4316; basic then heal
in 4309; heal then buff in 4315 (7/7). Ours had Augmenters then Rifts every
time; now the second segment's kind is rolled and the third's follows.

The healing rift (4309 [1994..2054], 4315 [6396..6509]) fires every 4 ticks
from its add (add+4, +8, ..): `vfx_ocellus_voidflare_healing_projanim` (0 / 30,
angle 2, heights 160/140 raw) at his middle, and he is healed on the spot, a
heal splat of 12 or 14 (10 heals: 12 x4, 14 x6). A raider standing on the line
from it to him takes the shot instead and he is not healed; nothing is recorded
on the raider (4/4: 4309 [2002] B0aty, [2050], [2054] Gnomonkey, 4315 [6400]
Saxerpillar). Its spawn and despawn seqs are `vfx_ocellus_voidflare_heal_*`.

What a killed rift gives its killer (the guide's "empower the player who kills
it"): nothing reaches the wire. `vfx_human_ocellus_voidflare_default` and
`_buff` are never sent; `_heal` (delay 39) only on every raider at phase 1's
middle strike (4301 [1028], [1082]: the strike's small heal). Not built. An
unkilled rift's "charge" is unrecorded (every basic and buff rift was killed).

### Checked and unchanged

- **"If he is unable to reach the targeted player, he will attack with a
  powerful spread void shot"**: the target was beside him at every recorded
  "Your panic... is futile..." (5/5, distance 1). It is on his schedule, not a
  reach test.
- **60% Defence reduction cap** (hotfix): held on his tick (40% of base), as
  Sotetseg's floor is. **Defence not restored between phase 3's quadrants**
  (hotfix): nothing of ours restores it.
- **Phase 1's missed line heals him 2**: now with its heal splat (recorded:
  4301 [970], 4308 [1520], [1522], [1585], [1640], [1668]).
- **The censers' effects** stay the wiki's guess (no recording shows them).
- **Phase 2, the tendrils, the untether, the slam, the quadrant destruction**:
  as audited above.

### Open

- What the deflects send back (no raider used the prayed style).
- The void floor's hit rate (one tick on it recorded).
- A killed rift's empowerment and an unkilled one's charge (nothing on the wire).
- The heal rift's projectile on a raider: no splat or heal recorded on them.

