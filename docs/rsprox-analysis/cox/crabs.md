# Crystal crabs: recorded behaviour

3389 is the only recording that holds the room: a solo raid in **Challenge Mode**, on a Grid Master world (see [README.md](README.md)). It covers about 450 ticks:

- 4 crabs, 27 orbs and about 80 paint changes
- 16 hammer smashes and 62 crab swings
- all four small crystals lit, and the shatter

It was read **untranslated** (instance tiles).

- Tool: `tools/cox/rsprox_crabs.py`, with sections `room`, `orbs`, `paint`, `smash`, `swings` and `movement`. It refuses translated input.
- `tools/rsprox_tells.py --anchor 7576-7583` lists every tell in the room.
- The content cites these rows as `RC-*` in `cox_crabs.rs2`, `cox.constant` and `cox.npc`.
- The room's spec is `docs/minigames/cox/rooms/crabs_spec.md`.

    python3 tools/cox/rsprox_crabs.py <raw>/archive/3389.txt

**E** is an orb's end tick.

## The orbs

| Row | Recorded |
|---|---|
| RC-O1 | **One orb at a time.** Each is added on the tick the last one is removed (25 of 26; once a tick later), on the same tile and heading the same way every time. It does not move on the tick it is added. It then moves a tile a tick (27 of 27), each step sent as a teleport with its type re-sent. |
| RC-O2 | **Bounce:** on a crab ahead, the orb takes the crab's colour, turns, and steps the new way on the same tick. `anma_axe_bounce` (3274) plays round the tile it turned on, range 6 (8 of 8). |
| RC-O3 | **An end on nothing** (a wall ahead): the orb stays put on E, with `windbolt_impact` on itself at height 80 (no delay) and `windbolt_hit` (219) **loops 6**, range 6 (15 of 15, every one a white orb). No map graphic. It is removed on E+1, and the next orb is added then. |
| RC-O4 | **An end on a small crystal:** the orb steps onto the crystal on E. Its colour's bolt impact plays at height 80, delay 10: `windbolt` (119) white, `firebolt` (128) red, `waterbolt` (122) blue, `earthbolt` (125) green. The sound is `place_crystal` (1476) when it lights the crystal (4 of 4) and `tl_place_fractured` (1294) when it does not (3), delay 10, range 6. On a light, the crystal becomes `raids_lasercrabs_smallcrystal_white` (29762) at its own rotation, on E. The orb is removed on E+1. |
| RC-O5 | **An end on the raider:** the orb steps onto his tile on E. Its wave impact plays at height 80, delay 10 (`windwave` 160, `firewave` 157), with `windwave_hit` (223) or `firewave_hit` (163), delay 10, range 6, and **a hit of 22** that tick (4 of 4). The raider was at 96-99 hitpoints with no overhead on; NR's quarter of his hitpoints would be 24-25. The orb is removed on E+1. |

## The crabs

| Row | Recorded |
|---|---|
| RC-N1 | 4 crabs in this Challenge Mode solo raid, added together in a column at 2-tile spacing. |
| RC-A1 | **A swing:** `horror_crab_pincer_attack` (2368) with `rock_crab_attack` (718) to the target (62 of 62). The hit lands that tick (58 zeros, 4 hits). Swings come every 4 ticks (41 of 61). The block is `horror_crab_defend` (1313), delayed to the attacker's projectile, with `rock_crab_hit` (720). Hits on a crab are always 0. |
| RC-P1 | **Paint:** on the attack's own tick (the launch, for a shot or a cast), magic paints blue, ranged green, and melee or the hammer red. A colour lasted 5-14 ticks after the last attack on that crab (26 colours). |
| RC-S1 | **Smash:** the raider plays `human_hammer_hit` (1755) (16 of 16). On the crab that tick come `stunned_thieving` (245) at **height 100, delay 30**, `kalthite_egg_crack` (556, delay 20) and `stunned` (2727, delay 30), both round it with range 5. **The crab plays nothing.** It also turns red. |
| RC-S2 | **The stun stops walking, not swinging.** A smashed crab next stepped 51-61 ticks after its last smash (8 of 8; a smash on a stunned crab restarts it). It swung 1-3 ticks after a smash 9 times. |
| RC-M1 | Crabs wander a step every 4-5 ticks (169 of 197 step gaps). Within 3 of the raider they step a tile a tick toward him. |

## The crystals

| Row | Recorded |
|---|---|
| RC-C2 | **The big crystal is re-sent every 3 ticks** (145 of 147 gaps). A crystal lit shows on it at the next re-send: 3, 2 and 3 ticks after. The fourth shows 1 tick after, with the removal of the orb that lit it. |
| RC-C3 | **The solve.** The fourth crystal is lit on E. Then: <ul><li>E+1: the orb is removed and the big crystal reaches stage 5, with `whiteout` (2425) round it, range 10.</li><li>E+2 to E+5: one crab a tick plays `horror_crab_hide` (1314), its menu hidden (`enabled_ops 0`) and its facing cleared, with `rock_crab_death` (719): to the raider for the first three, round the crab (range 10) for the one on E+5. No hitsplat, no death animation.</li><li>E+4 to E+7: each crab is removed 2 ticks after its hide.</li><li>E+5: the shatter, `raids_lasercrabs_shattercrystal` (1326) three times: on the crystal's tile at height 10, then on two more tiles of its 2x2 delayed 5 and 10; with `100_cauldron_explodes` (1021) and `kr_smash_window` (3821, delay 5), range 10, on the crystal.</li><li>E+7: the 2x2 becomes `invisible_type8_nonblocking`.</li></ul> |

## Fixed (2026-10-19)

| Was | Now |
|---|---|
| the crab swing `horror_crab_attack` (1312), silent | 2368 with `rock_crab_attack` (RC-A1) |
| the smash: a pound animation, the crab's defend animation, the stun graphic at height 140 with no delay, no sound | `human_hammer_hit`; no crab animation; 245 at height 100, delay 30; the two sounds (RC-S1) |
| an orb at a wall: a windbolt graphic, removed on its last step's tick, silent | still for one tick with `windbolt_hit`, no graphic, removed the tick after (RC-O3) |
| an orb on a crystal: judged a tick after stepping on, the impact at height 124, no sound; green's impact the wind bolt | judged on the step, impacts at height 80, delay 10, with `place_crystal` or `tl_place_fractured`; green's impact is the earth bolt (RC-O4) |
| an orb on the raider: removed on that tick, the wave at 124, no sound | the wave at 80, delay 10, with its sound, removed the tick after (RC-O5) |
| orb steps sent as walks; a bounce silent | teleport steps; `anma_axe_bounce` (RC-O1, O2) |
| the big crystal stepped the moment a crystal lit | the stage shows at the next 3-tick re-send (RC-C2) |
| the solve: crabs deleted 1 tick and the shatter 2 ticks after, one graphic | crabs one a tick from E+4, three shatter graphics on E+5, opened on E+7 (RC-C3) |
| the solve silent; the crabs removed without a tell (found by the full `rsprox_tells.py` inventory, 2026-10-19) | `whiteout` on E+1; each crab hides with `rock_crab_death` from E+2 and goes 2 ticks later; the shatter's two sounds (RC-C3) |
| a wall end with no graphic | `windbolt_impact` on the orb (RC-O3) |

`test/raids/_crabs_tells.lua` checks every orb (the chain, its steps, its end and removal ticks, its impacts), every crab swing, every smash and the solve off the server's tick log. It plays the room solver on 8 of 8 layouts. Three worktree mutations are each caught: the old swing animation, the old stun graphic, and a wall-ended orb removed on its own tick. The room test (`cox_crabs.lua`) passes 8 of 8.

## Open

| Item | State |
|---|---|
| `windbolt_hit`'s loops | Sent with 1. This engine's `sound_area` has no loops argument yet (the engine item in the CoX list). |
| The orb's hit | 22 at 96-99 hitpoints with no overhead (4 of 4). Ours keeps NR's quarter of hitpoints. |
| The big crystal's re-send | Only the stage timing is modelled. The every-3-ticks re-send itself is not sent. |
| The orb's type re-sent every step | Not sent; ours sends a change only. |
| Crab count | 4 recorded, in Challenge Mode. Ours puts 3 in a solo room; normal mode is unrecorded. |
| Wander | Ours wanders on the engine's random cadence, not every 4-5 ticks. |
| Paint length | 5-14 ticks recorded, a spread no rule fits yet; ours is 8. |
| The orb's start | One room's start tile and heading; ours follows NR per variant. |

## Index 4392 (rev 241, normal world, 2026-10-26)

The first normal-mode recording of the room (solo; [1576]-[1966]; `tools/cox/rsprox_crabs.py` on the `--raw` transcript).

| Row | Recorded | Was | Now |
|---|---|---|---|
| crab count | **4** (normal solo, as 3389's CM) | 3 | 4 (`^cox_crab_min`) |
| wall end | the orb's colour: `windbolt_impact` + `windbolt_hit` loops 6 (white, 7/7); `firebolt_impact` + `firebolt_hit` loops 6 (red, 4/4) | wind pair, loops 1, every colour | colour's pair, `sound_area_loops` 6 |
| big crystal | re-sent every 3 ticks at every stage, stage 0 too (131 sends, 128/130 gaps 3) | only on a stage change | every 3 ticks |
| orb step | teleport + `level_change 0` + `enabled_ops` + the orb's own `transformation`, every step (never on a still tick) | teleport only | `~cox_crab_beam_step` |
| stun | the stunned crab swings 1 tick after the smash (14 of 22) | (RC-S2 already) | unchanged |
| crab hits | 19 unprayed swings: 16 zeros, 1, 1, 10 (protect melee from [1655]: all 0) | max from strength | open (A17: too few to identify) |
| paint length | 6-15 ticks after a smash | 8 | open |
