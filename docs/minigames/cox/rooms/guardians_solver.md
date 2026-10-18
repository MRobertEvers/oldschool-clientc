# Guardians -- solver plan (phase 4)

Target shape: `script/plugins/quest_driver/cox2_olm.lua` (a `QD.T2.register` room) and
`test/raids/cox_guardians.lua`, entered with `::coxtest 1 <n>`. Spec: `guardians_spec.md`.
No code this round.

## The strategy a strong solo human plays

From V1 (Synq), W3 (CoX/Strategies), W4 (CA Dancing with Statues), V2 (Tubby), V3:

1. Before the room: drink an overload (+) at the entrance; wield the pickaxe (swap from the weapon slot; one inventory op), set the combat style to **Smash** (crush -10 vs slash +180), Piety on. Protect from Melee is optional at full HP; on when low (it halves the cleave, never the rock).
2. Pick a statue (the left one first is arbitrary; the wiki and Synq do not care). Stand on a **home tile** two to three tiles out from the statue's free side, square with it (Synq "stand three tiles away"; wiki "run two tiles away ... then walk back one tile"; Tubby marks the tile four out).
3. **Flinch cycle** (pickaxe 5 ticks): click the statue; the swing goes out on arrival (tick P, beside it); immediately click the home tile (run). The hit lands on the statue at P+1 and pushes its swing to P+3; at the end of P+1 the bot is already two tiles off, so the statue cannot swing (it never moves) and no rock is dropped. Walk back in to arrive as the pickaxe's 5-tick timer is ready. With one statue down, use Synq's "one-click method" (attack from one tile out, step back one).
4. Never step into the gap between the statues or onto the tiles in front of the exit while either stands (knockback: 15-30 + a rock from the other statue).
5. After both: walk the exit (the blocking tiles are gone), swap back to the main weapon, pick up seeds only if the run wants them (the supplies unit owns the farming plan).

Engine caveat found by the probe (2026-10-18, `scratchpad/probe/g2|g3`): **this engine swings an npc BEFORE it drains the npc's queue in the same npc phase** (`torirs_server_world.c` advance_npcs comment: "here the swing runs first"). A statue whose clock is ready when the bot arrives therefore swings on P+1 although the bot's hit lands on P+1 -- the flinch arrives a tick late. The reference (and OSRS) drains the queue first. Until that is fixed in the engine (engine work E1 below), the flinch only protects the bot when the statue's clock is not already ready on arrival; the bot must also time its approach so it arrives no sooner than the statue's last swing + 4. The probe's own approach (attack verb + walk, ~8-tick cycle) took a stomp or a cleave on most cycles because of it.

## Perception (api_drive, both lanes)

| fact | verb | notes |
|---|---|---|
| statue tiles, ids 7569/7570, dead forms 7571/7572 | `api_drive.npcs(0)` (`npc_id`, `server_x/z`, `health_ratio`) | 2x2 SW tile; a dead form appearing = that statue down |
| the swing and its kind | npcs row `seq_id` + first-seen tick (1203 cleave, 4278 stomp) | key on the tick first seen, not `seq_tick` |
| where the rock lands | `api_drive.projectiles(0)`: `spotanim_id == 645`, `dst_x/dst_z` (target 0) | sent on the swing tick, lands next tick |
| the rock's landing | `api_drive.spotanims(0)`: id 71 at the tile | confirmation only |
| knockback | my tile jumps to a push tile + a hitsplat | tick log `hit_player` with no npc (b = -1) |
| my HP, prayer, run energy | existing player verbs (`players`, `skill`, `run_energy`) | |
| pickaxe in hand, style | `inv_count`, `varp` 43 (com_mode) via `t.ui.style("Smash")` | |
| the iron pickaxe loc | `api_drive.locs` / `loc_row` for 41754 | fallback only |

Missing / engine work:

- **E1 (engine, blocking a faithful flinch):** npc phase order -- drain the npc queue (hits) before the clock-owned combat swing, as `Npc.processNpc` does. Today every flinch in the game (not just here) lands a tick late.
- E2 (both lanes): a `server_los`/line-of-walk verb exists only on the live plugin; scriptrun has none. Not needed here (statues never move), needed for mystics.
- No new verb is needed for this room otherwise.

## Threat field (expected HP units, per tick)

| term | window | where | E[dmg] / max | prayer |
|---|---|---|---|---|
| cleave | statue swing tick T when I was orthogonally beside it at end T-1 and its clock is ready (last swing + 4, or hit-landing + 2) | 4-neighbourhood of the 2x2 | ~acc x 10 / 20 | PfM halves |
| rock | lands T+1 on the tile I stood on at end T-1, 3x3 | Chebyshev <= 1 of that tile at end of T | 7.5 / 15 | none |
| knockback | every 3rd statue tick, standing on/next to a blocked tile | blocked tiles + orthogonal neighbours | 22.5 + 7.5 / 30 + 15 | none -> `forbid` |
| partner | a hit on one statue makes the other target me (2-tick delay) | beside the partner | as cleave | |

Clock model per statue: `next_swing = max(last_swing + 4, last_hit_landed + 2)` with the E1 caveat (until E1 lands: a ready clock on my arrival tick swings that tick).

## Mode machine

`PREP` (overload, wield pickaxe, Smash, Piety) -> `APPROACH(s)` (walk to home tile of statue s) -> `CYCLE(s)`: `STRIKE` (attack press when pickaxe timer will be ready on arrival and the statue's predicted clock is not ready on arrival) -> `RETREAT` (run to the home tile on the tick after the press) -> `WAIT` -> `STRIKE` ... -> s dead -> `CYCLE(other)` with the one-click home (one tile out) -> `EXIT` (walk past the blocked tiles, swap back to the main weapon). `RESCUE` from any mode: HP below a threshold -> step away from both statues (they cannot follow), drink Xeric's aid (+), restore with a revitalisation (+) if stats dropped.

## Kit (start from `test/raids/cox_olm_solo.lua`)

The Olm kit (We Do Raids minimum: Barrows gloves, salve(ei), imbued god cape, fire cape, dragon defender, dragon sword, trident, abyssal tentacle, toxic blowpipe, occult, berserker ring (i), fury, BGS; helm of neitiznot, fighter torso, black d'hide chaps, dragon boots; CoX (+) potions) **plus a dragon pickaxe in the inventory**. Where it comes from in a real raid: the player brings it in (Synq "you'll need the highest level pickaxe you can use"; wiki "If guardians are in the raid, bring the best pickaxe possible"; Beleti's inventory shows it), wielding needs 60 Attack and its damage term needs 61 Mining; the room's iron pickaxe (41754, Take) is the fallback when the raid has none (`maxstats` gives 99 Mining). A crystal pickaxe is equal (capped). The kit is fixed once the owner confirms it.

## Probes (first run prints one row each)

- P1 per swing: tick, kind (1203/4278), my tile at end T-1 and T, rock tile, damage -- verifies the 3x3 and the read tick.
- P2 cleave damage with/without PfM (the 50% is OURS).
- P3 the E1 order: statue clock ready at my arrival -> did it swing on the hit's landing tick?
- P4 knockback rows (should be zero in a passing run).
- P5 kill time per statue (regen 1/8 ticks fights a slow pickaxe).

## PASS

Both statues dead and the player alive on 48 seeds; **0 rock (stomp) damage** (the CA "Dancing with Statues" condition: the strategy's whole point) and **0 knockback**; cleave damage reported, not gated, until E1 lands (then gate it at 0 too); no `abort` / `no primary player bound` in stdout.

## Round 2 (2026-10-18): built and swept

`script/plugins/quest_driver/cox2_guardians.lua` (QD.T2 room `cox_guardians`) and
`test/raids/cox_guardians.lua`. **48/48** (`--name gd0..gd47`, n = the run name's
number): both statues down, the exit Passage taken, 0 statue swings, 0 rocks, 0
knockbacks; the only damage taken is the overload's 50.

As built: the pickaxe and Smash (`combat_interface:style_slot_2`, com_mode 2; the
names are not on the buttons on scriptrun) in `enter`, after a walk to the room's
middle (the plan's horizon cannot find the route in); then the plan holds a
target on the statue with `t_ok0` = the pickaxe's next swing, FORBIDS the
statue's side at every other tick end, forbids within one tile of the swing tile
at the next tick end (the rock), and forbids the partner's side and the exit's
knockback tiles. The monitor rule `guard_swing` notes a statue swing before its
clock or at a raider not beside it: 0 in 48 runs.

E1 (the engine swung an npc before draining its queue, so a flinch landed a
tick late) was fixed by the root session during this round; the stomp's
landing delay went back to 1 with it.
