# OSRS pathing and line of sight

How Old School RuneScape paths players vs NPCs, how line of sight works, how
this engine decides, and where the named references agree or diverge.

Cross-links: [`COLLISION_MAP.md`](COLLISION_MAP.md) (wall flag set/check and
one-way orphan failure mode), [`PATHING_INTERACTION_PARITY.md`](PATHING_INTERACTION_PARITY.md)
(click → route), [`ASH_MOVEMENT_CORPUS.md`](ASH_MOVEMENT_CORPUS.md) (Mod Ash
verbatim), [`PORTING_GUIDE.md`](PORTING_GUIDE.md) §2.3 / §8.

Written 2026-08-03, rewritten the same day for the pathing-parity rework.
Re-measure rather than trusting prose counts.

---

## 0. Evidence hierarchy

When sources disagree, prefer them in this order:

1. **Mod Ash (JagexAsh)** — content-side behaviour Jagex states in public.
   Corpus: [`ASH_MOVEMENT_CORPUS.md`](ASH_MOVEMENT_CORPUS.md). Ash cannot read
   the engine's pathfinding code and says so; treat his "I can't see under the
   hood" answers as a hard stop, not an invitation to invent.
2. **Convergent reverse-engineering** — when rsmod, LostCity's vendored
   routefinder, and Kronos's decompiled-client BFS agree byte-for-byte on a
   constant or algorithm, that agreement is the strongest available evidence
   for the parts Ash cannot see. The OSRS Wiki [Pathfinding](https://oldschool.runescape.wiki/w/Pathfinding)
   article matches that convergence.
3. **Official Jagex patch notes / polls** — e.g. the 17 Dec 2013
   "routefinding moved to the server" note, the 2019 LoS LMS update.
4. **Named reference servers** for *how they wire the same library into a
   game loop* (op/ap processors, click handlers). Prefer rsmod/OpenRune for
   modern OSRS; LostCity for the 2004 client-routefinder era; Kronos as a
   corroborating 184-era port.
5. **Private-server heuristics that invent their own reach model** (XRSPS's
   `deriveLocRouteProfile` keyed off `clipType` / action list) are **not**
   authorities. This tree briefly copied one; that was the bug.

---

## 1. The decision

**Modern OSRS (this tree's `osrs` era) is server-authoritative, shape-keyed
reach over a shared rsmod-family BFS.** Concretely:

| Question | Answer | Authority |
|---|---|---|
| Who paths? | The **server**. Client sends target identity only. | Ash [1132253909869891584](https://x.com/JagexAsh/status/1132253909869891584), [1143272005841674242](https://x.com/JagexAsh/status/1143272005841674242); Patch Notes 17 Dec 2013 |
| How many algorithms? | **One** global BFS for players; naive/greedy for NPCs. | Ash [466156765550497792](https://x.com/JagexAsh/status/466156765550497792), [480295033766416384](https://x.com/JagexAsh/status/480295033766416384), [573892810312548352](https://x.com/JagexAsh/status/573892810312548352) |
| How is "close enough" decided? | **By the placed loc's shape** via `exitStrategy` (wall / wall-decor / rectangle / exclusive-rectangle). | Ash [872866346383683585](https://x.com/JagexAsh/status/872866346383683585); rsmod / LostCity / Kronos identical |
| Adjacent includes diagonals? | **No** (melee / op). | Ash [1659582689119174658](https://x.com/JagexAsh/status/1659582689119174658) |
| `forceapproach`? | Cardinal sides relative to the loc's orientation, rotated by angle. | Ash [1640686954256769026](https://x.com/JagexAsh/status/1640686954256769026) |
| `breakroutefinding`? | Path *through* the loc toward the interaction; stop at reach. | Ash [1443150721734660096](https://x.com/JagexAsh/status/1443150721734660096) |
| AP range cap? | **10** tiles + LoS. | Ash [1541484420347076614](https://x.com/JagexAsh/status/1541484420347076614) |
| Run / NPC speed? | Player run 2 tiles/tick, walk 1; NPCs 1 (preset routes can double). | Ash [1018589293692977152](https://x.com/JagexAsh/status/1018589293692977152), [1179011519830462464](https://x.com/JagexAsh/status/1179011519830462464) |
| Follow? | Deliberately different routing so the dance works. | Ash [1227166364366000129](https://x.com/JagexAsh/status/1227166364366000129) |

The 2004 / LostCity era keeps **client-side BFS + legacy shape tests** —
correct for that generation, selected by `ToriRS_FeatureTable`.

### 1.1 Why the previous model was wrong

Under `approach_model = RECT`, loc approach discarded the placed shape and
picked a strategy from `blocks_walk` / has-an-action / is-floor-decor — a
copy of XRSPS's `deriveLocRouteProfile`. Every wall, door, ladder and
wall-decor (shapes 0–3, 4–8, 9) fell into a generic 1×1 adjacent-with-overlap
test. That is the opposite of Ash's "awkward for wall-shaped pieces" and of
every named reference's `exitStrategy(shape)`.

Secondary errors that rode with it:

- Exact-tile shortcut deleted for RECT, citing "rsmod has no such test" —
  **false**. rsmod and LostCity both open `ReachStrategy.reached` with
  `if (exitStrategy != RECTANGLE_EXCLUSIVE && srcX == destX && srcZ == destZ) return true`.
- `collision_test_rect_adjacent` read **both** tiles' wall bits; rsmod's
  `reachRectangle1` reads only the **source** tile plus `blockAccessFlags`.
- No exclusive-rectangle kind for NPCs/players (shape `-2`).
- Client still pathfound and emitted `MOVE_OPCLICK` — a packet rev 230/239
  does not have. Modern `MOVE_GAMECLICK` is a fixed 5-byte destination.

---

## 2. Two pathfinders

OSRS has exactly two route finders. Which one an entity may call is the whole
player-vs-NPC difference — not a sophistication that scales with size.

### 2.1 Intelligent pathfinder (BFS) — players

- Breadth-first search over a **128×128** window centred on the mover
  (source at local `(64,64)` — 64 tiles west/south, 63 east/north).
- Neighbour expansion order: **West, East, South, North, SW, SE, NW, NE**.
- Arrival via shape-keyed `ReachStrategy` (not coordinate equality).
- Path reduced to at most **25 corner "checkpoint" tiles**.
- On failure (`moveNear`): scan a **21×21** (±10) box around the destination;
  among tiles with BFS distance `< 100`, pick the least squared distance to
  the destination rect (ties → shorter BFS distance).
- Ring buffer capacity **4096**.

Between checkpoints the player walks in **"follow mode"** — the same naive
step as NPCs.

The Wiki's **101×101** "destination must be within this box or pathing is not
attempted" figure is uncorroborated by any reference implementation; this tree
does not implement it.

References: OSRS Wiki
[Pathfinding](https://oldschool.runescape.wiki/w/Pathfinding);
rsmod `RouteFinding.kt`; LostCity `PathFinder.ts`; Kronos `RouteFinder.java`.

### 2.2 Naive / "dumb" pathfinder — all NPCs

One destination tile per call, then a greedy walk: **diagonal first, else
X-only, else Z-only, else stop**. No obstacle avoidance beyond that — NPCs
*slide along* walls. Used for chasing, wander, and patrol.

`naiveDestination()` bisects the plane with
`diagonal = (srcX-destX)+(srcZ-destZ)` and `anti = (srcX-destX)-(srcZ-destZ)`,
picks a cardinal side of the target, clamps an offset along that side, and
returns **nothing** when the source sits exactly on a corner (authentic empty
path / dead tick).

**Safespotting is the naive pathfinder working.** Ash repeatedly refuses to
make NPC routing "smarter" because it would break safespots
([573892810312548352](https://x.com/JagexAsh/status/573892810312548352)).
A few named NPCs (Red Flag minotaurs, Balance elemental, thralls post-2024)
are exceptions that content opts into; the default is still naive.

Reference: LostCity `NaivePathFinder.ts`; rsmod `RouteFinding.naiveDestination`;
Kronos `DumbRoute`.

### 2.3 Per-tick step (`takeStep`)

Face the current waypoint; try the full diagonal **only when `width === 1`**;
else E/W; else N/S; else return `[0,0]` **keeping the waypoint** (a temporary
actor block is why the reference retains it —
[JagexAsh](https://x.com/JagexAsh/status/1727609489954664502)).

### 2.4 Reach strategies (`exitStrategy`)

| `locShape` | Strategy | Notes |
|---|---|---|
| **-2** | exclusive rectangle | NPCs / players — adjacent, must **not** overlap |
| **-1** | none | plain ground destination (exact tile only) |
| **0–3, 9** | wall | shape+angle case matrix; source-tile wall flags |
| **4–8** | wall decoration | shape+angle case matrix |
| **10, 11, 22** | rectangle | overlap **or** cardinal-adjacent; `forceapproach` vetoes sides |

`ReachStrategy.reached` opens with the exact-tile shortcut for every strategy
**except** exclusive-rectangle. Adjacent excludes diagonals. Rectangle
adjacency reads the **source** tile's facing wall bit plus rotated
`blockAccessFlags` / `forceapproach`.

---

## 3. Per-reference comparison

### 3.1 Algorithm constants (all three agree)

| Constant | rsmod / OpenRune | LostCity | Kronos |
|---|---|---|---|
| Search window | 128×128 | 128×128 | 128×128 |
| Ring buffer | 4096 | 4096 | 4096 |
| Max waypoints | 25 | 25 | 50 stored / BFS same |
| Alt-route radius | ±10 | ±10 | ±10 |
| Alt-route `dist <` | 100 | 100 | 100 |
| Expansion order | W E S N SW SE NW NE | same | same |
| Reach termination | `ReachStrategy.reached` | same | `RouteType.method4274` |

### 3.2 Who paths, and when

| | rsmod (full) | OpenRune | LostCity (default) | Kronos |
|---|---|---|---|---|
| Click packet | target only | target only | waypoints (client BFS) | target only |
| Server paths | yes, every click | yes, once at click | re-validates client path | yes |
| Re-path moving NPC | every tick when ≤1 waypoint left | broken / none | every tick at last waypoint (full path) | every tick (`TargetRoute`) |
| op/ap processor | full | not implemented | full (`tryInteract`) | distance + LOS only |

### 3.3 Reach / approach

| | rsmod / LostCity / Kronos | XRSPS (rejected) |
|---|---|---|
| Dispatch key | **placed loc shape** | loc def `clipType` + action list |
| Walls | `reachWall` geometry | generic 1×1 adjacent |
| Exact-tile shortcut | yes (non-exclusive) | no |
| Rect wall check | **source** tile only | both tiles |
| NPC approach | exclusive rectangle (`-2`) | sized adjacent |

### 3.4 Wire (modern OSRS, rsprot rev 221–239)

- `MOVE_GAMECLICK`: **5 bytes** — `x`, `z`, `keyCombination` (0 / 1 = ctrl /
  2 = ctrl+shift). No path. `MOVE_OPCLICK` does not exist.
- `OPLOC*`: loc id + SW-corner coords + ctrl + op (+ subop in `_V2`).
- `OPNPC*`: npc index + ctrl + op. **No coordinates** — server derives reach.
- Map flag: server → client via `SET_MAP_FLAG` (255,255 clears).

LostCity still has the 2004 waypoint-bearing `MOVE_*` body and
`clientRoutefinder: true` by default; that is correct for its era and wrong
for rev-230.

---

## 4. Line of sight

### 4.1 The ray cast (shared by both eras)

Fixed-point Bresenham over tile flags — LostCity
`LineValidator.rayCastLine` / rsmod / this tree's `collision_ray_cast`:

1. Clamp each entity to its nearest tile with
   `coordinate(a,b,size) = a>=b ? a : (a+size-1<=b ? a+size-1 : b)`.
2. `start == end` → true.
3. **Sight only:** start tile with `LOC` → false (standing in an object blinds
   you). No matching destination check — one asymmetry source.
4. Major axis = larger `|delta|`; **ties go to the Z-major branch**.
5. Minor axis in 16.16 fixed point: `HALF_TILE = 0x8000`, seed
   `scaleUp(start) + HALF_TILE + offset` (direction-dependent),
   `tangent = (scaleUp(deltaMinor) / absDeltaMajor) | 0`.
6. Per step, test the *entering* edge mask. On a minor-axis boundary crossing,
   also test the new tile against the minor mask.
7. **Sight only:** on the final destination tile, strip `LOC_PROJ_BLOCKER` —
   shoot *at* an object, not *through* it.

Sight masks: `SIGHT_BLOCKED_{N,E,S,W} = LOC_PROJ_BLOCKER | WALL_*_PROJ` =
`0x20400 / 0x21000 / 0x24000 / 0x30000`. Walk masks reuse
`COLL_FLAG_BLOCK_*`.

### 4.2 What "modern" LoS actually means

- Pre-2019 / PvM today: LoS is **asymmetric**.
- 29 Aug 2019 LMS update: symmetric LoS applied to **PvP only**. *"This does
  not apply to PvM."*
- NPCs deliberately cast LoS **backwards** (from the player to themselves).

This tree implements PvP symmetry as `los(a→b) && los(b→a)`, selected by
`ToriRS_FeatureTable.los_symmetric_pvp`. Behavioural match, not a verified
algorithmic reconstruction of the 2019 change. Ash cannot see the LoS
internals ([1594221116431732738](https://x.com/JagexAsh/status/1594221116431732738)).

### 4.3 Where LoS is required

| Check | LoS? |
|---|---|
| OP / operable (`reached*`) | No (line-of-walk / adjacency) |
| AP / approach (default range 10) | Yes (+ `BLOCK_NPC_AND_PLAYERS`) |
| Hunt / `npc_find*` / `map_findsquare` with `checkvis` | Optional (0=off, 1=sight, 2=walk) |
| Script `lineofsight` / `lineofwalk` | Yes / walk ray |

Overlapping footprints are never in AP range. `aprange(-1)` demotes AP to OP.
Ash's definition: *"ap triggers can be executed at a distance, requiring
line-of-sight. op triggers must be executed from an adjacent tile, requiring
line-of-walk"* ([1095653912743460865](https://x.com/JagexAsh/status/1095653912743460865)).

---

## 5. The follow "dance"

Following walks to the target's **previous** tile, not its current one. Ash:
Follow uses deliberately custom routing so the dance survives
([1227166364366000129](https://x.com/JagexAsh/status/1227166364366000129)).

1. Each entity records `last_step_x/z` = the tile it occupied before this
   tick's step *attempt* (including a blocked one).
2. At the top of the follower's turn, `follow_x/z` is snapshotted from the
   target's `last_step_*` **before** movement.
3. Spawn / login / teleport seeds `last_step = (x-1, z)` (west).
4. Tick order: NPCs phase 4, players phase 5 — within a tick the NPC sees the
   player current; the player sees the NPC stale. (Ash explicitly cannot
   confirm NPC-vs-player ordering from engine code.)

**Getting stuck is correct.** Beyond diagonal→X→Z there is no recovery; the
waypoint is kept. Escapes are per-mode stuck counters.

---

## 6. Entity occupancy

[osrs-docs entity-collision](https://osrs-docs.com/docs/mechanics/entity-collision/):
`Player`, `NPC`, `Projectile` (blocks LoS), `Full` (blocks movement).

Occupancy gates the **step**, never the flood/route. Clearing is
**unconditional** — that is why NPC stacking works.

What each entity **writes** (`blockwalk`, default **npc**):

| `blockwalk` | writes |
|---|---|
| none | nothing |
| npc (default) | `NPC_OCC` |
| all | `NPC_OCC` + `BLOCK_NPC_AND_PLAYERS` |
| player | `PLAYER_OCC` |

What each mover **reads**:

| mover | reads |
|---|---|
| player | `BLOCK_NPC_AND_PLAYERS` only |
| npc, `moverestrict=blocked` | nothing |
| npc, otherwise | `BLOCK_NPC_AND_PLAYERS` + `NPC_OCC` (unless `blockwalk=none`) + `PLAYER_OCC` (unless `passthru`) |

An ordinary npc does **not** stop a player — only `blockwalk=all` does.
Ash: [1677654049238265857](https://x.com/JagexAsh/status/1677654049238265857)
(`blockwalk`), [1678810351091974159](https://x.com/JagexAsh/status/1678810351091974159)
(`moverestrict`).

### 6.1 Flag-layout warning

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
- `COLL_FLAG_ROOF = 0x10000000`

Those bits collide with rsmod's route-blocker tier (bits 22–30). See §7.3.

---

## 7. What this tree implements

| Piece | Where |
|---|---|
| Ray cast + LoS / LoW / approached | `collision_map.c` |
| Occupancy `change_square` + `can_travel` + naive path | `collision_map.c` |
| Shape-keyed `exitStrategy` + exclusive rectangle | `collision_map.c` |
| Scene world-coord wrappers + `checkvis` | `mock230_scene.c` |
| Naive NPC movement (no flood), waypoints, stuck counters | `mock230_world.c` |
| Player BFS → **corner** waypoints (max 25) + greedy `takeStep` | `mock230_world.c` `queue_path_as_waypoints` |
| `last_step` / `follow`, occupancy on step/spawn/death | `mock230_world.c`, `mock230_combat.c` |
| AP LoS gate (npc casts backwards) | `mock230_world_process_interaction` |
| Symmetric PvP AP | `mock230_world.c`, `mock230_ops_player.c` |
| `CollisionType` strategies + `COLL_FLAG_ROOF` | `collision_map.c`, `mock230_scene.c` |
| Configurable BFS window (`route_window`) | `collision_map.c` |
| Occupancy re-stamp after a scene rebuild | `world_occupancy_restamp` |
| Era flags (`pathing_mode`, `approach_model`, …) | `features.h` / `features.c` |
| `blockwalk` / `blocksight` / `moverestrict` / `forceapproach` | fields + content/codec |

### 7.1 The routing window

The window is a property of the **router**, not of the loaded scene:

- `CollisionMap.route_window` (tiles, centred on the mover; 0 = the whole map).
- A fresh map is 0 (Client-TS floods the resident scene).
- `ToriRS_FeatureTable.route_window_tiles`: 0 for `lostcity`, **128** for
  `osrs` / `server_routed`. `mock230_scene_reset` applies it on the server;
  `App_WorldLoadFinish` applies it on the client after every scene rebuild.

At `MOCK230_SCENE_TILES = 104` the two agree until a map wider than 128
exists; `test_route_window` builds a 300-tile map to exercise the clamp.

### 7.1b Corner waypoints (not run-starts)

The BFS (`collision_map_route_tiles`) emits every tile. The server then
subsamples into ≤25 dest-first **corner** waypoints — the last tile of each
straight run — matching LostCity `PathFinder`'s backtrace and this tree's
own `collision_route_backtrace`. The greedy `takeStep` fills the gaps; a
pure cardinal/diagonal delta from a run's start to its corner reproduces the
BFS exactly.

An earlier `queue_path_as_waypoints` recorded the **first** tile of each run
instead. That handed `takeStep` a mixed-axis aim past the corner, so it tried
an unvalidated diagonal shortcut, fell through to the wrong cardinal, and
could walk into a dead-end alcove with no ground-click re-path — the
"stuck on a wall with a way round a few tiles away" symptom. The 25-turn cap
also used to overwrite the last stored turn with the raw destination (a
beeline through walls); it now drops destination-end corners like the
reference `pop()`.

Pinned by the `mock230 --selftest` movement L-corridor leg.

### 7.2 `moverestrict` → `CollisionType`

| `moverestrict` | `CollisionType` | rule |
|---|---|---|
| normal, nomove, passthru | `NORMAL` | plain mask test |
| blocked | `BLOCKED` | `FLOOR` required, stops blocking |
| indoors | `INDOORS` | normal + must carry `COLL_FLAG_ROOF` |
| outdoors | `OUTDOORS` | normal + must not |
| blocked_normal | `LINE_OF_SIGHT` | wall/loc bits read as projectile twins (`<< 9`) |

### 7.3 `breakroutefinding`

Ash: *"you'd navigate 'through' the booth when you click on the banker, though
the server would stop you when you reach the booth"*
([1443150721734660096](https://x.com/JagexAsh/status/1443150721734660096)).

Two implementations exist in the wild:

1. **Decoder collapse** (Client-TS / this tree / LostCity): opcode 74 sets
   `break_routefinding`, then `RSCache_Dat2ConfigLocFinish` forces
   `blocks_walk = blocks_projectiles = 0`. The booth carries no `LOC` flag, so
   BFS walks through; shape-keyed rectangle reach stops the player adjacent
   (or overlapping). Matches Ash's description.
2. **Route-blocker tier** (rsmod): nine `WALL_*_ROUTE_BLOCKER` /
   `LOC_ROUTE_BLOCKER` bits; pathfinding to a banker uses
   `useRouteBlockerFlags=true` so the flood ignores `LOC` but still sees the
   softer route-blocker mask. Normal walking still cannot step onto the booth.

This tree keeps (1). Implementing (2) would require remapping occupancy /
roof bits that currently sit where rsmod puts the route-blocker tier
(§6.1). Recorded, not deferred casually — it needs a coordinated bit-layout
change.

### 7.4 Era table (post-rework)

| era | `pathing_mode` | `approach_model` | npc size | op nearest |
|---|---|---|---|---|
| `lostcity` | CLIENT_BFS | LEGACY_SHAPE | 1×1 | none |
| `osrs` | **SERVER_AUTHORITATIVE** | RECT (shape-keyed) | `npc->size` | range 10 |
| `server_routed` | SERVER_AUTHORITATIVE | RECT (shape-keyed) | `npc->size` | n/a |

`osrs` and `server_routed` now share the same pathing mode; `server_routed`
remains as a named alias for manifests that already state it, and still
carries the xrsps lighting flags.

---

## 8. Tests

`make -C src test-world`:

- `test_line_of_sight` / `test_line_of_sight_asymmetry`
- `test_route_window` — 300-tile map, 128 window clamp
- `test_collision_types` — one tile per `CollisionType` branch
- `test_naive_path_safespot`
- `test_occupancy_stacking`
- `test_follow_dance_semantics`
- `test_features_eras` — pathing_mode / approach_model / window per era
- `test_try_route_op` / `test_try_route_op_forceapproach` /
  `test_try_route_op_rect` / `test_try_route_op_exit_strategy` —
  per-shape reach, exact-tile shortcut, exclusive rectangle, source-only
  wall bits
- `test_force_approach_rotation`

`./src/build/mock230 --selftest`: ordinary npc does not block a player; door /
ladder / large-NPC approach under the shared collision map.
