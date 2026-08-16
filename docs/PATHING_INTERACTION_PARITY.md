# Interaction pathing parity: Client-TS vs XRSPS-Typescript vs torirs

What happens between "the user clicks an NPC / loc row in the minimenu" and "the
player starts walking", in each of the three codebases, and what torirs
(`src/`) does differently today.

For modern OSRS LoS, naive NPC pathing, occupancy, shape-keyed reach, and the
follow dance, see [`OSRS_PATHING_LOS.md`](OSRS_PATHING_LOS.md) — that doc is
the authority (Ash-first evidence hierarchy, per-reference comparison, the
decision). §7.2 of this doc covers the server-side movement model.

Scope: the **approach/route** decision only. The right-click menu build, the
pick/hit-test, and the OP packet encodings are covered elsewhere
(`docs/CLIENT_TS_PARITY.md` §5, §9). Ground clicks and minimap clicks are
included only where they share code with the interaction path.

Sources read for this document:

| | path | role |
|---|---|---|
| Client-TS | `Client-TS/src/client/Client.ts` `tryMove` (5999-6260), `interactWithLoc` (5926-5996), `doAction` (8720-9120) | client-side BFS, authentic 2004 semantics |
| Client-TS | `Client-TS/src/dash3d/CollisionMap.ts` `testWall`/`testWDecor`/`testLoc` | arrival predicates |
| XRSPS | `xrsps-typescript/src/client/webgl/WebGLOsrsRenderer.ts` `performWorldEntryAction` (10178-10262), `resolveLocInteractionTile`/`isLocalPlayerAdjacentToLoc` (10644-10740) | click → packet, client-side adjacency shortcut |
| XRSPS | `xrsps-typescript/src/client/movement/OsrsRouteFinder32.ts` | 32×32 route finder — **remote-actor interpolation only** |
| XRSPS | `xrsps-typescript/server/src/pathfinding/legacy/pathfinder/{Pathfinder,RouteStrategy,CollisionStrategy}.ts`, `server/src/game/interactions/LocInteractionHandler.ts` | the real pathing, server-side |
| torirs | `src/app.c` `app_try_move*` (4229-4506), `app_minimenu_run_option` (7572-7954) | click dispatch |
| torirs | `src/engine/world_builder/collision_map.c` (292-961) | flood + arrival predicates |
| torirs | `src/engine/world_builder/world_scenery.u.c` (647-1316) | what gets registered as the route footprint |
| torirs | `src/net/net_out.c` `out_move` (245-281) | waypoint packet |

---

## 1. Client-TS — client-side BFS, one shot per click

### 1.1 The single entry point

Every interaction funnels through one function:

```ts
tryMove(srcX, srcZ, dx, dz, type, locWidth, locLength, locAngle, locShape,
        forceapproach, tryNearest): boolean
```

- `src` is **always** `localPlayer.routeX[0] / routeZ[0]` — the *end of the
  player's currently queued route*, not the tile they are standing on. Clicking
  again while walking paths from where you will end up.
- `type` selects the packet: `0` = MOVE_GAMECLICK, `1` = MOVE_MINIMAPCLICK
  (+14-byte anticheat trailer), `2` = MOVE_OPCLICK.
- The BFS is a plain 8-direction flood over `levelCollisionMap[minusedlevel]`
  writing `dirMap` (DirectionFlag back-pointer) and `distMap` (step count).
- **No local movement ever happens.** Movement comes back exclusively as
  PLAYER_INFO walk/run codes and is interpolated by `routeMove`.

### 1.2 Arrival predicate, checked on every popped tile

```ts
if (x === dx && z === dz)                                   → arrived
if (locShape !== WALL_STRAIGHT /*0*/) {
    if ((locShape < WALLDECOR_STRAIGHT_OFFSET /*5*/ || locShape === CENTREPIECE_STRAIGHT /*10*/)
        && testWall (x, z, dx, dz, locShape - 1, locAngle)) → arrived
    if (locShape < CENTREPIECE_STRAIGHT /*10*/
        && testWDecor(x, z, dx, dz, locShape - 1, locAngle)) → arrived
}
if (locWidth !== 0 && locLength !== 0
    && testLoc(x, z, dx, dz, locWidth, locLength, forceapproach)) → arrived
```

`testLoc` accepts a tile **inside** the `locWidth × locLength` rect anchored at
`(dx,dz)`, or a tile in one of the four **cardinal edge bands** whose facing
wall bit is open and whose direction is not vetoed by `forceapproach`. Corner
(diagonal) contact is *not* accepted — the edge bands are exclusive of corners
by construction.

### 1.3 Per-call-site parameters

| call site | dst | width/len | angle/shape | forceapproach | tryNearest |
|---|---|---|---|---|---|
| ground click (`World.groundX`, 2065) | clicked tile | 0,0 | 0,0 | 0 | **true** |
| minimap click (3200) | derived tile | 0,0 | 0,0 | 0 | **true** |
| `interactWithLoc` shape 10/11/22 | loc tile | `loc.width/length`, **swapped when angle is 1 or 3** | 0,0 | `loc.forceapproach`, **rotated by angle** | false |
| `interactWithLoc` any other shape | loc tile | 0,0 | `angle`, `shape + 1` | 0 | false |
| OPOBJ1..5 / OPOBJT / OPOBJU | obj tile | 0,0 → **retry 1,1 on failure** | 0,0 | 0 | false |
| OPNPC1..5 / OPNPCT / OPNPCU | `npc.routeX[0]/routeZ[0]` | 1,1 | 0,0 | 0 | false |
| OPPLAYER1..5 / OPPLAYERT / OPPLAYERU | `player.routeX[0]/routeZ[0]` | 1,1 | 0,0 | 0 | false |

`interactWithLoc` reads `shape` and `angle` from a **live scene lookup**
(`world.typecode2(level, x, z, typecode)`), so a loc swapped at runtime routes
against its current shape. If that lookup returns `-1` the function returns
early: **no walk, no cross, and no OPLOC packet at all**.

The forceapproach rotation is:

```ts
if (angle !== 0) forceapproach = ((forceapproach << angle) & 0xf) + (forceapproach >> (4 - angle));
```

### 1.4 Failure fallback

Only `tryNearest` (ground + minimap) has one: a 3×3 ring around the
destination, taking the lowest `distMap` value under 100, first-found wins, and
setting `tryMoveNearest = 1` (which rides in the minimap anticheat trailer).
**Interaction clicks (`type 2`) have no fallback** — an unreachable loc/NPC
produces no MOVE_OPCLICK, but the OP packet is still sent.

That ring is the *2004* rule and this section describes the 2004 client only.
Modern OSRS answers the same failure server-side over a 21×21 box ranked by
squared distance — a different model, selected by the era's
`ground_click_nearest_model`. See
[`OSRS_PATHING_LOS.md`](OSRS_PATHING_LOS.md) §2.1.1.

### 1.5 Backtrace and packet

Backtrace from the arrival tile toward the source, recording an entry **only on
direction change**; `routeX[0]` = arrival tile, ascending toward the source; the
source tile itself is never stored.

```ts
bufferSize = min(length, 25);      // max turns per pathfind request
length--;                          // → index of the waypoint nearest the source
startX/startZ = routeX[length];    // absolute anchor in the packet
minimapFlagX/Z = routeX[0];        // the flag goes on the *destination*
for (i = 1; i < bufferSize; i++) { length--; p1(dx); p1(dz); }   // toward the dest
p1(keyHeld[5] ? 1 : 0)             // ctrl = run, written for ALL three types
```

Truncation past 25 drops the waypoints nearest the **destination**, keeping the
route connected to the source.

### 1.6 Two reference quirks worth knowing

- The BFS queue and the output route share `routeX/routeZ` (**4000** entries)
  and the queue index wraps `% 4000`. A flood that enqueues more than 4000 tiles
  silently overwrites its own frontier. On a 104×104 mostly-open scene this
  happens on any *failed* flood, so the try-nearest fallback operates on a
  partially-corrupt `distMap`.
- The backtrace loop has no bound check; a route with >4000 turns writes past
  the typed array (silently dropped in JS) and then reads `undefined`.

---

## 2. XRSPS-Typescript — why its reach model was rejected

XRSPS is **server-authoritative on the wire** (click carries the target, no
waypoints), which agrees with Ash and with modern OSRS. Its *reach* model does
not. That distinction matters: this tree briefly copied the wrong half.

### 2.1 What XRSPS got right

- Client does not path for interactions; `LOC_INTERACT` / `WALK` carry target
  identity / destination only.
- Server BFS with a 21×21 alternative-route fallback (`dist < 100`, ranked by
  squared distance to the rect) and a 25-checkpoint cap.
- Map flag is server-owned.

### 2.2 What XRSPS invented (and what broke loc pathing here)

Strategy selection (`LocInteractionHandler.deriveLocRouteProfile`) is driven by
the **loc definition** (`clipType`, model types, action list), **not** by the
placed shape:

```
loc.types ∩ WALLISH_TYPES        → "cardinal"
clipType === 0 && FLOOR_DECORATION in types → "inside"
clipType === 0 && has any action  → "adjacent_overlap"
clipType === 0                    → "inside"
otherwise                         → "adjacent"
```

rsmod, LostCity and Kronos all dispatch on **placed shape** via
`exitStrategy` (wall 0–3/9, wall-decor 4–8, rectangle 10/11/22, exclusive −2).
Ash's "awkward for wall-shaped pieces" only makes sense under that dispatch.
XRSPS's heuristic collapses every wall/door/ladder into a generic 1×1
adjacent-with-overlap test — and that is exactly the bug
`app_scenery_approach` / `mock230_scene_loc_approach` shipped until the
pathing-parity rework.

Further XRSPS-only divergences this tree must **not** keep:

- Wall check reading **both** tiles' bits along the shared edge (rsmod's
  `reachRectangle1` reads the **source** tile only).
- No exact-tile shortcut for non-exclusive strategies (rsmod / LostCity both
  have one).
- Corners counting as adjacent in the client-side UI helper
  (`isLocalPlayerAdjacentToLoc`).

See [`OSRS_PATHING_LOS.md`](OSRS_PATHING_LOS.md) §0 / §1 / §3 for the authority
order and the shape-keyed decision.

---

## 3. torirs today

Click dispatch (`app_minimenu_run_option`, `src/app.c:7572`):

| pick kind / action | helper | approach passed |
|---|---|---|
| `UI_MINIMENU_PICK_TERRAIN` (Walk here) | `app_try_move(type 0)` | exact tile, `try_nearest = true` |
| minimap | `app_minimap_click` → `app_try_move(type 1)` | exact tile, `try_nearest = true` |
| `UI_MINIMENU_PICK_SCENERY`, `USEHELD_ONLOC`, `TGT_LOC` | `app_try_move_loc` → `app_scenery_approach` | sized (10/11/22) or `{angle, shape+1}` |
| `UI_MINIMENU_PICK_NPC`, `USEHELD_ONNPC`, `TGT_NPC` | `app_try_move_npc` | `{1,1}` at `npc->pathing.route_x[0]` |
| `UI_MINIMENU_PICK_OBJ`, `USEHELD_ONOBJ`, `TGT_OBJ` | `app_try_move_obj` | exact tile, then `{1,1}` retry |

`collision_flood` / `collision_flood_arrived` / `collision_route_backtrace`
(`collision_map.c`) are line ports of the reference flood, arrival predicate and
backtrace; `collision_test_wall` / `_wdecor` / `_loc` are line ports of
`CollisionMap.testWall/testWDecor/testLoc`; `out_move` (`net_out.c:245`) is a
byte-faithful port of the packet body including the 25-cap and its
drop-the-destination-end truncation. Those parts are correct and are **not** the
subject of this document.

The gaps are all in what feeds those functions.

---

## 4. Divergences

### D1 — `forceapproach` is never decoded, and never rotated  *(loc; behavioural)*

`app_scenery_approach` (`app.c:4457`) hardcodes `forceapproach = 0`.
`3rd/rscache/src/datatypes/dat2_config_loc.c:1032` reads config opcode 69 and
**discards it** (`g1(&buffer); // Skip unsigned byte`); `struct ToriRS_Location`
(`src/engine/torirs_types.h:185`) has no field for it. The angle rotation
`((fa << angle) & 0xf) + (fa >> (4 - angle))` is absent too.

Effect: the client will route to — and flag — a tile on a side of the loc that
the reference forbids. The server then refuses the interaction or re-paths, so
the symptom is "walked to the wrong side of the object and nothing happened".
It bites exactly the locs that set opcode 69: stall/counter fronts, one-sided
ladders and gates.

### D2 — the ctrl/run flag is hardcoded 0 on every op-click and on Walk-here  *(all kinds; behavioural)*

The reference writes `keyHeld[5]` inside `tryMove`, so it applies to types 0, 1
**and 2**. In torirs only `app_minimap_click` reads it
(`app.c:8206`, `LibToriRS_Input_IsKeyHeld(input, TORIRSK_CTRL)`); the Walk-here
row calls `app_try_move(..., ctrl_held = 0)` (`app.c:7862`) and
`app_try_move_loc/_npc/_obj` all pass `0` (`app.c:4489/4448/4501`). Ctrl-click
to force-run works from the minimap and nowhere else.

### D3 — ground-decor locs get a 1×1 route footprint  *(loc; behavioural, narrow)*

The reference puts shape 22 (`GROUND_DECOR`) in the **same sized branch** as
centrepieces: `loc.width/length`, swapped when angle is 1 or 3.
`scenery_add_floor_decoration` (`world_scenery.u.c:1294`) hardcodes `1, 1` into
`scenery_load_model`, and that is what reaches `World_SceneryRegister` and hence
`WorldEntity_Scenery.size_x/size_z`. `scenery_add_normal` does do the swap
(`world_scenery.u.c:1220-1225`), so this is a floor-decoration-only gap. Only
multi-tile ground decor is affected.

### D4 — a missed scenery lookup drops the walk but still sends OPLOC  *(loc; behavioural)*

`app_try_move_loc` (`app.c:4481`) is `void` and no-ops when
`World_SceneryGetByElementId` misses; the `PICK_SCENERY` branch then sends
`net_out_oploc` anyway and the cross has already been shown. The reference
`interactWithLoc` returns `false` when `typecode2` yields `-1` and sends
**nothing** — no walk, no cross, no OPLOC.

### D5 — NPC approach ignores NPC size  *(npc; matches Client-TS, diverges from XRSPS)*

`app_try_move_npc` passes `{1,1}`, exactly like Client-TS. XRSPS uses the
NPC's `sizeX × sizeY` rect. `WorldEntity_NPC.size` (`entity_npc.h:16`) is
already populated, so this is a policy choice, not a missing input. Under a
2004-era server the 1×1 form is correct; under a modern server it makes the
client flag a tile inside a large NPC's footprint.

### D6 — player-click pathing  *(done on the client; osrs230 wire gap)*

`UI_MINIMENU_PICK_PLAYER` / `WORLD_PICK_PLAYER` land players in the pickset
(including local, so a tile-occupancy winner can expand stacked NPCs/players
into the minimenu). `add_player_rows` skips the local player.
`app_try_move_player` mirrors the reference
`tryMove(..., player.routeX[0], player.routeZ[0], 2, 1, 1, 0, 0, 0, false)`
and `app_minimenu_run_option` dispatches `net_out_opplayer` / `opplayeru` /
`opplayert`. **osrs230 `packetout.h` still has no OPPLAYER\* opcodes** — encode
returns -1; menu and pathing still run on revisions that define the packets.

### D7 — no fallback for interaction clicks under a modern server  *(loc/npc; era-dependent)*

`collision_map_try_route_op` passes `try_nearest = false`, faithfully copying
Client-TS. XRSPS's server always runs the 21×21 alternative-route search for
interactions. Against a rev-230+ server, an interaction with a loc behind a
one-tile obstruction produces no MOVE_OPCLICK at all where the real client's
server would still walk you most of the way.

### D8 — the BFS queue does not wrap  *(cosmetic parity, torirs is "more correct")*

torirs sizes the queue at `size_x * size_z` (10816); the reference wraps at
4000 and corrupts its own frontier on large failed floods (§1.6). Only observable
in the try-nearest fallback for a far, unreachable ground click.

### D9 — backtrace overflow fails the whole route  *(latent)*

`collision_route_backtrace` returns `-1` when the turn count exceeds
`max_route`, and `app_try_move*` then sends nothing. The reference truncates at
the packet layer instead. With `max_route = 4000` this is unreachable in
practice, but it is a different failure mode.

### D10 — `scenery->shape` is not the placed shape for diagonal centrepieces  *(latent)*

`scenery_add_normal` (`world_scenery.u.c:1236`) passes
`RSCACHE_LOC_SHAPE_SCENERY` (10) to `scenery_load_model` for both shape 10 and
shape 11, so a `SCENERY_DIAGONAL` loc registers `shape = 10`. Harmless for
routing today (both take the sized branch) but the field is documented as "the
loc shape" and one more consumer would make it a bug.

### Not divergences (checked)

- `collision_test_loc`'s edge bands already exclude diagonal contact, matching
  both references' "no diagonal interaction" rule.
- Scenery pick tiles come from `scenery->grid_position` (`torirs_pick.c:41`),
  i.e. the loc's registered anchor, so `testLoc`'s rect is anchored correctly.
- NPC approach uses `npc->pathing.route_x[0]`, matching `npc.routeX[0]`.
- The player route source is `player->pathing.route_x[0]`, matching
  `localPlayer.routeX[0]`.
- `out_move`'s 25-waypoint cap and truncation direction match both references.
- Wall (0/1/2/3), wall-decor (4..8) and diagonal-wall (9) shapes register their
  true shape and map angle, and `app_scenery_approach`'s `shape + 1` convention
  lines up with the reference's `locShape` numbering (rscache
  `RSCACHE_LOC_SHAPE_*` values are identical to Client-TS `LocShape` ids).

---

## 5. What shipped

Both modes are supported and selected by era. Section 6 keeps the original
proposal for the parts deliberately left out.

### 5.1 The era seam — `src/features/`

`struct ToriRS_FeatureTable` (`src/features/features.h`) is to *client
behaviour* what `struct GameProtoRevTable` (`src/net/rev/`) is to the wire and
`cache_provider.h` is to the cache format: a table of slots whose **zero value
is the 2004/Client-TS behaviour**, so `{0}` is a working era and adding a field
cannot move an existing one.

```c
enum ToriRS_PathingMode   { CLIENT_BFS = 0, SERVER_AUTHORITATIVE };
enum ToriRS_ApproachModel { LEGACY_SHAPE = 0, RECT };
```

| era | `pathing_mode` | `approach_model` | npc size | op-click fallback |
|---|---|---|---|---|
| `lostcity` | CLIENT_BFS | LEGACY_SHAPE | 1×1 | none |
| `osrs` | SERVER_AUTHORITATIVE | RECT (shape-keyed `exitStrategy`) | `npc->size` | range 10, rect-ranked |
| `server_routed` | SERVER_AUTHORITATIVE | RECT (shape-keyed) | `npc->size` | n/a |

`osrs` flipped to `SERVER_AUTHORITATIVE` in the pathing-parity rework (Ash:
server paths since end-2013; rev-230 has no `MOVE_OPCLICK`). RECT no longer
means XRSPS's clipType heuristic — it means rsmod's shape-keyed
`ReachStrategy`. See [`OSRS_PATHING_LOS.md`](OSRS_PATHING_LOS.md) §1 / §7.4.

Resolution (`App_Init`), unconditional — an offline boot still clicks locs:

```
[features:boot] era=…   >   TORIRS_FEATURES_ERA   >   ToriRS_Features_ForCache()
```

`ToriRS_Features_ForCache` keys off the **lineage, not the revision**: dat1 →
`lostcity`; dat2+`oldschool` → `osrs`; dat2+`rs2` (the rev-634/643 caches) stays
`lostcity`, because those are still the classic client. Nothing derivable
selects `server_routed` — that is a property of the *server*, so
`manifest_xrsps.ini` states it.

### 5.2 Server-authoritative mode

`app_try_move_op` returns immediately after latching the minimap flag: no BFS,
no MOVE_OPCLICK — the interaction packet carries the target and the server
paths, exactly as xrsps does. A ground click degenerates to a 1-entry route,
which `out_move` already encodes as "absolute destination, no deltas" — the
shape of xrsps's `WALK`.

### 5.3 The rect approach model (corrected)

`struct CollisionApproach` has a `kind` discriminator (`collision_map.h`).
`COLL_APPROACH_LEGACY_SHAPE` is the Client-TS shape+angle path;
`RECT_ADJACENT` / `RECT_INSIDE` / `RECT_WITHIN_RANGE` /
`RECT_EXCLUSIVE` are the rsmod `ReachStrategy` family.

**The exact-destination shortcut is NOT legacy-only.** rsmod and LostCity both
open `ReachStrategy.reached` with
`if (exitStrategy != RECTANGLE_EXCLUSIVE && srcX == destX && srcZ == destZ)
return true`. The earlier claim that "rsmod has no such test" was describing
XRSPS's `Pathfinder`, not rsmod. Exclusive-rectangle (NPCs/players, shape
`-2`) is the only strategy that refuses the exact tile.

Loc strategy selection (`app_scenery_approach` / `mock230_scene_loc_approach`)
is **shape-keyed** via `collision_exit_strategy(shape)` — wall 0–3/9, wall-decor
4–8, rectangle 10/11/22 — matching rsmod / LostCity / Kronos. The XRSPS
`deriveLocRouteProfile` heuristic (`blocks_walk` / has-action) was deleted.

### 5.4 The defect fixes

| | fix |
|---|---|
| **D1** | `force_approach` decoded (rscache loc opcode 69, encoder + cachepack text + `loc_equal` round-trip), carried to `ToriRS_Location` → `WorldEntity_Scenery`, **rotated by the placed angle at register time** (`((fa << angle) & 0xf) + (fa >> (4 - angle))`, once, where the size swap already happens). 2459 of `cache.osrs230`'s locs carry it. Under RECT it becomes `blocked_sides` — same bits, same meaning. |
| **D2** | `app->ctrl_held` latched each frame in `App_RunOnce` from `TORIRSK_CTRL` and threaded into every `app_try_move*`. The reference reads `keyHeld[5]` *inside* `tryMove`, so it covers ground, minimap and interaction clicks alike. |
| **D3** | `World_SceneryRegister` now takes the **route** footprint, derived once inside `scenery_load_model` from `config_loc->size_x/size_z` angle-swapped — so ground decor routes as its true size while still rendering on one tile. |
| **D4** | `app_try_move_loc` returns 0 when the scenery lookup misses, and all three loc call sites drop the whole click (no OPLOC/OPLOCU/OPLOCT), matching `interactWithLoc`'s early return on `typecode2 == -1`. |
| **D5** | `npc_approach_uses_size`. |
| **D7** | `struct CollisionNearestOpts` + `collision_nearest_fallback`, shared by the ground click and the op click. Both are era-conditional: the ground click reads `ground_click_nearest_model` through `collision_nearest_opts_from_model` (2004 = the 3×3 ring, OSRS = the official 21×21 rect search), the op click reads `op_click_nearest_range` / `nearest_ranks_by_rect_distance`. The earlier claim that the ground ring was "identical in both references, so not era-conditional" was wrong — see [`OSRS_PATHING_LOS.md`](OSRS_PATHING_LOS.md) §2.1.1. |
| **D10** | `map_tile->shape_select` registered instead of the model-selection shape, so a diagonal centrepiece records shape 11. |

### 5.5 Tests — `make -C src test-world`

`test_try_route_op_forceapproach`, `test_force_approach_rotation`,
`test_try_route_op_rect`, `test_features_eras`, plus the existing op tests
updated. Two assertions are worth calling out because they pin the *era
difference* rather than either era alone:

- **Asymmetric wall.** A wall added through `collision_map_add_wall` flags both
  tiles of the edge, so both models refuse it — that is *not* where they
  differ. Flag the target tile alone (reachable in practice: `del_wall` is an
  unconditional clear) and legacy accepts the edge while RECT refuses it,
  because only RECT reads the target's bits.
- **Overlap.** Starting the route on the target: legacy arrives immediately,
  `RECT_ADJACENT` without `allow_overlap` steps off, with it arrives.

rscache: `make -C 3rd/rscache test` — `semantic` stays 100% on every cache,
loc byte-exact rose 508 → 545 on osrs230. See the note in `EXCEPTIONS.md` for
why 175 records moved `same-len` → `differ` without anything regressing.

---

## 6. Deliberately not done


- **D6 (player picks)** — client pick/menu/pathing landed (see §3 D6). Remaining
  gap is osrs230 OPPLAYER\* opcodes in `packetout.h` (and trading/INVOTHER,
  which is out of this pathing doc's scope).
- **D8 / D9** — torirs's behaviour is a superset of the reference's and the
  failure modes are unreachable in practice. Leave them; they are recorded here
  so a future "why does the reference route differently on a huge failed flood"
  investigation starts from the answer.

---

## 7. Server half (mock230)

§3 covered only `app.c`. The mock server used to discard the client's correct
`MOVE_OPCLICK` route on every loc/npc/obj click and substitute a four-neighbour
guess of the SW tile, routed by an exact-arrival BFS with no nearest fallback
and truncation at the wrong end of long paths. That stack is gone.

### 7.1 What the server now shares with the client

| piece | shared primitive |
|---|---|
| tile flags | `collision_map.c` via `mock230_scene.c` (same builder rules as `world_collision.u.c`) |
| flood + approach + nearest | `collision_map_route_tiles` / `collision_map_try_route_op` |
| arrival / reach | `collision_map_reached` (rect-adjacent, cardinal, both tiles' wall bits) |
| loc approach | `mock230_scene_loc_approach` mirrors `app_scenery_approach` (shape, angle, rotated footprint, `forceapproach`) |
| era nearest box | `ToriRS_Features_ForCache` → `mock230_scene_op_nearest_opts` |

`handle_move` takes the **last** packet waypoint as the destination and
re-routes (LostCity `MoveClickHandler`), with `distanceToSW > 104` rejected.
Op handlers route to the **target** with its approach — they never call
`walk_beside`.

### 7.2 Movement model

Players store at most 25 dest-first **corner** waypoints (`PathingEntity.waypoints`)
— the last tile of each straight BFS run, matching LostCity `PathFinder` /
`collision_route_backtrace` — and advance with a greedy `takeStep` that
re-validates `mock230_scene_can_step_extra` (with NPC/BLOCK occupancy) and stalls
instead of clearing the route. Interaction recovery re-floods when the corner
queue is empty or a post-move step is blocked, and queues the **full** approach
path (`queue_path_as_waypoints` / LostCity `pathToTarget`) — not a single
adjacent tile. Truncating a fresh `walk_to_approach` on the packet-handler call
(`steps_taken == 0` alone) forced `move_count == 1` on every op approach and
ignored run mode.

Per-tick interaction matches LostCity `processInteraction`: **tryInteract
(pre-move) → pathToPathingTarget → move → tryInteract (post-move)**. When an
npc/player target is on the last waypoint (`waypoint_index <= 0`), SMART repath
is a full `walk_to_approach` to the live tile — not a one-adjacent-tile crawl.
Mid-path (`waypoint_index > 0`) does not repath. Standing under a pathing
target is not operable; the engine queues a one-tile cardinal step-off
(`randomWalk`). Locs/objs only OP when `allowOpScenery` is set (packet-handler
immediate try, or post-move with `steps_taken == 0`).

NPCs never flood: `mock230_world_npc_walk_to` queues one tile from
`collision_map_naive_path` (via `mock230_scene_naive_path`) and advances with the
same takeStep shape, gated by entity occupancy flags written at spawn/move.

Unreachable interactions terminate with content's
`[proc,cannot_reach_message]` (`player/messages.rs2`), not a latched op.

AP-range triggers also require `mock230_scene_approached` LoS (player→npc;
npc APPLAYER casts the reverse).

### 7.3 Still asymmetric

1. **The client still paths for `CLIENT_BFS` eras and sends waypoints.** The
   server re-routes from the last waypoint rather than replaying the client's
   tile list. Agreement is "same flood, same arrival", not byte-identical
   packets.
2. **`route_straight` remains only when there is no collision map at all** (cache
   missing). An in-scene source with an out-of-scene destination returns -1 —
   it no longer walks through unmapped walls.

### 7.4 Defects that were the server drift

| id | failure | fix |
|---|---|---|
| P1 | `walk_beside` → `steps_clear` threw away `MOVE_OPCLICK` | deleted; op handlers route with approach |
| P2 | four-neighbour SW guess ignored footprint/shape/angle | `mock230_scene_*_approach` + `route_op` |
| P3 | exact BFS, no nearest fallback | era `CollisionNearestOpts` on `route_tiles` |
| P4 | long paths truncated at the destination end | source-end emit in `collision_map_route_tiles` |
| P5 | `route_straight` for out-of-scene endpoints; no per-step collision for players | refuse out-of-scene; `can_step` in `takeStep` |
| P6 | op-approach recovery kept one BFS tile → run never took 2 steps | full-path re-flood; stall only when next step blocked |
| P7 | `queue_path_as_waypoints` stored **run-start** tiles, so greedy `takeStep` cut unvalidated diagonals past corners and stuck on walls; >25-turn cap overwrote the last turn with the raw destination | record run-**end** corners (LostCity backtrace); cap drops destination-end turns (`pop()`) |
| P8 | mover chase re-aimed one tile **after** move → walk-speed crawl / stack on NPC, Talk-to nearly impossible | LostCity order: pre-try → last-waypoint full `walk_to_approach` → move → post-try; under-target `randomWalk` |
