# Verzik P1, Hard Mode, five raiders, on v2 (`tob2_verzik_p1.lua`)

Entry: `QD.raid.verzik_p1_v2_solve(opts)`, opts `{max_ticks, prep, weapon}` (the relay calls
`verzik_p1_prepare()` first). Done = her shield broke and every pillar's fall has read
(break_read + FALL_WINDOW + 1). Then P2.

Geometry (local to `prep.base`): her throne SW (30,35), size 5 (x30-34, z35-39); pillars size 3,
SW (25|37, 30|24|18), 185 hp each. Cover box (C `~tob_verzik_hiding_pillar`, v1 `_vzp1_box`): west
pillar x-3..x, z-3..z; east pillar x+2..x+4, z-2..z. A box is 5-6 tiles from her south face, so
a strike is 3 running ticks each way.

## Threat field (all FORBID: owner "no avoidable damage")

| hazard | B term | window (tick ends) | dmg |
|---|---|---|---|
| bolt (her wind-up seen k, reads k+2 = R, every 14) | `B.zone` require, tier lethal, the team box's core rect | t = R; before the first wind-up: every t >= guessed R (v1 F.hold) | 137 (68 prayed) |
| her body (5x5) | `B.hit` forbid | always | stomp |
| pillar collapse (the team pillar that could fall at a read: 185 - 60 x hits <= 60) | `B.hit` forbid, centre +-3 | [R+3, R+3+FALL_WINDOW 3] | 32-65 + shove + 5 stun |
| HM rubble (spotanim tob_bloat_falling_flesh1, new on cycles_left restart) | `B.hit` forbid 1x1 | [land-2, land-1], land = seen+3 | 35 + stun |
| the break (death seq on D, or her P2 transition row) | `B.hit` forbid on EVERY pillar centre +-3 | [D+2, D+5] (main tree: falls land as late as D+4) | 32-65 |

## Mode machine (main-tree `_vzp1_machine`, owner 2026-10-12: rigid; only the state wields)

State from what I hold and whose turn it is (`(dawn_drops % 5) + 1`, v1 `_vzp1_dawn_turn`):

| state | when | wields (intents.gear) | target | other |
|---|---|---|---|---|
| WAIT | no P1 body | - | - | - |
| SPEC | holding the Dawnbringer, energy >= 350 | {dawn} | her, range 10 (LoS), uncapped | `spec` once it is worn |
| PASS | holding it, energy < 350 | {weapon} while dawn worn | NONE (lesson: a re-wield after a spent Dawnbringer turns a stand-still ranged swing into a melee walk at her) | `drop` dawn once in the box |
| FETCH | my turn, it lies on the floor | - | - | pull to its tile; on it: obj op 3 (room-side, GAP) |
| STRIKE | otherwise | - | her, range 1 (tentacle, capped 10) | - |
| BREAK | shield broken | - | - | falls only |

Cover: the team pillar ALTERNATES the two front pillars after each read (owner; HMT_STRATEGIES:
"alternate the two front pillars after each attack"): standing (not falling) pillars in v1 order
(near row first, west first), team = standing[alt] with alt toggling 1<->2 at each read past.

The Dawnbringer as a gear channel: `intents.gear` makes the run loop schedule a `held` entry
(`ends_attack`) at the tick it goes out, so the plan knows the swing it costs (TOB2_DESIGN "held").

## Value (HP_PER_BOSS_HP = 0.25, PROBE)

P1's cost of length is the pillar budget: ~22 reads (6 x 185 / ~50) = ~310 ticks; past it every
read is 5 x ~34 prayed = 170 team hp per 14 ticks (12/tick). With a ~9/tick team rate inside the
budget the expected marginal cost is ~0.25 team hp per shield hp. Swing values: tentacle
min(hit,10) exp 8.5 -> 2.1; Dawnbringer exp 40 -> 10; its special exp 110 -> 27.

## PROBES

- P1 swings/seat per 14-tick cycle (target 2-3; v1 committed: ~1, 1149 dmg in 400 ticks, 0/48).
- The bolt read tick vs the cover zone: any `hit_player` from her bolt = a cover miss.
- Rubble window [land-2, land-1] and fall window vs measured `hit_player`.
- The Dawnbringer path is NOT exercised by the from=verzik harness (no Xarpus, nobody holds it).

PASS: P1 cleared within 400 ticks, no raider dead, 0 avoidable damage (bolt/fall/rubble hits).
