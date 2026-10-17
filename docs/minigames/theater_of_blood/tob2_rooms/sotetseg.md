# Sotetseg, Hard Mode, five raiders: the v2 room plan

Code: `script/plugins/quest_driver/tob2_sotetseg.lua` (`QD.raid.sotetseg_v2_solve`).
Sources: solver_specs/sotetseg.md (spec), content `tob_sotetseg.rs2` (S.rs2), `tob.constant` (K),
v1 `tob_sotetseg.lua` / `tob_sotetseg_hm.lua` (worktree = committed; main tree read for rules).

## The budget, and what it says about the death balls

Oracle: 174 hp a seat-run, 158 of it death balls. Content (S.rs2 ~606-650): a ball rolls
`total = 1 + random(188)` at five, THEN divides it among everyone within 1 of its target's tile.
So the team's damage from a ball is `total` whatever the stack size: 94.5 expected a ball,
189 a pair, ~4 pairs a fight = ~756 = 151 a seat. The stack size moves damage between seats
(per seat: `ball / n`), it cannot reduce the team's total. In the planning seat's currency the
stack costs `E/n` on the stack and 0 off it (if I am not a target), so a per-seat optimiser would
LEAVE the stack and kill a target: the stack is a team rule (E14), a lethal zone, not a priced
choice. The per-seat charge on the stack is `dmg_exp = sum(94.5/n)`, `dmg_max = sum(188/n)`
(n = raiders alive, the expected stack) - it feeds hp_worst, the eat floor and the rescue.

The HP levers are therefore the NUMBER of pairs that land:
- a pair is nulled if a maze is active at its impact (S.rs2 `~tob_sote_maze_nulls_hit`) or he is
  dead (`~tob_sote_dead`). Hits land 15 after the throw (fixed flight).
- so THE HOLD: within HOLD_PM permille above a threshold (666, 333, the kill at 0) stop dealing
  damage, wait off his melee reach (gap 2: every attack is then magic and counts toward the pair),
  and when the pair is thrown BURST through the threshold before land-2. Saves 189 team hp a hold
  at the cost of the wait: a fight tick costs ~3.2 team hp (189 a pair per ~60 fight ticks), so
  hold iff `expected wait x 3.2 <= P_NULL x 189` (wait from the cast counter: (10 - casts) x 5 + 5).

## Threat field (B terms, per seat)

| hazard | B term | window | damage (exp / max) | prayer |
|---|---|---|---|---|
| his body 5x5 | hit forbid | always | - | - |
| melee (target within 1 at T-1, 1 in 2) | hit, rect = footprint grown by 1 | t0=t1=T-1 per slot T, land 1 | 1/n x 1/2 x 23 / 45 | Magic is up: unprayed |
| ball / ricochet at me | hit, rect around me (follows me), land 0 | its landing L | 0 when prayable; 25.5 / 50 when L <= block end, or a second style the same tick | landing read (R3) |
| ricochet onto my own landing (v1 "ricochet-onto-mine") | zone tier damage, annulus dmin..dmax round the victim | tm (victim's queue) | 0.25 x (25.5 + BLOCK_COST 25) = 12.6 | - |
| the same band for a hit of mine whose victim was unknown (its land2; v1 banded land only) | zone tier damage | tm | 12.6 | - (r5_sote q16 seat 1, t352-354: grey + red one tick, block, 46+24+45) |
| death ball pair | zone require (target: hi 0 on D; others: hi 1 of D), tier lethal | [L-2, L] | - (team rule) | unprayable |
| death ball share on the stack | hit, 3x3 round D | [L-1, L], land to L | sum 94.5/n / sum 188/n | - |
| maze (runner) | v1 `_sote_hm_runner` through an adapter: off-path forbids (sample PASS = the no-skipping rule), tornado lethal zones, row-2 hold, the carrot goal | | | |
| maze (waiter) | v1 `_sote_hm_waiter`: the arena grid forbidden, wait (15,20) | | | |
| maze stun | S.x2.stunned_until = proc seen + 4 | | | |

## Mode machine (re-derived each tick, E14; hold state from the shared view)

```
wait        no boss row / no base
fight       target him (tentacle; the hammer whenever spec >= 500, owner), station pull
soon        fight, casts >= DEATH_SOON: the pair's eat floor
hold        within HOLD_PM of the next threshold, no pair in flight, holding pays: no target,
            pull to the hold tile (station one step out, gap 2)
burst       within HOLD_PM, a pair in flight, now <= land - BURST_STOP: target him, stack window ahead
stack       a pair in flight, not bursting: target him until the window, stack on D=(15,44) in [L-2,L]
maze-wait   the one up top (v1 waiter)
maze-run    in the realm (v1 runner)
maze-off    north of the realm grid: stay (main-tree lesson: the carrot pulled runners back on)
done        his death (v1 _sote_phase)
```

Roles: every seat melee; stations west/east/north-centre/north ends (v1 HM). D = (15,44), two north
of his face, OUT of melee reach. Targets of a pair stand on D, the others within 1 of it.

Why not (15,43) (the v1 main tree's STACK, in reach): the pair lands on T+15, which is also a slot
(T+10, T+15 after a pair), read at the end of L-1 - the pair's own read. On a tile in reach the
share's max (2 x 188/n) plus the melee's 45 put hp_worst <= 0, a risk equal to leaving the lethal
stack zone; the tie went to stepping off D on L-1, the targets ended two apart, each ball split
three ways and two seats died (r5_sote q16 t388-391; v2_sote_48 q16 t152 made the same step).
Shared fix under way: rule risk (forbids, lethal zones) ranked above hp risk.

## Prayer: the ricochet's landing window

v1 gives a ricochet whose victim it cannot name a window [land, land+1] (a lower-pid recipient's
queue runs first, +1). The room names the victim from the ricochet's source tile (the raider on it
at the end of this tick or the last) and closes the window; and a landing KNOWN for next tick
outranks one that only may land then. (fix5 q1 seat 2, t242: red "242/243", grey 243 -> Magic held,
grey unprayed, a 15-tick block cascade: 23 + 50 + 35.) Scriptrun sees a projectile on its server
launch tick (dbg2/q1: grey launched 241, first seen 241, land 243 correct), so no late-sight shift.

## Value

HP_PER_BOSS_HP = 870 team hp (174 x 5) / 4000 boss hp (K `^tob_sote_hp_5`) = 0.22.
Tentacle swing exp 11 (PROBE), speed 4; hammer exp 22, speed 6.

## Eat floors (relay brew budget, 2026-10-12)

fight 50. Only while a pair is IN THE AIR (stack, burst): `2 x 188 / n + 5` (80 at five), capped at
115. No floor for "soon" (a pair the hold nulls needs no brew; 15 ticks of flight is five brews), and
no RICOCHET_HIT (v1's +40 floored seats at 115 for prayed hits: overheal). Two styles that may land on
one tick (their [land, land2] windows overlap) are a priced hit of 2 x 50, so the rescue eats before
them; every ricochet's victim is named from its source tile (v1 named the same victim for both fans
when two balls landed on one tick: brew1/q4 seat 2 dead at t196, 48 + 26 at 74 hp).
Measured (sote_dose.py, so1..so8 / q1..q8, team doses a run): brew 36.6 / 24.6 -> 17.1 / 15.6,
overheal 114 / 74 hp -> 0, restores ~17 / 16 unchanged, 16/16 pass, no deaths.
Piety off in the maze only (intents.pray_off; want.boost nil there): restores 16.6 / 16.1 -> 13.8 /
13.1 a run, brews 17.1 / 17.4, 16/16 (pray2). Off in the hold too (pray1) saved one more restore but cost
4.6 brews and 98 team hp a run: the burst starts with Piety a tick or two late and fewer pairs are nulled.

## The block cascade's floor

Two styles that may land on one tick (or protection already blocked) floor the seat at a brew's
overheal cap (115) until 5 ticks past the overlap / the block's end. The rescue cannot act there: the
two-style max (2 x 50) is beyond any brew, so no heal lowers the plan's risk. (relay_r12_128 f32 seat 2:
foreseen t1956, no drink until t1960, 25 + 7 + 48 + 26 in renewed blocks, dead t1972; passes now.)

## Specials and boosts (owner 2026-10-12)

Opener: ONE Dragon Warhammer special a seat (30% of his current Defence on a damaging hit,
pvm_dragon_warhammer.rs2 ~46; Defence 200, floor 100, ^tob_sote_defence_floor: two landed hammers reach
the floor). Then burning claws (bone_claws, 350, two-handed) on every swing at him with the energy, P3's
machine (wield -> armed -> back to the melee set, defender included). Not in the maze or the hold.
Super combats by P3's rule (restore at -20, combat under +5) on the budget's spendable stock.
spec2 vs pray2 (so1-8 / q1-8): fight 368 / 360 -> 346 / 360 ticks, pairs landed 21 / 21 -> 19 / 18,
brews 17.1 / 17.4 -> 14.6 / 16.5, restores 13.8 / 13.1 -> 10.9 / 12.8, team damage 513 / 558 -> 491 / 502,
boosted (+10 both) 78 / 73% -> 84 / 84%, 1 hammer and ~2.8 claws a seat, 1.5 combats a seat-run; 16/16.

## Probes

1. Cast counter vs the pair (S.casts at each pair; 10 expected).
2. Does the burst cross before land-2 (pair "nulled" count)? DPS a tick (taken from the hp bar).
3. Is the hold's wait as estimated (hold ticks per hold)?
4. Melee taken per seat; block events per seat (unprayed landings).
5. Rescue fires before a pair lands (rescue lines in the trace).
6. Maze: rags (env hits > 3), time proc -> end.

PASS: SCRIPTRUN PASS, no raider dead, 0 wrong-style (0 unprayed ball/ricochet outside a block
cascade). Compare v1 (hm_sotetseg_v1.lua) on the same seeds: pass, ticks, taken per seat.

## The maze's end: the portal (2026-10-13)

Owner: "the sotetseg maze completion requires the completer to go through the gate to get back to
the overworld." Wiki_Sotetseg.wikitext:97: "The player must follow the red path ... and enter the
portal at the end to return to the arena"; :102 (Hard) "all but one player into the Shadow Realm ...
the maze divided among themselves". Content before this: the maze ended at the 4-tick check that
found nobody on EITHER grid, and the runner was lifted out - a step off the top row (or off the south
edge at P+5) ended it; the portal op was a shortcut to the same lift.

THE PORTAL. Loc 33037 `tob_sotetseg_darkrealm_exit` (all.loc: "Portal", op1 Enter, 8 x 2,
anim tob_sotetseg_shadow_portal; tob-qol SotetsegConstants MAZE_UNDERWORLD_PORTAL = 33037), placed by
the cache map maps/m52_67.jl2 `3 29 42: 33037 10`: realm local x 29..36, z 42..43, four rows north
of the grid's last row (z 37).

BLERT (tools: scratchpad sote_work/blert_portal.py, blert_portal_hm.py, blert_lastmirror.py,
blert_hm_realm.py, blert_mazelen.py; Normal = build/blert/sotetseg, 150 trio rooms, mode 11;
Hard = 60 five-man rooms, mode 12, fetched 2026-10-13 with blert_reference.harvest). A recorder who
IS a runner keeps its own realm tiles (x 3354..3367 = local 26..39), so the walk is visible:

| | Normal | Hard |
|---|---|---|
| runner-recorded mazes | 118 | 88 |
| path from the last row | z37 -> z39 -> z41 (a run; a column outside 29..36 pulled in to 29/36 on the way), z41 again, z42 (the portal's own south row), then the arena | the same, every runner |
| last row -> arena | 5: 71, 6: 45, 7: 2 (6 = left from row 13 through row 14) | 5: 68, 6: 19, 7: 1 |
| return tile | (3275,4327) = his SW + (-2,+1), 154 of 154 | same |
| re-activation - runner's return | 0: 27, 1: 30, 2: 28, 3: 19, 4: 8, 5+: 5, -1: 1 | the FIRST runner through: 0: 1, 1: 6, 2: 9, 3: 9, 4: 4, 5-7: 3 (32 mazes, all runners seen) |
| re-activation - last realm-grid tick | 5..13, mode 6; never < 5 (118) - "nobody on either grid" would be 1..4 | 3: 9, 4: 15, 5: 31, 6: 14, 7: 3 (78) |
| runners still inside at re-activation | - | 3 of 32: they walk on and enter after (18eb075b maze 1: re-activation t171, two on z41, through t173/t174) |
| re-activation phase, maze 2 - maze 1 mod 4 = 0 | 140 of 150 (the room's 4-tick check) | 27 of 60 (weaker; disclosed) |
| proc -> re-activation | 14 / 25 / 29 / 33 / 51 (min, quartiles, max), mean 29.7, n 300 | 26 / 34 / 39 / 43 / 55, mean 38.4, n 120 |

CONTENT (tob_sotetseg.rs2, tob.constant):
- `~tob_sote_maze_tick`: the 4-tick check ends the maze only when every uncaged arena raider has
  CROSSED the grid - row >= 15, north of its last row (`~tob_sote_arena_unfinished`, was
  `~tob_sote_arena_occupied`, which asked only "nobody standing on it" and so let a raider who
  waited south of row 0 hold no maze at all; yt_EntowMeBPNg 0:33 "they must all run one tile past
  the grid to complete the maze") AND `~tob_sote_realm_open` is false:
  the realm gone, or nobody inside, or a runner through its portal (`^tob_var_maze_exited`, realm
  register 118, reset at `~tob_sote_start_maze`). The realm's grid is no longer asked.
- `[oploc1,tob_sotetseg_darkrealm_exit]`: this engine runs the op the tick AFTER arrival (measured),
  so `p_delay(^tob_sote_portal_hold_ticks = 0)`, a one-tile `p_exactmove` into the portal's nearest
  tile (`~tob_sote_portal_tile`), `~tob_sote_enter_portal` (counted on the tick IN the portal, so the
  check lands 0..3 after the return), `p_delay(0)`, `~tob_sote_leave_realm`. Arrival A, A+1 same
  tile, A+2 in it, A+3 out - Blert's shape.
- `~tob_sote_player_tick`: a runner still in the realm after the end is NOT lifted out (Hard's late
  runners); the realm's chip (`~tob_sote_realm_chip`, factored out of `~tob_sote_runner_tick`) keeps
  draining them until they enter.
- `::tobmazeout` (tob_selftest.rs2) puts the runner south of the portal, not past the last row.

SOLVERS. v1 Normal (tob_sotetseg.lua) and v1/v2 Hard (tob_sotetseg_hm.lua, tob2_sotetseg.lua):
- the runner's pull is the top row's path tile (was one north of the grid); ON the last row it ops
  the portal (`QD.raid._sote_portal`: one world_op; no walk order while it runs; re-sent after 2
  unmoved ticks; no eat/drink on the op's tick - a held op clears the pending interaction: a_nm n5
  before the fix stood beside the portal 530 ticks). v2: state `portal`, `intents.loc_op` on the
  portal's element with (x, z) on its north row so the exec's stall re-send still fires beside it.
- a seat back through the portal while the maze still runs (`S.ran_maze == S.mazes`) holds its
  fight station off the grid (`QD.raid._sote_back`), not the waiters' tile south of the grid.
- the arena seats already key the realm phase's end on HIS form (8387 -> 8388), which is now the
  real return; the Normal team's "leaving" on the glow's last two rows is unchanged.

THE KILL HOLD (owner, live: "at like 1.7% hp left, they all just stood there for a long while after
a red ball and stopped attacking until the next red ball"). Not a lane divergence: scriptrun does it
too - `SV.fight_mode` held within HOLD_PM of EVERY threshold, the kill (0) included, so from a few
casts after a pair to the next pair the team stood still (relay_r17_128 f1 t1995-2028; b_hm: 42 of 48
runs, 1150 seat-1 hold ticks, no hit on him for 41 ticks). At the kill the hold buys nothing: killing
him now nulls the next pair as surely as killing him under it. Now no hold at th 0 (the burst through
a pair in the air stays): a_hm 0 hold ticks, longest no-hit gap after the last maze 5.

RESULTS (scriptrun, 48 seeds each, before = v3 48b1733a1 content+solvers, after = this):

| | pass | runs with a death | wrong-style hits (runs) | team dmg | death-ball | chip | maze (proc->react) | realm (proc->out) | fight ticks |
|---|---|---|---|---|---|---|---|---|---|
| Normal before (n1-n48) | 48 | 0 | 4 (4) | 326 | 288 | 11 | 26.1 | 26.1 | 416 |
| Normal after | 48 | 0 | 0 (0) | 323 | 295 | 8 | 25.4 | 22.7 | 411 |
| Hard v2 before (q1-q48) | 48 | 0 | 10 (8) | 538 | 467 | 46 | 26.1 | 26.1 | 348 |
| Hard v2 after | 48 | 0 | 20 (11) | 517 | 442 | 45 | 27.9 | 27.3 | 330 |

Our walk now matches Blert's: last row -> out 5 on every runner, re-activation 0..3 after the
(first) return. Our maze length is set by the ARENA team, not the runner (Normal re-activation is
7..10 after the runner's last row, before and after): the team leaves the grid late against Blert's
teams. Hard's wrong-style rise is the kill phase (2 -> 10 hits, 2 -> 5 runs): two styles landing on
one seat in one tick while fighting (q27 t415 "two-styles k3", predicted, no escape) - the hold used
to keep the team still and spread there. That is the shared planner's two-style escape, not the maze.

Relays (scriptrun, before -> after, all PASS, no raider dead): Hard v2 f1 3409 -> 3321 ticks, f15
3194 -> 3169, f43 3164 -> 3136; Normal r1 3754 -> 3865, r2 3991 -> 3849, r3 3662 -> 3617.

Parity (Normal n1, live `run.py tob_sotetseg --party 3 --name n1` against scriptrun, kindcmp
--align abs): both end on t517; every input, mark, player tile, hit and projectile identical. The
only rows that differ: maze 2's REALM tornado (npc 8389, plane 3, spawned t356 when the runner - p2,
not the host - passed row 3) walks the path on scriptrun (15 npc_tile rows, freed t372 on its last
tile) and never moves live (no npc_tile, freed t372 on its spawn tile). Maze 1's realm tornado, whose
runner was p1 (the embedding host), is identical on both lanes. So the live lane does not run a
plane-3 npc's timer when only a REMOTE seat stands in the realm instance: an engine/embed divergence
that predates this change (the tornado code is untouched); it did not touch the runner here (behind
him), but it can. OPEN.

## Zero wrong-style (2026-10-13)

The measure: every damaging hit of his outside the maze and the death-ball windows that is not his
melee (seq 8138 at T, its splat at T+1 on one raider; Protect from Magic is the standing overhead, so
a melee is unprayed by design). The first count (window heuristic, sote_damage_by_cause.ball_at)
also counted melees: before 7 hits / 5 runs, after-portal 19 / 11 (b_hm, a_hm, 48 seeds).

Four causes, found one seed at a time (sote-overhead / sote-victim tick-log marks behind
QD.SOTE_V2.DEBUG_MARKS, off by default):

1. ENGINE: a zone that LOADS for a client on a tick drops that tick's projectiles. The FULL and its
   state carry locs and objs, so that tick's zone events were skipped; nothing carries a projectile.
   Runners lifted out of the realm (q36: three of them, t345) never saw the grey thrown at them on
   t346; the raider walking back from the far end lost the ball at itself. TORIRSSERVER_ZONE_TRACE
   showed the server sending the t346 projanim to the two raiders already there and nobody else.
   Fix: `zone_send_transients` (src/torirsserver/torirs_server_zone.c) sends the load tick's
   PROJANIM and MAPANIM after the state. Six other rooms' harnesses, one seed each, HEAD binary vs
   this: tick logs byte-identical (maiden, bloat, nylocas, xarpus, verzik P1/P2, verzik P3).
2. PERCEPTION, a key collision: a projectile was keyed spotanim:target:source tile, so a second grey
   off the same victim tile at me, thrown while the first was still in the air, was never seen
   (q39 t260: a 6-tick grey of t255 and a 3-tick one of t260). Now keyed with its launch tick too
   (`QD.raid._sote_proj_key`, tob_sotetseg.lua; both lanes count elapsed flight in whole ticks).
3. PERCEPTION, the ricochet's order: a ricochet lands +1 on a recipient with a lower pid than its
   victim. With the victim out of sight (21 tiles north) v1 named the wrong one (q27 t403) and the
   grey was expected a tick late. `SV.resolve_victims` now falls back to the balls landing this tick
   whose target is not seen elsewhere, then to every room raider not in sight (`M.party_pids`).
4. TWO STYLES ON ONE TICK that position could have avoided (q12/q31/q45 fight start, q3/q13 the
   walk back, q27/q35 two balls landing on two victims on one tick): `SV.landing_guard` replaces
   v1's priced ricochet band with forbids. Every landing on me whose flight my tile still decides -
   a ricochet off a ball landing on a teammate (Lv + (41+8d)//30, +1 below the victim's pid, my
   tile at the end of Lv or Lv-1), his next ball at me (T + (56+8d)//30, my tile at the end of T-1),
   and the ricochet off his next ball to any teammate - is forbidden on a tile that puts it on a
   tick where something already in the air at me lands (the other style), or where another such
   landing fixed on the same tick or the next lands. `SV.victim_hold`: a raider more than
   HOLD_RING (4) from his SRC holds its tile while his ball is in the air at it, so the others read
   its ricochet tile right (q3: the victim walked 3 tiles in the two ticks before its impact).

Results (scriptrun, the final code):

| | seeds | pass | runs with a death | wrong-style | team dmg | death ball | pairs landed/run |
|---|---|---|---|---|---|---|---|
| Hard v2 before (v3 HEAD) | q1-48 | 48 | 0 | 7 (5 runs) | 538 | 467 | 2.48 +- 0.09 |
| Hard v2 portal + kill-hold, no guard | q1-48 | 48 | 0 | 12 (7 runs) | 510 | 438 | 2.35 +- 0.09 |
| Hard v2 final | q1-96 | 96 | 0 | 0 | 528 | 466 | 2.40 +- 0.07 |
| Normal final | n1-48 | 48 | 0 | 0 | 323 | 295 | - |

(Hard final q1-48 alone: team 555, death 489; q49-96: ~501. The death-ball term is the rolls of
the pairs that land - sd 195 a run, se 28 on 48 - and pairs landed did not move.)
Relays (scriptrun, final): HM v2 f1 3465 / f15 3098 / f43 3172 ticks, Normal r1 3865 / r2 3849 /
r3 3617: all PASS, no raider dead, 0 wrong-style in the Sotetseg room.
Parity: HM v2 relay f1 live (`run.py tob_relay_hard_v2 --party 5 --name f1`) against scriptrun,
kindcmp --align abs: 0 differing ticks over all 3465.
Server selftest (`torirsserver --selftest`): the Sotetseg maze stanza now checks that stepping off
the grid does NOT end the maze and that the portal does (`::tobmazeportal`); the four other FAILs
(the starting-kit stanza) fail identically on HEAD.
