# The solo Muttadiles solver: plan (2026-10-18)

Target file: `script/plugins/quest_driver/cox2_muttadiles.lua`, a `QD.T2` room
(`cox_muttadiles`) on the v2 stack, shaped like `cox2_olm.lua`. Test:
`test/raids/cox_muttadiles.lua`, entered with `::coxtest muttadiles <n>` (root,
landed 2026-10-18: builds the n-th raid holding the room, clears the room
before, stands me past the room's entry Passage). Spec and every number:
`muttadiles_spec.md`. No solver code until this plan is read against it.

## What a strong solo human does (the strategy the bot plays)

Synq (0:42:50-0:54:44), ThePerfectG, Tubby, Gnomonkey and the wiki Strategies
agree on the **chop method**; the ZGS freeze method is a speedrun gamble and
the We Do Raids kit carries a BGS, not a ZGS.

1. **Entry.** Pass the tendrils once (the first pass is free; never pass back).
   Pray Protect from Missiles on entry (Melee if the small one is already at the
   entrance, PG 0:01:06).
2. **Chop.** Walk to the tree and Cut. Between rolls, attack the small one only
   while it is above ~55% ("damage the Mutated Dawns up to about 55% HP while
   the meat tree is still active", Synq; WS "until it reaches 55%"); with the
   blowpipe, two throws after each Woodcutting XP drop, then re-click the tree
   (Synq 0:45:01-0:46:06). Being hit does not stop chopping. Eat/drink right
   after an XP drop and re-click the tree (Synq 0:53:03).
   Prayer: Melee while the small one is in melee reach, Missiles otherwise
   (Synq, Tubby, PG); Magic only when the small one is far and the water's
   magic is the only threat (Synq 0:44:27) -- the submerged magic is the minor
   term (max 23 x 1/3 every 4 ticks, 33% through prayer).
3. **Small one.** Tree down: kill it with the blowpipe (low defence; Synq,
   WS). Keep it in melee reach and pray Melee ("constantly kite under the
   Mutated Dawn so you're always within melee distance ... 100% protection",
   Synq 0:47:00); if it walks off, Missiles. Its melee is fully blocked; its
   spit leaks 33%.
4. **Surface.** When it dies: pick up the overload and prayer enhance it drops
   (sip the overload: the kit's own are kept for Olm), then go to a safespot
   BEFORE the large one finishes surfacing (retype 3 ticks after the death,
   first swing ~7 later).
5. **Large one.** From outside melee reach (a safespot where the 5x5 cannot
   reach an edge-adjacent tile, or simply range), pray Missiles; on the first
   magic of a run switch to Magic for the 2nd and 3rd, then back to Missiles
   ("You only want to pray magic for the second and third magic attack",
   Synq 0:48:38). Kill with the crossbow (ruby bolts) or blowpipe (no tbow in
   the kit). Never stand in melee reach: the stomp (72, through Protect from
   Melee) is the room's killer. If pinned, pray Melee and run through it
   (PG 0:03:50).
6. **Loot.** Pick up the large one's four potions (overload, prayer enhance,
   Xeric's aid, revitalisation): they are the Olm budget.

## Perception (only what the client is sent; verbs that exist)

| fact | verb |
|---|---|
| both Muttadiles: tile, size (3 / 5), hp bar, target | `api_drive.npcs` rows: `server_x/z`, `size`, `health_ratio/scale`, `facing_entity` |
| which attack and when | `npcs` `seq_id`/`seq_tick`: 7420 bite, 7421 spit, 7422 magic, 7424 stomp, 7423 emerge, 7426 death (key on the tick FIRST SEEN, not `seq_tick`) |
| what lands when | `api_drive.projectiles`: 1291 spit, 393 magic (`target`, `cycles_left`); the landing tick = first seen + round(flight/30) |
| the water's sweep at me | `projectiles` 393 from the water + my own spotanim 157 (players row) |
| meal in progress | the npc stops facing me and walks to the tree; its hp bar rises at the bites (npc_heal is not sent; the bar is) |
| the tree's hp, dead tree | tree npc 7564 row `health_ratio`; loc 30013 appearing (`api_drive.locs`) |
| my chop rolls | `api_drive.skill` woodcutting xp (the XP drop); the tree's hitsplat (`hit_damage`/`hit_cycle`) |
| my hp / prayer / overload | `skill`, `varbit` prayers (client-sent), inventory dose counts |
| line of sight for a safespot | `api_drive.server_los` / `QD.world.los` |
| drops | `api_drive.objs` |

### MISSING verbs (engine work, before the solver)

1. **A sized npc chaser in the planner.** `B.chaser` is a 1x1 tornado
   (`tob2_plan.lua:441`). The room needs a 3x3 and a 5x5 chaser that steps by
   the OSRS npc rule (diagonal then axis, blocked = stuck; south-west-tile
   aligned, Synq 0:49:44 "NPCs are based off of their southwest tile") and
   prices "edge-adjacent to the footprint at step k" (melee reach; the stomp
   reaches every adjacent raider). Without it the plan cannot see a safespot.
2. **Collision flags for a footprint** (or a `npc_reach(slot, x, z, horizon)`
   in C answering "can this npc's naive path put me in its melee reach within h
   ticks"). There is no collision read in the verb list today; `route` is the
   player BFS only. Safespots depend on the turned room (F1's rotations), so
   they must be computed, not tabled.
3. Nothing else: every tell in the spec is already a packet the lanes record.

## PREDICT: the threat field

| threat | term |
|---|---|
| small bite | my tile edge-adjacent to its footprint at the swing tick (its last swing seq_tick + 4, first seen): 3/4 x E[bite] unless Melee is on at the swing (prayer pressed after seeing d covers d+1) |
| small spit | otherwise in range 6: E[spit] x (1 or 0.33 under Missiles); lands at swing + round((43+5d)/30) -- an eat between swing and landing counts (tick-eat) |
| submerged magic | every 4 ticks while the small one lives, if the water sees me: 1/3 x E[magic] x (1 or 0.33) |
| large stomp | a mark on every tile edge-adjacent to its footprint at its swing tick: 1/2 x 1/2 x E[72-roll], unprayable, lands +1 (forbid in mode `large` unless pinned) |
| large bite | 1/2 x 1/2 x E[bite], 0 under Melee |
| large thrown | the run's style: E[45] or E[23] x (1 or 0.33); the run length is known (3) and the coin after it is 1/2 |
| meal | not a threat: a worth term (a meal undoes 62 hp a bite of my damage) |

## STRATEGY: the mode machine

`enter -> chop -> small -> loot_small -> safespot -> large -> loot -> done`

- **chop**: target the tree (`press` Cut) every roll; the small one is a target
  worth `value` only while its hp > 55% of max (never below: a meal costs
  2 x 62); prayer per the small one's reach at its next swing; eat after an XP
  drop. Leaves on loc 30013 / the tree row gone.
- **small**: blowpipe; stay edge-adjacent (Melee), else Missiles. If it walks to
  the tree anyway (tree down, so it cannot), nothing.
- **loot_small**: overload + prayer enhance off the ground (one tick each),
  sip the overload if the kit's timer is out.
- **safespot**: the tile chosen by the sized-chaser search (missing verb 1/2):
  outside every reachable edge-adjacent tile of the surfaced 5x5 for the
  planner horizon, in its line of sight for my weapon range.
- **large**: crossbow/blowpipe from the safespot; Missiles by default, Magic
  for the 2nd-3rd of a magic run; if the chaser model says it reaches me,
  re-plan to the next safespot (the run-through is the last resort).
- **loot**: four potions.

## Kit

The Olm kit (`test/raids/cox_olm_solo.lua`): We Do Raids minimum + the wiki
minimum's open slots; supplies the raid's (+) potions. This room needs beyond it:

- **an axe** (any: type does not matter, AshT). The room's own bronze axe spawn
  (26 Jun 2024) has no known tile -- bring one (`bronze_axe`).
- the blowpipe and the dragon crossbow are already in the kit (small: blowpipe;
  large: crossbow, the wiki's "high defence" pick; no twisted bow).
- Xeric's aid as the only healing (as Olm): the room is "one of the most
  punishing ... use more of your supplies than any other room" (PG). Budget it
  in the resource layer: ~6 doses here is the expected spend; the large one
  refunds one 4-dose aid.

## Probes the first run prints (facts the spec cannot settle)

1. Small spit / bite ratio in reach (expect 1/4 spits) and the large's melee
   ratio (1/2) -- `npc_anim` rows by tile adjacency.
2. Style runs: every run >= 3; the coin's split (want: compare with Tubby's
   "usually switches back to ranged").
3. Landing offsets per distance (spit +2/+3, magic +3/+4, stomp +1) against the
   solver's prediction -- the monitor's `hit` rule.
4. Does the large one find the safespot's corner (naive pathing) -- the
   chaser model vs the measured `npc_tile` path.
5. Damage taken per source (hit_player by npc_type) per seed.

## PASS (written before any sweep)

On 48 seeds of `::coxtest muttadiles`:

- the player alive and both Muttadiles dead;
- **0 hp healed from the meat tree** (the Mutta-diet CA's condition; the chop
  method guarantees it if the small one is held above 50% until the tree
  falls) -- counted from the bars on the client, cross-checked by `npc_heal`
  rows labelled `cox_mutta_meal_tick` in the tick log;
- **0 stomps landed** on the player (an avoidable hit) and 0 small bites taken
  off Protect from Melee while it was in reach before the swing;
- 0 script aborts in stdout;
- reported, not gated: room ticks, Xeric's aid doses spent.

Then one live seed with an identical tick log (`tools/raid_gate/run.py`).

## Round 2 (2026-10-18): what was built, and the sweep

`script/plugins/quest_driver/cox2_muttadiles.lua` (`QD.COXMU`; `QD.COXM` is the
mystics'), `test/raids/cox_muttadiles.lua`. As built, against the plan above:

- **enter** (before the loop): Rapid; the room from the `::coxtest` centre's
  instance zone (or a Muttadile row); walk to the tile in front of the first
  tendril; Pass; wait until my template row is past the tendrils.
- **chop** -> **small** -> **surface** -> **large** -> **out**, by what the
  client shows: the tree npc gone, the small one's row gone, the large one's
  row present, her row gone. `done`: she is dead and the exit crystal is gone.
- **Prayer** from the swing tells (seq 7420/7421/7422/7424, keyed on the seq's
  start stamp, timed on the tick first seen): Missiles; Melee when the small
  one's footprint is within a tile; Magic for the 2nd and 3rd of a large
  one's magic run. No projectile reads were needed.
- **PREDICT** is one term: her footprint + 2 tiles forbidden over the horizon
  (the stomp). Damage terms (spits, the water's magic) are not priced yet: the
  monitor's `threat` rule counts them as unpredicted.
- The sized-chaser engine gap is planned round by the forbid box (her
  footprint + 2) and range (crossbow 7 against her 6): no safespot search.

Sweep, scriptrun, `--name mutta<N>` = `::coxtest 12 <N>`:

| | result |
|---|---|
| seed 0 probe | PASS after three content fixes (chopping, emergence, kit Woodcutting) |
| 8 seeds | 8/8 (after the exit check stopped relying on the next room's walk) |
| 48 seeds | **44/48**; the 4 failures (n = 44..47) are `::coxtest` itself exceeding 500,000 instructions in `cox_place_room_turned` (root) |
| zeros | 0 meal bites, 0 stomps in reach, 0 script aborts in 44/44 rooms |
| per room | 176 ticks average (max 213); damage taken: small 45, water 31, large 84 (plus the overload's 50) |

## Round 3 (2026-10-18)

- Shared helpers back (`cox_test_enter`, `cox_walk_on`); the solver finds its
  room itself (my cell or the four beside it, by instance zone) and takes no
  `::coxtest` answer. **48/48**, ~160 damage and 176 ticks a room.
- No axe (the whole raid's kit): the solver skips the tree; the room still
  ends (4/4 probes) but both eat (12 bites, ~350 damage). The raid needs an
  axe for the Mutta-diet zero (a scavenger's iron axe, or the room's bronze
  axe spawn once its tile is known).
- Pricing the spits and the water's magic in the threat field did not lower
  damage (158 -> 168 a room, 48/48 both): reverted.
- Live seed muttadiles0: PASS. Not tick-identical to scriptrun: the live run
  starts a tick earlier (constant), and the raid's RNG already differs at the
  `::coxtest` build (loc 29738 vs 29736 at 6473,82 L1, another room), so every
  roll after differs (tree chop 42 vs 45 at the same tick).
