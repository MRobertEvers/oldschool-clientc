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

What each entity **writes** — LostCity `PathingEntity.refreshZonePresence`'s
`blockWalk` switch, and `blockwalk`'s default is **npc**:

| `blockwalk` | writes |
|---|---|
| none | nothing |
| npc (default) | `NPC_OCC` |
| all | `NPC_OCC` + `BLOCK_NPC_AND_PLAYERS` |
| player | `PLAYER_OCC` |

What each mover **reads** (`blockWalkFlag()`), which is the other half and is
not the same set:

| mover | reads |
|---|---|
| player | `BLOCK_NPC_AND_PLAYERS`, and nothing else |
| npc, `moverestrict=blocked` | nothing at all |
| npc, otherwise | `BLOCK_NPC_AND_PLAYERS` + `NPC_OCC` (unless `blockwalk=none`) + `PLAYER_OCC` (unless `passthru`) |

The player row is the one that surprises: **an ordinary npc does not stop a
player**, because the default `blockwalk=npc` writes only `NPC_OCC` and the
player does not read it. Only `blockwalk=all` npcs block, and `NPC_OCC` exists
to keep npcs off each other. Reading `NPC_OCC` as a player here froze the mover
behind the first npc on its route, and stayed invisible for as long as every
scene rebuild threw the occupancy bits away before anyone could walk into them.

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

`COLL_FLAG_ROOF = 0x10000000` sits beside them — rsmod puts it at `0x80000000`,
which is the sign bit of the `int` this array holds, and nothing else needs the
top nibble.

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
| Symmetric PvP AP (`MOCK230_INTERACT_PLAYER`, `p_opplayer`) | `mock230_world.c`, `mock230_ops_player.c` |
| `CollisionType` strategies + `COLL_FLAG_ROOF` | `collision_map.c`, `mock230_scene.c` |
| Configurable BFS window (`route_window`) | `collision_map.c` |
| Occupancy re-stamp after a scene rebuild | `world_occupancy_restamp` |
| `los_symmetric_pvp`, `route_window_tiles` era flags | `features.h` / `features.c` |
| `SS_OP_LINEOFSIGHT`, hunt/`npc_find*` checkvis, `map_findsquare` modes | `mock230_scripts.c`, `mock230_ops_npc.c` |
| `blockwalk` / `blocksight` / `moverestrict` | `fields/npc.ini` + content/codec |

### 5.1 The routing window

The window is a property of the **router**, not of the loaded scene, and that is
the whole point of stating it separately: read the scene's size instead and a
route silently becomes a function of how much terrain happens to be resident.

- `CollisionMap.route_window` (tiles, centred on the mover; 0 = the whole map).
  Set with `collision_map_set_route_window`; `collision_flood` clips every
  expansion to it.
- A fresh map is 0, because that is literally what Client-TS does — its BFS is
  over the resident scene and has no window of its own.
- `ToriRS_FeatureTable.route_window_tiles` states the era's: 0 for `lostcity`,
  **128** for `osrs` / `server_routed` (rsmod / LostCity
  `PathFinder.DEFAULT_SEARCH_MAP_SIZE`). `mock230_scene_reset` applies it.

At `MOCK230_SCENE_TILES = 104` the two agree, because a map narrower than the
window is covered whole — so this is not observable today and only becomes so
when a map wider than 128 exists. `test_route_window` builds a 300-tile map to
exercise it: unwindowed reaches 200 tiles away, windowed reaches the window's
last column (`src + 63`) and no further.

### 5.2 `moverestrict` → `CollisionType`

Selected per step, from the npc's `moverestrict` (`npc_collision_type` in
`mock230_world.c`, LostCity `PathingEntity.getCollisionStrategy`), and applied by
`collision_can_move` — rsmod `CollisionStrategy.canMove`, branch for branch:

| `moverestrict` | `CollisionType` | rule |
|---|---|---|
| normal, nomove, passthru | `NORMAL` | the plain mask test |
| blocked | `BLOCKED` | `FLOOR` stops blocking and becomes **required** |
| indoors | `INDOORS` | normal, and the tile must carry `COLL_FLAG_ROOF` |
| outdoors | `OUTDOORS` | normal, and the tile must not |
| blocked_normal | `LINE_OF_SIGHT` | the wall/loc bits are read as their projectile twins (`<< 9`) — walks where a projectile could fly |

`nomove` has no strategy because it means "does not step", which every caller
answers before asking. `COLL_FLAG_ROOF` (bit 28) is stamped from the map square's
own `REMOVE_ROOF` tile setting in `mock230_scene.c`, which is the same bit the
reference reads (LostCity `GameMap`).

### 5.3 Non-gaps (closed, with the evidence)

- **Route-blocker tier.** Not deferred — *deleted upstream*. LostCity's
  `flags.ts` reclaimed bits 22–30 with the note that "the route-blocker
  subsystem was removed (it only ever fed `CollisionType.LINE_OF_SIGHT`, was
  never written — `changeLocCollision` hardcoded false)". `LINE_OF_SIGHT` is
  implemented here the way the reference implements it, by shifting into the
  projectile twins, so the tier has nothing left to carry. The era's actual
  `breakroutefinding` behaviour is the decoder collapse — opcode 74 sets
  `blocks_walk = blocks_projectiles = 0` (`dat2_config_loc.c`), matching
  `Client-TS/src/config/LocType.ts`.
- **Scene rebuild dropping occupancy.** `mock230_scene_build` re-reads the map
  squares, so the flags it returns describe terrain and locs and nothing else.
  `world_occupancy_restamp` re-stamps every live npc and player after the
  rebuild; without it the first tick after a re-centre lets entities walk
  through each other until each happens to move.

  Landing it exposed the read-side bug in §4 above (the player was reading
  `NPC_OCC`), because until occupancy survived a rebuild there was nothing on
  the map for the wrong read to trip over. Fixing both took the mock server's
  selftest from 30 failures to 4 — the 20 the re-stamp had turned up plus 6 that
  were already there, all of them "the walk did not complete".
- **Symmetric PvP LoS had no caller.** It has one now:
  `MOCK230_INTERACT_PLAYER`, reached from `p_opplayer` (`mock230_ops_player.c`)
  against the secondary active player, whose AP rung calls
  `mock230_scene_approached_pvp`. Rev 230 assigns no OPPLAYER wire opcode, so a
  click still cannot start one — content with two players in hand can.

---

## 6. Tests

`make -C src test-world`:

- `test_line_of_sight` — same tile, open lines, plain vs proj wall, LOC blind /
  dest strip, approached overlap refuse.
- `test_line_of_sight_asymmetry` — pins `los(a→b) != los(b→a)` (or both
  blocked); symmetric AND for the modern PvP construction, and that
  `collision_map_approached_symmetric` reads the same in both orders and equals
  the AND of the two one-way casts.
- `test_route_window` — the window is the router's, not the map's: a 300-tile
  map, whole-map flood vs the 128 window's last reachable column.
- `test_collision_types` — one tile per `CollisionType` branch that only that
  branch accepts, plus the same through `can_travel_typed` and the roof stamp.
- `test_naive_path_safespot` — axis-align stop; BFS finds a route naive does
  not walk through a blocking column.
- `test_occupancy_stacking` — extra_flag refuses the step; flood ignores
  occupancy; unconditional clear.
- `test_follow_dance_semantics` — west seed; mutual-follow lag corridor.
- `test_features_eras` — `los_symmetric_pvp` and `route_window_tiles` per era.

`./src/build/mock230 --selftest`:

- *an ordinary npc does not block a player* — the §4 read/write asymmetry, put
  against the two extra masks rather than against a walk (a walk passes for the
  wrong reason as soon as its route goes round): a `blockwalk=npc` npc beside
  the player stops another npc and not the player, and turning it into
  `blockwalk=all` stops the player too.

Two npc stanzas still fail and are **stale, not broken**: *"an npc whose
straight step is blocked still moves"* / *"walks round to …"* and playerfollow's
*"and reach the player"* were written when npcs had the flood, and §1.2 is the
reason they cannot hold — a naive mover whose straight step is refused does not
step at all, which is what safespotting *is*.
