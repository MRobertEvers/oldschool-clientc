# Bloat HM on v2 (tob2_bloat.lua)

Owner rule: **0 damage**. Any hit on any seat fails the room. Kit: committed void melee
(tentacle, speed 4). Seats 1-5 stand by the EAST barrier (QD.BLOAT_HM.SEAT_X/Z).

## Threat field (predict)

| hazard | term | window | damage | notes |
|---|---|---|---|---|
| flies, his likely way (dir, speed) | `B.watcher` per step k, his footprint after m-1 and m moves by the npc phase of now+k+1 | t0 = t1 = now+k | 10-20, `forbid` | v1 `_bloat_watchers` CALLED with a shim `add.watcher` -> `B.watcher`; same mapping as collision_plan |
| flies, a reversal (turn_cd <= H+1) or the other speed (permille 570..630) | `B.watcher` | as above | `dmg_exp = WATCH_COST 50`, `dmg_max 20` | an owner price, not hp: v1 lesson "all lethal covered the whole ring" (b22/b27); a branch that happens is seen next tick |
| hands | `B.hit` 1x1 per shadow, `sample = "pass"`, `stun 5` | seen+1 .. seen+2 (impact seen+3 reads end seen+2) | 30-50, `forbid` | v1 `_bloat_shadows`; HM the whole fight, the down included |
| stomp | `B.watcher` at his down tile | t0 = t1 = down+28 | 40-80, `forbid` | seen at any range (v1, Blert 17/24 in sight 4-6 off) |
| spread | `B.zone` soft, gap 0..3 round each mate | walking ticks | cost 1 | v1 SPREAD_COST |

No prayer: flies are only taken when seen (hiding is the answer), PfMissiles keeps 75%.

## Mode machine (strategy), every seat the same (no roles)

| mode | when | terms |
|---|---|---|
| `cross` | still outside the barrier (main-tree v1 lesson r089) | barrier op every 2 ticks, nothing held, plan held on my tile |
| `wait` | no Bloat row | none |
| `attack` | down, until the stomp lead (v1 `_bloat_state`, CALLED: run to the opposite hug tile + 2) | `B.target` Bloat, range 1, speed 4, value; Piety boost; melee set |
| `hold` | hiding, my ring tile holds the horizon against EVERY branch (v1 `_bloat_holds` = collision_plan with the exact watchers), or nothing better and it holds his likely way | `B.pull` to my tile, GOAL_PULL 3 |
| `hedge` / `go` | a ring tile that lasts longer both ways (HEDGE 14, HEDGE_GAIN 2) / the longest-lasting ring tile | `B.pull` to it, GOAL_PULL 3 |
| `hug` | no ring tile at all | v1 hug pulls to the ring tile opposite his footprint now and in 6 |

The hide picker (pick_hide / safe_ticks / ring_dist) is v1 STRATEGY ported into the file; its
predicate is v1's `_bloat_holds`. v1's lesson kept: never "safe the way he is going" for a new
tile; a pick is kept until his likely way would see it.

## Value (HP_PER_BOSS_HP)

The formula gives ~0: unavoidable team damage at Bloat is 0 by the owner rule. Bloat's
cost is exposure: every down not used adds a ~70-tick walk phase with its reversal risk.
So a swing in a down is a strategy constant, `SWING_VALUE = 1.0` hp (> the click + route
costs, ~0.2, of a chase from the ring), PROBE. Swings happen only in `attack` mode.

## PROBE rows

- P1 the stomp lead (v1's run + 2) leaves enough margin with HM hands on the route.
- P2 flies seen at all with aplan watchers (watcher count in the decision log `why`).
- P3 swings per seat per down (v1 HM q1: 8 downs, 550 fight ticks).
- P4 hold/stay vs weave: run energy stays above 300 (E10).

PASS: SCRIPTRUN PASS, every seat `taken 0`, no raider dead.

## Results (2026-10-12, scriptrun, seeds bl1..bl8, watcher binary)

Probe bl1: PASS, no fly/hand/stomp hit, but the branch watchers' dmg_max (20 each, summed over
the horizon) ran hp_worst under 0, so the rescue drank brews while the seat hid unseen (16-36
rescues a seat, hp 120, the overheal's decay logged as 5 "-1" hits). Fix: a branch watcher's
dmg_max is 0 (a price, not a worst case).

| | v1 (committed) | v2 |
|---|---|---|
| pass | 8/8 | 8/8 |
| damage, every seat | 0 | 0 |
| mean ticks | 601 | 566 |
| downs (total) | 51 | 47 |
| drinks a seat | 1 | 2-3 (Piety never turned off: the executive has no prayer-off) |
| monitor | | move 0, swing 0, threat 0 mismatches on every seed |

## Batch 2 (v2_bloat_48: 48/48 pass, 6 seeds with damage)

| hit | cause | known to the plan? |
|---|---|---|
| q12 p3 t141, q32 p3 t141 (hand, then flies t149-150 while stunned) | a member's op crossed from (41,32) onto (39,31) two ticks after the op; v1's crossing check asks only (39, my z) one tick on; the start volley (t138) had (39,31) | yes, too late: le40 lm50 on its first decision (t140), the read tick was that tick |
| q20 p1/p5 t141, q25/q47 p1 t149 (hand, then flies) | the leader lands on (39,30) on the start tick; v1's entry came back from the start dialogue a tick late and waited one more, so its first move went out after the read; q20 p5 crossed from (42,32) onto (39,30) | yes, too late (le40 at the first decision) |
| q27 all seats t535-536 (flies) | a reversal at the NW corner at speed 2, seen at t529, confirmed by v1 at t530; at t530 every path was seen at k1 | the reversal branch only, priced at 50 (loss 400-500 for 15 ticks: no tile safe both ways) |
| q20 all seats t538-540 (flies) | after a reversal at t531; the plan said safe at t532-533 (l0) and seen at k1 at t534 | NOT explained: the prediction changed between t533 and t534 with no new turn; still open |

Fixes: the entry ported (MB.enter): the leader returns on the answer's own tick; a member returns at
its stand and the room's loop crosses it (mode cross, intents.loc_op) only when ALL crossing tiles
x39 z30..33 are safe on arrival delays 1..3 (v1 _bloat_safe_crossing). A reversal seen once is
forbidden like his likely way (flies-turning). Piety and Protect from Missiles are turned off while
hiding (intents.pray_off). Every term has dmg_max 0: a summed max ran hp_worst under 0 and the shared
rescue drank brews at full hp.

| seed | v2 before (damage by seat) | v2 after | v1 |
|---|---|---|---|
| q12 | 0,0,71,0,0 | 0 | FAIL (116, 2 dead) |
| q20 | 81,30,28,27,78 | 0 | FAIL (104,124) |
| q25 | 58,0,0,0,0 | 0 | 58 |
| q27 | 33,24,32,30,39 | 0 | 0 |
| q32 | 0,0,61,0,0 | 0 | FAIL (136, 2 dead) |
| q47 | 56,0,0,0,0 | 0 | 56 |

Restore doses a seat on these six: before 1-2, after 0; drinks a seat 1 (the combat before the
start), as v1.
