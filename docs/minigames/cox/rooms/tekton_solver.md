# The solo Tekton solver: the plan (2026-10-18)

To be `script/plugins/quest_driver/cox2_tekton.lua`, a `QD.T2` room
(`cox_tekton`) on the v2 stack (PREDICT + STRATEGY here; `aplan`, EXECUTE,
MONITOR and the resource layer shared), and `test/raids/cox_tekton.lua` entered
with `::coxtest 0 <n>`. Mechanics, ids and every number: [`tekton_spec.md`](tekton_spec.md)
(rows T1-T20 are cited below). Shape: `docs/minigames/cox/olm_solver.md`.

**2026-10-18: the solver plays Synq's 4-tick method** (the kit's tentacle is
a 4-tick weapon), not the 6-tick step-back below. It goes round him
counterclockwise on his edge tiles, attacking as it goes, on an edge at every
scan. The strike model is the recorded one (`docs/rsprox-analysis/cox/tekton.md`):
his held facing, the front row for a stab or a crush, plus his right column for
a slash a tick later. The terms in `cox2_tekton.lua` are:
- the next strike's tiles, exact every tick;
- a cost on ending a tick off the edge ring;
- a cost on the side clockwise of the one the bot is on (or came off a corner from);
- his footprint forbidden while he is solid.

12 seeds: no hit from Tekton, 215 side changes, all counterclockwise.
Spark landings are predicted from the server's `(end + 15) // 30`. The solver's
old `+ 15` on `cycles_left` landed every spark a tick early, because a row read
on its arrival tick already counts 30 cycles.

## The strategy a strong solo human plays

From Synq (1:09:31-1:25:34), RuneScythed's flinch guide, Theoatrix, the
Academy and the wiki Strategies page:

1. **Lure far.** Walk in, stand on the wake ring (4 tiles off his footprint,
   T2) on the side AWAY from where you want the fight, then walk to the far
   end of the room so he walks the whole way ("lure Tekton as far away from
   the anvil as possible": every tile of walk-back is a free hit, T7). Stay
   out of the smoke vents (T12) and never cross the fire (T15).
2. **Spec start on the orange form** (his defence is lowest; red is "the form
   you don't want to spec on"). Synq: attack him while he is 3 tiles away so
   the attack lands as he arrives and you are already on the method's tile.
3. **The method by weapon speed.** Every one stands on an EDGE tile (never a
   corner, T5) at the end of the tick before each swing, and is off his
   front row + right column at the end of the swing tick (T4):
   - **6-tick (BGS / elder maul / DWH): the step-back flinch** (Synq 1:22:58,
     RuneScythed): edge at end `S-1`, one tile straight out during `S`, back
     in during `S+2`; the swing goes out from the edge tile every other
     cycle. Uses no run energy, takes no damage. **This is the plan's method**
     (the kit's BGS on Smash is crush, the style his defence is lowest to).
   - 4-tick (tentacle, mace): counter-clockwise round him (Synq "4-Tick",
     "Monkey"; the Academy): attack from the tile before a corner, run round
     the corner on the swing tick; never clockwise (the right-hand cleave).
   - 5-tick (scythe, fang): Synq's 5-tick path (step away on the third, stall
     on the fourth).
4. **Specs when he turns red → orange** (T8: 18 ticks after the red contact),
   or at the session's start if orange.
5. **The repair:** dodge the sparks: after each landing, move 2 tiles (T11;
   Synq "click the next tile the moment the meteors land"); end on the tile
   you want to fight from, with the next lure in mind (he rises in red and
   walks at you, T9b).
6. **Prayer:** Protect from Melee as insurance in the learner kit (it halves,
   T13); piety. Synq's advanced play turns it off for vengeance; not this kit.
7. **Late kill:** "If Tekton is low enough HP, you should get closer to it when
   it gets off of the anvil."

## The kit

Start from `test/raids/cox_olm_solo.lua` (We Do Raids minimum CoX gear, CoX
(+) potions only). This room needs nothing new to be cleared with zero damage:

| | |
|---|---|
| weapon | **bandos godsword on Smash (crush)**, 6-tick: the flinch method; its spec drains (and on a miss 10 defence, T19) |
| alt | abyssal tentacle (slash, 4-tick) for the counter-clockwise method if the BGS plan is too slow |
| supplies | overload (+), Xeric's aid (+) for spark mistakes, prayer enhance (+), 1 stamina (the spark dance and the lure run) |
| prayer | Protect from Melee + Piety |

Recommended additions for a faster kill (a decision for the owner, the kit is
fixed once named): a dragon warhammer or elder maul (crush, and the first spec
is guaranteed to hit, T19 -- NOT yet implemented, spec O2), an inquisitor's
mace. The room drops 2 overload (+), a prayer enhance (+) and a revitalisation
(+) (T18): the resource layer should bank them for later rooms.

## Perception (only what a client is sent)

| fact | verb |
|---|---|
| his form (orange/red, walking/fighting/hammering/dormant) | `npcs` row `npc_id` 7540-7545 |
| his footprint | `npcs` `server_x/z`, `size` 4 |
| the swing clock | `npcs` `seq_id` in {7482,7483,7484,7492,7493,7494}, keyed on the tick it is FIRST SEEN (never `seq_tick`, one early, manual Part 2); the next swing is +3 |
| his facing at the swing | `npcs` `face_x/z` (a far tile due N/E/S/W) -- also derivable: the side I was on at end `S-1` |
| contact / leave / anvil / rise | seqs 7478/7488 (stance), 7479/7489 (leave), 7475 (lean), 7474/7487 (rise); `locs` `raids_tekton_walkblocker` under him while in stance |
| sparks | `projectiles` spotanim 660: destination tile + cycles left → landing tick (first-seen + round(cycles/30)); `spotanims` 659 at the landing |
| vents | `locs` `raids_lava_smoke3` (2x2): treat the 4x4 round each vent as the hitbox (the spec's 3x3 overhangs the vent by a row and a column, T12) |
| the fire | `locs` `raids_tekton_fire` at the entrance |
| his health | `npcs` `health_ratio/scale` |
| my state | `player_tile`, hp, prayer, run energy, inventory (existing verbs) |

**Missing verbs: none found.** Every tell above is a packet the client already
records on both lanes (npc info, projectile, map spotanim, loc). To check on
the first run: that `projectiles` reports a MAP-targeted projectile's
destination tile on scriptrun as on the live lane (Olm's acid uses the same
shape), and that the face-coordinate update reaches the npc row on both.

## PREDICT: the threat field (one currency: expected hp)

| hazard | when / what is read | tiles | cost |
|---|---|---|---|
| strike | swing slots `S` = last first-seen windup + 3k; reads my tile at end `S` | front row + right column of the facing chosen from my side at end `S-1` | E = P(hit)·mean(0..max)/2 on prayer (max 52/59 → 26/29); max to `hp_worst` |
| scan miss | at end `S-1` | not on one of the 16 edge tiles | not damage: the session ends early -- priced as the remaining swings' value (my DPS × ticks lost) |
| trample | each tick he walks (forms 7541/7543 moving) | his footprint | 2 / tick (max 3) |
| spark | each projectile 660: landing `L`, read end `L-1` | 3x3 on its destination | 15 (max 20) each; two per volley land together or a tick apart |
| vent | ticks with `clock % 3 = 0` | 4x4 round each vent | 2 (max 3) |
| fire | always | the fire loc | forbid |

## STRATEGY: the mode machine

| mode | goal | leaves when |
|---|---|---|
| ENTER | pot (overload, prayer enhance), prayers up, walk to the lure tile | on the wake ring |
| LURE | step onto the wake ring, then walk to the far fight tile (chosen: max walk-back distance from his work tile, ≥2 from every vent) | rise seq seen |
| APPROACH | spec on his last approach tile (Synq's 3-tiles-away start) | stance seq / walkblocker |
| FLINCH (orange / red) | the 6-tick step-back: edge at end `S-1`, out at end `S`; swing from the edge every other cycle; spec when red → orange | leave seq (session over or a scan miss -- a monitor fact) |
| CHASE | hit him while he walks back; never under him | lean seq |
| DODGE | the repair: be ≥2 from each spark's destination at its landing; end on the next lure tile | rise seq 7487 |
| KILL | his health at 0 | death seq; then loot the drop (supplies) and leave by the exit |

Swing value: E[damage] × HP_PER_BOSS_HP as in ToB; specs scheduled on the
orange form.

## Probes (the first run prints these; spec §6)

P1 wake at 4 not 5 · P2 swings per session 10 / 15 · P3 spark landing tick vs the drawn projectile · P4 Protect from Melee halves (max on prayer ≤ 29) · P5 the flinch is never hit (content probe 2026-10-18 already showed 0 melee hits over 13 red/orange swings, `scratchpad tek_probe3`).

## PASS (written before the sweep)

On 48 seeds of `::coxtest 0 <n>`: Tekton dead, the raider alive, **0 damage
taken from Tekton's swings, sparks and vents** (the Blind Spot CA, which is
what the flinch method gives a strong solo player), zero `abort` / `no primary
player bound` lines, and the monitor's strike/scan predictions matching every
swing. A scan miss (early anvil) is reported, not failed.

## Built (round 2, 2026-10-18)

`script/plugins/quest_driver/cox2_tekton.lua` (`QD.COXTK`, room `cox_tekton`)
and `test/raids/cox_tekton.lua`. As built: the kit's abyssal tentacle (no
weapon switch, no spec), Protect from Melee + Piety, the Olm room's own
overload / prayer enhance / stamina machine (`QD.COXO.drinks`). PREDICT states
the strike as a forbidden ring of his 16 edge tiles at the end of every swing
tick and the scan as a 10-hp cost anywhere off that ring at the end of the
tick before; the planner finds the flinch on its own (a 4-tick weapon against
a 3-tick boss). Contact-ahead: walking at me with me on an edge, the first
swing is assumed at now+2. Sparks are forbidden 3x3s at their landing read;
vents a forbidden 4x4 round each `raids_lava_smoke3`.

Result: 46/48 PASS, 0 Tekton damage on every passing seed (the two failures
are `::coxtest`'s layout search running out of instructions for n = 46, 47).
The way on (`t.raid.cox_walk_on`) stalls after the Passage on 16 of 46
seeds (the Passage teleports one tile past the loc, still inside this room,
and the next room is not reachable from there): logged, not judged -- a
foundation item. Not built yet: the BGS / DWH spec start and the step-back
method with a 6-tick weapon.
