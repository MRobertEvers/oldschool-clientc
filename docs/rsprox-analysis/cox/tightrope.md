# Tightrope: recorded behaviour

Read from all five raid recordings (see [README.md](README.md)), seven tightrope rooms:

- 843: two solo rooms
- 3282: two solo rooms
- 3285: one solo room
- 3389: one solo Challenge Mode room
- 820: a four-man party. Four mages and four rangers; the local raider fought the mages and another raider (SangKodai) crossed.

In all, 32 guards, 117 casts, 63 shots, 12 crossings by the local raider and 6 dispels. Every transcript was read **untranslated** (instance tiles, every raider shown; README.md, "Instance tiles"). The guards are 1x1, so distances need no frame. Template tiles come from the rope ends and the keystone the raider clicked; all six solo rooms solve to the ccw template, unturned, turned 180 or a quarter.

- Tool: `tools/cox/rsprox_tightrope.py`, with sections `room`, `attacks`, `cadence`, `wake`, `crossing`, `blocks`, `movement`, `death` and `barrier`. It refuses translated input.
- `tools/rsprox_tells.py --anchor 7559-7560` lists every tell in the rooms.
- The content cites these rows as `RT-*` in `cox_puzzles.rs2`, `cox.constant` and `cox.npc`.
- The room's spec is `docs/minigames/cox/rooms/tightrope_spec.md`.

    python3 tools/cox/rsprox_tightrope.py <raw>/archive/{843,3282,3285,3389,820}.txt

Notation:

- **C** is an attack's tick (its projectile). **L** = C + end // 30.
- The **gap** is the Chebyshev distance from the guard to its target.
- Heights are raw; our projectile byte is raw / 4.
- **S** is the tick a crossing's click takes effect (after the arrive delay).
- **T0** is the crossing's first step onto the rope.
- **K** is the dispel's tick.

## The guards

| Row | Recorded |
|---|---|
| RT-S1 | Two platforms, in template tiles: the mages on x 21..23, z 19..20, the rangers on x 20..22, z 25..26. Two guards are often first seen on one tile. A solo room holds 2 of each; 820's four-man party had 4 of each. |
| RT-S2 | They step about while idle. **No guard stepped after its first attack** (32 of 32). |
| RT-W1 | **No hunt.** 820's four rangers stood 6-9 tiles from raiders for 137 ticks and opened on nobody. A guard that is hit faces its attacker and **attacks 2 ticks later** (24 of 24, counted from the face, which is the hit's tick or up to 3 before its splat). |
| RT-W2 | The fight holds at range. Mages cast at gaps 5-9 and rangers shot at 2-8. Targets were up to 9 off a guard's first tile (dx 9, dz 8 at the widest). In 820, a raider 9 off a mage's first tile was cast at every 4 ticks for 40 ticks. |

## Attacks

| Row | Recorded |
|---|---|
| RT-A1 | **A mage's cast** (117), on C: <ul><li>seq `human_caststrike_staff` (1162), 117 of 117</li><li>graphic `fireblast_casting` (129) at height 92, 116 of 117</li><li>sound `fireblast_cast_and_fire` (155), a `sound_area` round the mage, range 10, 117 of 117</li><li>projectile `fireblast_travel` (130) from the mage's tile at the raider: start 51, angle 16, progress 64, heights 172 -> 124</li><li>the flight ends at **46 + 10 x the gap**: 96 at gap 5 up to 136 at gap 9 (117 of 117)</li></ul> |
| RT-A2 | **A ranger's shot** (63), on C: <ul><li>seq `human_bow` (426), 61 of 63</li><li>graphic `rune_arrow_launch` (24) at height 96, 60 of 63</li><li>sound `arrowlaunch2` (2693) round the ranger, range 10, 63 of 63</li><li>projectile `rune_arrow_travel` (15): start 41, angle 15, progress 11, heights 163 -> 146</li><li>the flight ends at **46 + 5 x the gap**: 56 at gap 2 up to 86 at gap 8 (63 of 63)</li><li>no impact graphic and no impact sound</li></ul> |
| RT-A3 | **The landing.** For a cast, everything is sent on C, delayed to the flight's end. On the raider: `fireblast_impact` (131) at height 124, with `fireblast_hit` (156) round his tile (range 10), or `failedspell_impact` (85) with `spellfail` (227). The hit lands on L, which is the wiki's hit delay at every recorded gap. A landed cast splats on L (24 of 24 single landings, one of them a 0). **A splash sends no hitsplat** (13 of 13). A ranger's shot splats on L (17 of 20; the other 3 waited for a crossing's end, RT-X2); a miss splats 0. |
| RT-A4 | Both aim at the tile the raider ended C-1 on (180 of 180). |
| RT-A5 | **One attack every 4 ticks** (148 of 152; 4 ranger gaps of 6). |
| RT-A6 | The solo hits, unprayed: up to 16 (mage) and 34 (ranger). Under the matching protection: up to 7 (mage) and 17 (ranger). These fit the wiki's 22 and 70 with NR's 33% let through. 820's party took a mage's 13 under Protect from Magic. |

## Blocks, deaths

| Row | Recorded |
|---|---|
| RT-B1 | The block seq is `human_staff_block` (415) for a mage (156 of 156) and `human_unarmed_def` (425) for a ranger (52 of 52), delayed to the attacker's projectile end. The hit sounds are `godwars_spirit_hit` (3878) and `blade2` (29), already in `cox.npc`. |
| RT-D1 | `human_death` (836) comes with the killing hit (15 of 24 solo deaths; the other 9 a tick after it), with `godwars_spirit_death` (3891) for a mage or `human_death` (512) for a ranger. The guard is **removed 2 ticks after the death seq** (32 of 32), and bones (526) appear under it then, despawn 200 (24 of 32 seen). |

## The crossing

| Row | Recorded |
|---|---|
| RT-X1 | **12 crossings, all alike:** <ul><li>S and S+1: `face_angle` along the rope.</li><li>T0 = S+2: the appearance changes (the balance animations) and the raider moves **two tiles**, onto the tile past the near rope end.</li><li>T0+1 to T0+8: one tile a tick; T0+8 lands one past the far end.</li><li>`tightrope` (2495) to the raider on T0+1 to T0+7 (the step onto the far end is the 7th): loops 2 six times, then loops 1. The step on T0+8 is silent.</li><li>T0+9: the appearance changes back.</li><li>**No message.**</li></ul> |
| RT-X2 | **The dump:** every hit landing from T0 to T0+8 splats on T0+9. In 843, three ranger landings on T0, T0+3 and T0+7 all splatted on T0+9. |
| RT-X3 | **The wake** (820: SangKodai's crossing, T0 = 4166): all four idle rangers faced him and shot on **T0+1**, then every 4 ticks, through both of his crossings, with him on the rope. The mages, already fighting the local raider, stayed on him. |
| RT-X4 | **Shouts** (820, 5): from rangers whose target was the crosser, while he was on the rope: T0+2, +4, +5 and +6 on the way over, and T0+5 on the way back. The lines were "Stop them at all costs!" three times and "Protect the keystone!" twice. There were none on the wake tick, and none in the solo crossings: 843's ranger r222 fought the crosser for 9 rope ticks without a word. That is 5 shouts in 72 guard-ticks. |

## The keystone and the barrier

| Row | Recorded |
|---|---|
| RT-K1 | **Take:** two ticks after the raider stops, facing the crystal on the first (6 of 6). He gets `human_pickuptable` (832) and `pick2` (2582), and the keystone goes into the pack. **No message.** |
| RT-K2 | **Dispel:** the tick after the raider stops (5 of 5), all on one tick: <ul><li>the barrier's tile becomes `invisible_type8_nonblocking` at the barrier's rotation</li><li>the keystone leaves the pack</li><li>`human_pickuptable` with `magical_barrier` (1657)</li><li>the message "Your keystone glows as it is absorbed into the barrier, which disperses."</li></ul>Every recorded solo dispel came with every guard already dead. The points are in [points.md](points.md) RP-K1. |
| RT-K3 | **The keystone's kill** (820: four rangers, another raider's dispel). The barrier went invisible on K. On **K+1** every ranger got `human_death` and its death sound, with no hitsplat. They were removed on **K+3**. |

## Fixed (2026-10-19)

| Was | Now |
|---|---|
| `maxrange` 7 (the default): a raider 9 off a guard's post was dropped, so the guards never answered a shot from the far platform | 8, the least the recordings allow (RT-W2) |
| the mage's seq `human_caststrike` (711), its graphic at 90, projectile 43 -> 25 ending 57 + 18 + 5/tile, splash graphic 339 with a 0 hit; the ranger's projectile 42 -> 30 ending 40 + 10 + 5/tile; no sounds; hits on (arrive + 15) / 30 | the recorded seqs, graphics, sounds and flights, the hit on C + end / 30, and no hit for a splash (RT-A1..A4) |
| both guards blocked with `human_unarmedblock` (424) | 415 and 425 (RT-B1) |
| the death seq a tick after the blow and removal on D+3 | the seq with the blow (`npc_setdeathanimnow`), removed 2 ticks after it (`death_delay=1`) (RT-D1) |
| the crossing started on the click's own tick: ten one-tile steps, a creak on every one, two messages | the recorded shape: two facing ticks, a two-tile step, eight one-tile steps, seven creaks, no message (RT-X1) |
| a raider on the rope was out of every guard's reach: the engine's ranged reach asks its line from the player's tile, and a line cannot start from a tile that holds a loc (every rope tile does) | when the player stands inside a loc, the npc's side is asked (`npc_ranged_approached`, `torirs_server_combat.c`). The woken rangers shoot the crosser, and the hits wait for T0+9 (RT-X2, X3) |
| 1-4 shouts on the wake tick | each guard whose target is the crosser shouts 1 tick in 14 while he walks the rope (RT-X4) |
| the take on arrival with "You take the keystone crystal." | two ticks after arrival, with `pick2` and no message (RT-K1) |
| the dispel on arrival, silent | the tick after, with `magical_barrier` (RT-K2) |
| the guards still standing were deleted on the dispel | they die on K+1 (`npc_die`) (RT-K3) |
| the solver's safespot: standing 9 off a post, outside the old leash | the leash dance: inside the leash only on the tick before a cast and the cast's own tick, from 8-9, so each guard's flinch reads him outside and drops him. 0 damage on 12 of 12 layouts and 7 or less on 3 drained arrivals |

`test/raids/_tightrope_tells.lua` checks every attack, opening, block, hit sound, death, crossing, dump, take, dispel and the keystone's kill off the server's tick log. It plays the room solver with two options, both only for this test:

- `tank`: stands in the guards' fire under the protection, from 8.
- `cross_with = 1`: crosses with one ranger alive.

It also uses `::god 1`, so every hitsplat is still sent but no damage is taken.

Mutation checks, both in worktrees:

- With the engine's reach fix reverted, no ranger shoots the crosser and `tells.crossings` fails (0 landings dumped).
- With the mage's seq back at 711, the death seq back a tick late and a creak on every step, `tells.recorded` names all three.

## Open

| Item | State |
|---|---|
| The leash | 8 is the least the recordings allow; nothing past it, and not the (9, 9) corner, was recorded. The solver's dance depends on it. |
| The keystone's kill | It removes the guard on K+2: `npc_die` starts the corpse wait on its own tick, where a killing blow's starts a tick later. Recorded K+3. |
| The death seq's tick | 9 of 24 solo deaths animated a tick after the killing hit, for no reason found. Ours always animates with it. |
| Max hits, accuracy, the 33% | From the wiki and NR; every solo hit fits them (RT-A6). Party scaling is unrecorded beyond 820's 13. |
| Shouts | The rate is fitted to 5 shouts, all in 820. NR's third line, "Intruder!", was never heard. |
| The ranger's heights | 163 -> 146 is sent as 164 -> 148, the nearest values our height byte can carry. |
| The wake out of sight | Never recorded: every recorded crossing was in the guards' sight. |
| The attack range (2026-10-19) | No target ever stood 10 or more from an engaged guard, so `attackrange=10` is neither confirmed nor refuted. Every due attack came at the gaps recorded: a mage's target stood 9 off for 278 engaged ticks in 820 and was cast at on schedule (69 casts), a ranger's at most 8 off. No source (wiki infobox, dps-calc, NR) states the range. `maxrange=8` (dropped past 9 off its post) fits: 9 is the farthest any target stood. |
| The Agility requirement and xp | Not read here (Mod Ash's 80-100% and NR's 7.5 stand). |
