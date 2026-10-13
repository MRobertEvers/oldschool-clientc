# ToB v2: threats over time, a plan over actions, outputs that take time

Status: started 2026-10-12 on branch `tob2-v2`, in its own worktree, beside the
v1 Hard Mode relay (which keeps working and keeps being improved). First room:
Maiden, Hard Mode.

## Why a v2

The v1 room solvers model the world well and the player badly. The planner
(`collision_plan`) searches over "my tile at the end of tick k" against
time-windowed hazards. But a click is not a tile: it starts a server process
(the walk queue, an attack that follows its npc, a held op that ends a swing,
a special that delays the next swing, a press refused while stunned). None of
that is in the search, so every room's ORDER step grew special cases for it,
each found by a full run (`_tob_order`, `_vzp3_decide`; the digest lists E1-E14,
most of them in more than one room). That is why the work thrashes: the only
oracle for the action model was a 10-second seed.

v2 splits the solver into five layers. Only the first two are per room.

```
 observe -> PREDICT -> STRATEGY -> PLAN -> EXECUTE -> server
            (room)     (room)      (shared) (shared)
   ^                                                     |
   +------------- MONITOR: predicted vs measured --------+
```

| layer | owns | file |
|---|---|---|
| PREDICT | the threat field: what can hurt which tile at which absolute tick, how much (expected and max), and threats that depend on where I stand | `tob2_<room>.lua` `predict` |
| STRATEGY | a mode state machine: roles, goals, targets and their worth, the non-spatial intents (overhead, gear set, special) | `tob2_<room>.lua` `strategy` |
| PLAN | a beam search over MY ACTIONS (stay, walk a tile, attack/cast a target), with the actuator state in the node, scored in one currency | C `action_plan.c`, verb `api_drive.aplan` |
| EXECUTE | one state machine per output channel, the interaction table between channels, intentions kept until invalidated | `tob2_exec.lua` |
| MONITOR | every tick: what the plan predicted for this tick vs what was measured, mismatch counts by rule | `tob2_monitor.lua` |

The action model is ONE model, used by the planner to imagine and by the
monitor to check. Its rules come from the peer session's replay oracle
(`ORACLE_RULES.md`, R1-R5 timing, M1-M6 movement, P1-P4 prayer/targeting) and
from the v1 lessons (E1-E14 in `ROOM_DIGEST.md`); every rule in code cites its
id.

## The one currency: expected HP

Every cost is in hitpoints of the seat that plans:

- a threat on my tile costs its EXPECTED damage after the prayer I will have
  (`dmg_exp`); its MAX damage (`dmg_max`) runs down a worst-case hp
  (`hp_worst`, starting at my hp). A path on which `hp_worst <= 0` is a LETHAL
  path at that step, whatever the hazard's label: a "damage" hit at 10 hp is
  lethal, at 99 it is not.
- an owner rule ("never stand on a pool", "no seat should be poisoned") is a
  hit with `forbid = true`: lethal by definition.
- a swing that lands is worth `value` hp: the strategy's estimate of the hp the
  team does not lose because the fight is shorter by that swing's damage.
  Per room: `value = E[damage of the swing] x HP_PER_BOSS_HP`, where
  `HP_PER_BOSS_HP` = (team damage taken per tick, unavoidable) / (team damage
  dealt per tick). It is a constant per room first, measured from the tick
  logs (budget.py) and refined later.
- strategy terms (stand at my station, stay out of the tank's spot) are soft
  costs in the same units, so their size is a statement: "standing off my
  station for a tick is worth 0.5 hp".

Plans compare by: risk (lethal steps, earlier counts more), then
`loss + soft + clicks*click_cost + steps*move_cost - value`, then fewer tiles
walked, fewer clicks, insertion order (deterministic).

## Tick mapping (every layer uses this, R1-R5)

- F.tick = d is the tick whose END I see; my click goes out between d and
  d+1 (R1) and moves me during d+1. Plan step k is my tile at the END of tick
  d+k.
- An npc decision on tick T reads raider tiles at the end of T-1.
- A prayer pressed after seeing d is in force for swings at d+1 (R5); the
  prayer at a swing on S is the one in force at S (R3).
- A threat's window `[t0, t1]` is in absolute ticks and names the tick-ENDS on
  which my tile is read. A hazard that "reads the end of T-1" for a hit on T
  has `t0 = t1 = T-1`.
- npc rows are on their server tile (`server_x`); `seq_tick` is one tick
  early (R4).

## PLAN: `api_drive.aplan(spec)`  (C: src/engine/world_builder/action_plan.{c,h})

A new search beside `collision_plan`; it reuses `collision_map`'s step tests
and line of sight. Node state:

```
x, z                 my tile at the end of tick now+k
mid_x, mid_z         the run's middle tile this tick (-1 when none)
inter                -1 none, else the target index whose attack is under way
swing_ready          absolute tick my next swing may go out
stunned_until        absolute tick; while t <= it I neither move nor swing (M5)
hp_worst             worst-case hp so far
mark_x[], mark_z[]   where I stood at each reactive mark's tick (-1 before)
cx[], cz[]           chasers (the v1 tornado rule)
risk, loss, soft, value, steps, clicks   accumulators
```

Actions at each step (the click decided after seeing now+k-1, acting in
now+k):

| action | the server's process (the model) | click |
|---|---|---|
| STAY | no movement; `inter` unchanged if none; if an attack is under way, staying needs a walk to my own tile (E3), which ends it | 0 if inter == -1, else 1 |
| WALK m (24 moves: 8 one-step, 16 two-step when running) | a tick-end-tile click: one or two steps (two only when `run`), middle tile by the v1 rule (straight step on the dominant axis first, diagonal second). Ends any attack. (E6: always click the tick's end tile, so the server's route cannot pass anywhere else.) | 1, unless the same heading as the previous step (v1's click count) |
| ATTACK i | `inter = i`. Reach is judged against the target's footprint at step k (npcs move first, M4/E1). In reach: stand. Not in reach: the server walks me toward it, `run ? 2 : 1` steps, each step the legal neighbour (diagonal first, then the axes) that most reduces the gap to the reach set, stopping once in reach. Then, if in reach AND `t >= swing_ready` AND the target accepts swings at t: the swing goes out (lesson 8: on arrival), `value += target.value`, `swing_ready = t + target.speed`. A target that does not accept swings at t refuses the press: I stand still and `inter = -1` (lesson 21). | 1 if `inter` was not already i, else 0 |

Reach: `range == 1` is melee: beside the footprint, not diagonal, not under.
`range > 1`: Chebyshev gap 1..range to the footprint AND line of sight (the
server's ranged rule; v1 zone `sight`).

Scheduled held ops (`held`, from the executive's resource channel, absolute
tick t): at that step the swing timer is pushed `swing_ready = max(swing_ready,
t + swing_delay)`, `hp_worst += heal`, and if `ends_attack` the interaction is
cleared BEFORE the step's action (E4/M6) - the step must re-click to keep
attacking, and the click is counted.

Scoring at step k (`t = now + k`), after the move:

- `hits`: rect `[x0..x1]x[z0..z1]`, window `[t0, t1]`, `sample` `"end"` (the
  tick-end tile only: M3) or `"pass"` (the run's middle tile too: webs, Bloat
  hands), `dmg_exp`, `dmg_max`, `stun`, `forbid`. A hit on my tile charges
  `loss += dmg_exp`, `hp_worst -= dmg_max`, and a stun sets `stunned_until = t +
  stun`. `forbid` or `hp_worst <= 0` adds `h - k + 1` to `risk` and records the
  why (`kind`, `index`, `k`) the first time.
- `marks` (threats that land where I stood): at `t == t_mark` the node records
  its tile; for `t` in `[t0, t1]` a tile within Chebyshev `r` of the recorded
  tile is hit as above. (Maiden's blood throw lands on my end-of-B-1 tile; P2
  urns, Xarpus spit, P3 webs are the same shape.)
- `chasers`: the v1 rule (`collision_map.h`): step sign(dx), sign(dz) toward my
  end-of-last-tick tile; touching costs `dmg_exp`/`dmg_max`.
- `zones`, `pulls`, `goal`, `edge`: the v1 soft terms, in hp.
- `move_cost * steps`, `click_cost * clicks`.

Dedup: one node per (x, z, inter, min(swing_ready - t, 15), stunned, marks,
chasers); the best by the comparison above is kept, beam-wide per step.

Result:

```
"ok", { path = { {x, z, mid = {x, z}|nil, act = "stay"|"walk"|"attack",
                  tgt = i|nil, swing = true|nil}, ... },     -- step k = 1..h
        risk, loss, soft, value, hp_worst, why = "hit[3] k2"|nil, expanded }
```

`path[1]` is the click to make now. The whole path is the prediction the
monitor checks a tick later.

Spec (absolute tiles and ticks; lists optional):

```
from = {x, z}, now, h (<=16), beam (<=64), run = true|false, hp,
inter = i|nil, swing_ready, stunned_until,
click_cost = 0.05, move_cost = 0.01,
targets = { {size, path = { {x, z}, ... }  -- footprint SW at k = 0..h (k = 0 now;
                                          --  short paths hold their last tile)
            range, speed, value, t_ok0, t_ok1}, ... }           -- <= 6
hits    = { {x0, z0, x1, z1 | x, z, size, t0, t1, sample, dmg_exp, dmg_max, stun, forbid}, ... }  -- <= 512
marks   = { {t_mark, r, t0, t1, sample, dmg_exp, dmg_max, stun, forbid}, ... }                   -- <= 8
held    = { {t, ends_attack, swing_delay, heal}, ... }                                          -- <= 8
chasers = { {x, z, dmg_exp, dmg_max}, ... }                                                    -- <= 4
zones, pulls, goal, edge  -- as api_drive.plan, costs in hp
```

## EXECUTE: the channels  (`tob2_exec.lua`)

Each channel is a state machine with an INTENTION (what it is trying to make
true), the click it sent and the tick it sent it, and what it expects to see
next tick. A channel re-clicks only when its intention changes or the
expectation failed (then the monitor names the rule).

| channel | states | rules (ids) |
|---|---|---|
| interact (move/attack/cast) | `idle`, `walking(x,z)`, `attacking(npc)`, `casting(npc, spell)` | plan `path[1]` is the intention; walk = tick-end tile (E6); a walk not taken is re-sent after PATH_STALL ticks unmoved or when pushed off (E5); an attack re-pressed after any held op in the same tick (E4); a cast: walk while the plan moves, cast once it stands and the cast clock allows (E2) |
| held (eat, drink, wield, drop) | `ready`, `cooldown(until)` | the backpack tab must be showing before the op (E9); a held op ends an attack (E4/M6); gear re-sent at most every 2 ticks unless the set changed (E11); a two-handed wield needs a free slot |
| worn (unequip) | | the worn tab, `wornitems:slotN` op 1 |
| prayer | `set`, `pending(until d+1)` | a press is a toggle: never twice inside 2 ticks unless forced (E7); press only to change what is lit; in force at d+1 (R5) |
| special | `off`, `armed` | the orb is a toggle: press only while not armed (E8) |
| run | `on`, `off` | back on at >= 500 energy, at most every 3 ticks (E10); plans assume running only at >= 300 (M2) |

Per tick the executive sends, in order: run, prayer, held, worn, special,
interaction (v1 `_tob_emit`'s order, lesson 32). The resource channel
(`QD.T2.supplies`, v1 `_tob_supplies` underneath) decides eat/drink BEFORE the
plan, and hands the plan a `held` entry at `now + 1` so the plan knows the
swing it costs.

## MONITOR (`tob2_monitor.lua`)

On every tick, before the next decision, compare the previous tick's
prediction with the measurement:

| rule | predicted | measured |
|---|---|---|
| `move` | `path[1]` tile | my server tile |
| `swing` | `path[1].swing` | my attack animation started this tick (v1 `_vzp3_weapon_ticks`) |
| `refused` | a click accepted | the drive verb's answer |
| `threat` | `dmg_max` total on my step-1 tile | hp lost this tick (a hit above the max = a threat the predictor missed) |
| `stall` | movement | unmoved with a walk under way |

Counts by rule go in the solver's summary line; the first 20 mismatches per
rule go to the trace with the tick. The monitor never changes behaviour: it is
the evidence for the next batch of model fixes, the same job as the peer's
offline oracle, on the live decision.

## Room interface

```lua
QD.T2.rooms.<room> = {
  init     = function(S, opts) end,          -- roles, constants, S.room
  enter    = function(S) end,                -- the room's entry/start (v1 helpers: _maiden_enter, _tob_start)
  predict  = function(S, F, B) end,          -- threats into the builder B
  strategy = function(S, F, B) return intents end,
             -- targets/goals/zones into B; intents = { pray, pray_force,
             --   gear = {obj...}, unequip = {comp...}, spec, cast = {spell},
             --   eat_below, brew_below, mode = "name" (for the trace) }
  done     = function(S, F) return "ok"|nil end,
  summary  = function(S) return "text" end,
}
QD.raid.<room>_v2_solve(opts) -> result, detail, S    -- the relay's contract
```

B (the spec builder, `tob2_plan.lua`) gives `B.hit{...}`, `B.mark{...}`,
`B.target{npc = row, range, speed, value, path = {...}, t_ok0, t_ok1}`,
`B.zone{...}`, `B.pull{...}`, `B.goal{...}`, `B.chaser{...}`, `B.prayed(style,
t)` (the factor the executive's prayer schedule gives a hit of that style at t),
and names every term for the trace.

## Files

- `src/engine/world_builder/action_plan.{c,h}`, its unit test, the Makefile
  lists, `src/plugin/torirs_drive_plan_lua.h` (`DriveActionPlanLua`), the verb
  `aplan` in `torirs_plugin_drive_ui.c` and `torirs_server_scriptrun.c`.
- `script/plugins/quest_driver/tob2_plan.lua`, `tob2_exec.lua`,
  `tob2_monitor.lua`, `tob2_run.lua`, `tob2_maiden.lua`, listed in
  `src/plugin/torirs_plugin_drive_parts.h` after `tob_relay.lua`.
- `tob_relay.lua`: `opts.v2 = {maiden = true, ...}` picks
  `QD.raid.<room>_v2_solve` for those rooms; every other room stays v1.

## The run protocol (unchanged, ROOM_SOLVERS.md section 0)

Code a room in full before its first run; the first run is ONE probe seed on
scriptrun (private build `build_tob2`, isolated content worktree); read the
whole tick log, the trace and the monitor's counts before any edit; fix in one
batch; then 8 seeds, then 48 with a continuous measure. Never a small edit
followed by a run.

## What the probes taught (2026-10-12, Maiden HM, scriptrun)

Every run writes a DECISION LOG: one tick-log `mark` row per seat per tick
(`t2 p<pid> t<tick> <mode> @x,z <act>>x,z[!] r<risk> l<loss> v<value>
hw<hp_worst> [why] i<inter> sr<swing_ready> le<exp> lm<max> hp<hp>`, `!` = a
swing planned at step 1). Read it with
`awk -F'\t' '$3=="mark" && $10 ~ /^t2 p3 /' ticklog.tsv | cut -f10`.

| probe | finding | rule now in the model |
|---|---|---|
| 1 (786 ticks, v1 384) | every melee tick planned value 0: reach required the step INTO the footprint to be legal, and a boss's footprint is blocked | melee reach = `collision_map_reached` (wall flags on my edge only, never the footprint's occupancy) |
| 1 | freezers chasing: 47/664 move mismatches, predicted diagonal, server went straight | the chase is the client's BFS route to the reach set (M1), reach tested at the step's START, the swing on arrival at the tick END |
| 1 | a beam ranked by cost kept the chase that started last tick and dropped the one furthest along | the beam reserves, per target, its cheapest attacking node and its fewest-route-steps-left node |
| 48 seeds | S1 leaks in 49 of 48 seeds: the first slot's cast went out a tick late, because putting the cast set on takes ~3 ticks (one panel a tick) and the 5-tick cast clock was invisible to the plan | freezers prepare by her HP RATE (ticks to the wave), spawn freezes yield to a due slot, the cast clock is in the target's t_ok0. Next: the gear channel's readiness as executive state the planner reads |
| 48 seeds | a seat died at predicted risk > 0 with two storms in flight: nothing acted on the plan's own warning | `QD.T2.rescue`: risk from hp -> food AND brew this tick (separate timers, content-checked), one re-plan, adopted if risk falls |
| 48 seeds | 21.7% swing mispredictions: eating (seq 829) counted as a swing; a fresh press that swings on arrival runs on server tick d, stamped d+1; the next swing read at the NEW target's speed | swing seqs exclude human_eat; between-ticks stamp d-1; `swing_next` set once from the swing's own weapon. 5.1% left: presses at a crab that stepped |

Results, the same 48 seeds (mb1..mb48), committed v1 vs v2:

| | v1 | v2 first | v2 now |
|---|---|---|---|
| pass | 45/48 | 47/48 | 48/48 |
| mean ticks | 434 | 416 | 416 |
| mean team damage | 355 | 253 | 203 |
| worst seat | 657 | 212 | 202 |
| mean leaks | | 2.2 | 1.2 |
| swing mispredicted | | 21.7% | 5.1% |

v1 here is the COMMITTED v1 (41c470cba) on committed content (c3d2c922c9); the
main tree's uncommitted v1 work is newer. The live lane is not yet checked
(every-solver-identical-ticklogs): the client objects compile, no live run.

## The RESOURCE layer (2026-10-12): the relay's supplies as one budget

The local planner sees 12 ticks; relays die in Verzik P3 of a problem spread over the whole raid. Measured on
the 128-seed all-v2 relay (runs/relay_v2_128, team doses per relay):

| room | brew | food | restore | combat |
|---|---|---|---|---|
| maiden | 3.4 | 3.9 | 11.3 | 5.2 |
| nylocas | 10.6 | 4.3 | 24.6 | **21.6** |
| sotetseg | **48.8** (fail runs 63.7) | 0.1 | 31.3 | 0.7 |
| xarpus | 5.8 | 3.0 | 1.8 | 0 |
| verzik (P1-P3) | 52.2 | 18.3 | 60.1 | 9.9 (P3: **5**) |

P3 is fought with one super combat dose per seat (35 of the kit's 40 are gone before Verzik), and every brew dose
drains Attack/Strength by about 10% + 2: a seat that brews loses its boost and never gets it back. Failing relays
spent 17 more brew doses before P3 and then needed 52 more P3 ticks (412 vs 360), each tick an auto on every seat.

THE LAYER (`tob2_budget.lua`, shared, used by QD.T2.resources in every v2 room):

1. STOCK: per seat, the doses and food carried, read from the pack each tick (brew, food, restore, combat,
   ranging), plus the chest purchases the relay makes.
2. NEED: per remaining room, the doses it needs per seat, from the measured relay table above (the passing
   relays' median), with Verzik's split into P1+P2 and P3. Owner rule kept: every room still boosts ("use
   supercombats throughout every room"); it spends only its share.
3. RESERVE: what later rooms need. A room may drink only stock minus reserve, except the rescue on predicted death,
   which ignores the reserve (a death ends the relay sooner than any shortage).
4. P3 POLICY (tob2_verzik_p3.lua on the budget's API): heal with FOOD while boosted (food does not drain);
   brew only to the floor; after brews, a super restore once Attack or Strength is 20 under base, then a super
   combat when either is under base + 5, while doses last; the swing's value = its expected damage at the CURRENT
   boosted levels and weapon × HP_PER_BOSS_HP, so a drained seat sees what a restore and re-dose buys.
5. MONITOR: per room, the doses spent against its share; per P3 tick, the boosted Attack/Strength (the "st" probe
   made permanent in the decision log).
