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
| Raiders arrive on any tile of (4126..4128, 5635..5637, 1), local (30..32, 3..5), their facing reset, O+8 after the door (`nexus_plan.md` §6); the arena is 33 tiles on | 4301 [876..892] |
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
| the volley his step reaches the corner on, or a step short with the corner's tile already under his 3x3 (going north or east: §2c) | played as `npc_ocellus_phase1_basic_projectile_end` (the others `_start`), shots and spot as usual; the middle faced +1 and +5; C = +6 (16/16) |
| a volley's tick with him already standing at the corner (a walk of 8 or 12 tiles arrives 2 or 4 ticks before it) | the `_end` seq alone (no shots, no spot), the middle faced and his overhead cleared on it; C = +1 (4309 [1386] [1423], 4312 [3591] [3628], 4315 [5975], 4316 [1913] [1950]: 7/7) |
| C | the charge: `npc_ocellus_phase1_charge` and a crack line every 2 ticks, seven (C..C+12); `vfx_ocellus_p1_charge` every 2 from C+5; one of "Pointless..." (9), "You... fail..." (8), "Unworthy..." (4) at C+15 (21 corners); `npc_ocellus_phase1_explode` at C+18 with `vfx_ocellus_p1_explode_npc` |
| C+19 | the blast: `vfx_ocellus_p1_explode_floor` (a random one of `_90/_180/_270`) on every open tile beside his 3x3 at delay 0 and the next ring at delay 2 (6/6) |
| C+21 | the next corner: one of the other three (19 recorded: the next anticlockwise 10, clockwise 3, across 6; the four that went by way of the middle were all across); he keeps it through a visit to the middle |
| health at or under 66% or a quarter: checked after a corner's explosion, and while he walks (or waits at the corner) on each volley's tick, in its place (4301 [1040], 4309 [1546], 4312 [1508] [3660], 4315 [6088], 4316 [1982] [2073]: 7/7) | he stops where he is (a walk's last step is T-1); `npc_ocellus_phase1_teleport_out` + `vfx_ocellus_p1_teleport_out` slot 2, `transparency 30..45, 0 -> 255`, his headbar `removed=true`, his overhead cleared; +4 a `telejump` (the same index, snapped) to the middle (sw (-1,-1)) with `_teleport_in`, `transparency 0..20, 255 -> 0`, facing the side of his first cracks with `instant=true` (16/16); +5 `tfa_wallkit_archive03_center01_blockwalk` on his sw tile (12/12 teleports and 6/6 floors, 4301 [986] [1040], 4308 [1573] [1654], 4309 [1546], 4312 [1508]; §2a) |
| +8, then every 8, four beats B | the charge seq and two diagonal cracks from neighbouring corners of his 3x3, clockwise from a random pair (the earlier at delay 0, the later at 60); `vfx_ocellus_p1_charge` at B+4 and B+6; the explosion at B+6, the blast at B+7 |
| the last explosion +2 | "Death... awaits..." (every recorded strike); `tfa_wallkit_archive03_center01` back on his tile; the meter emptied |
| +1 | nothing of his: the raiders' heal splats here are the middle lines they stood on, paid on their own hits (§2c) |
| +2 | `npc_ocellus_phase1_expend`; a `scriptedproj_add` of `ocellus_proj_test` at each raider (2 / 39 to a raider within 4 of his middle as the tick began, 2 / 54 further: 37/37 and 1/1; curve scripts 10024 / 10018 / 10017) and on each raider `vfx_human_ocellus_voidflare_heal` delayed by the shot's endtime (46/46); the shots go out with his first `[facing] player=` (13/13), on the expend's tick 10/13 and the next 3/13; his Protect from Melee headicon +1 after the expend (13/13) |
| the shot's landing | each raider hit 17..19, its own roll (4301 [1083]: 19, 17, 18), on (endtime + 15) / 30 ticks after the shot: +1 at 39, +2 at 54 (4301 [1030]); +1 after late shots too (4309 [1590], 4312 [1461], 4315 [6132]) |
| +8 | he walks on to the next corner |
| nothing left | "You... will disappear..."; if not at the middle: teleport out +2 (the same fade and headbar removal), a `telejump` to the middle +4 with `seq -1` and `transparency -1..-1, 0 -> 255` (still unseen) and no floor, in +5 (`_teleport_in`, the fade in); `npc_ocellus_phase1_transformation` 3 ticks after he is at the middle; +7 phase 2 (§3), whose T+2 lays the floor (4301 [1092..1106], 4309 [1610..1624], 4316 [2136..2150]: 3/3) |

**Volleys.** A shot at every raider: fire (`vfx_ocellus_fire_attack01_projanim`,
the wiki's orange, ranged) or water (`_water_`, blue, magic), one element a
walk, with `vfx_ocellus_<element>_attack01_spotanim` on him; 30 / 95 within 4
tiles, 30 / 125 beyond, angle 40, progress 0, heights 578/124 (ours
144/31). Both the source and the distance are as the tick began, before
anyone's step: his middle then (126/126 shots' source while he walked) and
each raider's tile then (256/256; measured after the tick's moves, 30 miss).
Ours snapshots both in his `[ai_timer]`, which runs before the players move.
Prayed, a shot is 0; unprayed it rolls accuracy (5 of 13 unprayed shots 0),
then 0..25.

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

**The meter (the yellow bar).** A closing line adds 2 and heals him 2
(hitsplat 6) unless every raider in the room stands on it, its meet tile
counted (261/261 closes over the six sessions; "nobody on it", the old rule,
missed every close one raider of several blocked). Every raider on it takes
3..5 either way (4308 [1557..1569]: every raider every 2 ticks). The sound
(12512 or 12513, r20 size 5) plays on every close, blocked or not, on the
meet tile: the line's tile `len`, or `len - 1` for a 6-long line. The strike empties it. The recorded meters were
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

### 2a. The teleport to the middle, audited (2026-10-21)

Recording: every phase-1 teleport of 4301, 4308, 4309, 4312 (12, plus the
three "nothing left" ones). Ours: `test/guthixian_archive/tfa_ocellus.lua`,
tick log T = 202 / 283 / 366 (the last the zero path), aligned on the
teleport-out seq.

| T+ | recorded | ours before | ours now |
|---|---|---|---|
| -1 | his last walk step, if walking | same | same |
| 0 | seq 14812, spot slot 2 4266, `transparency 30..45 0->255`, headbar `removed=true`, headicons cleared | seq, spot, headicons; no fade, bar kept | matched (wire: `01 0a ff ff` headbar, transparency `1e 00 00 2d 00 81 01`) |
| 1..3 | nothing of his (hits ride as usual, the bar re-sent by them) | same | same |
| 4 | `telejump`, the SAME index, jump bit; seq 14813, spot slot 2 4267, `transparency 0..20 255->0`, `facing coord= instant`, level_change, enabled_ops, headicon re-sent (the add) | removed and re-added under a NEW client index (55 -> 56 -> 57 -> 58); no fade; the floor loc on this tick | matched: same index kept (`TELEJUMP` trace, 55 throughout), fade in |
| 5 | `loc_add_change` center01_blockwalk | an entity-face reset (the Talk-to's playerfaceclose mode dropped by the teleport) | the floor; no reset (the walk's reset now drops the mode, so the +5 re-face on the talker in phase 1 is gone too) |
| 8 | charge seq, `facing angle` | same | same |
| zero path +4 | `telejump`, seq -1, `transparency -1..-1 0->255`, instant face north, no floor | jump, seq -1, face; the floor laid | matched; floor left to phase 2's T+2 |
| zero path +5 | seq 14813, spot 4267, fade in | seq, spot | matched |

Client: the TRANSPARENCY block was decoded and dropped
(`osrs241_entity_info.c`); it is now applied to the body's faces per frame
(`world/entity_transparency.h`, the deob's class657 / Model.method4912;
`app_world_sync_npc_transparency`). Not yet seen on screen: the shared tree's
uncommitted toridraw edits stop the client linking (2026-10-21).

### 2b. Healing, timing and attacks, audited (2026-10-22)

Against all six `--all-players` sessions (4301, 4308, 4309, 4312, 4315,
4316: 21 corners, 19 middle visits, 13 strikes), each mechanic aligned on its
own anchor (the corner's first charge seq; the teleport out):
`tools/oc_p1_check.py charge|middle <ticklog> <transcripts>` (events from
`tools/oc_p1_events.py`). Ours: `test/guthixian_archive/tfa_ocellus.lua`, solo,
15/15.

| what | recorded | ours before | ours now | verdict |
|---|---|---|---|---|
| line heal | 2 unless every raider is on the line or its meet tile (261/261) | 2 when nobody was on it | the recorded rule | fixed |
| line sound | 12512/12513 r20 s5 on every close, on the meet tile | only on a heal | every close | fixed |
| raider heals at a line or the strike | **WRONG, reverted in §2c.** The counts were true, the reading was not: a raider off a line on the heal's tick had still stood on one since their last heal (381 of 386 phase-1 heals came with lines banked). The weapons named heal nothing | 4..8 at the strike, some on lines | none | **wrong; §2c** |
| volley flight | 95 / 125 by each raider's tile as the tick began, from his middle then (256/256) | after the tick's moves | as the tick began (9/9 ours) | fixed |
| volley damage | prayed 0; unprayed 5 of 13 shots 0, the rest 2..24 | 0..25, no roll | accuracy roll, then 0..25 | fixed |
| strike flight | 39 within 4 of his middle as the tick began, 54 beyond (37/37, 1/1) | 39, +15 a repeat | 39 / 54, +15 a repeat | fixed |
| strike impact spot | `vfx_human_ocellus_voidflare_heal`, delay = endtime, with the shot (46/46) | not sent | sent | fixed |
| strike hits | 17..19 each its own roll, on its shot's landing | one roll, all at +1 | each, at (end + 15) / 30 | fixed |
| after the strike | the next volley at expend +7 (13/13, the far shot's strike too) | +7 | +7 | matched |
| beats' facing | `angle=` +512 a beat from a random start (14/14 visits) | same | same | matched |
| particles | `vfx_ocellus_sword_repair_particles_01` at the middle every 3 ticks through phase 1 | until the fight only (none in phase 1); phase 2 a tick off the clock | every 3 on one clock, load to phase 2 (132/132 ours) | fixed |
| charge, middle, explosions, says, seqs, spots | the timetable above | same | same | matched (both profiles) |

Our instance vars for the tick-start snapshot are 7..9; the first cut put them
on 21 and 22, inside the shots in flight (20..25), which a party of three or
more would have overwritten.

Open:

- **The late strike.** In 3 of 13 strikes (4309 [1588], 4312 [1459], 4315
  [6130]) his face on the raider, the shots and so the hits come a tick after
  the expend; the faced raider's tile does not say why. Ours does the 10/13.
- **His face re-sent.** Between the expend and the next volley the recordings
  re-send `[facing] player=` (the same raider, 13/13) up to five times; a
  move of that raider explains some and not others (13 moves with none, 11
  re-sends with the raider still). Ours sends it once.
- **Regeneration.** Our engine regenerates him 1 every 100 ticks (`regen`);
  no splat, and a 2501-health bar cannot show it. Unverifiable on the wire.
- **The solo test** never stands on a line or far from the middle at a strike's
  expend with one shot: the blocked-line path and the lone 54 are exercised
  only through the rules above, not by a run.
- **A fuller bar's extra shots** (one per 24) stay the wiki's: no recorded
  strike had a meter above 10.

### 2c. The line heal and the corner arrival, audited again (2026-10-22, later)

§2b removed the raiders' line heal as "their gear's". That was wrong. Against
every raider heal splat (PLAYER_INFO hitsplat 6) of phase 1 in 4301, 4308,
4309, 4312, 4315 and 4316, with each heal set against the lines that raider
stood on since their previous heal (`tools/oc_line_heal.py`):

| what | recorded | before §2b | after §2b | now |
|---|---|---|---|---|
| a heal with no line since the last | 5 of 386 (the Saradomin censer on three raiders, 4312 [1384]; two in the fight's first ticks, 4315 [5888]) | - | - | none |
| when it is paid | on the raider's own attack tick (382/386), and only when their hit lands for more than 0: a lone melee swing that healed was followed by a damage splat on him 108/108; one with lines banked that did not heal, by a 0 36/47 (the other 11 a thrall's 1..3 beside a 0) | on the line's tick | never | the hit funnel (`~tfa_oc_line_heal` from `player_hit_npc_prepare`), on the attack's tick |
| the order in a tick | the line closes first: a raider hit by a line and attacking on the same tick is healed for it then (185 heals) | - | - | the same (his timer, then the players) |
| how much | 4 a line less 1, plus random(2) + random(3), independent of the lines' chips (1 line: chip 3 -> 4.15, 4 -> 3.96, 5 -> 4.23; raiders off full: 1 line 3..6, 2 lines 7..10, 3 lines 10..14, 4 lines 14..18); log-likelihood -351.8 against -401.7 for the deleted 3..5 a line | 3..5 a line | - | the fit; one line runs half a point low (4.11 recorded, 4.5 ours) |
| a bank at the change | never paid (4309, 4316: 3..4 banked, no heal on The Blade or after) | - | - | dropped (phase 1 only) |
| the strike's heal | none of its own: the raiders' heals around it are the middle lines | 3..5 on everyone | none | none |
| his heal on a line | 2 unless every raider stands on it (206/206 blocked, 17/18 not) | | | unchanged |

Ours: `test/guthixian_archive/tfa_ocellus_party.lua`, three raiders, two of
them stepping onto each new line (`::tfaocline`): one line healed 4 or 5, two
8..10, on the attack's tick.

**`enabled_ops`** (`npc_block_inventory.py`: the recordings sent 0b00010 and
0b00000 in phase 1, ours never): 0b00010 on every landing at the middle (14/14,
kept through the corners after), 0b00000 with "You... will disappear..." (7/7)
and again with the jump to the middle (5/5), 0b11111 with the change. Ours now
the same (`npc_setops(2)` on the landing, `npc_setops(0)` with the line; the
jump's re-add re-sends it).

**The corner arrival.** Measured from the volley V he arrives on, every
recorded corner is one of three shapes, and ours matched each: his last step
on V (the middle faced V+1 and V+5, the charge V+6: 10), a step after V (faced
V+2 and V+5, C = V+6: 4), there before V (`_end` alone, faced V, C = V+1: 7).
What ours had wrong was which shape a walk takes. A 12-tile walk is a tile short
of its corner at S+10. Going north or east (to (5,5): 4301, 4308, 4312, 4315)
that volley is the `_end` and the charge follows at S+16. Going south or west
(4309 x2, 4312, 4315, 4316) it is a `_start`, and the next volley (S+15) is the
`_end` alone. The difference is whether the corner's tile is under his 3x3 a
step short (9/9). Ours took every step-short volley as the arrival: 7 of 10
corners a step after, never the wait. It is now the footprint test, and it
gives the wait as recorded (3 of 10 in the party run).

**The strike's walk on a tick late in the party run** is the fuller bar: seat 3
blocks nothing, so three shots each and the wiki's one more per 24 of meter,
which no recording has. Solo, at one shot, it is the recorded +49.

Still open:

- **His face re-sent** between the expend and the next volley (§2b), unchanged.
- **The heal splat bytes** are not compared on the wire: the tick log has no
  heal rows. It is the phase-4 orb's `hitmark_bar` call, whose splat and bar
  were audited against these sessions; the amounts and ticks are read off the
  raiders' health.
- **The heal at one line** runs low (3 is 32% recorded, 17% by the fit).

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
| 123 | (round 3 cut short: no R+28 pools) "This... is enough..."; `npc_ocellus_p2_to_p3_part1`, `vfx_ocellus_p2_to_p3_arm_glow01` d150, `vfx_ocellus_sword_root_fire` slot 5, `vfx_ocellus_p2_to_p3_arm01` slot 2 d150; `enabled_ops 0b00000`; The Blade gone; 4428 r15 s15; `invisible_type8_nonblocking` and `vfx_ocellus_sword_drop` at the middle; song 894 from here |
| 124 | round 3's R+29 volley still fires (the lit portals, then dark); 444 r10; `tfa_arena_sword_drop_explosion` |
| 125 | all 24 portals `loc_anim` `vfx_ocellus_p2_portal_despawn` |
| 127 | the hall's walls come down: `tfa_wallkit_archive03_straight_collapse_01..04` at (-1,-13), (-13,-1), (-1,3), (3,-1); `invisible_type8_blocking` (-1,-15), `invisible_type0_nonblocking` (0,-13) |
| 131 | `npc_ocellus_p2_to_p3_part2`, `vfx_ocellus_null` h154 d300 (slots 0 and 2), he becomes `tfa_ocellus_p3`; `level_change` 1601, an empty `headicon_customisation`, `enabled_ops 0b00000` |
| 132 | the 24 portals deleted; `invisible_type8_blocking_size5` (-2,-2), `_size3` (-1,-1); `tfa_arena_collapsing_explosion01` at the middle; each arm breaks: `tfa_arena_collapsing_explosion02/03/04/01` 3/6/9/12 out at delays 0/15/30/45; stepping stones `tfa_ocellus_platform_jump_outer_*` at 4 and 10, `_middle` at 7; the four walls `loc_anim` `dungeonkit_guthixian03_cracking_collapsing_01..04` |
| 135 | `[transparency] 0, 28, 0, 255` (he fades out); the walls' `_empty`; `inviswall_blockrange` along the chasms' edges (phase 3, §4) |
| 137 | his jump to the first corner (§4, S+0), fading in; 138 `enabled_ops 0b00010` |

The change is the same to the tick in 4301, 4308, 4309 and 4316.

A round R, on the quadrant's two arms:

| R+ | recorded |
|---|---|
| 0, 5, .. 25 | ten of the twelve portals `_active`, a gap on each arm (the wiki's orange portals); the gaps' rows cross at the safe spot |
| 4, 9, .. 29 | each lit portal fires across the quadrant along its two rows (or columns): `vfx_ocellus_portal_projectile` from the portal + (1,1) to the arena's wall a tile across (c the source's offset across the line: min(14, 22 - c) out on the arm's positive side, min(14, 20 + c) on its negative, 15 the cap southward); east and north 6 cycles a tile plus 6, progress 100; west and south 6 a tile less 6, progress 250; angle 127, heights 100 (ours 25); from a pair of invisible `tfa_invisible_npc_2x2` on it (deleted +3); the portals go dark (2,247 shots, every portal both ways, seven fights) |
| 28 | 13 `vfx_ocellus_p2_aoe_dodge` pools on the quadrant; (round 1) `tfa_the_blade1_idle2` |
| 31 | all twelve lit |
| 33 | `vfx_ocellus_p2_safe_portal_spawn` on a pool's tile; `_idle` every tick to R+39 |
| 34 | `vfx_ocellus_p2_sword_discharge` d30 on him; 4428 r15 s15 |
| 35 | 444 r10 |
| 34..39 | a raider on the safe portal is put on a random tile of the next quadrant (4301 [1153], 4308 [1761], [1763]) |
| 40 | all twelve fire (5 a tile plus 5 east and north, less 5 west and south); `_despawn` |

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
three segments, each ending in that quadrant's fall, then the next
counter-clockwise (4301 NE, NW, SW; 4308 NW, SW, SE). **What ends a segment
is his health, not a clock** (all 21 segments of the seven fights: 4301,
4308, 4309, 4312 twice, 4315, 4316; `p3tok.py`-style token streams in the
audit below). From the segment's start S (the jump):

| S+ | recorded |
|---|---|
| 0 | `telejump` to the corner, `level_change`, `[facing] coord=` the middle `instant=true`, `[transparency] 0, 28, 255, 0` (fading in), an empty `headicon_customisation` |
| 1 / 2 | the quadrant's barriers `tfa_ocellus_quadrant_barrier_01/02` (`_spawn`, then standing): north-east's at (8,1) r1 and (1,7) r2, each quadrant clockwise turning them a quarter; `enabled_ops 0b00010` |
| 6 (and 9 in the first) | void bombs, one on each standing quadrant: `vfx_ocellus_quadrant_bomb_idle_01` h255 at 0 and 30; +2 `_drop_01` and `_impact_01` and a `tfa_ocellus_void_degen_bomb` that stays |
| 3, 51, 99 (later segments) | void rifts, 44 hp (the second segment's kind rolled, the third's the next of Augmenter -> Void Rift -> healing); one at three raiders, two at four. One due during a spin waits for the swing after it (S+108/111/114: 4301, 4308, 4312, 4315, 4316), during Untether for U+32 (4312 [2090]); none when the quadrant falls first (4308) |
| 4 | the rotation's next attack (below) |

**The rotation** runs on across the segments: a triple, a triple, then a big
attack, the spin and Insolence by turns (4309's and 4312's third segments open
with Insolence because their second fell after a spin; 4/4 such carry-overs).

| attack | recorded | next attack |
|---|---|---|
| triple | `npc_ocellus_phase3_melee_basic` +0, +3, `_melee_slash` +6 (the slash a short line of `vfx_ocellus_p3_shadow_slash_spotanim01`) | +10 |
| the volley | when his target is not beside his 3x3 as a triple starts: `npc_ocellus_phase3_basic_projectile_all`, `vfx_ocellus_magic_spotanim01` slot 4 d30, a shot at everyone; no line of its own (4301 [1378], [1453], 4308 [2027], 4309 [1792], [1867]: 5/5 target not beside him; every triple start checked with the target beside him a swing). The wiki's "unable to reach the targeted player" | the triple 3 later |
| the spin | `npc_ocellus_phase3_prayer_in` + slot 4 spot; a `_prayer_loop` 3 apart for each quadrant fallen; `_prayer_out` 5 + 3 x loops on | out + 4 |
| Insolence | the line, `reset`; three `npc_ocellus_phase3_melee_superior_slash` d30 at +2, +7, +12, each its target's tile | +20 |
| Untether (in Insolence's turn when he is within 8% of the next quarter) | see U below | the triple at U+37 if the quadrant has not fallen (4316 [2650]) |

**The vine** comes with the first triple after the segment's first spin: its
line ("The void... will take you..." 8, "Your panic... is futile..." 5,
"Await... the void..." 4 of 17) as the triple starts, the vine 3 ticks on,
unless he is within 8% of the next quarter (the first segments: 85.4, 84.1,
83.8% vine; 82.9, 82.6, 82.4, 82.7% none; every later segment had one, its
lowest 33.2%). "Your panic... is futile..." is a vine line, not the volley's.

**Near and the fall.** With his health h% and the next quarter Q (75, 50, 25):

- **Untether** for Insolence when h <= Q + 8: at 76.2..81.1% in the first
  segments (7/7), 51.2..54.9 and 29.5..31.2 later (8/8); Insolence at
  62.2..66.6 and 35.4..46.7 (14/14). The margin is 6.1..10.4 from these; 8
  fits the vine too.
- **The fall** when h <= Q + 1, at once unless he is spinning (through its out
  seq and the tick after) or unleashing (to U+30): 21/21 at 73.2..76.0,
  48.2..51.0, 24.6..25.9%, never above; not at 51.01, 51.06, 51.18, 26.12,
  26.28%. In place of a triple's swing (4316 [2497]); 2 ticks after a spin's
  out (4308 [2242], 4312 [4313]); after Untether at U+31 (16/21) or later
  when the slam left him above it (4312 U+33, 4315 U+32, 4316 U+39). 5 of 21
  fell with no Untether at all.
- Nothing holds his health: hits land through the fall (4301 [1326..1334]
  74.9 -> 74.2%).

| U+ | recorded (Untether) |
|---|---|
| 0 | "Untether..."; `telejump` to the corner with `exact_move` (old, 0, new, 90, the yaw); `level_change`; `npc_ocellus_phase3_unleash_in`; `[bas]` ready and walk `_unleash_loop`; `[transparency] -1, -1, 255, 0`; empty `headicon_customisation` |
| 2 | `_unleash_loop`; pools `vfx_ocellus_p2_aoe_dodge` (10..15) on the quadrant at U+6, 10, 14, 18 |
| 18 | `vfx_ocellus_p3_unleash_spotanim` d360 (11 of 14) |
| 25 | `vfx_ocellus_unleash_arena` (NE `_180`, NW `_90`, SW plain) over it |
| 29 | `_unleash_out`, the slam |
| 30 | `[bas]` reset, `[facing] type=reset` |

| D+ | recorded (the fall, 21/21) |
|---|---|
| 0 | "Obliteration..." or "Run... It matters not..."; `npc_ocellus_quadrant_destroy_in`, `vfx_ocellus_phase3_arena_collapse_in` slot 2; `[bas]` ready and walk `_destroy_loop`; the barriers' `_despawn`; `[facing] type=reset` unless Untether already let it go |
| 2 | `_destroy_loop`, `_arena_collapse_loop` slot 2 (`loop=true`) |
| 11 | "You can't... escape... the end..." / "This end... comes..." / "Everything... ends..."; `npc_ocellus_phase3_unleash_out_90f`, `_arena_collapse_out_01` slot 2, `_out_02` slot 4; `[bas]` reset |
| 12 | `[transparency] 0, 30, 0, 255` (fading out) |
| 13 | the quadrant gone: `invisible_type0_blocking` on its 84 tiles, its wall's `tfa_wallkit_archive03_slice_collapse_<quadrant>_01..03` |
| 17 | the next segment's jump (S+0 above); after the third, the same jump to the last quadrant's corner |
| 18 | after the third: "End..." (phase 4) |

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
| 0 | he is at the last quadrant's corner, his 9 tiles blocked; its barriers (`_spawn`, +1 standing); the three stones of the arm clockwise of it play `dungeonkit_guthixian03_steppingstone_despawn_01..03` (7/7), +2 blocked |
| 3, then every 60 (D) | `npc_ocellus_phase3_deflect_in` with `[bas] ready, walk = _deflect_loop` and his target faced (11/11); +2 `_deflect_loop`; +3 `_soul_fragment_start`, every raider `_player_idle` slot 2 and their defend seq `delay=30`; +4 `vfx_ocellus_soul_fragment_ocellus_levitating_idle` slot 2 `loop=true` and the wind: every raider hit for half their health (4301 41/40/49, 4308 39/49/46/55) |
| D+21 | `_deflect_out`; slot 2 let go (`loop=true`), `[bas]` reset, his face reset |
| D+24, D+52 | `npc_ocellus_phase3_melee_hybrid` with `vfx_ocellus_phase3_melee_hybrid_magic`, `_ranged` or the plain `vfx_ocellus_phase3_melee_hybrid` (melee) slot 2 (16, 13, 4 of 33; the melee only from 4312's second fight on); +5 everyone hit the same value, 5..8 (one 10) under the matching prayer |
| D+34 | `_soul_fragment_ocellus_idle` slot 2 `loop=true`, `_soul_fragment_start`; +1 the wind again; +16 `_soul_fragment_end`, slot 2 let go (`loop=true`) |
| D+59 | void rifts: two, three, four (11/11 cycles, any party: 4301 [1769], [1829], [1889]; 4308 [2322] two at four raiders), each on a floor tile 2..5 from his 3x3 (27/27) |

His overheads, the void floor, the explosions, the orbs and the 2-tick
window are in "Phase 4's private mechanics and the wiki check" below, and
every hidden rule as measured over all seven fights in "Audit: phase 4's
hidden rules" (2026-10-22), which supersedes it where they differ.

## 6. Death (K)

| K+ | recorded |
|---|---|
| 0 | "Challenge complete: Ocellus. Duration: ... Total: ..." and "The Fractured Archive: Total completion time: <col=ef1020>m:ss</col>"; his song stopped; `npc_ocellus_phase3_death`, `vfx_ocellus_death_skirt_spotanim01` d1, `vfx_ocellus_death` slot 5 d1, `vfx_ocellus_death_crack_spotanim01` slot 3 d1, slot 2 let go (`loop=true`); ops cleared; `angle=` the 8-way from his middle to the tile his last face on his target named (7/7) |
| 0 (or -1) | every phase-3 void patch left on the quadrant replaced by `invisible_type0_nonblocking`, turned as the patch was (42/42, 6 a fight), on the tick his health reaches 0: K in 4 fights, K-1 in 3 (the death seq a tick after the killing hit: 4309, 4312's first, 4316) |
| 7 | seq -1, his body hidden |
| 14 | `tfa_blade_npc_vault_entry` ("The Blade": Commune, Travel) on his tile |
| 16 | he is deleted |

## Audit: the change and phase 3 against all seven fights (2026-10-22)

The earlier phase-3 audit compared one fixed timetable with two fights that
happened to share it. Every fight that reached phase 3 (index 4301, 4308,
4309, 4312 twice, 4315, 4316), each segment as a token stream (J jump, t
slash, S spin, + loop, V volley, W" vine line, w vine, I Insolence, U
Untether, D fall) with his hud health at each, showed the rotation and the
health rules of §4. Ours: `test/guthixian_archive/tfa_ocellus_p3.lua` (phase
2 from `::tfaocphase 2`, party of three, his health replayed from a
recording by P+k), tick logs diffed with `oc_stream.py`, `ticklog_tells.py`
and the tick log's `npc_info` rows.

**The change (T+123..T+138), 4301:**

| T+ | recording | ours before | ours now | verdict |
|---|---|---|---|---|
| 123 | line, part1 seq, 3 spots, ops none, Blade gone, 4428, sword drop, song 894 | song 893 until T+135, ops untouched | the same as the recording | fixed |
| 124 | round 3's last lit volley, 444, explosion | no volley (the round's code stopped at T+123) | the volley | fixed |
| 125 | 24 portal despawn `loc_anim`s | none | 24 | fixed |
| 131 | part2, 2 null spots, form, level, ops none, empty headicon | ops all on (the form change) | ops none | fixed (the empty headicon block is not resent: nothing was shown) |
| 132 | portals gone, blockers, explosions, stones, 4 wall `cracking_collapsing` anims | no wall anims | 4 | fixed |
| 135 | fade out (0, 28, 0, 255), `_empty` walls, chasm walls | no fade | the fade | fixed |
| 137 | jump, level, face the middle `instant`, fade in | plain face, no fade | the same | fixed |
| 138 | his target faced, ops Attack, barriers | same | same | matched |
| phase 2 stream | 11 rows (seq, spot, say) | | 11/11 | matched |
| portal shots | to the wall, `6d+6` p100 / `6d-6` p250 by direction | 2 tiles short, `6d-6` p250 every way | the recorded tiles and timings | fixed (all of phase 2) |

**Phase 3, 4301's health replayed** (ours from its P; the first three
columns the recording's P+k):

| | 4301 | ours | verdict |
|---|---|---|---|
| segment 1 | t16 t26 S30 W"39 w42 t45 t55 U59 D90 | t16 t26 S30 W"39 w42 t45 V49 t58 U62 D93 | matched but a volley: the bot stood rooted out of his reach at P+49 (the rule; 4301's raiders were beside him) |
| segment 2 | t117 t127 S131+ V143 W"143 w146 t152 t162 I166 t192 t202 S206+ V218 t227 t237 U241 D272 | t120 t130 S134+ W"146 w149 t152 t162 I166 t192 t202 S206+ t224 t234 U238 D269 | matched (4301's two volleys were its raiders' positions) |
| segment 3 | t299 t309 S313++ W"328 w331 t334 t344 I348 t374 t384 S388++ t409 t419 U423 D454 End472 | t296 t306 S310++ W"325 w328 t331 t341 I345 t371 t381 S385++ t406 t416 U420 D451 End469 | matched (3 early from segment 1's volley) |

**4309's health replayed** (its second segment falls after a spin with no
Untether; it is four raiders, scaled to three's health):

| | 4309 | ours | verdict |
|---|---|---|---|
| segment 1 | t16 t26 S30 V39 t48 t58 U62 D93 | t16 t26 S30 t45 t55 U59 D90 | matched: no vine (82.7% at the line's tick), the volley again the raiders' position |
| segment 2 | V114 t123 t133 S137+ W"149 w152 t155 t165 I169 t195 t205 S209+ D221 | t117 t127 S131+ W"143 w146 t149 t159 I163 t189 t199 S203+ D221 | matched: the fall on the same tick, no Untether |
| segment 3 | t248 t258 I262 t288 t298 S302++ W"317 w320 t323 t333 U337 D368 End386 | the same, to the tick | matched |

**His npc info in phase 3** (ours, P-relative, against 4301's offsets):
fade out P+4, jump + level + fade in P+6, Attack P+7; each Untether: level,
`[transparency] -1, -1, 255, 0`, exact move, `[bas]` `_unleash_loop`, reset
U+30; each fall: `[bas]` `_destroy_loop`, reset D+11, fade out D+12, jump +
level + fade in D+17: all on the recorded offsets (before: no `[bas]`, no
fades, the jump's face not instant). The full fight
(`tfa_ocellus.lua`, solo, 4301's health as a share of his) reaches "End..."
at P+472, 4301's tick, and passes through the death and the vault.

Still open:
- Protect from Melee shown from Untether to the next jump (the 21 October
  hotfix); the recordings, older, send only empty blocks there.
- The Untether's `vfx_ocellus_p3_unleash_spotanim` (U+18) is in 11 of 14
  recorded Untethers (not 4301's first two, not 4312's second); ours always.
- A fall during Insolence: none recorded; ours holds it through the third
  slash. Untether's next attack at U+37 is one sighting (4316).
- An Untether from a tile already at the corner: ours sends no exact move
  (every recorded one moved).
- The 8% margin is bounded 7.9..8.2 by the vine, 6.1..10.4 by Untether.

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
| strike | `scriptedproj_add` (curve scripts) | `npc_scriptedproj_pl`: SCRIPTEDPROJ_ADD, his centre + index at raw 500, the raider at 124, 2 / 39 (+15 a repeat), curves 10024 / 10018 / 10017; the client flies it per deob class56.method1439 (a straight line weaving three times, half a tile either side) | fixed 2026-10-21 (bytes match RSProx's 241 decoder field for field) |
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

Phase 3's timetable in this section (a fixed schedule per segment, Untether
at S+53 / S+134) was one roll of 4301 and 4308; it is replaced by the
rotation and the health rules in §4 (2026-10-22).

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
| p1 strike | 17..19 (13/13), a roll per raider, as each shot lands | 17..19, one roll for all, all on the expend + 1 | a roll each, on its shot's landing | fixed |
| p1 volley | 256 shots over 4301..4316: 248 0 (prayed: no chip through), 9 hits 2..24; the prayer read on landing (a switched volley 47/50 0 vs 179/185); the element carries volley to volley and changes 14 of 65 | 0..25, prayed 0; one element a walk | 0..25, prayed 0 on landing; the element changes 14 in 65 a volley | fixed (the element; the accuracy roll added 2026-10-22: 5 of 13 unprayed shots 0); the top of 0..25 unverified |
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
from him, hit 10 at [2210] (the other 10, [2143], is his ruby bolts' own
cost). Ours: 10 every tick on a void tile, D+4..21 (his ring; D+1..21 before
2026-10-22) and D+35..50 (the rest). How often it really hits is unverified
(one tick on it recorded).

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
three at random; one roll for the whole party (2026-10-22, below).

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
`_buff` are never sent; `_heal` is phase 1's strike shot's impact on each
raider, delayed by its flight (§2: 46/46), not a heal or an empowerment. An
unkilled rift's "charge" is unrecorded (every basic and buff rift was killed).

### Checked and unchanged

- **"If he is unable to reach the targeted player, he will attack with a
  powerful spread void shot"**: the target was beside him at every recorded
  "Your panic... is futile..." (5/5, distance 1). It is on his schedule, not a
  reach test.
- **60% Defence reduction cap** (hotfix): held on his tick (40% of base), as
  Sotetseg's floor is. **Defence not restored between phase 3's quadrants**
  (hotfix): nothing of ours restores it.
- **Phase 1's unblocked line heals him 2**, with its heal splat (recorded:
  4301 [970], 4308 [1520], [1522], [1585], [1640], [1668]); blocked only by
  every raider (§2, 261/261).
- **The censers' effects** stay the wiki's guess (no recording shows them).
- **Phase 2, the tendrils, the untether, the slam, the quadrant destruction**:
  as audited above.

### Open

- What the deflects send back (no raider used the prayed style).
- The void floor's hit rate (one tick on it recorded).
- A killed rift's empowerment and an unkilled one's charge (nothing on the wire).
- The heal rift's projectile on a raider: no splat or heal recorded on them.


## Audit: phase 4's hidden rules, all seven fights (2026-10-22)

Every phase-4 fight recorded: index 4301, 4308, 4309, 4312 (two fights),
4315, 4316, 18 full cycles. All are spectated (the recorder's local index is
none of the raiders'), so nothing private reaches the wire: no explosion or orb
spotanim, and no raider's own hits as seen by them. Each hidden rule below is
rebuilt from what a spectator does see. That is every raider's tile per tick
(before and after its move), seqs, spotanims and splats; their overhead prayer
and weapon from the `--bas` transcripts (`home-all-bas`: `[status]
overheadicon=`, `wearpos=righthand`); and his own npc info. Tools:
`tools/oc_p4.py` parses the seven fights tick by tick (D = each `deflect_in`),
and `tools/oc_p4_<rule>.py` prints each count below. `oc_p4_ours.py
<ticklog.tsv>` prints ours the same way. Ours:
`test/guthixian_archive/tfa_ocellus_p4.lua` (three raiders, `--bots 3`, three
cycles and the death) and `tfa_ocellus.lua` (solo, the whole fight).

### The hidden rules

| rule | recorded | ours before | ours now |
|---|---|---|---|
| explosion: the tile judged | starts on the tile the raider's tick began on (D+9, 11, .., 19; D+40, 42, .., 50) and lands the next tick on whoever still began it there: 38/38 live stayers hit, 0/659 who moved off (the 3 other stayers were Fatalsystem dead, 4301 [1935]) | the same | the same |
| explosion: damage, reaction | 14..16, the raider's defend seq on the tick (35/38; the rest an attack) | no defend seq | the defend seq |
| wind | half the health; at W-1 every raider's defend seq with `delay=30` (94/118; the 24 others an attack of theirs that tick) | no defend seq | the seq at W-1, `delay=30` |
| orb: when taken | the tick after the raider steps on it, or the ready tick (D+16, D+41) if already there: 87/87 on the tile the tick began on (moved-onto model: 36/87) | the same | the same |
| orb: heal | the wind's hit back, +8 on the ready tick only, never above the maximum | the same | the same |
| orb: tile | first wind: any floor tile 2..5 from his 3x3; second: his ring (1). The quadrant's floor is 2..12 out from the arena's middle each way (3203 of 3204 raider-ticks; he stands 5..7) | 2..5 within 5 of his corner, in the quadrant by the middle (offset 1 allowed) | floor 2..12, as recorded |
| orb unclaimed | at W+25 / W+19 he is healed each one's wind hit, splats in party order (2/2), `nex2021_nex_heal` at his middle, range 9: 13/13 heal ticks; the rest fall after his death | the same | the same (our D+54 heal and sound checked) |
| void ring (D+0..21) | hurts from the wind (D+4): 14 raider-ticks in it at D+0..2 not hit, D+8 hit 10 (4309 [2210]) | from D+1 | from D+4 |
| void quadrant (D+31..50) | not before D+35 (41 raider-ticks at D+31..34, none hit; 4 at D+34), none hurt after D+50 (1) | D+35..50 | the same |
| 2-tick attacks | from the first explosion's start to the last landing, D+9..20 and D+40..51: a shot 2 after the last at D+10 (4301 [1720]); 161/221 2-gaps at D+12..20, 167/246 at D+41..52; 0 of the 40 at D+20 again at D+22, 0 of the 21 at D+51 again at D+53, 14 from D+50 at D+52 | clamped from D+11 and D+41 to D+20 and D+52 (the timer's first fire a tick late) | from D+9 and D+40, clamped on that tick |
| hybrid: damage | ONE value a cast, every raider the same (28/28 casts); every raider prayed the matching style by the land tick (the overhead switches register on it): 5:2, 6:9, 7:12, 8:4, 10:1 | a roll per raider, 5..10 | one roll: 5 + three coin flips (5..8, 1:3:3:1); the 10 unmodelled |
| hybrid: reaction | the defend seq (84/97) | none | the defend seq |
| his target | a new one each deflect (9 changes in 11; never the nearest by rule) | kept from phase 3 | drawn each cycle |
| his death angle | the 8-way from his middle to the tile in his last `[facing] player=` (7/7: north, north-west x2, north, west, south-east, north-east) | toward his target's tile at death (2/7) | the tile his last face named |
| rifts | 2, 3, 4 at D+59 (11/11 cycles), on a floor tile 2..5 from his 3x3 (27/27), every one killed (1..2 hits) | 2, 3, 4 on 2..9 out from the middle, beside him or not | floor tile 2..5 from him |
| phase-3 patches in phase 4 | the last quadrant's `tfa_ocellus_void_degen_bomb`s stay and hurt: the 12 unexplained 6s and 7s on melee raiders (4301 [1757], [1821], [1878], 4312 [2156], ..) were all raiders standing on one | inert from the end of phase 3 | hurt through phase 4 |
| patch cadence (phases 3 and 4) | per raider: never the tick after a void hit (0/19), always 2 after (7/7), first contact 46/50 (two misses on a wind's tick, one the tick they were cleared, one unexplained: 4312 [4428]); not the tick after a bomb lands on them (0/7) | every even map clock, anyone on one | per raider, a bomb's landing counted |
| patches at his death | every one left becomes `invisible_type0_nonblocking`, turned as the patch was (42/42), on the tick his health reaches 0 | left | replaced (ours: 4 at K, rotations 4/4) |

### His npc info, every block kind, per cycle tick (11 full cycles)

| D | recorded (11/11 each) | ours (party run, 3 cycles + death) |
|---|---|---|
| 0 | `deflect_in`, `[headicon]` 15, `[bas] ready, walk = _deflect_loop`, `[facing] player=` | the same (the face set at D+59, out a tick on) |
| 2, 3 | `_deflect_loop`; `_soul_fragment_start` | the same |
| 4 | slot 2 `_levitating_idle loop=true` | the same (`spotanim_npc_slot_loop`; was without the loop flag) |
| 21 | `_deflect_out`, slot 2 `-1 loop=true`, `[bas]` (reset), `[facing] type=reset` | the same (no `[bas]` before) |
| 24, 52 | `melee_hybrid` + its kind's spot slot 2 | the same (the kind random) |
| 25, 30, 31, 53, 59 | `[headicon]` 11, none, 16, 11, none | the same |
| 34 | slot 2 `_ocellus_idle loop=true`, `_soul_fragment_start` | the same |
| 50 | `_soul_fragment_end`, slot 2 `-1 loop=true` | the same |
| E-1, E | the jump with the middle faced `instant` and an empty headicon; at E `[headicon_customisation]` empty, `enabled_ops 0b00010`, "End...", and no face until D+0 (7/7) | the jump and headicon at E-1, "End..." at E (the whole fight, `tfa_ocellus.lua`); his target was faced at E by phase 3's jump rule, now dropped on the last jump. The empty headicon and ops are not re-sent at E (unchanged values, nothing shown) |
| K | death seq, three spots, slot 2 `-1 loop=true`, headicon none, ops none, `angle=` | the same |

`oc_stream.py --phase 4` (4301 against ours): every seq, spot and say matched
but the hybrid's random kind; faces 8 of ours on the recorded ticks.

### The arena and the stones

| | recorded | ours |
|---|---|---|
| arena npc spots | range dome D+0, loop D+1 and every 3 to D+19, end D+21; melee D+31, D+32..47, D+50 (11/11); south-west unsuffixed (4309, the first SW recording), SE `_270`, NE `_180` | the same; north-west `_90` still unrecorded |
| the arm's stones at E | `steppingstone_despawn_01..03`: south and west arms 7, 4, 10 out as 01, 02, 03 (4301, 4312 x2, 4316; 4309), the east arm 10, 7, 4 (4308, 4315) | none before; now as recorded, the north arm (unrecorded) as the east |

### Still open

- His face is re-sent on most ticks of a deflect (72 in 4301's phase 4), never
  the tick after his target attacks (1 of 82); ours sends it once. Wire only,
  the same target. Rule not found.
- What a deflect does to the prayed style: no recorded attack of it (the 3
  candidates were at a rift). Ours: nothing lands.
- The hybrid unprayed, and the one 10 (4301 [1919]).
- The death a tick after the killing hit (3 of 7 fights; the patches go on the
  hit's tick either way): ours always dies on the hit's tick.
- One unexplained patch miss (4312 [4428], D+34).
- The void floor's hit rate (one hit recorded).
- The north-west quadrant's arena suffix and the north arm's stone order.

## Recheck: index 4318, 4319, 4320 (2026-10-22, later)

Three new sessions hold five more phase-4 fights: 4318 (a party of five
wiped after eight cycles, then another of five who won), 4319 (five), 4320
(three, then three). All spectated again (the recorder follows one party
after another, so `oc_p4.py` now takes each fight's raiders from the party
HUD's names, script 10042), 30 full cycles in all. 4315's `--bas`
transcript had no `[status]` lines; re-transcribed (`--force`) it has them.
A patch from a fight in another instance shares the template tiles, so
`oc_p4_patch2.py` counts only patches laid in the same fight.

### Still agreeing

| rule | over 12 fights |
|---|---|
| his blocks by cycle tick (seqs, `[bas]`, slot-2 loops, icons, faces) | 30/30 cycles |
| explosions: start-of-tick tile at both ends | 124/124 live stayers hit; of 1456 who moved off, none a full hit |
| orb taken on the start-of-tick tile | 224/224; +8 on the ready tick only, never more later |
| second-wind orbs on his ring | 89/89 |
| unclaimed orbs heal him at W+25 / W+19, sound 5281 at his middle, range 9 | every one before his death |
| defend seq: wind at W-1 `delay=30`; explosions, hybrid on the tick | 221, 118, 207 (the rest an attack, or dead) |
| 2-tick attacks D+9..20, D+40..51 | 2-gaps only in the windows, but a blowpipe's own and one mace swing at D+4 (4320 [2428], two after a swing into his deflect) |
| the quadrant's floor 2..12 out | every live raider-tick |
| barriers by quadrant | 12/12, the first north-west fight too |
| arena npc spot suffix | north-west `_90` (4320's second fight): as inferred |
| patch cadence | never the tick after a void hit 0/35, two after 12/13, first contact 83/89 |
| a new random deflect target each cycle | as before |

### Answers to the open questions, and what changed

| question | answer | ours now |
|---|---|---|
| an unkilled rift | detonates 30 ticks from its add: its detonate seq, ops none, bar off, gone +2; every raider, anywhere, `vfx_human_ocellus_voidflare_<kind>` delay 30, 39..41 a rift, the defend seq (4318 [3663] two Rifts, [3723] one; 4320 [2293] a phase-3 Augmenter). The `vfx_human_*` spots are this, not "never sent" | `~tfa_oc_rift_detonate`, phases 3 and 4 (the healing rift's unrecorded, as the others') |
| rifts past four | 2, 3, 4, then 5 every cycle (4318's wipe: 2, 3, 4, 5, 5, 5, 5) | capped at 5 |
| the hybrid unprayed | 35 where the party took 6 (4319 [2072]) | prayed x 35/6 |
| the hybrid's 10s | it rises with the cycle: 5..8 in the first two, then a point a cycle, at most 3 (4318's wipe: 5,7 / 7,6 / 8,9 / 8,9 / 10,9 / 9,11 / 9; not his health: 9..11 there at 12..16%, 7..8 elsewhere at 2..5%); prayed values the same for the whole party 66/66 | `+ min(max(0, c - 1), 3)` |
| the void floor's rate | every tick, 9..11 (9 x2, 10 x4, 11 x5), on the start-of-tick tile; never on the wind's tick (4319 [2079]: the wind only); D+5 and D+6 both (4319 [2080..2081]); ring hits D+5..18, quadrant D+37..48, none at D+50 | D+5..20 and D+36..49, 9..11 |
| what a deflect does | blocks the hit (as ours) and turns a tenth of it back the next tick when that is 1 or more, as Ancient Curses' Deflects: 3 of 7 attacks into one (4318 [3478] 2, [5818] 3, 4320 [2337] 1, before the bow's arrow landed); other-style attacks 3 small hits in 1268 | `queue(tfa_oc_reflect, 1, damage / 10)` in phase 4 |
| his death angle | his last face's `fallbackangle` to 8 ways: 11/11; that angle is to his target's tile as the tick began (4320 [2509] 1839 -> 1792, where the tile it reached gives 0) | the start-of-tick tile (as before; the comment corrected) |
| the face re-sends | sent on exactly the ticks the angle to his target's start-of-tick tile changes: 800/800 deflect ticks. Pets re-send the same way: rev 241's npc info re-sends an entity face when its `fallbackangle` changes | **open (engine)**: our encoder sends `fallbackangle` 0 and the face once |
| the arm that crumbles | the one between the last quadrant and the quadrant he jumped from: 12/12 (4319 went NE -> SE and crumbled the east arm; the fixed "clockwise" arm was wrong whenever the order turned) | from his last jump |
| a first-wind orb in his ring | 1 of 142 (4318 [5905], taken at D+25 after the ring's void) | 2..5 kept; the one exception noted |

### Found while testing

- A raider killed by the hybrid under Soul Split (4319 egghead31, D+57)
  was followed by a heal of 40 on him at D+59 with `nex2021_nex_heal`;
  three deaths under his deflects healed nothing. One sample: not built.
- **The raid's own death handling (not phase 4):** ours wipes the room on the
  first raider's death (`tfa_death_spectate` sets `tfa_state_wiped`); the
  recordings fight on until the last (4318's first fight: Solo Kev [3563],
  Miidgeh [3621], then the rest). Our phase-4 tests keep the bots healthy.
- Our bots shoot rifts first now (`quick` attacks), as every recorded party
  did; a lone bow cannot kill a second wave of three in time, so the solo
  test kills him inside a cycle.

### Still open

- The face re-sends with their `fallbackangle` (engine, every npc).
- The death a tick after the killing hit: 3 of 11 (4309, 4312's first,
  4316), no feature of the killing hits told them apart.
- The healing rift's detonation, the north arm's stone order (no recording).
- Two unexplained patch misses (4312 [4428] at D+34, 4318 [5318]).
