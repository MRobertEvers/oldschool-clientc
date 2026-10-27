# Ice demon — solver plan (2026-10-18)

> 2026-10-26: the room's thaw, chop, light and fiend rules were rebuilt from RSProx
> (`icedemon_spec.md`, "Superseded by the recordings"). The solver's own fuel model
> (`CI.V`: a 480 pool, 2 a unit, 5 units a kindling burning one a tick) predates that
> and is now conservative: it under-counts what a stack thaws, so it never stops
> chopping early. Green on ice0, ice3, ice7 after the rebuild (49-54 kindling, two
> braziers, the thaw 121-142 ticks after the first light).

Room file to write after F3/F4: `script/plugins/quest_driver/cox2_icedemon.lua` (a
`QD.T2` room, `cox_icedemon`), test `test/raids/cox_icedemon.lua`, entered with
`QD.raid.cox_test_enter("icedemon", n)` (`::coxtest 3 n`). Every number and tell:
`icedemon_spec.md` (rows In). Method: `docs/CONTENT_SOLVER_MANUAL.md`.

## 1. The strategy a strong solo human plays

Synq 0:14:39, PecanBread (CM, 18:29), Tubby 29:08, The Academy, wiki Strategies:

1. **Make room.** Three free slots: axe, tinderbox, kindling (one stack). Drop (or
   store) potions to make them; pick them up while the demon thaws.
2. **Tools.** Take the bronze axe and the tinderbox off the corridor floor (or bring
   an axe: PecanBread drops an item the tick he clicks the first tree).
3. **Chop toward the braziers.** Start on the nearest sapling; on each xp drop move
   one tree closer so the stack fills next to the braziers ("make sure your last chop
   is on the tree closest"). A sapling that falls → the next nearest.
4. **Light.** At 28 (Synq) or ≥24/25 (PecanBread's simulated thresholds 25/24/12),
   light the nearest **unguarded** brazier (no icefiend beside it: Tubby "the one that
   does not have the ice next to it"). Chop a second stack, light the second unguarded
   brazier. A third, small stack (~10-12) on a third brazier speeds the thaw.
5. **While it thaws** (watch the bar): pick up dropped items, pre-pot, quick prayers
   = Protect from Missiles (always — it forces the boulder, the visible attack) +
   Rigour/Augury.
6. **Fight.** Demonbane / fire (Emberlight, burning claws spec, fire spells) if the
   kit has them; otherwise the kit's best (ranged with the blowpipe/crossbow; its
   damage is cut 67%). DWH/BGS spec for defence if spare. Dodge: every boulder is a
   3×3 on the tile I stood on at the swing — **move two tiles** before it lands
   (blowpipe at 1-2 tiles: "attack and run every two ticks"; further away "lands
   later": alternate two attacks then one).
7. Dead → storm gone; collect the secondaries; Passage on.

## 2. Perception (client-sent only)

| fact | verb | notes |
|---|---|---|
| room, variant, turn | `api_drive.instance_zone` via `QD.COX.here()` | template tables in `QD.COX` |
| saplings / stumps | `api_drive.locs` (29763 / 29764) | a stump is the fall tell |
| axe / tinderbox spawns | `api_drive.locs` (31862 / 31634) | |
| brazier state | `api_drive.locs` (29747 unlit / 29748 lit) per brazier tile | the only per-brazier fuel read is lit/unlit |
| guarded braziers | `api_drive.npcs` rows of 7586 at the fiend tiles | each fiend's tile names its brazier |
| fiend actions | npc row `anim_primary` = 1582 | counts deduction attempts (each 1/6) |
| thaw progress | npc row of 7584: `health_ratio / health_scale` (healthbar 7) | the bar the player watches; 0 → pop |
| pop | `api_drive.spotanims` id 188 at the braziers; 7584 → 7585 type change; demon walk | stage 1 → 2 is the type change 7 ticks later |
| kindling held, items | `api_drive.inv_count` | |
| chop success | xp drop / `messages` "You get some kindling." | |
| attack style + aim | `api_drive.projectiles`: spotanim 1324, `dst_x/z` (the aimed tile), `cycles_left`; Ice Burst: npc `anim_primary` 69 with **no** projectile → assume a burst on my tile landing next tick | prayer missiles → no bursts after the first swing |
| landing | `spotanims` 1325 (boulder) / 363 ×9 (burst) | for the monitor |
| demon tile | npc row `server_x/z` (2×2, SW) | |
| my hp, prayer, run | `skill`, `prayer`, `run_energy` | |

## 3. PREDICT: the threat field

| term | when | where | cost |
|---|---|---|---|
| boulder | land tick = launch + round(cycles/30) (I12), known from the projectile row on the tick it is seen | 3×3 round `dst` | E = p_hit(my ranged def) × 15 × 0.5 (prayer at landing); max 15 |
| burst | the tick after anim 69 with no projectile | 3×3 round my tile at the swing | E = p_hit × 12.5 × 0.33; max 8; + freeze 8 ticks (counts as a forbid on movement plans) |
| next swing | 3 ticks after the last anim 69 | aimed at my end-of-tick tile | plan to be ≥2 tiles from the tile I stand on at the swing tick by landing |

Stage 0 has no threats: the plan is a time budget (chop/light path), not a field.

## 4. STRATEGY: the mode machine

`SETUP` (drop to 3 free slots) → `TOOLS` (axe, tinderbox) → `CHOP` (nearest sapling,
step toward braziers on each success; re-target on a stump) → `LIGHT` (nearest
unguarded unlit brazier at ≥ threshold: 28, then 28, then ≥10; a guarded one only
when all unguarded are lit) → `CHOP`… until the projected thaw (bar ratio × 480 vs
fuel left: 10 thaw per kindling) covers the pool → `PREP` (pick up, pots, prayers
Missiles + Rigour) → `FIGHT` (attack; on each boulder seen, step 2 tiles off `dst`
along the shortest route that keeps range; re-attack) → `DONE` (7585 gone, 29876 gone)
→ Passage.

Targets/worth: only the demon. Specs: BGS/DWH first if the kit has one.

## 5. Kit (start from `test/raids/cox_olm_solo.lua`)

The Olm kit (WDR minimum, CoX (+) potions) works: tentacle / blowpipe / dragon
crossbow; the demon has 140 hp ×0.33 for non-demonbane. Room-specific:
- **tinderbox**: the room spawn's Take is blocked (spec A4) — carry one until fixed,
  then take it off the floor; **no axe** (take the room's bronze axe, the honest path);
- 3 free slots on arrival (drop potions, pick up later — the strategy, not a cheat);
- optional upgrades a strong player brings (state in the test, owner to rule): an
  Emberlight / burning claws or a fire-spell setup (Tome of fire) — **not** in the WDR
  minimum; without them the fight is ~13 boulders longer.
- Woodcutting / Firemaking 99 (kit sets every combat stat to 99; add these two).

## 6. Probes (each prints a row on the first run)

P1 ticks from first light to graphic 188 vs kindling burnt (expect 48 kindling, ~120
ticks for two parallel braziers); P2 fiend count and tiles; P3 per boulder: launch,
`dst`, landing splat tick vs `cycles`; P4 every 2-tile dodge took 0; P5 the bar ratio
per tick against `fuel` (bar must fall monotonically while ≥1 brazier burns);
P6 `grep abort` / `no primary player bound` = 0.

## 7. Engine / content work found

- **Content** (not ours): `[oploc1,raids_icedemon_tinderbox]` is bound in
  `quest_corsaircurse/scripts/corsaircurse.rs2`; move it to the quest's own loc so the
  CoX Take can be bound (`~cox_icedemon_take_tinderbox` is ready).
- **Content** (shared combat): the demon's magic defence must come from Defence 160
  (`~npc_defence_roll` magic case), spec A18.
- **Verbs**: none missing for the plan above — `locs`, `npcs` (health_ratio,
  anim_primary, server tile), `projectiles` (dst, cycles_left), `spotanims`,
  `inv_count`, `world_op` / `world_use` for loc ops and kindling-on-brazier exist on
  both lanes. Check before coding: the npc row `health_ratio` must update on a
  splatless hitmark on BOTH lanes (live client reads it off the headbar block).

## 8. PASS criterion (written before any sweep)

GREEN on a seed: the demon dead, I am alive, **0 damage taken from boulders** (Cryo No
More is the standard a solver dodging every boulder meets; bursts must not occur
after the first swing with Missiles on), no cheat that skips chopping/lighting
(`::cox_icedemon_fuel` never used), thaw reached with ≤ 70 kindling deposited, and the
room cleared in ≤ 600 ticks. Judge on 48 seeds.

## 9. Built (round 2, 2026-10-18)

`script/plugins/quest_driver/cox2_icedemon.lua` (QD.T2 room `cox_icedemon`) and
`test/raids/cox_icedemon.lua` (run name `ice<N>` → `::coxtest 3 <N>`).

- Modes: `axe` / `tinderbox` (world ops on the floor locs) → `chop` (the nearest
  sapling; the server's op3 loop does the rest) → `light` / `fuel` (the nearest
  unguarded unlit brazier first, a stack whole) → `wait` (enough fuel burning:
  fuel units left, a guarded brazier at two thirds, ≥ 1.15 × the bar's pool / 2)
  → `fight` (blowpipe, Protect from Missiles + Rigour; a world op when far, else a
  planner target; every boulder a forbidden 3×3 from its sighting to its landing).
- Boulder timing as measured: landing = sighting + round(cycles left / 30) + 1, and
  the tile is read the tick before (the first probe was a tick short, 45 hits).
- The kit (test header): WDR worn set, blowpipe, Ava's, Xeric's aid / revitalisation
  / prayer (+), WC and FM 99, room in the pack for the floor axe and tinderbox.
- The exit check takes the Passage op and watches the jump; the walk on into the
  next room (`cox_walk_on`) stalled past the doorway on seed 0 — foundation's.

### Sweep, 2026-10-18 (scriptrun, `ice0..ice47`)

45/48 GREEN: demon dead, 0 hp lost (every boulder dodged), 56 kindling deposited
(two stacks, both unguarded braziers), thaw ~165 ticks after the first light, room
~600 ticks. The three red seeds are layout faults, not the room: ice19 builds TWO
ice demon rooms in one raid (they share the instance registers, so neither thaws);
ice26 and ice47 populate the Muttadiles into the ice demon's own cell (the
Muttadiles' `raids_icedemon_axe` meat-tree axe and a junior muttadile in the
corridor; the solver walks at the wrong axe). Also seen: `::coxtest` answers after
up to 9 ticks for a high n, past `cox_test_enter`'s 6 (the tests read the answer
late).

### Round 3 (2026-10-18)

- The whole-raid contract: the solver finds its own room (the cell I stand in or
  the one beside it), needs no `::coxtest` answer, and plays with the raid's full
  pack: it drops the room's unused switches (mystic robes, imbued cape, occult,
  trident, rune pouch, crossbow, BGS — in that order) to make the three slots the
  axe, tinderbox and kindling need, and picks them up again (after dropping the
  room's axe and tinderbox) before it answers "ok".
- Loc and npc rows are filtered by my level: the client holds every plane's
  locs, and the Muttadiles' room one floor up can share the cell (ice26/ice47).
- Sweep (round 3): 46/48 — the two reds (ice1, ice6) solve the room, the storm is
  gone, and `cox_walk_on` stalls walking into the next room (6453,92 → 6448,112).
- Live lane, `icedemon0_p1` (embed client `torirs_cox_icerope_opt`): GREEN, 7/7, and
  its tick log matches scriptrun `icedemon0` exactly — 539 decision rows and 164
  hit/loc/retype rows identical, the live run one tick earlier throughout.

### Round 3 follow-up (2026-10-18): the overload's own hits

`ice.no_damage` read the room loop's tally of every hp drop, so the fight's
overload (five hits of 10, every 2 ticks from the tick after the sip) failed
it. `CI.self_damage` takes up to 10 a hit, five hits, inside 14 ticks of the
sip, off `S.taken` into `o.self` (a 9 is the 10 with the regen's +1; a boulder
on the same tick still counts); the summary prints `overload self-damage`.
The overload is not gated on the brews left: in raid6, poisoned and on the
last dose, the sip took the demon to 29 before the poison killed me; without
it the demon was still on about 92 (darts with Ranged drained: 48 damage in
175 throws).
The room tests now carry the `cox_raid_solo.lua` kit line for line
(antidote++, salve (ei), 5 Xeric's aid, 3 revitalisation, 3 enhance, no
stamina).

### Room lookup (2026-10-18, raid7)
`find_room` looks in this order: the cell I stand in, then `QD.COX.room_ahead`
of that cell, then each cell beside mine, probed at its tile nearest me, then
the `::coxtest` centre. The room's middle can lie outside the zones the client
holds: raid7 left me on the small scavenger room's way-out edge. The wrapper
`QD.raid.<room>_solve` finds the room before the room loop starts. If it finds
none, it returns `"no_room", why` for the relay to report, never an assert.
