# Tightrope — solver plan (2026-10-18)

Room file to write after F3/F4: `script/plugins/quest_driver/cox2_tightrope.lua` (a
`QD.T2` room, `cox_tightrope`), test `test/raids/cox_tightrope.lua`, entered with
`QD.raid.cox_test_enter("tightrope", n)` (`::coxtest 4 n`). Every number and tell:
`tightrope_spec.md` (rows Tn). Method: `docs/CONTENT_SOLVER_MANUAL.md`.

## 1. The strategy a strong solo human plays

Synq 0:26:21 (the standard solo method), The Academy, Plank2g, wiki Strategies:

1. **Kill the mages first** (lower max hit; Academy, wiki), then the rangers — one
   at a time. Twisted bow on mages / shadow on rangers (Synq); with the WDR kit the
   **blowpipe** (best on both: low defence, Plank2g) or the crossbow.
2. **Make it 1:1.** The guards attack every 4 ticks; use an obstacle (a pillar/gate
   between me and the others) or their attack range so only the one I am killing
   sees me, and tank gear on the turns they swing (Synq: "flick on your tank gear
   every other hit"; BCP-style body; the ranger's 70 max is mostly misses into high
   ranged defence). Pray Missiles against a ranger, Magic against a mage.
3. **Reset** a mistake (two guards on me): run out of their render range / step on
   the skull tiles so they lose aggro (Synq, wiki).
4. Guards dead → **cross** (no one wakes), take the keystone, cross back, **Dispel**.
5. Not played: the **rope skip** (cross under fire with a phoenix necklace, eat the
   volleys; ~1 in 20 death on the first cross, Nang) — faster, but it fails the
   no-death criterion; a later fast mode, not this solver's.

## 2. Perception (client-sent only)

| fact | verb | notes |
|---|---|---|
| room, variant, turn | `QD.COX.here()` (instance_zone) | template tables |
| guards | `api_drive.npcs` rows 7559 / 7560: `server_x/z`, `health_ratio`, `anim_primary` (426 / 711 = a swing), `facing_entity` (am I its target) | a guard facing me is engaged |
| guard shots | `api_drive.projectiles` 15 / 130 with `target` = me, `cycles_left` | landing tick = seen + round(cycles/30) |
| line of sight | `api_drive.server_los` (guard tile → candidate tile) | the 1:1 positioning |
| rope ends, keystone, barrier | `api_drive.locs` 29750 / 29751 / 29749 | barrier gone = room done |
| my delayed state on the rope | `api_drive.player_delayed` | |
| hp, prayer, worn | `skill`, `prayer`, inventory/equipment verbs | |

## 3. PREDICT: the threat field

| term | when | where | cost |
|---|---|---|---|
| ranger shot | landing = launch + round((50 + 5d)/30) | me (homing) | E = p_hit(my ranged def) × 35 × (0.33 if Missiles at landing) |
| mage shot | landing = launch + round((75 + 5d)/30) | me | E = p_hit(my magic def) × 11 × (0.33 if Magic) |
| next swing | 4 ticks after its last swing, if it sees me within 10 | per guard | the plan prices standing in an idle guard's sight when it would wake (it only wakes by retaliation or a crossing) |
| crossing | 10 ticks delayed; every engaged guard's shots stack and land on the first tick off | — | forbid crossing while any guard is alive (solver rule; the content allows it) |

## 4. STRATEGY: the mode machine

`SCOUT` (count guards, pick target order: mages nearest-first, then rangers) →
`ENGAGE` (stand where only the target sees me — LOS check against every other alive
guard — attack it; prayer = its style) → `KILL` loop (re-attack off cooldown; if a
second guard engages → `RESET`: walk out of its sight / range until it drops, then
back) → next target → `CROSS` (all dead; click the rope end) → `KEY` (take) →
`RECROSS` → `DISPEL` → Passage.

Gear switch per tick: tank body worn on the ticks a guard's swing lands (Synq's flick)
— optional refinement once the base passes.

## 5. Kit (start from `test/raids/cox_olm_solo.lua`)

The Olm kit as is: blowpipe (main weapon here), dragon crossbow, Xeric's aid (+) for
healing, prayer (+). Agility 99 (the kit already sets it; the requirement is 79-99 at
99). Nothing room-specific is needed (Synq: "I don't really need anything special for
tightrope"). One free slot for the keystone.

## 6. Probes

P1 per guard: swing ticks (anim) every 4, landing tick vs projectile cycles; P2 the
1:1 tile set per variant (LOS from each guard); P3 a guard's de-aggro distance when I
leave (engine `maxrange 7` vs the wiki's "render distance"); P4 crossing with all
dead: 0 hits; P5 Dispel removes the barrier, the Passage is reachable; P6 `grep
abort` = 0.

## 7. Engine / content work found

- No verb missing for this plan. Check on both lanes: `server_los` from an npc tile
  (the 1:1 positioning depends on it) and `player_delayed` during the rope walk.
- Content open (spec §5): guards wander (`defaultmode=wander`, generated) — if they
  leave their platform the LOS tables do not hold; settle before coding P2.

## 8. PASS criterion (written before any sweep)

GREEN on a seed: the barrier dispelled, I am alive, every guard killed before the
first crossing (0 hits taken on or after a crossing), ≤ 2 Xeric's aid doses used,
room ≤ 500 ticks. Judge on 48 seeds.

## 9. Built (round 2, 2026-10-18)

`script/plugins/quest_driver/cox2_tightrope.lua` (QD.T2 room `cox_tightrope`) and
`test/raids/cox_tightrope.lua` (run name `tr<N>` → `::coxtest 4 <N>`).

- Modes: `enter` (pull toward the room's centre until a guard is in sight — tr1
  crossed blind from the door and woke all four) → `kill-magic` ×2 → `kill-ranged`
  ×2 (an attack world op when the guard is out of range, else a planner target;
  the overhead for the guard fought) with `topup` between guards (hp to 85, Ranged
  restored: Xeric's aid drains it, and a drained blowpipe hit a ranger 15 times in
  125 swings on tr0) → `cross` → `take` → `cross-back` → `dispel`.
- PASS also requires 0 hp lost from the first crossing on (the solver's rule:
  never cross with a guard alive).

### Sweep, 2026-10-18 (scriptrun, `tr0..tr47`)

46/48 GREEN: four guards killed one at a time (mages first), 0 hp lost from the
first crossing on, keystone taken, barrier dispelled, Passage taken; ~150-200 hp
lost to the guards per room, healed with Xeric's aid. The two red seeds (tr46,
tr47) abort inside `::coxtest` itself (`cox_layout_cell_get`, cox_layout.rs2:104,
from cox.rs2:911) — the foundation's.

### Round 3 (2026-10-18)

- The whole-raid contract: own room from where I stand, the raid's full pack —
  two switches go on the floor before the first guard (the blowpipe's second
  hand and the keystone each need a slot) and come back after the dispel.
- The barrier, once seen gone from its tile, stays gone (a walk away brought the
  out-of-view stand-in back and sent the solver across again).
- Loc and npc rows filtered by my level.
- Sweep (round 3): 47/48 — the red (tr10) solves the room and stalls in
  `cox_walk_on` (6414,98 → 6416,80). Crossing damage counts hp lost at or under base
  only (Xeric's aid's overheal decays a point at a time).

### Room lookup (2026-10-18, raid7)
`find_room` looks in this order: the cell I stand in, then `QD.COX.room_ahead`
of that cell, then each cell beside mine, probed at its tile nearest me, then
the `::coxtest` centre. The room's middle can lie outside the zones the client
holds: raid7 left me on the small scavenger room's way-out edge. The wrapper
`QD.raid.<room>_solve` finds the room before the room loop starts. If it finds
none, it returns `"no_room", why` for the relay to report, never an assert.

## Round 4 (2026-10-18): the solo safespot, on the raid's supply budget

What a guide plays solo, taken from three sources:
- Wiki CoX/Strategies "Tightrope": "the mages should be dealt with first,
  due to their lower max hit". For a solo it gives the safespot: "a weapon
  with a range of 8 or greater ..., then running away until they are no
  longer rendered ... While slower, it does ensure that supplies are not
  touched".
- Synq 0:28:41: "attack any mob and run out of its render distance ... go
  back and forth attacking it without taking any damage at all".
- Synq 0:26:22: magic on the rangers. Their magic defence is their Defence,
  with bonus 0 (wiki infobox).

How the solver plays it:
- Kill mages first, then rangers, one at a time.
- Wear the mage switch (trident of the swamp, occult, imbued cape, mystic;
  the defender stays) and use Longrange: 9 tiles, 4 ticks.
- The planner may not end a tick within LEASH = 8 of that kind's posts.
  That is where the engine's leash (maxrange 7 + 1, from the spawn tile)
  drops the guard's target, so the guard never swings.
- The protection prayer goes up only inside that box, or for 8 ticks after
  a hit lands.
- Restores come from QD.COX.restore_intent on Magic (gap 20). Heals happen
  only under 30.
- The pack is no longer emptied before the fights. Only before the first
  crossing does one droppable item go to the floor, and only when no slot is
  free for the keystone.
- The armour the switch replaced, and the tentacle, are worn again at the
  end.

Room test results:
- Fresh kit (tr0-7): 8/8. 0 hp lost, 0 sips, 0 prayed ticks, 228-384 ticks.
- Drained arrival (trd0-7: restores dropped, 9 heal doses, 3 Saradomin sips
  drunk): 8/8. 0 sips, 0 prayed ticks, 378-635 ticks. The 3-6 hp in the
  tally is the brew's overheal decaying 1 per 100 ticks.

Caveat: the leash is A15's open question. If OSRS's leash is the render
distance, the wiki's run-away step is what keeps the cost at 0, and the
solver would need it.
