# Interaction pathing parity: Client-TS vs XRSPS-Typescript vs torirs

What happens between "the user clicks an NPC / loc row in the minimenu" and "the
player starts walking", in each of the three codebases, and what torirs
(`src/`) does differently today.

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

## 2. XRSPS-Typescript — server-authoritative, strategy-driven

### 2.1 The client does not path for interactions

`performWorldEntryAction` (`WebGLOsrsRenderer.ts:10178`) resolves the loc's
anchor tile, spawns the click cross, and hands off to the network layer. The
wire is high-level:

```
LOC_INTERACT(231) = u16 locId, u16 tileX, u16 tileY, u8 level, str action, u8 opNum
WALK(210)         = x, y, run, modifierFlags
PATHFIND(213)     = id, from{x,y,plane}, to{x,y}, size
```

No route, no waypoints, no collision map consulted at click time.

Two client-side helpers do exist and matter:

- `resolveLocInteractionTile(locId, approxTile)` — expanding-ring search
  (radius ≤ 8, all 4 planes) for a tile actually carrying `locId`, returning its
  anchor tile + `typeRot`. This is XRSPS's replacement for Client-TS's
  `typecode2` lookup; it falls back to the raw clicked tile.
- `isLocalPlayerAdjacentToLoc(locId, tile)` — clamps the player tile into the
  loc's `sizeX × sizeY` rect and accepts Chebyshev distance ≤ 1 (so **corners
  count** here, unlike the server predicate). Used purely to suppress a
  redundant client-initiated walk.

`OsrsRouteFinder32` is *not* on this path. It is used only by
`PlayerMovementSync.buildRunTargetPath` to interpolate **remote** actors toward
a server-supplied run target. Its notable properties (32×32 window centred on
the mover; separate size-1 / size-2 / size-≥3 neighbour tests using the
`19136830/19136911/19136995/19137016` extra masks; partial fallback over a 21×21
box ranked by squared distance-to-rect then route distance with `dist < 100`;
50-waypoint cap that drops the destination end) are worth knowing because the
server pathfinder mirrors them.

### 2.2 Server: `Pathfinder` + `RouteStrategy`

`Pathfinder` (32×32 graph, BFS, `findPathS1/S2/SX` by mover size,
`CollisionStrategy` ∈ {NORMAL, BLOCKED, FLY}) is generic; the interaction
semantics live entirely in `RouteStrategy.hasArrived(tileX, tileY, level, size)`.

| strategy | arrival rule |
|---|---|
| `ExactRouteStrategy` / `ApproximateRouteStrategy` | `tile === dest` (mover size ignored) |
| `RectRouteStrategy` ("inside") | mover footprint **overlaps** the loc rect |
| `RectAdjacentRouteStrategy` | overlap → `allowOverlap`; else must be **flush against one cardinal side with axis overlap** (no diagonal unless `allowLargeDiagonal`); then arrived if **any** tile along the shared edge is not wall-separated. Wall check reads **both** tiles' wall bits (`playerFlag & WALL_EAST` or `targetFlag & WALL_WEST`, etc.) |
| `CardinalAdjacentRouteStrategy` | same flush-side test plus explicit `blockedSides`; a size>1 mover whose footprint contains the wall tile is arrived unconditionally |
| `RectWithinRangeRouteStrategy` | overlap → false; else rect-to-rect Chebyshev ≤ range |
| `RectWithinRangeLineOfSightRouteStrategy` | as above + a single rsmod-style LoS ray between the nearest edge tiles |

Strategy selection (`LocInteractionHandler.deriveLocRouteProfile`, cached per
loc id) is driven by the **loc definition**, not by the placed shape:

```
loc.types ∩ WALLISH_TYPES        → "cardinal"
clipType === 0 && FLOOR_DECORATION in types → "inside"
clipType === 0 && has any action  → "adjacent_overlap"
clipType === 0                    → "inside"
otherwise                         → "adjacent"  (RectAdjacent, no overlap, wall-checked)
```

Door actions (`open`/`close`/`unlock`/`lock`/`pay-toll(`) additionally get
`allowOverlap = true`, per-door `blockedSides` from the door manager, and
**skip** the wall-edge gate — you must be able to reach a closed door from
either side.

### 2.3 Server fallback and caps

`findPath(..., findAlternative = true)`: on failure, scan
`approxDest ± ALTERNATIVE_ROUTE_RANGE (10)` for the tile with the lowest
squared distance-to-rect (ties broken by fewest steps) whose
`distances[..] < ALTERNATIVE_ROUTE_MAX_DISTANCE (100)`. Unlike Client-TS this
fallback **is** applied to interactions.

`MAX_PATH_CHECKPOINTS = 25`, same drop-the-destination-end overflow rule as
Client-TS.

### 2.4 The one-line summary of the difference

Client-TS decides *"which tile is close enough"* from the **placed shape +
angle** of the loc. XRSPS decides it from the **loc definition's model types,
clip type and action list**, with a mover-size-aware rectangle, and does it on
the server.

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

### D6 — no player-click pathing, because players are not pickable  *(player)*

`enum` in `src/ui/uitree_minimenu.h:27-33` has no `UI_MINIMENU_PICK_PLAYER`, so
the OPPLAYER1..5 / OPPLAYERT / OPPLAYERU encoders that already exist
(`net_out.c:762/775/794`) are unreachable and the reference's
`tryMove(..., player.routeX[0], player.routeZ[0], 2, 1, 1, 0, 0, 0, false)`
(Client.ts:8998/9054/9092/9108) has no counterpart.

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

## 5. Proposed implementation

Two stages. Stage 1 closes the Client-TS gaps and is unconditional — it is
strictly "the port finished". Stage 2 introduces the modern arrival semantics
behind an era gate, for rev-230+ boots.

### Stage 1 — finish the Client-TS port

**1.1 Decode `forceapproach`** (`3rd/rscache`, then the adaptor)

- `dat2_config_loc.c` case 69: `loc->force_approach = g1(&buffer);` — plus the
  matching encoder branch, a default of `0`, and the field on
  `struct RSCache_Dat2ConfigLoc`. Register the change in
  `3rd/rscache/EXCEPTIONS.md` and prove it with the standard byte-exact
  round-trip (the existing decoder-validation discipline for this vendored tree).
- `struct ToriRS_Location`: add `int force_approach;`, filled by
  `torirs_location_from_rscache.c`.

**1.2 Register the rotated forceapproach and the true route footprint**
(`world_scenery.u.c`)

`World_SceneryRegister` currently takes the *render* size. Split the two:

- Add `int route_size_x, int route_size_z, int force_approach` to
  `scenery_load_model` (and thread them to `World_SceneryRegister`), defaulting
  to the render size at the existing call sites.
- `scenery_add_normal`: pass the already-swapped `size_x/size_z` (unchanged
  behaviour) and `force_approach` rotated **at register time** by the map angle,
  mirroring how the size swap is already done there:
  ```c
  int fa = config_loc->force_approach;
  int angle = map_loc->orientation & 3;
  if (angle != 0) fa = ((fa << angle) & 0xf) + (fa >> (4 - angle));
  ```
- `scenery_add_floor_decoration`: keep the model/painter at `1,1` (the reference
  draws ground decor on one tile) but pass the loc's true, angle-swapped
  `config_loc->size_x/size_z` as the **route** footprint, plus the same rotated
  `force_approach`. Fixes D3.
- Add `int force_approach;` to `struct WorldEntity_Scenery` and have
  `app_scenery_approach` read `scenery->force_approach` instead of leaving 0.
  Fixes D1.
- While in `scenery_add_normal`, pass `map_loc->shape_select` rather than the
  literal `RSCACHE_LOC_SHAPE_SCENERY` so a diagonal centrepiece registers shape
  11. Fixes D10; no routing behaviour changes.

**1.3 Thread ctrl-held into the menu action path** (`app.c`)

The minimenu dispatch has no `struct LibToriRS_Input*`. Cheapest faithful fix:
latch it once per frame where the input is already in hand (next to the existing
`app_minimap_click` call at `app.c:8202`):

```c
app->ctrl_held = LibToriRS_Input_IsKeyHeld(input, TORIRSK_CTRL);
```

then use `app->ctrl_held` in `app_try_move` (Walk-here call site),
`app_try_move_op`, `_npc`, `_loc`, `_obj`. Keep `app_minimap_click`'s explicit
parameter — it is already correct — or collapse it onto the same field. Fixes D2.

**1.4 Loc-lookup miss aborts the whole click** (`app.c`)

- `app_try_move_loc` returns `int`: `-1` = "no such scenery" (reference
  `typecode2 === -1`), `0` = routed/unreachable but the loc exists.
- `PICK_SCENERY` / `USEHELD_ONLOC` / `TGT_LOC` return early on `-1` without
  sending OPLOC/OPLOCU/OPLOCT.
- The cross is currently shown before the pick switch
  (`app_minimenu_run_option:7594`); move the `UICross_Show` for the three loc
  actions after the lookup, or hide it on the `-1` path. Fixes D4.

**1.5 Tests** (`src/world/test/world_test_route.c`, `make -C src test-world`)

- `test_try_route_op_forceapproach`: 1×1 loc with `forceapproach = DIR_WEST`,
  player due west with an otherwise clear approach → the flood must **not**
  arrive on the west tile and must instead arrive north/south/east.
- `test_forceapproach_rotation`: the same loc at angles 0..3, asserting the
  `((fa << angle) & 0xf) + (fa >> (4 - angle))` table.
- `test_try_route_op_ground_decor_2x1`: multi-tile ground decor arrives from the
  far edge of the rect.
- `test_try_move_loc_missing_scenery`: element id with no scenery → no packet.

### Stage 2 — modern (XRSPS) arrival semantics behind an era gate

The gate already exists: `BootManifest.cache_revision` /
`cache_epoch` (`src/bootmanifest/bootmanifest.h:83-87`). Add
`enum World_PathingMode { WORLD_PATHING_LEGACY_SHAPE, WORLD_PATHING_RECT }` on
`struct World`, set at boot (`LEGACY_SHAPE` for dat1 / lc254 / rev < 230,
`RECT` for rev ≥ 230), and let the manifest override it
(`[game] pathing=legacy|osrs`) so a mock server can be pointed either way.

**2.1 Generalise `struct CollisionApproach`**

```c
enum CollisionApproachKind {
    COLL_APPROACH_EXACT,          /* dst tile only (obj, ground click) */
    COLL_APPROACH_LEGACY_SHAPE,   /* today: testWall / testWDecor / testLoc */
    COLL_APPROACH_RECT_ADJACENT,  /* XRSPS RectAdjacentRouteStrategy */
    COLL_APPROACH_RECT_INSIDE,    /* XRSPS RectRouteStrategy */
    COLL_APPROACH_WITHIN_RANGE,   /* XRSPS RectWithinRangeRouteStrategy */
};

struct CollisionApproach
{
    enum CollisionApproachKind kind;
    int loc_width, loc_length;      /* target rect, anchored at dst */
    int loc_angle, loc_shape;       /* LEGACY_SHAPE only */
    int forceapproach;              /* LEGACY_SHAPE only */
    int mover_size;                 /* 1 today; plumbed for later NPC pathing */
    int allow_overlap;              /* RECT_ADJACENT: doors, non-clipping scenery */
    int blocked_sides;              /* DirectionFlag bits, RECT_ADJACENT */
    int range;                      /* WITHIN_RANGE */
};
```

`collision_flood_arrived` becomes a `switch (approach->kind)`. The existing
branch moves under `COLL_APPROACH_LEGACY_SHAPE` unchanged; `NULL` stays
equivalent to `COLL_APPROACH_EXACT`. Nothing else in `collision_flood` changes,
so Stage 1's tests keep passing byte-for-byte.

**2.2 Implement `collision_test_rect_adjacent`**

Direct port of `RectAdjacentRouteStrategy.hasArrived` +
`isEdgeWallBlocked`:

1. footprint overlap → return `allow_overlap`;
2. flush cardinal side with axis overlap, else reject (no diagonal);
3. if any tile along the shared edge is not wall-separated, arrive — reading
   **both** tiles' `COLL_FLAG_WALL_*` bits, not just the source tile's. This is
   the one place where the modern predicate is genuinely stronger than
   `testLoc`, which only checks the source tile.

`collision_test_rect_inside` and `collision_test_within_range` are three-line
functions off the same footprint helper.

**2.3 Strategy selection for locs** (`app_scenery_approach`)

Under `WORLD_PATHING_RECT`, derive from the loc *definition*, mirroring
`deriveLocRouteProfile`, and cache it on `WorldEntity_Scenery` at register time
(the shape and the config are both in hand there — keep `world/` a leaf):

| condition | kind |
|---|---|
| shape ∈ wall/wall-decor set (0..9) | `RECT_ADJACENT`, `blocked_sides` from the wall angle |
| `blocks_walk == 0` and shape 22 | `RECT_INSIDE` |
| `blocks_walk == 0` and any action non-empty | `RECT_ADJACENT`, `allow_overlap = 1` |
| `blocks_walk == 0` | `RECT_INSIDE` |
| otherwise | `RECT_ADJACENT`, `allow_overlap = 0` |

Door actions (`Open`/`Close`/`Unlock`/`Lock`/`Pay-toll(`) additionally set
`allow_overlap = 1` and skip the wall gate — same carve-out as
`LocInteractionHandler.isDoorAction`. The action string is on the pick, so this
is decided in `app_try_move_loc`, not at register time.

`ToriRS_Location.blocks_walk` already exists (`torirs_types.h:199`), so no new
decode is needed for this table.

**2.4 NPC approach** (D5)

Under `WORLD_PATHING_RECT`: `loc_width = loc_length = npc->size`, kind
`RECT_ADJACENT`, `allow_overlap = 0`. Under `LEGACY_SHAPE`: unchanged `{1,1}`.

**2.5 Alternative-route fallback for interactions** (D7)

Add `struct CollisionRouteOpts { bool try_nearest; int nearest_range; int nearest_max_dist; }`.

- `LEGACY_SHAPE`: op-clicks keep `try_nearest = false` (Client-TS).
- `RECT`: op-clicks use `try_nearest = true, nearest_range = 10,
  nearest_max_dist = 100`, ranked by **squared distance to the target rect**
  then step count — i.e. the `Pathfinder.findPath` alternative-route block, not
  the 3×3 first-found ring. The ground-click 3×3 ring stays as-is in both modes;
  it is the same in both references.

**2.6 Tests**

Mirror each Stage-1 case under `WORLD_PATHING_RECT`, plus:
- a wall between player and loc on the flush edge → `RECT_ADJACENT` rejects,
  `LEGACY_SHAPE` (testLoc, source-tile-only check) accepts — the concrete
  behaviour difference between the two eras;
- diagonal-only contact rejected in both modes;
- size-3 NPC: `RECT` arrives beside the 3×3 footprint, `LEGACY` beside the SW
  tile;
- alternative-route ranking: two reachable tiles equidistant in steps, the one
  closer to the rect wins.

### Deliberately out of scope

- **D6 (player picks)** — needs `UI_MINIMENU_PICK_PLAYER`, player rows in
  `rs_minimenu_world.c` and the `OPPLAYER*` dispatch before the pathing question
  even arises. Its route call is one line (`{1,1}` at
  `player->pathing.route_x[0]`, identical to `app_try_move_npc`) and should land
  with that work, not this.
- **D8 / D9** — torirs's behaviour is a superset of the reference's and the
  failure modes are unreachable in practice. Leave them; they are recorded here
  so a future "why does the reference route differently on a huge failed flood"
  investigation starts from the answer.
