# The root rebuild did not drain EntityRemoved (debug-client abort)

Worker `drain`, 2026-09-06. Fixed, tested, and re-verified end to end under a
debug (`OPT=0`) client with asserts live.

## Symptom

`tools/sailing_collision_acceptance.py` passed every ocean / underway /
mid-motion / shore / restore check and then killed the client at its
land-spawn-rejection step:

```
torirsserver: cheat 'vesselgoto 3072 3133 0'
message_game: Teleported to 3072,3133 level 0.
torirsserver: scene built at zone 384,391 (base 3024,3080 — 4672 locs)
world_load: 4 chunks, 11 underlays, 28 overlays, 26 textures, 443 locs, 430 models, 19 seqs
Assertion failed: (world->event_count == 0 && "drain EntityRemoved before
World_ResetSceneAlloc"), function World_ResetSceneAlloc, file world.c, line 515.
```

Backtrace (macOS crash report for `torirs_drain`, 2026-09-06 20:36:33):

```
World_ResetSceneAlloc            world.c:515
World_ResetScene                 world.c:590
WorldBuilder_RebuildCenterzoneBegin  world_builder.c:473
WorldBuilder_RebuildCenterzone   world_builder.c:1109
Task_WorldLoad_Run               task_world_load.c:476
task_run                         asyncio.h:402
Task_GameProtoExec_Run           task_gameproto_exec.c:265
task_run                         asyncio.h:402
ToriRS_TaskQueue_RunTask         asyncio.h:776
TaskRunner_Step / SettleFrame    task_runner.h:167 / 293
app_pump_net_packets             app.c:16658
```

A release client did not abort — it silently reset the scene with the queue
full, which drops the queued removals on the floor and orphans the DYNAMIC
scene elements they name.

## Cause

`World_ResetSceneAlloc` asserts the world's `EntityRemoved` queue is empty,
because the queue names scene elements that only the render side can free.
Every rebuild path drained first *except the root one*:

- boat deck rebuild — `task_gameproto_exec.c` REBUILD_WORLDENTITY branch,
  `App_WorldDrainEntityRemovedFor(app, view->world)` before its load;
- boat despawn — `App_WevDespawn` drains the departing view;
- offline / editor load — `app_world_load_begin` calls
  `App_WorldDrainEntityRemoved(app)`;
- **root REBUILD_NORMAL / REBUILD_REGION — nothing.**

The per-tick drain (`app_world_frame` → `App_WorldDrainEntityRemoved`) cannot
cover the gap: it sits behind `app->world_active && app->world_view_valid`,
which a rebuild has already closed, and the packets that queue a removal exec
on the same serial queue as the rebuild that follows them. So a despawn that
arrives in the same packet pump as a teleport-driven rebuild — the acceptance
tool's boat despawn followed by `cheat vesselgoto 3072 3133 0` — reaches
`World_ResetSceneAlloc` with `event_count == 1`.

## Fix

`src/game/task_gameproto_exec.c`, REBUILD_NORMAL / REBUILD_REGION branch:
drain the root world immediately before the world-load await, exactly where
the boat branch drains its own view.

```c
self->prev_base_z = self->had_world ? rebuild_view(self)->world->_base_tile_z : 0;
/* The same C2 drain rule the boat branch below obeys, for the ROOT world ... */
App_WorldDrainEntityRemovedFor(app, rebuild_view(self)->world);
PT_TASK_AWAITSELF_IF(CreateTask_WorldLoad(...));
```

Draining, not clearing, is the point: `App_WorldDrainEntityRemovedFor` is what
hands each removal to the plugin host (`PluginHost_NpcDespawn` with the
queue-owned npc snapshot), drops the element's entity spotanim and pending seq
bind, and calls `ToriDraw_SceneElementRemove` — the element release the reset
would otherwise make unreachable.

Audit of the other `World_ResetScene*` callers: `App_WevSpawn` resets a
freshly created `World` (queue empty by construction), `app_world_load_begin`
already drains, `App_WevDespawn` already drains, and the boat rebuild already
drains. No other gap. The assert was not weakened; it was reformatted to the
project's one-condition-per-assert rule (`assert(world->event_count == 0);`
with the message moved into the comment above it, `src/world/world.c:513-518`).

## Proof

**Unit test** — `src/world/test/wev_rebuild_test.c`,
`test_root_rebuild_drains_entity_removed()` (runs under `make -C src
test-wev-rebuild`; cache-free, never skips). It spawns an npc with a live
DYNAMIC element on a root world, despawns it, asserts one queued
`EntityRemoved` carrying the npc copy, runs the production
`App_WorldDrainEntityRemovedFor`, asserts the queue emptied *and the scene
element was freed*, then runs `WorldBuilder_RebuildCenterzoneBegin` — the exact
frame that aborted — and asserts the scene reallocated.

Negative control (temporary edit, reverted): with the drain call removed the
test aborts at `Assertion failed: (world->event_count == 0), function
World_ResetSceneAlloc, file world.c, line 518.` — i.e. the test does fail
without the fix.

```
make -C src -j4 OPT=0 EMBED_SERVER=1 PLATFORM_OBJ_BASE=build_drain test-wev-rebuild
  ok - the root rebuild drains EntityRemoved before World_ResetSceneAlloc
  wev_rebuild_test: all passed
make -C src -j4 OPT=0 EMBED_SERVER=1 PLATFORM_OBJ_BASE=build_drain test-wev
  wev_test: all passed
```

**Runtime** — debug client `src/torirs_drain` (`OPT=0 EMBED_SERVER=1`,
asserts live), isolated pack `/tmp/sailing-drain-pack`, headless session
`/tmp/sailing-drain-accept`:

| tool | result |
|---|---|
| `tools/sailing_collision_acceptance.py` | `ok: true`, all checks, 1923 ms (the same run that aborted before the fix) |
| `tools/sailing_cargo_acceptance.py` | `ok: true`, 10 checks |
| `tools/sailing_deck_acceptance.py` | `ok: true`, 773 ms |

All three ran in one client, which was still alive and serving `status` after
the last one; `client.log` contains no assertion of any kind.

**Visual** (soft3d, 807x503):

- `world-drain-land-rejected.png` — the frame that used to be the abort. After
  `vesselgoto 3072 3133`, the post-teleport root rebuild completes and draws
  the real coastline: grass and cliff face, several trees, the player standing
  on the shore inside the teal pick highlight, and the sidebar back on the
  combat tab because the player is no longer aboard. Chat reads "The boat runs
  aground. / Teleported to 3072,3133 level 0."
- `world-drain-ocean-restored.png` — after `restore collision_ocean`: the skiff
  back at 3072,3160 on open water, sail furled to the mast, red pennant up,
  player amidships in the teal deck highlight, Facilities sidebar with Steering
  and 80/80 hull, minimap showing the boat alone on blue.
- `world-drain-deck-net-installed.png` — deck acceptance after
  `cheat sailnetfixture 4`: the same skiff with the trawling-net mesh now
  draped at the port bow beside the mast (absent in `deck-baseline.png`), hull
  patched to 80/80 in the chat log.

Full tool output for this run is under `/tmp/sailing-drain-out/{collision2,cargo,deck}`
(scratch, not published).
