# The debug client's terrain-id assert on the ocean fixture

A debug (`OPT=0`, asserts on) embedded client aborted a few frames into the
ocean fixture (skiff at 3072,3160) at `src/render/torirs_frame.c:2208`, inside
`frame_lookahead_element_id` on the `TORIRS_FRAME_TERRAIN_ID` default path:

```
Assertion failed: (element_id == World_TerrainElementAt(
    frame->view_stack[frame->view_depth].world,
    cmd->_terrain._bf_terrain_x, ..._z, ..._y)),
    function frame_lookahead_element_id, file torirs_frame.c, line 2208.
```

## Root cause — the prefetch pipeline reads across an unconsumed view marker

The abort did **not** come from the command's own turn (line 2533). The macOS
crash report puts the caller at `try_emit_world_draw_model` line 2482, the
`ToriDraw_SceneElementPrefetchNode(..., frame_lookahead_element_id(frame, cur +
depth))` step of the emit loop's four-deep prefetch pipeline.

That pipeline runs at the **top** of the loop body, and the `BEGIN_WORLD` /
`END_WORLD` markers are consumed **below** it. Its reach walk stopped at a
marker, but only looked at commands `cur + 1 .. cur + depth` — never at `cur`
itself. So when the current command *was* the marker, the walk saw three
ordinary commands after it and resolved them while the view the marker opens
was still not on the stack. A boat-deck terrain command was therefore resolved
against the **root** world, and the ring remembered that answer for the
command's own turn.

Instrumented reproduction (temporary print, since removed), first failing frame:

```
TEMPDIAG idx=715/822 cur=712 curkind=4 kind=2 depth=0 view_id=0
         world=0x72191c000 rootworld=0x72191c000 x=3 z=6 y=1
         cmd_id=268437254 world_id=-1
  cmd[712] kind=4 ent=1        <- BEGIN_WORLD, view 1 (the skiff's deck)
  cmd[713] kind=1 ...          <- deck loc
  cmd[714] kind=1 ...          <- deck loc
  cmd[715] kind=2 tx=3 tz=6 ty=1   <== HERE: deck terrain, resolved at depth 0
```

`cur = 712` is the deck's `BEGIN_WORLD`; command 715 is the deck world's own
terrain tile (3,6 on level 1), and the root world has no such tile, so
`World_TerrainElementAt` answered −1 while the command carried the deck
painter's id.

Release builds never tripped it because the default arm takes the id from the
command (`cmd->_element_id`), which is view-independent and correct; only the
assert consults the world. The `TORIRS_FRAME_TERRAIN_ID=0` control arm, which
*does* resolve through the world, would have dropped those deck tiles as dead
ids — the same bug, silently.

This is not the sailing paint-order rewrite: `sailing_paint_order_ground` moves
parent ground only to a position immediately before the child's marker, which
is still inside the parent's view scope, and `sailing_paint_order_flat`
relocates whole balanced marker subtrees. Both leave every command in the scope
it was painted in.

## Fix

`src/render/torirs_frame.c`, the emit loop's prefetch reach walk: start the
marker test at `cur` instead of `cur + 1`. A marker command now prefetches
nothing; the command after it resolves at its own turn, under its own view. The
assert is unchanged. Nothing in the command stream, the view stack or the drawn
set changes — only which ids the prefetch pipeline resolves early.

The neighbouring hole was checked and is safe: a view the App never bound
(`xf->live == 0`) inherits the parent's world in `frame_view_push`, but the
painter emits such a descent as an adjacent empty `BEGIN`/`END` pair
(`test_unbound_view_emits_an_empty_pair`), so no terrain command is ever
resolved inside one.

## Proof

Private binaries, objdir `build_frame`, isolated pack `/tmp/sailing-frame-pack`
(30,076 scripts, `21e8598069…`):

- `src/torirs_frame` — `OPT=0 EMBED_SERVER=1`, asserts on.
- `src/torirs_frame_opt` — `OPT=1 EMBED_SERVER=1`.

1. **Before:** the debug client aborted during fixture startup, three crash
   reports at `torirs_frame.c:2203/2241/2243`, all reached from the prefetch
   call site in `try_emit_world_draw_model`.
2. **After — 360 cycles under way with asserts on.** `/tmp/sailing-frame-repro`,
   `cheat vesselsail H 2` at headings 0, 2, 4, 6, 8, 12 with `step 60` between
   each; the hull moved 393280,404544 → 392736,404608 and turned through six of
   the sixteen headings without an abort.
   `frame-assert-underway-h00.png` (heading 0, hull north, sail set, deck
   facility marker on the planking), `-h06.png` (hull rotated west, coastline in
   frame, deck and rigging composited over the water) and `-h12.png` (hull east,
   boom swung across) were opened and reviewed: the deck draws over the water it
   sits on, no ocean tile punches through the planking, and the facility marker
   turns with the deck.
3. **`tools/sailing_client_acceptance.py` against the debug binary: PASS**,
   9 captures, 2696 ms (`frame-assert-client-results.json`; full output in
   `/tmp/sailing-frame-client-out`). Reviewed
   `frame-assert-acceptance-three-overlap.png` (three skiffs, correct
   compositing, three minimap markers) and
   `frame-assert-acceptance-raised-coast.png` (hull stopped by the boat
   collision map at the raised shore, HP 78/80, "The boat runs aground", the
   raised terrain correctly *not* drawn over the nearer hull).
4. **Unit gates**, `make -C src PLATFORM_OBJ_BASE=build_frame …`, all `rc=0`:
   `test-frame-flat`, `test-sailing-paint-order`, `test-painters-world-entity`,
   `test-pick-level`. The zero-boat byte equality still holds:
   `sailing_paint_order_test` asserts `memcmp` against the untouched stream for
   `count == 0` and passes, and `test-painters-world-entity`'s
   "repaint emits the same stream" passes. (The fix cannot affect the stream at
   all — it only changes which ids are resolved early.)
5. **Debug vs release, same paused frame.** Both binaries ran the identical
   sequence (`pause`, `camera 1024 256 1200`, `cheat vesselsail 4 2`,
   `step 90`, `capture`). The `wev` readout is byte-identical: one view, one
   marker, 7 model / 10 terrain / 1 actor commands, bounds
   `392685,404046,393373,404534`, yaw 379. The captures
   `frame-assert-pose-debug.png` and `frame-assert-pose-release.png` differ in
   **4,034 pixels, every one of them inside the debug HUD's own
   `Frame:/Effective FPS:/Memory:` readout or the three chat lines that print
   the session directory** (`…-dbgcap` vs `…-optcap3`); the 3D scene and the
   whole interface are pixel-identical. `frame-assert-pose-diff-x8.png` is the
   8× amplified difference and is black everywhere else.

   Caveat, recorded because it bit this comparison: the fixture's rendered
   image is not bit-reproducible across processes. Two runs of the *same*
   release binary landed on different animated-water phases and then differed
   in 145k pixels at magnitude ≤ 6, while a third landed on the same phase as
   the debug run and matched it exactly. Compare captures only when the phases
   agree, or compare `wev` counts instead.

## Still open (not this worker's files)

`tools/sailing_collision_acceptance.py` against the debug binary reached its
final land-spawn-rejection step and then aborted in a **different** debug-only
assert, `src/world/world.c:515`
(`world->event_count == 0 && "drain EntityRemoved before World_ResetSceneAlloc"`),
on the **root** rebuild path:

```
World_ResetSceneAlloc  world.c:515
World_ResetScene       world.c:590
WorldBuilder_RebuildCenterzoneBegin  world_builder.c:473
WorldBuilder_RebuildCenterzone       world_builder.c:1109
Task_WorldLoad_Run     task_world_load.c:476
Task_GameProtoExec_Run task_gameproto_exec.c:265
```

The WEV rebuild path drains first (`task_gameproto_exec.c:381`,
`App_WorldDrainEntityRemovedFor(app, view->world)`); the root centrezone
rebuild has no equivalent drain of `app->world`, so a teleport that lands while
an `EntityRemoved` is still queued (here: `cheat vesselgoto 3072 3133 0` right
after a boat despawn) aborts a debug client. Everything before that step in the
collision acceptance passed. This is in world/lifecycle code, not the frame or
paint-order files.
