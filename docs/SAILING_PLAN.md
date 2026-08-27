# Sailing implementation plan

Companion to `docs/SAILING.md` (the research doc — read it first; §5 has the
deob ground truth this plan is built on). Work happens on the
`worktree-sailing` branch.

## Scope

Client: world entities — each boat is its own world (own `struct World`, own
`struct Painter`), inserted into the main painter's grid as a regular loc, with
the painter descending into boat worlds via an **explicit stack** (no
recursion), flattened "billboard" rendering for overlapped/over-budget boats,
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
3. Rebuild routing: the deob prefixes the rebuild with a world-entity id +
   plane; wire the same into our REBUILD handling (`rs_gameproto_exec.c:1142`),
   erroring loudly (assert) on an unknown id.

Test: existing selftests still pass with the registry in place and only the
root view live; add a decode test for SET_ACTIVE_WORLD.

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
3. Drain rule: boats must drain their entity-removed queue before rebuilds
   (`World_ResetSceneAlloc` asserts `event_count == 0`).

Test: offline harness that hand-feeds a rebuild for a 1-zone raft deck and
asserts tile heights/locs land in the boat world, not the root.

### C3 — painter descent with an explicit stack

The heart of the feature. Design:

1. **Pseudo-loc insertion, per frame**: after interpolation, insert each
   entity into its *parent* painter as a temporary 1×1 loc at
   `(x>>7, z>>7)` via the existing dynamic-registration pass
   (`World_CycleRegisterPainterDynamics`), with a reserved element-id range
   (or a flag bit) marking "world entity N". Height = parent terrain under
   the boat. This gives painter-correct ordering against real locs, actors
   and projectiles for free — same as the deob's radius-60 trick.
2. **Descent**: convert the paint driver to a loop over an explicit stack of
   paint contexts. `PainterBucketCtx` already holds per-painter traversal
   state; give it explicit resume indices so a paint can be suspended.
   `stack[0]` = root painter. When the drain pops a world-entity element, it
   emits `CMD_BEGIN_WORLD(view id)` into the `PaintersBuffer`, pushes the
   boat's painter context (camera pre-transformed into boat space — inverse
   yaw+translate applied to eye and focus, computed once per boat per frame),
   and the outer loop continues with the new top of stack. When a context
   finishes, emit `CMD_END_WORLD`, pop, resume. Nested boats come free
   (depth cap = 16 views, asserted).
3. **De-static the painter**: move the three qsort-context statics
   (`painters.c:138-140`) into `struct Painter` (or pass-through context) so
   a suspended paint can't be corrupted by the inner one.
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
   groups 2, 0, 1; full-detail budget (server-configured count, CS2-readable);
   group-1 entities additionally flatten when a player/opted-in NPC/
   already-drawn entity overlaps them (16-bucket oriented-box test +
   per-frame scene stamp — first placed wins, i.e. "topmost renders full").
3. Flat-colour override: a per-view HSL override honoured by the emit path
   (model-level recolour at emit; the toridraw HSL pipeline already exists).

Optional follow-up (perf, software rasterizer): bake a merged deck model per
boat with `ToriDraw_ModelNewMerge`, invalidated on deck loc change, drawn
instead of the flattened sub-scene. Deob-faithful flatten ships first; the
bake is an optimization with a compare mode against the flatten render.

Test: three-boat scene — assert budget forces flatten; assert overlap
frame-stamp picks the first-placed; assert flattened boat contributes no
actor commands and no pick hashes.

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

Note: the OSRS-Content submodule is not checked out in this worktree — S3
needs it initialized (`git submodule update --init OSRS-Content`).

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
- **R2**: C3 converts the hot paint loop into a resumable state machine —
  benchmark `render` p50 on the bench scenes before/after; the restructure
  must be performance-neutral when zero boats are live.
