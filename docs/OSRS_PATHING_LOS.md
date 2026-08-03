# OSRS pathing and line of sight

How Old School RuneScape paths players vs NPCs, how line of sight works, and
what this engine implements. Cross-links:
[`PATHING_INTERACTION_PARITY.md`](PATHING_INTERACTION_PARITY.md) (click → route),
[`PORTING_GUIDE.md`](PORTING_GUIDE.md) §2.3 (engine owns pathfinding / modes /
hunt mechanism).

Written 2026-08-03 from LostCity's vendored rsmod routefinder, the OSRS Wiki,
osrs-docs, and community reverse-engineering (Henke / Icy001). Re-measure
rather than trusting prose counts.

---

## 1. Two pathfinders

OSRS has exactly two route finders. Which one an entity may call is the whole
player-vs-NPC difference — not a sophistication that scales with size.

### 1.1 Intelligent pathfinder (BFS) — players

- Breadth-first search over a **128×128** window centred on the mover.
- Neighbour expansion order: **West, East, South, North, SW, SE, NW, NE**.
- Arrival via shape-keyed `ReachStrategy`.
- Path reduced to at most **25 corner "checkpoint" tiles**.
- On failure: scan a **21×21** box around the destination; among tiles with
  BFS distance `< 100`, pick the least squared distance to the destination
  rect.
- Destination SW tile must lie inside a **101×101** box around the source or
  pathing is not attempted.

Between checkpoints the player walks in **"follow mode"** — the same naive
step as NPCs. Normally that is a straight line; it shows when the map changes
mid-walk (a gate opens) or the player is displaced.

References: OSRS Wiki
[Pathfinding](https://oldschool.runescape.wiki/w/Pathfinding);
`LostCity_Server/engine/src/engine/routefinder/PathFinder.ts`.

### 1.2 Naive / "dumb" pathfinder — all NPCs

One destination tile per call, then a greedy walk: **diagonal first, else
X-only, else Z-only, else stop**. No obstacle avoidance beyond that — NPCs
*slide along* walls. Used for chasing, wander, and patrol.

`naiveDestination()` bisects the plane with
`diagonal = (srcX-destX)+(srcZ-destZ)` and `anti = (srcX-destX)-(srcZ-destZ)`,
picks a cardinal side of the target, clamps an offset along that side, and
returns **nothing** when the source sits exactly on a corner (authentic empty
path / dead tick). The greedy loop is
`while (currX != destX && currZ != destZ)` — it stops when **either** axis
aligns, so a pure diagonal request often ends one tile short.

Community consensus
([rune-server](https://rune-server.org/threads/npc-pathing.687888/)): *"None of
them use smart pathfinding… Private servers often use smart pathfinders cause
it's the easiest way out of safespots; it is not the way RS does things."*
**Safespotting is the naive pathfinder working.** Giving NPCs a BFS is the
single most common emulator divergence; this tree did that until the rewrite
below.

Wander also uses the dumb pathfinder
([osrs-docs random-walk](https://osrs-docs.com/docs/mechanics/random-walk/)).

Reference: `LostCity_Server/engine/src/engine/routefinder/NaivePathFinder.ts`.

### 1.3 Per-tick step (`takeStep`)

`PathingEntity.takeStep` (`PathingEntity.ts:633-679`): face the current
waypoint; try the full diagonal **only when `width === 1`**; else E/W; else
N/S; else return `[0,0]` **keeping the waypoint** (a temporary actor block is
why the reference retains it —
[JagexAsh](https://x.com/JagexAsh/status/1727609489954664502)).

---

## 2. Line of sight

### 2.1 The ray cast (shared by both eras)

Fixed-point Bresenham over tile flags — LostCity
`LineValidator.rayCastLine` / rsmod / this tree's `collision_ray_cast`:

1. Clamp each entity to its nearest tile with
   `coordinate(a,b,size) = a>=b ? a : (a+size-1<=b ? a+size-1 : b)`
   (closest tile of a multi-tile NPC — henke18's BA/Inferno tool).
2. `start == end` → true.
3. **Sight only:** start tile with `LOC` → false (standing in an object blinds
   you). No matching destination check — one asymmetry source.
4. Major axis = larger `|delta|`; **ties go to the Z-major branch**.
5. Minor axis in 16.16 fixed point: `HALF_TILE = 0x8000`, seed
   `scaleUp(start) + HALF_TILE + offset` (direction-dependent),
   `tangent = (scaleUp(deltaMinor) / absDeltaMajor) | 0`. Truncating divide +
   offset are the other asymmetry sources.
6. Per step, test the *entering* edge mask
   (`xFlags = travelEast ? blockedWest : blockedEast`). On a minor-axis
   boundary crossing, also test the new tile against the minor mask.
7. **Sight only:** on the final destination tile, strip `LOC_PROJ_BLOCKER` —
   shoot *at* an object, not *through* it.

Sight masks: `SIGHT_BLOCKED_{N,E,S,W} = LOC_PROJ_BLOCKER | WALL_*_PROJ` =
`0x20400 / 0x21000 / 0x24000 / 0x30000`. Walk masks reuse
`COLL_FLAG_BLOCK_*` (`WALK_BLOCKED_NORTH == BLOCK_SOUTH`, etc.).

### 2.2 What "modern" LoS actually means

LostCity's routefinder is a **verbatim rsmod port** — the same asymmetric ray
live OSRS uses for PvM. The era split is the **2019 symmetry change**, and it
is narrower than "modern vs LostCity":

- Pre-2019 / PvM today: LoS is **asymmetric** (A can range B while B cannot
  range back). Jagex stated it in the
  [Line-of-Sight Beta](https://oldschool.runescape.wiki/w/Line-of-Sight_Beta);
  henke18 measured it in 2018.
- 29 Aug 2019
  ([Last Man Standing update](https://oldschool.runescape.wiki/w/Update:Last_Man_Standing_(2019))):
  *"The algorithm that calculates line-of-sight to be symmetrical has now been
  applied to **PvP only**… **This does not apply to PvM.**"* That update is
  what the linked video covers (credits Henke / Icy001).
- NPCs deliberately cast LoS **backwards** (from the player to themselves) —
  `PathingEntity.ts:431-434`; henke18 independently: *"When an Npc targets a
  Player, it checks sight starting from the Player, not from itself."*

**Honest gap.** No public reverse-engineering of the exact 2019 symmetric
*construction* exists. This tree implements symmetry as
`los(a→b) && los(b→a)`, which satisfies the stated guarantee. Behavioural
match, not a verified algorithmic one. Selected by
`ToriRS_FeatureTable.los_symmetric_pvp` (0 = lostcity, 1 = osrs /
server_routed) and read **only** in the player-vs-player AP gate.

### 2.3 Where LoS is required

| Check | LoS? |
|---|---|
| OP / operable (`reached*`) | No |
| AP / approach (`isApproached`, default range 10) | Yes (+ `BLOCK_NPC_AND_PLAYERS`) |
| Hunt / `npc_find*` / `map_findsquare` with `checkvis` | Optional (0=off, 1=sight, 2=walk) |
| Script `lineofsight` / `lineofwalk` | Yes / walk ray |

Overlapping footprints are never in AP range. `aprange(-1)` demotes AP to OP.

---

## 3. The follow "dance"

Following walks to the target's **previous** tile, not its current one:

1. Each entity records `last_step_x/z` = the tile it occupied before this
   tick's step *attempt* (including a blocked one).
2. At the top of the follower's turn, `follow_x/z` is snapshotted from the
   target's `last_step_*` **before** movement.
3. Spawn / login / teleport seeds `last_step = (x-1, z)` (west).
4. Tick order: NPCs phase 4, players phase 5 — within a tick the NPC sees the
   player current; the player sees the NPC stale.

Two mutual followers each chase where the other just was → the circular dance
(Wiki Ver Sinhaza illustration;
[rune-server](https://rune-server.org/threads/help-with-player-dancing-spinning-when-following-each-other.706121/)).

Adjacent behaviours:

- **Chase lag.** An NPC adjacent at tick start may not be after the player
  moves (NPCs move first) → intermittent attacks on a moving player.
- **Step-out shuffle.** Footprint intersect → `randomWalk()` (one random
  cardinal) instead of pathing.

**Getting stuck is correct.** Beyond diagonal→X→Z there is no recovery; the
waypoint is kept. Escapes are per-mode stuck counters: wander `>500` →
teleport spawn, patrol `≥32` → teleport waypoint, playerescape `≥5` → reset
defaults.

---

## 4. Entity occupancy

[osrs-docs entity-collision](https://osrs-docs.com/docs/mechanics/entity-collision/):
`Player`, `NPC`, `Projectile` (blocks LoS), `Full` (blocks movement).

- Set on every tile under the entity on spawn/login; moved tile-by-tile
  (both tiles when running); cleared on despawn/logout.
- Clearing is **unconditional** — that is why NPC stacking works.
- Occupancy gates the **step**, never the flood/route. A blocked NPC keeps
  its stale path and resumes when you move aside.
- LoS-blocking NPCs (Pest Control Brawlers, Inferno/Colosseum stacks) set the
  projectile occupancy flag.

LostCity: `BlockWalk.{NPC,ALL,PLAYER}` → `NPC_OCC` / `BLOCK_NPC_AND_PLAYERS` /
`PLAYER_OCC`.

### 4.1 Flag-layout warning

This repo's `COLL_FLAG_*` come from the 2004 client and **disagree with rsmod
above bit 17**:

| | this repo | rsmod / LostCity |
|---|---|---|
| antimacro / npc_occ | `ANTIMACRO = 0x80000` | `NPC_OCC = 0x80000` |
| floor blocked | `0x280000` | `0x240000` |
| walk blocked | `0x280100` | `0x240100` |

**Do not copy rsmod's numeric constants.** Bits below 18 (walls + projectile
blockers, and therefore every sight mask) *are* identical.

Runtime occupancy lives at **bit 24+** so `COLL_FLAG_BOUNDS (0xffffff)` never
swallows them:

- `COLL_FLAG_NPC_OCC = 0x1000000`
- `COLL_FLAG_PLAYER_OCC = 0x2000000`
- `COLL_FLAG_BLOCK_NPC_AND_PLAYERS = 0x4000000`
- `COLL_FLAG_PROJ_BLOCK_ENTITY = 0x8000000`

---

## 5. What this tree implements

| Piece | Where |
|---|---|
| Ray cast + LoS / LoW / approached | `collision_map.c` |
| Occupancy `change_square` + `can_travel` + naive path | `collision_map.c` |
| Scene world-coord wrappers + `checkvis` | `mock230_scene.c` |
| Naive NPC movement (no flood), waypoints, stuck counters | `mock230_world.c` |
| `last_step` / `follow`, occupancy on step/spawn/death | `mock230_world.c`, `mock230_combat.c` |
| AP LoS gate (npc casts backwards) | `mock230_world_process_interaction` |
| `los_symmetric_pvp` era flag | `features.h` / `features.c` |
| `SS_OP_LINEOFSIGHT`, hunt/`npc_find*` checkvis, `map_findsquare` modes | `mock230_scripts.c`, `mock230_ops_npc.c` |
| `blockwalk` / `blocksight` / `moverestrict` | `fields/npc.ini` + content/codec |

### 5.1 Recorded divergences (deliberately open)

1. **Player BFS window is 104, not 128.** Scene is
   `MOCK230_SCENE_TILES = 104` with rebuild margin 16. A 128×128 window centred
   on the mover does not fit. Closing it means decoupling the routing window
   from the scene — separate structural work; only reachable on very long
   routes.
2. **Route-blocker tier (`breakroutefinding`)** — modern rsmod bits 22–30;
   deleted from LostCity. Needs nine bits and would force `flags` to
   `uint32_t*`. Recorded, deferred.
3. **Scene rebuild** currently rebuilds loc collision only; entity occupancy
   must be re-stamped after rebuild (gap noted at landing).
4. **`moverestrict` beyond nomove/passthru** is stored; full
   `CollisionType` strategies (indoors/outdoors/blocked/line_of_sight) are not
   yet selected per step.

---

## 6. Tests

`make -C src test-world`:

- `test_line_of_sight` — same tile, open lines, plain vs proj wall, LOC blind /
  dest strip, approached overlap refuse.
- `test_line_of_sight_asymmetry` — pins `los(a→b) != los(b→a)` (or both
  blocked); symmetric AND for the modern PvP construction.
- `test_naive_path_safespot` — axis-align stop; BFS finds a route naive does
  not walk through a blocking column.
- `test_occupancy_stacking` — extra_flag refuses the step; flood ignores
  occupancy; unconditional clear.
- `test_follow_dance_semantics` — west seed; mutual-follow lag corridor.
- `test_features_eras` — `los_symmetric_pvp` 0/1 per era.
