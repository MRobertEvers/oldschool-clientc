# Sailing implementation plan

Visual acceptance evidence is in
[sailing_validation/README.md](sailing_validation/README.md). The
[completion audit](sailing_validation/plan-audit.md) tracks every requirement
against current source and executable evidence; historical PASS rows alone
do not establish completion.

Companion to `docs/SAILING.md` (the research doc — read it first; §5 has the
deob ground truth this plan is built on). Work happens on the
**`gameframe-2004-osrs239`** branch; the old `worktree-sailing` statement is
historical.

## Completion status — 2026-09-07

Every phase below is **implemented and verified**. The audit ledger carries the
per-requirement evidence pointer; this table is its summary, and nothing here is
a claim the ledger does not back.

| Phase | Status | What closes it |
|---|---|---|
| **C0** multi-world substrate | **Done**, one residual | C0.1/C0.2 verified; **C0.3** verified on the positive path but the five wire-refusal diagnostics are source-reviewed only, with no test pinning them |
| **C1** Wev core | **Done** | `test-wev` against real `cache.osrs239` — config, delta decoder, interpolator, bounded queue |
| **C2** boat world build | **Done** | `test-wev-rebuild`; the drain rule now covers the **root** rebuild too, with a negative-controlled test |
| **C3** painter descent | **Done** | `test-painters-world-entity`, `test-sailing-paint-order`, `test-frame-flat`; the emit prefetch no longer reads across a view marker |
| **C4** flatten, overlap, budget | **Done** | `test-wev-visibility`, `test-cs2-worldentity-limit`, `test-frame-flat`; four reviewed captures |
| **C5** actors, clicks, camera | **Done** | `test-wev-population`, `test-pick-level`, `test-minimenu-world`; native peer aboard a neighbouring deck |
| **S0** per-player scene window | **Done** | 15 PASS rows in the sailing suite on the shared server; the plan's "genuine multiplayer limit" is deleted |
| **S1** vessel entity + mover | **Done** | registry bound, 16-heading projection round trips, quarter-tile quantum, separate boat collision map |
| **S2** protocol | **Done** | spawn/delta/despawn per observer, the rev-230 refusal, the deck rebuild encoding, the zone sandwich, cross-world PLAYER_INFO/NPC_INFO |
| **S3** content | **Done** | full dock→helm→steer→disembark→relogin walked natively; **LIFE-1**, a logged-out client's auto-reconnect, is fixed **and in the shared binaries** as of the 09:35 pair `5e34b81f` / `6a0e7f04` — five clean aboard-logout cycles, `/tmp/sailing-fin3-life/client.log` |
| **Checkpoint A** | **Done** | client offline build + server mover |
| **Checkpoint B** | **Done** | the native sailing arc, re-captured and reviewed |
| **Checkpoint C** | **Done** | overlapping full/flat boats, on both the software and the GPU lane |
| **Checkpoint D** | **Done** | board at a dock, take the helm, steer, turn through 360°, disembark, relog |
| **Constraints** | **Held** | no `3rd/toridraw` or `painters*.c` change; `test-scanline` parity; rev-239-only wire, with the rev-230 refusal pinned |
| **R1** PLAYER_INFO regression risk | **Retired** | the two-coordinate contract, proven on the wire and on screen |
| **R2** zero-boat render neutrality | **Neutral** | 12 scenes × 6 runs, identical pixels and work counts, median render Δ +0.32 % |

Four items remain **Open** and are named in the ledger: **C0.3** (untested wire
refusals), **GPU-1/2/3** (three `gl3-zbuffer` findings, pre-existing),
**HARN-1** (`::vesselspawnat` ignores the 16-heading berth clearance) and
**EMB-1** (the `test-torirsserver-embed` decode break). None of them is a
sailing requirement that went untested.

**LIFE-1** (the logged-out client's auto-reconnect) and **SRV-1**
(`ToriRSServer_SceneOpNearestOpts` never writing `unbounded`) were the other two
and are now **closed**: root rebuilt the shared pair at 09:35 on 2026-09-07 with
both fixes, and re-ran twelve unit gates, the 563/0 sailing suite and all nine
acceptance tools on it. Two residuals are recorded rather than hidden — no gate
drives a reconnect, and no gate exercises the two server callers of
`ToriRSServer_SceneOpNearestOpts`. See `docs/SAILING_HANDOFF.md` and
`docs/sailing_validation/plan-audit.md`.

## Scope

Client: world entities — each boat is its own world (own `struct World`, own
`struct Painter`), inserted into the main painter's grid as a regular loc, with
the painter descending into boat worlds via **bounded nested calls checked
against an explicit painter stack**, flattened rendering for overlapping
yield-group entities and a per-group placement budget,
actors aboard, clicks, camera.

Server: boats as small instances (the existing map-instance system) plus a new
vessel entity that carries "this instance is currently at world position X,Z
heading H", a per-tick mover, and the world-entity packet family.

Rev-239 lane only: `SET_ACTIVE_WORLD_V2` (op 47), `WORLDENTITY_INFO_V7`
(op 122), `REBUILD_WORLDENTITY_V4` (op 109) exist only in the osrs239 wire
tables; the osrs230 lane does not carry sailing.

## Naming

This codebase already uses `WorldEntity_*`/`WorldEntityFacet_*` for ordinary
in-world entities (`src/world/entity_facets.h`), so the OSRS "world entity"
concept gets a distinct prefix:

- `struct Worldview` — a view: id, `struct World*`, `struct WorldBuilder*`,
  base coords, size, parent id (mirrors deob `class100` + its registry).
- `struct Wev` (`wev_*` functions, files `src/world/wev.{c,h}`) — the world
  entity itself: transform, target queue, interpolator, config, priority
  group (mirrors deob `class467`).
- `struct WevConfig` — cache config, archive 72 (mirrors `class387`).
- Server: `struct ToriRSServerVessel`, `torirs_server_vessel.{c,h}`.

## What already exists (leverage, don't rebuild)

| Piece | Where |
|---|---|
| Heap-allocated, multi-instance-clean `World`/`Painter`/scene | `src/world/world.c:17`, `src/painters/` |
| Iterative painter with instance-local scratch | `painter_paint_bucket`, `src/painters/painters_bucket.u.c:582` |
| Entity→pseudo-loc injection, fully world-parameterized | `World_CycleRegisterPainterDynamics`, `src/world/world_cycle.c:1532` |
| Instance scene build (13×13 zone templates + rotation) | `WorldBuilder_RebuildInstance`, `src/engine/world_builder/world_builder.h:196` |
| Model merge (for optional baked billboards) | `ToriDraw_ModelNewMerge`, `3rd/toridraw/toridraw_model_transform.h:19` |
| Element positions already carry pitch/yaw/roll | `ToriDraw_Position`, `3rd/toridraw/toridraw_types.h:309` |
| Vendored rev-239 codecs for the whole packet family | `3rd/rsprot/gen/rev239_prot.h:82-85`, `3rd/rsprot/packets/rebuild_worldentity_v4.c` |
| `SET_ACTIVE_WORLD` declared + server-encoded + selftested (root=0) | `src/net/rev/pktnames.h:213`, `torirs_server_encode.c:455` |
| Server map-instance system (alloc/setchunk/build/free, collision, REBUILD_REGION, script opcodes) | `src/torirsserver/torirs_server_mapinstance.*`, `ss_opcode.h:11009-11086` |

Known blockers, found in the surveys:

- `struct App` and `struct ToriRS_Frame` hold exactly one world/painter/scene
  triple (`src/app.h:732-739`, `src/render/torirs_frame.h:28-73`).
- The painter's only reentrancy hazard: three qsort-context statics
  (`src/painters/painters.c:138-140`).
- Server: the scene/collision window is a world singleton
  (`torirs_server.h:3829-3833`, `torirs_server_world.c:2604-2611`) — a boat
  instance and a mainland player would rebuild each other's scene every tick.
- Server: `player_in_view` is an absolute ±15-tile test
  (`torirs_server_encode.c:3697`) — deck players (pool coords, x≥100 squares)
  and shore players can never see each other without root-coord projection.

---

## Client phases

### C0 — multi-world substrate

1. Replace the singular `app->world`/`app->world_builder` with a root view +
   a registry of up to 16 `struct Worldview` (id 0 = root). Keep `app->world`
   as an alias for the root so the ~hundreds of existing call sites don't
   churn; new code goes through the registry.
2. Add an **active-world cursor** to the packet-apply layer. Decode
   `PKT_NAME_SET_ACTIVE_WORLD` in `src/game/rs_gameproto_exec.c` (declared but
   currently parsed-and-dropped): it flips the cursor; `SERVER_TICK_END`
   resets it to root. Thread the cursor through the zone applicators
   (`rs_gameproto_exec.c:302-334, 433-600`) so zone updates land in the right
   view's world/builder.
3. Rebuild routing. **Corrected 2026-09-06 against the shipped wire and the
   vendored codec — the old "the rebuild is prefixed with a world-entity id +
   plane, assert on an unknown id" wording was wrong on both halves.** What
   the three rebuild packets actually carry at rev 239:

   - `REBUILD_NORMAL` (`RebuildNormalV2`) *does* carry a world-entity prefix:
     `world_area` (0 = root), read in `src/net/rev/osrs239/osrs239_parse.c:630`
     into `PktMapRebuild.world_area`. No plane.
   - `REBUILD_REGION` (`RebuildRegionV2`) carries **no** id at all — the arm
     at `osrs239_parse.c:1672` memsets the packet, so `world_area` reads 0
     (root) by construction.
   - `REBUILD_WORLDENTITY_V4` (op 109) carries **neither an id nor a plane**.
     The vendored codec is exactly two fields —
     `3rd/rsprot/packets/rebuild_worldentity_v4.c`:
     `RSPROT_U2(base_x); RSPROT_U2(base_z);` — followed by the same
     `encodeRegionV2` zone-descriptor grid `REBUILD_REGION` uses. V3+ dropped
     the wire-carried view id (deob `field5861`).

   So routing is:

   - **Target view.** For `REBUILD_NORMAL`/`REBUILD_REGION` it is the packet's
     own `world_area`; for `REBUILD_WORLDENTITY` it is the **`SET_ACTIVE_WORLD`
     cursor**, captured into the exec task (`Task_GameProtoExec.wev_view_id`,
     `src/game/task_gameproto_exec.c:280`) *before the first await*, because
     `SERVER_TICK_END` resets the cursor and execs behind this task on the same
     serial queue.
   - **Base coordinates.** `base_x`/`base_z` are the view's SW corner in
     absolute root-world tiles (deob `field1405`/`field1395`); exec writes them
     onto `view->base_x/base_z` and derives the boat scene's zone centre
     (`base/8 + scene_size/16`) so `World_ResetScene` lands `_base_tile_*` on
     the wire base.
   - **Plane.** It comes from the *`SET_ACTIVE_WORLD` that aimed this rebuild*,
     not from the rebuild: `rs_gameproto_exec.c:1870` clamps the wire byte to
     0..3 into `app->active_world_level`, and the `REBUILD_WORLDENTITY` arm
     copies it onto `view->parent_level` and mirrors it onto the `Wev`
     (`Wevs_Get(...)->parent_level`) for terrain-height sampling.

   **Unknown-id contract — assert *internally*, refuse on the *wire*.** The
   old "assert on an unknown id" is right only for the registry lookup and
   wrong for wire data; an assert-only check is an abort in debug and an
   out-of-bounds registry/heightmap index under `NDEBUG`.

   - `WorldviewRegistry_Get` (`src/world/worldview.c:138`) asserts `reg`,
     `id >= 0`, `id < WORLDVIEW_MAX` and `views[id].live` — the loud stop for
     an internal caller, matching the deob's unknown-world-entity throw.
   - Every wire-driven bad state is a **guard that diagnoses and drops**, at
     the frame that caused it:
     `SET_ACTIVE_WORLD` with an out-of-range or dead id is refused
     (`exec: SET_ACTIVE_WORLD refused, view %d not live`);
     `REBUILD_NORMAL`/`REBUILD_REGION` addressed to a non-root `world_area` is
     dropped (`REBUILD addressed world_area %d, not root`);
     `REBUILD_WORLDENTITY` whose captured cursor is the root or a view that
     died between the `SET_ACTIVE_WORLD` and this exec is dropped
     (`REBUILD_WORLDENTITY with cursor on the root/a dead view %d`);
     a grid that does not decode against the view's spawn-time size is dropped
     (`grid does not match view %d's size`); and a base that is not
     zone-aligned is dropped (`base %d,%d not zone-aligned`).
   - Before the deck load resets the scene allocation, the boat world's own
     `EntityRemoved` queue is drained (`App_WorldDrainEntityRemovedFor`);
     `World_ResetSceneAlloc` asserts that emptiness.

Test: existing selftests still pass with the registry in place and only the
root view live; `test-net-exec` decodes `SET_ACTIVE_WORLD` (id then plane),
arms the cursor and proves `SERVER_TICK_END` resets id *and* level
(`src/game/test/rs_gameproto_exec_test.c:340-406`); `test-wev-rebuild` proves
V4 carries `base_x`/`base_z`, that the grid is carried raw and decoded against
the view's zone counts onto the 13-stride array, and that a short, long or
wrong-sized grid is rejected as a whole stream
(`src/world/test/wev_rebuild_test.c:136`).
**Still untested (audit row C0.3, the one client requirement left Open):** no
check pins the five refusal diagnostics above (root cursor, dead cursor,
non-root `world_area`, size-mismatched grid, unaligned base) nor
`SET_ACTIVE_WORLD`'s not-live refusal. All six are implemented and were read in
source; none is exercised, so a regression that silently *accepted* one of them
would not be caught. The positive path is fully proven and was re-run on the
final binaries.

### C1 — Wev core: config, packet, interpolation

1. `struct WevConfig` loader from config index archive 72 (opcodes per
   SAILING.md §5.4: plane, pivot offsets, bounds w/h/off, name, 5 ops,
   category, click mode, default anim, flat HSL default 39188). Precompute the
   16-orientation footprint corner tables (SAILING.md §5.5) at load.
2. `struct Wev`: id, view id, parent view id, config, current transform
   {x, y, z, angle 0–2047}, target queue of 10 (max 9 pending, slot 0 newest),
   priority group, op mask, seq state.
3. Decode `WORLDENTITY_INFO_V7` (format in SAILING.md §5.4; codecs vendored in
   `3rd/rsprot`): despawn / flags-only / enqueue / snap ops with the
   2-bit-per-axis delta reader; new-entity trailer spawns the `Worldview`
   (size = packed nibbles ×8 tiles) and the `Wev`.
4. Interpolator: per queued segment, evaluate over **30 client cycles
   (600 ms)** from the enqueue cycle; linear x/z, shortest-arc angle
   (`d = (to−from) & 0x7FF; if (d > 1024) d −= 2048`); on queue-empty hold the
   last target; height overwritten each frame from root terrain under the
   boat. Per-frame driver iterates all views' entities, recursing into nested
   views **iteratively** (worklist, cap 16 views).

Test: unit test the delta reader and the interpolator against hand-computed
segments (including the 2047→1 wraparound arc); config-decode test against
archive 72 of `cache.osrs239`.

### C2 — boat world build

1. On spawn, build the boat's world: `World_New` +
   `World_ResetSceneAlloc(size)` (an 8–24-tile scene is ~170× smaller than the
   root's 104×104 — build eagerly) + its own `WorldBuilder` + own `Painter`.
   Share the single `ToriDraw_Scene` (element ids are scene-global, and the
   painter stores bare element ids — sharing avoids a second element
   namespace).
2. Feed `REBUILD_WORLDENTITY_V4` through the existing
   `WorldBuilder_RebuildInstance` zone-template path into the boat's world.
3. Drain rule: **every** rebuild path must drain its world's entity-removed
   queue before resetting the scene (`World_ResetSceneAlloc` asserts
   `event_count == 0`). **Corrected 2026-09-06: this said "boats", and the root
   rebuild was the one path that did not obey it.** A despawn arriving in the
   same packet pump as a teleport-driven rebuild reached
   `World_ResetSceneAlloc` with `event_count == 1` — an abort under an `OPT=0`
   client (`world.c:515`), and under a release client a silently dropped
   removal that orphans the DYNAMIC scene elements it names. The per-tick drain
   cannot cover the gap: it sits behind `app->world_active &&
   app->world_view_valid`, which a rebuild has already closed. The
   REBUILD_NORMAL / REBUILD_REGION branch of `src/game/task_gameproto_exec.c`
   now drains the root world immediately before its world-load await, exactly
   where the boat branch drains its own view. *Draining*, not clearing, is the
   point — `App_WorldDrainEntityRemovedFor` is what hands each removal to the
   plugin host and calls `ToriDraw_SceneElementRemove`.

Test: offline harness that hand-feeds a rebuild for a 1-zone raft deck and
asserts tile heights/locs land in the boat world, not the root; plus
`test_root_rebuild_drains_entity_removed()` in
`src/world/test/wev_rebuild_test.c`, which reproduces the exact aborting frame
and was negative-controlled (removing the drain call makes it abort).
Write-up: `sailing_validation/client/world-drain.md`.

### C3 — painter descent with an explicit stack

The heart of the feature. Design:

1. **Pseudo-loc insertion, per frame**: after interpolation, insert each
   entity into its *parent* painter with the native radius-60 temporary loc
   footprint (1×1 at tile centre, up to 2×2 at tile boundaries), via the existing dynamic-registration pass
   (`World_CycleRegisterPainterDynamics`), with a reserved element-id range
   (or a flag bit) marking "world entity N". Height = parent terrain under
   the boat. This gives painter-correct ordering against real locs, actors
   and projectiles for free — same as the deob's radius-60 trick.
2. **Descent**: the app keeps a *stack of painters*, and a paint that reaches
   a world entity pushes onto it and runs that entity's painter right there.
   `stack[0]` = root painter. When the drain emits a world-entity element it
   writes `CMD_BEGIN_WORLD(view id)` into the `PaintersBuffer`, pushes the
   boat's frame (camera pre-transformed into boat space — inverse
   yaw+translate applied to eye and focus, computed once per boat per frame),
   runs the boat's paint to completion, writes `CMD_END_WORLD`, pops, and
   carries on with the rest of its own batch. The paint stays one
   straight-line function: no resume indices, no suspend/resume states — the
   outer paint's traversal is its own locals, and the nested paint touches
   only the boat painter's arrays. The stack is what a descent is *checked*
   against: a painter already on it (two view ids can name one painter) or a
   full stack is refused, and the marker pair is emitted empty so the stream
   stays balanced. Nested boats come free (depth cap = 16 views, asserted).
3. **De-static the painter**: move the three qsort-context statics
   (`painters.c:138-140`) into `struct Painter` (or pass-through context) so
   an outer paint can't be corrupted by the nested one.
4. **Emit transform**: grow `ToriRS_Frame` from one `(world, painters)` to a
   per-view table with a per-view transform {translate, yaw, flatten scale +
   y-offset, flat-HSL override, anim matrix later}. `try_emit_world_draw_model`
   (`torirs_frame.c:1932-2107`) tracks the current world from
   BEGIN/END_WORLD commands, resolves terrain through that view's world, and
   composes: deck-local position → pivot recenter
   (−size·64 − cfgPivot) → yaw rotate → boat translate → camera-relative.
   Element yaw composes additively with boat yaw through the existing
   `ToriDraw_Position.yaw`; the rasterizer itself is untouched (and stays free
   of 64-bit arithmetic — all wide values live in the game layer).

   **The emit loop's prefetch must not read across a view marker
   (found and fixed 2026-09-06).** `try_emit_world_draw_model` runs a four-deep
   prefetch pipeline that resolves element ids ahead of the cursor, while the
   `BEGIN_WORLD`/`END_WORLD` markers that switch views are consumed *below* it.
   Its reach walk tested commands `cur + 1 .. cur + depth` and never `cur`
   itself, so when the current command *was* a marker the walk saw three
   ordinary commands after it and resolved them against the view the marker had
   not yet opened — aborting an asserts-live client in
   `frame_lookahead_element_id`. The fix starts the marker test at `cur`: a
   marker command now prefetches nothing past itself. It changes only which ids
   the pipeline resolves early, never the emitted set.
   Write-up: `sailing_validation/client/frame-assert.md`.

Deferred from this phase: the animation-driven bob/roll matrix (deob drives it
from bone 0 of the config's default seq). First cut renders boats level;
the transform slot for it is reserved.

Test: extend the scanline-parity/painter test family with a two-world scene —
golden-image or command-stream comparison; assert command stream contains the
boat's commands exactly between BEGIN/END markers positioned per painter
order (a loc in front of the boat must emit after END_WORLD).

### C4 — flatten ("billboard"), overlap, budget

Mirror the deob exactly (SAILING.md §5.3 — there is no bake):

1. Flatten state per entity per frame: Y scale 0.01, pre-scale Y offset
   −1200, flat HSL from config at strength 127, **skip actor population
   entirely**, skip from picking.
2. Draw order per frame: aboard-entity first (never flattened), then priority
   groups 2, 0, 1; placement budget (CS2 opcodes 7900/7901, default 30,
   nonnegative and counted separately per group); entities past the group's
   limit are omitted;
   group-1 entities additionally flatten when a player/opted-in NPC/
   already-drawn entity overlaps them (native actor/16-heading-footprint test
   and entity/entity fine-coordinate enclosing rectangles, plus a per-frame
   scene stamp — first placed wins, i.e. "topmost renders full").
3. Flat-colour override: a per-view HSL override honoured by the emit path
   (model-level recolour at emit; the toridraw HSL pipeline already exists).

Optional follow-up (perf, software rasterizer): bake a merged deck model per
boat with `ToriDraw_ModelNewMerge`, invalidated on deck loc change, drawn
instead of the flattened sub-scene. Deob-faithful flatten ships first; the
bake is an optimization with a compare mode against the flatten render.

**Status (2026-09-06): the optional bake was written and has since been
removed.** It shipped ahead of the deob-faithful path and became the *only*
path, which is the inversion this step forbids. `app_wev_flat_ensure`,
`app_wev_flat_free`, `App_WevFlatInvalidate` and the `TORIRS_WEV_BUDGET`
environment override are deleted from `src/app.c`/`src/app.h`, and
`task_gameproto_exec.c` no longer invalidates a bake on REBUILD. Flattening is
now live per-frame view state — `frame->views[id].flatten_scale = 0.01f`,
`flatten_y_offset = -1200`, `flat_hsl` — substituted per draw in
`src/render/torirs_frame_flat.u.h`. If the bake is ever revived it must come
back as the compare-mode optimization described above, behind the live path.

Test: three-boat scene — assert budget omits excess entities; assert overlap
frame-stamp picks the first-placed; assert flattened boat contributes no
actor commands and no pick hashes.

Research correction (2026-09-06): revision-239 `Statics.method2832` initializes
the count for each group call and calls `method1449` only below the cap.
Flattening over-budget entities was an error in the original research notes;
overlap flattening remains required separately. `Statics.method11128` implements
7900/7901. This follows the plan's instruction to mirror the deob exactly.
The same reinspection separates actor overlap (`method5535`, oriented footprint)
from entity overlap (`method8755` / `class521.method11500`, fine-unit enclosing
rectangles), correcting the earlier description of both as oriented-box tests.

### C5 — actors aboard, clicks, camera

1. **Membership is geometric** (deob rule): a player/NPC materializes in
   whichever view's base rectangle contains its global position; local player
   aboard ⇔ inside a non-zero view. The entity pipeline is already
   world-parameterized — run `World_MoversAdvance`/`World_Cycle`/dynamic
   registration per live view. `local_pid`-holding view becomes an App-level
   question (one int: `app->aboard_view`).
2. **Clicks**: extend the pick hash with a world-view id (deob: bits 52–63 of
   the 64-bit hash; if our pick hash is narrower, add a side table indexed by
   pick slot — open question OQ2). Implement the hash-override for sub-scene
   geometry honouring config click modes 0–3, one boat menu entry per frame,
   type-4 menu ops from config (gated by the 5-bit op mask), flattened boats
   unclickable.
3. **Camera**: focus = local player's position transformed through the boat
   transform into root space (smoothed /16 within ±500 units, else snap);
   focus height = deck height + root terrain height under the boat. Switch
   the root scene's roof-removal mode while aboard (deob scene-mode flip).

Test: click-routing unit test (hash → view id → op); camera focus math test
around the smoothing threshold.

---

## Server phases

### S0 — per-player scene/collision window (prerequisite)

The world-singleton scene window (`srv->zone_x/zone_z`) must become
per-player (bounded: `TORIRSSERVER_PLAYER_MAX` = 8), or a boat instance and a
mainland player thrash `SceneBuild` against each other every tick. The
remote-view machinery (`ToriRSServer_WorldRemoteViewStart`,
`torirs_server_world.c:2695`) is the working precedent for a player-scoped
scene. `maybe_rebuild` (`world.c:2614`) moves to per-player margins.

Test: two players >70 tiles apart, selftest asserts no rebuild thrash and
both collision queries stay correct (this is currently documented as "a
genuine multiplayer limit" — S0 deletes that limit).

### S1 — vessel entity + mover

1. `struct ToriRSServerVessel`: index (1-based), config id, deck instance
   handle (from `MapInstanceAlloc`), root-world fine position, angle 0–2047,
   commanded heading (16-point) and speed tier, priority group, owner uid,
   level. Registry sized modestly (e.g. 32).
2. **Mover, per tick**: turn toward commanded heading capped (e.g. 128
   units/tick per hull class → 2/4/6 ticks per 90°), then advance
   `dx = −sin(θ)·speed`, `dz = −cos(θ)·speed` in fine units, quantized to
   32-unit (quarter-tile) multiples; speed tiers 64/128/192/256 fine
   units/tick (0.5/1/1.5/2 t/t). Terrain collision: footprint test (16-bucket
   oriented box from config bounds) against a water/blocked map; a blocked
   step stops the boat. No boat-vs-boat collision (deob-faithful).
3. Deck↔root coordinate projection helpers: deck tile → root fine position
   through the vessel transform (and inverse). This is the server twin of the
   client's descent transform and the basis for S2's visibility work.

Test: mover unit tests (arc turn traces, quarter-tile quantization, shortest
arc); projection round-trip tests at all 16 headings.

### S2 — protocol

1. Encode `WORLDENTITY_INFO_V7` (rsprot transcription vendored): per-client,
   per-view entity list — spawn trailer (id, size nibbles, priority, config,
   absolute transform), per-tick delta ops (enqueue = smooth, snap =
   teleport), despawn by trailing trim. Wire slots go into
   `struct ToriRSServerWirePayload` for osrs239 only; osrs230 refuses the
   packet (per the wire-vtable convention).
2. Encode `REBUILD_WORLDENTITY_V4` from the vessel's deck instance zones
   (the existing REBUILD_REGION zone-descriptor machinery re-targeted, base
   zone coords from the instance).
3. `SET_ACTIVE_WORLD` gets real indices: deck zone updates flush inside a
   set-active-world sandwich addressed to the vessel's view; root zones
   unchanged. (The selftested root=0 ordering stays valid.)
4. **Cross-world visibility**: `player_in_view` and PLAYER_INFO/NPC_INFO
   positions project deck players through the vessel transform to root
   coordinates for view tests and low-res coords, so shore and deck players
   see each other. This is the largest single work item on the server.

Test: extend `torirs_server_world_selftest.c` — vessel spawn → client
receives rebuild-worldentity + info spawn; move command → op-2 deltas across
ticks sum to the commanded path; deck player visible to shore player at the
projected root coordinate.

### S3 — content

Sailing scripts live in OSRS-Content (`server/scripts/sailing/`, patterned on
`transport_charter`/`canoes`): dock gangplank boarding (telejump into deck
instance + vessel spawn), helm op → navigation mode (clicks steer: commanded
heading/speed → vessel), disembark, boat persistence varbits. New serverscript
opcodes in the engine band: `vessel_spawn`, `vessel_move`, `vessel_heading`,
`vessel_speed`, `vessel_free`, mirroring the `map_instance_*` family
(`ss_opcode.h`).

The OSRS-Content submodule is initialized. Native facilities and interface
content are included; the completion audit separately verifies persistence and
the complete dock-to-ocean flow.

#### Social, permission and interface facts corrected from the cache (2026-09-07)

Four claims that earlier drafts of this plan and the handoff got wrong. Each was
settled from the shipped cache or from source, not from a summary.

- **Passenger role is 3, not 2.** The sidepanel role varbit is **19233**
  (`sailing_sidepanel_player_role`). The crew NPC shells — npc 15255 and nine
  siblings — carry `multivarbit=sailing_sidepanel_player_role` and state a real
  npc only at rungs **0, 3, 6 and 10**; both this client's
  `VarPManager_ResolveTransform` and the reference's `class393` index
  `configs[value]`, so rung **2 is −1** and a passenger saw a deck with no crew
  on it at all. CS2 8732 tests 10 and 6, the captain-only affordances test 10
  alone, 0 is the not-aboard default, and 3 is the only remaining rendered rung
  (3/6/10 are `0b0011`/`0b0110`/`0b1010` — one shared aboard bit plus one role
  bit). Published from the sailing varbit sync in
  `ToriRSServer_WorldRefreshObservation`. Evidence:
  `sailing_validation/social/roles-results.json`.
- **Private chat reaches the client through varbit 13674, not through a new
  login opcode.** There is no need to implement chat opcode 5. Varbit **13674**
  = `chat_filter_private` = bits **13..15 of varp 1054** (`chat_filter_clan`).
  Proc 113 `torirs_chatbox_layout` reads it — `if (chat_getfilter_private !
  %varbit13674) { ~chat_set_filter_184(3, %varbit13674); }` then
  `~redraw_chat_buttons` — and that proc is `interface_162:0`'s
  `if_setonvartransmit` hook with `var1054` first in its list, so the varp
  transmit *is* the repaint trigger. Values are the chat button's own: op 3/4/5
  → 0 *Show all* / 1 *Show friends* / 2 *Show none*. The server writes it in
  `ToriRSServer_WorldSocialLogin` (hydrating the saved `[chat]` section) and
  `handle_chat_setmode`. **Content dependency:** varp 1054 must be declared
  `transmit=yes` (and `scope=temp` — the `[chat]` save section is the one
  persistent carrier) or the write never leaves the server. Detail:
  `docs/FRIENDS_PRIVATE_CHAT.md` §11.3–11.5.
- **A targeted send must carry the WIRE component identity and the published
  player index, never the runtime tree id.** `app->targetsel.component_id` was
  put straight on the wire, and for a `CC_CREATE` child that is a
  runtime-allocated id (measured **937|49210**) the server has never heard of,
  so `[opplayert,sailing_sidepanel:crew_content_clicklayer]` could not fire.
  All five targeted sends (`OPPLAYERT`/`OPNPCT`/`OPLOCT`/`OPOBJT`/`OPHELDT`)
  now resolve the wire identity through `app_if_button_target` — the same
  helper `IF_BUTTON` already used — so the packet names **937:10**. The player
  is named by the **GPI index the server published**
  (`ToriRSServer_WirePlayerIndex`); `handle_opplayert` reads only
  `(index, component)`. `net_out.c` is deliberately **unchanged**: rev-239
  `OPPLAYERT` does carry a selected-sub field (`g2Alt2`), but
  `mock239_inbound.c` reads `sub == 0xffff && obj == 0xffff` as the sentinel
  meaning "a target or spell rather than an item", so writing the child index
  there would route the grant to `OPPLAYERU`. Evidence:
  `sailing_validation/social/editnav-arming-results.json`.
- **IF3 target priority and the row gate.** `UITree_ApplyTargetPriority`
  (`src/ui/uitree.c:4819`) follows the reference exactly: **−1 resets to the
  default 4**, 1..32 stores *value − 1*, and any other value is ignored. The
  default matters — `Widget.field4122` starts at 4 in the rev-239 gamepack and
  `method5229` uses it as the boundary between ordinary component operations
  and `CC_OP_LOW_PRIORITY`, so leaving `calloc`'s zero there demoted ops 2..4 on
  every script-created cell. Separately, the **effective** target mask of an
  IF3 node is the decoded `behavior.target_mask` OR'd with
  **`IF_SETEVENTS` bits 11..16** — `(App_IfEventsGetEffective(app, com) >>
  TORIRS_TARGET_MASK_IF3_SHIFT) & TORIRS_TARGET_MASK_IF3_BITS`, shift 11, mask
  `0x3F` (`src/engine/torirs_types.h:1012-1013`). Reading only the cache mask
  refused the "Edit-navigator" row outright, because a `CC_CREATE` child's
  decoded mask is 0.

---

## Order of work and integration checkpoints

```
C0 ──► C1 ──► C2 ──► C3 ──► C4 ──► C5
                      ▲
S0 ──► S1 ──► S2 ─────┘      S2 ──► S3
```

Checkpoint A (after C2 + S1): boat world builds offline from a scripted
rebuild; vessel moves in a server selftest. No visuals yet.
Checkpoint B (after C3 + S2): a raft visibly sails an arc in the client
against the embedded server; deck locs painter-sort correctly against shore
locs.
Checkpoint C (after C4): two overlapping boats — topmost full, other
flattened flat-colour.
Checkpoint D (after C5 + S3): board at a dock, take the helm, steer, disembark.

**All four are met (2026-09-07).** Evidence, in the same order:

- **A — met.** Client: `test-wev` and `test-wev-rebuild` build a real
  `cache.osrs239` deck offline into the boat world, no skips. Server: the mover
  rows in the sailing suite on the shared server — "tick *n* turns to exactly
  *d*" and "tick *n* lands on the quarter-tile quantum", eight each, plus "the
  blocked sail parks short of the land tile".
- **B — met.** Ordering by `test-sailing-paint-order` and
  `test-painters-world-entity`; the arc itself re-captured on the final client
  and reviewed — `collision-underway.png`, `deck-sailing.png`,
  `lifecycle-native-steering.png`, and shore ordering in
  `client/client-raised-coast.png`.
- **C — met.** `client/client-results.json` with `three-overlap`,
  `priority-two`, `budget-one`, `budget-zero` all reviewed, and confirmed on the
  GPU lane (`gpu/gl3-three-overlap.png` vs `gpu/soft3d-three-overlap.png`
  flatten the same vessel with identical `wev` counters).
- **D — met.** The whole flow walked natively on the final client: shore
  gangplank → native selector 934 → Board → **physical** helm loc ("Navigate
  Helm") → Set sails → voyage out and back → disembark ("You walk down the
  gangplank.", combat tab restored) → raw logout → fresh `--resume-save`.
  Turning at a berth is proven with four headings covering a full 360° at a
  fixed fine position with no grounding. The former residual **LIFE-1** — the
  logged-out client's own auto-reconnect — is fixed and re-proven on the 09:35
  shared pair.

## Constraints that bind this work

- CLAUDE.md conventions: `assert()` contract violations (one per condition),
  never early-return on bad parameters; allocation failure is an assert;
  deallocators accept NULL; no tests pinning silent-failure behaviour.
- No 64-bit arithmetic inside `3rd/toridraw` — wide pick hashes and view ids
  stay in the game layer; the rasterizer sees only per-view-transformed
  positions.
- The gouraud span's 4-pixel palette blocking is pinned visible output
  (`toridraw_scanline_parity_test.c`) — the boat path must not perturb span
  rendering.
- rev-239 lane only for the new packets; the osrs230 wire vtable refuses them
  rather than mis-encoding.

**All four constraints hold, checked rather than assumed (2026-09-07):**

- *Conventions.* Read-only review of every sailing hunk and every new sailing
  file. Each allocation is asserted at the allocation
  (`torirs_frame.c`, `torirs_frame_flat.u.h:71`, five separate asserts in
  `sailing_paint_order.u.h`); pointer parameters are asserted one condition per
  assert; the only NULL-tolerant guard is inside a deallocator, which the
  convention exempts. The one forbidden pattern the audit found — an allocation
  failure handled as an `if`, with a `SELFTEST_CHECK` pinning the silent-failure
  behaviour, in `sailing_lifecycle_selftest.u.h` — has been deleted and is now a
  plain `assert`.
- *No 64-bit arithmetic in `3rd/toridraw`.* `git diff --stat` shows **no**
  change under `3rd/toridraw` at all.
- *4-pixel palette blocking.* No change to any `src/painters/painters*.c` or
  `.u.c`; the only `src/painters` entry in the tree is the new test.
  `test-scanline` passes every variant-parity check, and the zero-boat A/B
  produced **one unique image hash per scene across 72 runs**.
- *rev-239 only.* Pinned by an explicit negative case, run on the shared server:
  "revision230 refuses vessel packets and does not mutate v239 observer
  tracking".

## Open questions / risks

- **OQ1**: does `cache.osrs239` actually contain config archive 72 and the
  boat staging-region map squares? Verify first thing in C1 — if absent, the
  cache needs updating and everything downstream slips.
- **OQ2**: current client pick-hash width — if narrower than 64 bits, the
  world-view id needs a side table (C5).
- **OQ3**: nested world entities — deob supports 1 nested entity per
  sub-view, but the deob's nested *render* path is ambiguous (decompiler
  landmine noted in SAILING.md §5). The explicit-stack design supports
  nesting structurally; defer validating nested rendering until a real use
  case (trawling shoal under a boat) exists.
- **OQ4**: sharing one `ToriDraw_Scene` across views assumes element-id
  capacity headroom for ~16 small decks; verify pool sizing in C2.
- **R1**: S2's cross-world visibility touches PLAYER_INFO encoding — the
  highest-regression-risk area; the v5 delta state per player
  (`torirs_server.h:2861`) must be kept coherent across world switches.
- **R2**: C3 adds nested world traversal to the hot paint loop —
  benchmark `render` p50 on the bench scenes before/after; the restructure
  must be performance-neutral when zero boats are live.

**All four open questions are answered and both risks are discharged
(2026-09-07):**

- **OQ1 — yes.** `test-wev` and `test-wev-rebuild` load archive 72 and the deck
  map squares out of the real `cache.osrs239` with no skips: 14 records, four
  pinned, rotated footprints agreeing at all 16 headings.
- **OQ2 — a side table, and it works.** `test-pick-level` proves the
  side-channel view id across all four click modes plus the aboard override and
  the deck-local terrain frame; `test-minimenu-world` proves the row it produces
  cannot invent hull operations.
- **OQ3 — structurally supported, content validation still deferred.**
  `test_nesting_to_the_registry_bound` nests to the bound with one BEGIN per
  view, every level painted exactly once and the innermost strictly inside the
  outermost, and refuses at the 16-context boundary. Native *nested content* is
  still deferred by the plan and is not claimed.
- **OQ4 — yes, with headroom measured.** Root plus 15 real-cache decks hold
  **19,977 static elements** in one shared scene, with one actor migrated
  through all 15 pools and an isolated reverse despawn, no SKIP.
- **R1 — retired.** The final contract is projected root coordinates for
  visibility and coarse presence, and *raw* coordinates for high-resolution
  PLAYER_INFO. A standing passenger gets no fake walk when the hull moves, and a
  rider is never removed and re-added. Proven on the wire by the server suite
  and on screen by the multiplayer captures.
- **R2 — neutral.** 12 scenes × 6 runs: identical pixels and identical work
  counts everywhere, median render Δ +0.32 %. The two scenes that exceeded noise
  did not reproduce when re-measured alone. The largest reproducible per-scene
  render difference is under 0.07 ms on stages of 0.9–3.6 ms.

The detailed C3.2 design was revised in commit `6e77bd128` to preserve the
straight-line painter body and use bounded nested calls. Its checked stack
guards cycles, painter aliases and the 16-view bound. The older scope sentence
claiming no recursion and this risk's resumable-state-machine wording did not
describe that revision.
