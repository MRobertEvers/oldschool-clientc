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
