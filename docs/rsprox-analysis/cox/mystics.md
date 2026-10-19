# Skeletal Mystics: recorded behaviour

Read from submissions 3285, 3282 (solo, normal) and 3389 (solo, Challenge Mode), all on Grid Master worlds; see
[README.md](README.md). These are the only archive recordings holding npc 7604, 7605 or 7606 (search 2026-10-18).
Each holds one ccw-variant room with three mystics: 3285's is unturned, 3389's is turned 180 and 3282's a quarter
(`tools/rsprox_frame_check.py`: 34 of 34 pairs right in 3285, 0 wrong in 3389, 37 of 70 wrong in 3282). So
everything here is read from **untranslated** transcripts (instance tiles, every raider shown; README.md, "Instance
tiles"). Template tiles come from the three portals, which are 1x1 locs that 3285 prints exactly; each room's turn and
offset is solved from where its portals land.

Tool: `tools/cox/rsprox_mystics.py` (sections `attacks`, `melee`, `blocks`, `wake`, `hunt`, `movement`, `death`,
`room`). It refuses translated input. `tools/rsprox_tells.py --anchor 7604-7606` lists every tell in the rooms. The
content cites these rows as `RM-*` (`cox_minions.rs2`, `cox.constant`, `cox.npc`). The room's spec is
`docs/minigames/cox/rooms/mystics_spec.md`.

    python3 tools/cox/rsprox_mystics.py <raw>/archive/{3285,3389,3282}.txt
    python3 tools/cox/rsprox_mystics.py --section hunt --probe <built tools/cox/los_probe.c> <raw>/archive/*.txt

**C** is a cast's tick (its projectile), **L** its landing (C + end // 30), **D** the death seq's tick. The **gap** is
the Chebyshev distance from the 2x2 footprint to the raider (0 under it). "SW-Chebyshev" is measured from the
mystic's south-west tile. Heights are raw (our byte is raw / 4). The fights are short (Grid Master's damage): 20
casts, 9 mystics.

## Casts (20: 13 Fire Blast, 7 Vulnerability)

| Row | Recorded |
|---|---|
| RM-A1 | One cast every **4** ticks (11 of 11 gaps). The mystic's seq on C is `skeleton_update_mage_casting_swansong` (5528), 20 of 20. The cast graphic is `skeleton_big_casting_red` (1322) or, for Vulnerability, `skeleton_big_casting_purple` (1321), at **height 164**, delay 0. That is 18 of 20; the other two (3389) lost the graphic slot to the raider's own impact on the mystic that tick. |
| RM-A2 | The projectile is `fireblast_travel` (130) or `curse_travel` (109), **not** `vulnerability_travel` (168). It starts at cycle 30, with angle 0, progress 96 and heights 340 -> 124, leaves the mystic's **south-west tile** (20 of 20) and is aimed at the raider. |
| RM-A3 | The flight ends at **46 + 10 x gap**: 96 at gap 5, then 106, 116, 126, and 136 at gap 9 (20 of 20). SW-Chebyshev and the centre distance both fit worse. |
| RM-A4 | It is aimed at the tile the raider ended C-1 on (20 of 20). |
| RM-A5 | Everything for the landing is sent **on C**, delayed to the flight's end, and nothing extra is sent on L. On the raider, the impact graphic plays at height 124: `fireblast_impact` (131) or `curse_impact` (110) when the cast lands, `failedspell_impact` (85) when it splashes. That is 17 of 20; 3389's three lost the slot to the raider's own spell graphic. The sounds are synths to the raider: `fireblast_cast_and_fire` (155) or `curse_cast_and_fire` (127) at delay 0 (20 of 20). Then, at delay = end, `fireblast_hit` (156) for a landed Fire Blast (5 of 5), `zap_impact` (3012) for a landed Vulnerability (2 of 2), or `spellfail` (227) for a splash (13 of 13). |
| RM-A6 | L = C + end // 30, the wiki's magic delay 1 + (1 + gap) / 3 at every recorded gap (20 of 20). A landed cast splats on L (7 of 7). **A splash sends no hitsplat** (13 of 13). Every hitsplat on the raider while mystics lived belongs to a landed cast (7 of 7). |
| RM-A7 | Both landed Vulnerabilities (3285) left the raider's overloaded Defence (104) where it was: no stat update on L or the three ticks after. |
| RM-A8 | Every landed cast was under Protect from Magic: Fire Blast up to 10 (5 casts), Vulnerability up to 8 (2). |

## Melee

| Row | Recorded |
|---|---|
| RM-M1 | **None.** No seq other than the cast, the block and the death was sent, and no hitsplat came without a landed cast. The server's own line of walk (`tools/cox/los_probe.c`) shows that on every recorded tick no straight walk existed from a mystic to the raider. Each raider stood where the rocks broke it (the "god spots"), so Near-Reality's melee switch (50%, gated on that walk) was never eligible. |

## Blocks and hits

| Row | Recorded |
|---|---|
| RM-B1 | `skeleton_update_defend` (5489) is sent on the attacker's attack tick, delayed to its projectile's end (42 of 42). A cast the same tick wins the seq slot. |
| RM-B2 | `skelly_hit` (779), a synth to the raider, plays with every hit on a mystic (41 of 41). |

## The opening (9 mystics)

| Row | Recorded |
|---|---|
| RM-W1 | **Hunt range 7.** An idle mystic opens on a raider within **7 of its south-west tile** (Chebyshev) that it can see. "It can see" is the server's own line of sight, through `los_probe`. The hunt reads the tiles at the end of the tick before. Fitted over the 9 mystics, range 7 puts the first face on the recorded tick for 9 of 9 and the first cast for 8 of 9. Range 6 gives 8 and 7 of 9, range 8 gives 8 and 5, range 9 gives 4 and 1. No mystic opened on a raider it could see at 8-15, over dozens of ticks (3285 c for 16, b for 18). |
| RM-W2 | One the hunt has not opened **answers an attack**: it faces the raider on the tick the attack lands, and casts **2 ticks later** (6 of 6; attackrate / 2). |
| RM-W3 | A hunted mystic casts on the hunt's tick (3285 b, 3282 c). 3389 a faced on the predicted tick but cast 2 ticks later, which is not explained. |

## Movement

| Row | Recorded |
|---|---|
| RM-S1 | Each solo room holds one mystic of each id, every time in the same region: a (7604) round template (13..21, 14..17), b (7605) round (11..19, 20..25), c (7606) round (17..21, 20..24) (south-west tiles, 3 of 3 rooms). So the id goes with the spawn slot; Near-Reality rolls it per spawn. The positions fit Near-Reality's ccw tiles (14, 13), (14, 22) and (20, 23), but only if they wander at least 7 from them: a reaches x 21. |
| RM-S2 | Before a fight they **wander**: 28 steps, one tile a tick in short walks. In a fight they **never step**: 0 steps after a mystic's first cast, over 9 mystics and 20 casts, with the raider 6 to 10 off its south-west tile. |

## The death and the clear

| Row | Recorded |
|---|---|
| RM-D1 | **D:** `skeleton_update_death` (5491) with the killing hit (9 of 9), `skeleton_death` (777) as a synth to the killer, and the npc's ops cleared. |
| RM-D2 | **D+5:** the npc removed (9 of 9). On its south-west tile it drops big bones (despawn 199) and 5-10 each of golpar, buchu and noxifer seeds (despawn 2999). The party line "<col=ef20ff>The Skeletal Mystic slain by <name> dropped seeds for all.</col>" is sent as a friends-chat notification. |
| RM-D3 | The last kill clears the room on its D+5 (3285, 3282) or D+6 (3389). |
| RM-D4 | The Mark of Power (`raids_skeletalmystics_symbol`, 2x2) is added at **rotation 0** in every room, whatever its turn, on template (6, 16). The clear does not delete it: its own tile becomes `invisible_type8_nonblocking`, rotation 0, and the three portals are added at rotation 0. Their template tiles (ccw) are start (14, 2), middle (20, 18) and end (7, 18), the same in all three rooms. |

## Fixed in our content (2026-10-18)

| Tell | Ours before | Now |
|---|---|---|
| cast seq | `skeleton_update_mage_casting` (5523) | 5528 (RM-A1) |
| cast graphic | height 0 | height 164 |
| projectile | from the 2x2's middle; start 15, end ticks x 30, heights 200 -> 100 raw, angle 15, progress 64; Vulnerability on 168 | from the south-west tile; start 30, end 46 + 10 x gap, 340 -> 124, angle 0, progress 96; 130 / 109 (RM-A2, A3) |
| impact | `firewave_impact` 157 / `vulnerability_impact` 169 / 339 at height 0, delayed ticks x 30 | 131 / 110 / 85 at height 124, delayed to the end (RM-A5) |
| sounds | none from the mystic | cast and impact synths on C (RM-A5); `skelly_hit` on every hit, `skeleton_death` (`cox.npc` defend_sound, death_sound; the combat ledgers too) |
| splash | a 0 block splat on L | no hitsplat (RM-A6) |
| Vulnerability drain | 4-8 from any Defence | none on a boosted Defence (RM-A7); NR's 4-8 otherwise |
| hunt | any raider within 15 it could see | within 7 of its south-west tile, in sight (RM-W1) |
| ids | random per spawn, in table order (14, 13), (22, 17), (20, 23) | a, b, c by slot, at (14, 13), (14, 22), (20, 23) (RM-S1) |
| wander | the default 5 | 7 (`cox.npc`), the least the recorded walks allow from those tiles |
| death | the seq a tick after the blow; drops and despawn at D+3; seeds 200 ticks; no line | seq with the blow (`npc_setdeathanimnow`), D+5 (`death_delay=4`), seeds 3000, the line (RM-D1, D2) |
| the clear | `loc_del` of the mark; the mark and portals at the room's turn; middle (19, 18), end (8, 19) | the mark's tile becomes `invisible_type8_nonblocking`; everything at rotation 0; the recorded tiles (RM-D4) |
| melee walk | `npc_walk` at the raider's tile: the 2x2's south-west corner steered onto him | `npc_approach`, a new engine command: the engine's own pursuit, the naive path to a tile beside him, then diagonal, else X, else Z. Its line is what corner-traps a mystic on a rock (Synq: "they'll get trapped on objects either east or west of them") |

`test/raids/_mystics_tells.lua` checks every cast, opening, hit sound and death off the server's tick log. In a
worktree it fails with the hunt back at 15 (three openings at 9-10 tiles) and with the splash's 0 hit back (every
splash drew a hit).

## Open

| Item | State |
|---|---|
| Melee | Never eligible in the recordings (RM-M1). The 50% switch, the punch (5485), its max and the 8-tick give-up are Near-Reality's. The corner trap now walks the engine's pursuit line, but no recording shows one. |
| Max hits, accuracy | All recorded landings were under Protect from Magic on Grid Master (RM-A8). The 25 and the halving are Near-Reality's. |
| Vulnerability on an unboosted Defence | Not recorded (RM-A7). |
| Spawn tiles, wander range | Near-Reality's tiles with a range of 7 fit every recorded position, but other tiles with a smaller range fit too (RM-S1). Only the ccw room is recorded; the thru and cw tables, and slots 3-5, are Near-Reality's. |
| 3389 a | Faced on the hunt's tick, cast 2 ticks later (RM-W3). |
| The party | Count, scaling and targets are unrecorded (all three recordings are solo). `~cox_olm_party_size` is still the solo stub, so a duo or trio gets 3 mystics. |
| The line's message type | Sent with `.mes` (a game message); Jagex sends a friends-chat notification. The engine has no command for that type (the Vanguards' and Muttadiles' lines share this). |
| The opening when out of sight or range | Never recorded: every opening had the raider in sight and within 10. |
