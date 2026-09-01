# Frame budget plan: 15 ms -> under 10 ms (Moto X, painter, Lumbridge)

Companion to `ARMVX_KERNEL_STATE.md` (the face sort's own log). Numbers are
from the `ab3` simpleperf profile of 2026-09-01 (cpu-clock, 1 kHz, 10 s,
Lumbridge, camera still, fast path on), and the method every step below is
gated by is the client A/B in that file: one binary, an env toggle, alternate
arms, ms/frame = samples / frames-in-window.

## What the number is

The loop is paced to 20 ms (`frame_period_ms`, 50 fps). The main thread is
on-CPU 74% of the time: **15.1 ms of work per frame**, ~1.1 ms of it inside
`eglSwapBuffers`, then ~4 ms of pacer sleep. Everything runs on one thread
(99.8% of samples); the second Krait core is idle. So "under 10 ms" means
cutting CPU work by >= 5.1 ms per frame -- the plan aims at -5.5 to -6 ms so
the result is under 10 with the profiler's own ~3% overhead removed.

Two kinds of frame matter and they cost differently:

- **still frame** (camera and static scene unchanged, entities moving):
  what the profile measured. On a phone this is MOST frames -- the camera
  moves only while a finger drags.
- **moving frame** (camera dragged): every static element's projection,
  sort and emit is genuinely new. The plan has to pay here too, or the
  drag is what the user feels.

## Ledger (ms of the 15.1)

| bucket | now | target | lever(s) | conf. |
|---|---:|---:|---|---|
| GL driver + kernel | 2.40 | 1.30 | 2d: one UI stream, range draws, no per-batch attrib rebind, no orphaning | med |
| face sort | 2.10 | 1.30 | 1e per-model overhead; K16 gather; (2b: cached order on moving frames -> 0.6) | med |
| projection + cull | 2.05 | 1.20 | 1a bounds inline in the scene element; 1b vectorised tail (no software divides) | high |
| gles2 renderer CPU | 1.50 | 1.10 | 2e cheaper per-command path; 2a replay for static elements | med |
| UI tree + fonts | 1.35 | 0.90 | 2f slot cache, text-run cache | med |
| world paint walk | 1.35 | 0.40 | 2c walk cached per (camera tile, level, cullspan) | high |
| frame command bus | 1.30 | 0.80 | 2e terrain ids from the walk, hoisted view stack; scrollbar tiled quad | high |
| libc (memcpy, strcmp, memset, mutex, getenv) | 0.80 | 0.50 | 1c attribute and cache | high |
| anim / texture animate / bake | 0.40 | 0.40 | -- | |
| scripting | 0.35 | 0.35 | -- | |
| PLT + software divides | 0.35 | 0.20 | 1b/1d idiv build variant or no divides | med |
| long tail | 1.20 | 1.10 | falls with the rest | |
| **total** | **15.1** | **9.55** | | |

Still frames additionally get **2a** (retained static world): projection,
sort, walk, bus and dispatch of the ~1,300 static elements are skipped
outright and replayed, which takes a still frame to ~7-8 ms on its own.
Without 2b a moving frame lands at ~10.3; with it ~9.5. 2b is therefore the
step that decides the moving frame, and it carries the only visual risk in
the plan, so it is measured last and gated hardest.

## Rule (2026-09-01, from Matthew): no optimisation may depend on the camera being still

"DO NOT EVER DO OPTIMIZATIONS THAT RELY ON A STABLE CAMERA - THAT IS
BENCHMAXXING." A frame's cost is what it costs while the camera is being
dragged. So the plan's **2a (retained static world on still frames), 2b
(camera-quantised face order, re-sorted only when the bucket changes) and
2c (paint walk cached per camera tile) are withdrawn**, and the still-frame
column of the ledger is void. The moving-frame ledger is the only target:
15.1 → 9.55 needs every remaining per-frame lever (2d, 2e, 2f, the sort's
gather, the projection's per-model fixed cost) to land, and the second core
(Phase 3) is promoted from complement to a planned step if they fall short.
The Phase 0 paint census still reports `same_inputs`; it is diagnostic only.

## Phase 0 results (2026-09-01, run `p0` / `shim`)

- **Draws per frame:** world 17; UI batches 44 — ended by atlas switch 19,
  scissor change 19, texture 2, asked outright 4, vertex cap 0 — plus
  rotmask 2, widget model 1. **64 draw calls a frame**, UI upload 120 KB.
  The atlas/non-atlas alternation (sprite atlas vs font/other texture) and
  the scissor changes are the whole batch count: 2d's two targets.
- **Projection:** 1,641 models/frame reach the gate; FastCull rejects 212,
  the post-projection AABB 98 (6% — the projection was wasted on them),
  1,331 projected, 26.5K vertices, 264 with a non-multiple-of-four count.
  **The scalar-tail reading was wrong**: the hot addresses are the
  kernel's ENTRY block (push of 9 regs + vpush d8–d15 + a 328-byte frame
  + six handle-kind switches + prepared-camera loads), i.e. ~50% of the
  kernel is per-model fixed cost, ≈450 cycles a model, and there is no
  software integer divide anywhere in the .so's projection. 1b is
  re-aimed: an exact-four kernel for the 763 tiles (57% of projected
  models) with no loop, no bound registers, no spills; and the shell's
  own per-model work (four NormalizeAngle, the family near-clip, the
  handle switches, the cylinder chase — 1a) trimmed.
- **Paint walk:** 1 walk/frame, inputs identical to the previous frame
  100% of the time in this run (camera still), 1,296 pops, 1,641 commands
  of which 603 are element commands. 2c's hit rate on a still camera is
  1.0; on a moving camera it is the fraction of frames between tile
  changes — to be measured with a drag.
- **libc leaves (shim, calls/frame):** memcpy 986 at
  `gles2_core.c:2233`; strcmp 188 `torirs_plugin_host.c:1808`, 135+53
  `revconfig_refs.c:36-37`, 90/60/12/8 more plugin-host sites, 64+16
  `uitree_role.c`, 60 `torirs_plugin_lua.c:1460`; memset 159
  `uitree_frame.c:838`, 154 `uitree_emit.c:229`, 48 `cs1vm.c:202`;
  memcmp 48 `gles2_core.c:139`. Only 3 mutex lock/unlock pairs a frame
  are ours — the 0.6% of mutex samples is the GL driver's and malloc's.
- **64-bit divides (static scan):** `__aeabi_uldivmod` has 372 call
  sites in `hmap_search` and 78 in `ToriDraw_MapSearch` — a 64-bit hash
  modulo per lookup; `__aeabi_ldivmod` 112 in
  `ToriDraw_TriangleLerpPlaneProjecti`. 1d = mask or 32-bit modulo in the
  two hash maps.
- dwarf unwinding is not supported by this device's kernel; the shim
  (`scratchpad/shim.h`, `-include` via `PLATFORM_CC=`) is the way to
  attribute libc leaves here.

## Phase 0 -- instrumentation (half a day, no perf change)

Everything below is sized from samples; three counts are missing and each
decides a lever's worth. All go on the existing `gles2 */frame` logcat lines
(counters, not TORIRS_PERF):

0a. **Draw calls per frame**, split UI batch / rotmask / widget model /
    world, and the reason each UI batch ended (texture, scissor, program,
    clear, atlas dirty). `gles2_ui_flush` has 9 break sites; the driver's
    per-draw cost (`oxili_calc_vfd_regs`, `glVertexAttribPointer`,
    `core_glBindBuffer`, `rb_vbo_free`) says draws are the driver time.
0b. **Projection census on Android**: elements handed to
    `ToriDraw_ProjectWithVTable` per frame, culled by FastCull, projected,
    and the vertex-count histogram (the desktop `TORIDRAW_PROJ_CENSUS`
    build, made available in the android OPT build behind the same macro).
    The sort saw 1,300 models; the cull ran on more.
0c. **Paint walk**: tiles visited, commands emitted, static vs entity, per
    frame; and whether the walk's inputs (camera tile, level, cullspan)
    changed since the previous frame -- the hit rate 2c would get.
0d. **libc leaf attribution**: the fp call graph cannot attribute `strcmp`,
    `pthread_mutex_*`, `__findenv`, `__udivmoddi4`. One `--call-graph dwarf`
    record (works on this NDK's simpleperf against Android 5.1), or the
    `-include` shim the getenv audit used, on the android build.

## Phase 1 -- certain, local (2-3 days, ~ -2.2 ms)

1a. **FastCull off the scene element, not the model** (-0.4..0.5).
    Half of FastCull's samples sit on the first read of
    `model->bounds_cylinder`: per element per frame, scene element ->
    model struct (cold, hundreds of bytes) -> cylinder. Copy radius,
    top/bottom edge and `min_z_depth_any_rotation` into
    `struct ToriDraw_SceneElement` when the model is bound (they are
    immutable once `ToriDraw_ModelSetBoundsCylinder` ran; rebinding
    rewrites them) and cull off the element array. `sort_model_inputs`
    reads the same cylinder for `model_min_depth` -- same fix pays twice.
1b. **No scalar tail in the projection** (-0.3..0.4). The neon32 kernel's
    tail (`i < num_vertices`, 1..3 vertices) is 60% of its samples: two
    integer divides per vertex, and `-march=armv7-a` has no `sdiv`, so each
    is a `__aeabi_idiv` call. Run the last vector block at
    `i = num_vertices - 4` instead (recomputes up to three vertices with
    identical results; every array is `num_vertices` long, 4-vertex models
    already take the vector path). Same shape in the clip kernel.
    Second step, if the census shows divides elsewhere: an `-march=armv7ve`
    (idiv) compile of the projection + sort unity, chosen at load by
    `HWCAP_IDIVA` -- the Krait has it, the toolchain flag hides it.
1c. **libc leaves** (-0.25). `strcmp` 0.9% + mutex 0.6% + `__findenv` 0.3%
    per frame, attributed by 0d, then cached / replaced (the getenv audit
    found 90% of its calls at one site; expect the same shape).
1d. **Software 64-bit divides** (-0.1). `__udivmoddi4` 0.5%; find with 0d
    (`logic_cycle % blink` in app.c is one candidate, the swap-timing
    debug path another).
1e. **Sort per-model overhead** (-0.3..0.5). ~24% of the sort is fixed
    cost across 1,300 models: the `sort_model_inputs` switch and its
    cylinder chase (1a), the tile gate's getenv-cached probes, the
    `tmp_face_order` copy. Carry face/vertex counts and the tile kernel
    id in the placement the renderer already builds, hand the sort the
    order array directly. The gather (17%) is next: K16 eight-face int16
    blocks for models whose screen extent fits int16 (`scene->aabb` is
    already computed before the sort) -- build only if 1e leaves the
    gather as the top line.

Gate: each of 1a-1e is an env-toggled A/B in the client; anything under
0.1 ms or inside noise is reverted, as E1 and the fused emit were.

## Phase 2 -- structural (2-3 weeks, ~ -3.5 ms moving, ~ -7 ms still)

2a. **Retained static world across still frames** (-6..7 ms on a still
    frame). Key = (camera x/y/z, yaw, pitch, level, cullspan, static-scene
    revision). When the key repeats, do not walk, project, sort or dispatch
    the static elements: replay the previous frame's static draw list (the
    hot-ring windows and static pages are already GPU-resident; the frame
    index stream needs to survive one extra frame -- double-buffer it) and
    run only dynamic elements (~30 actors, ~540 faces) through
    project/sort/bake, merged at their painter positions. This is the
    D3D9-style retained model finished: today the GPU retains the
    vertices and the CPU still rebuilds the order every frame. Risk: a
    static element that changes without bumping the revision (loc anim,
    multiloc swap, texture animate) -- the revision must be bumped by
    every mutating path in `toridraw_scene.c`, checked by a headless test
    that mutates each and asserts a rebuild.
2b. **Camera-quantised face order for static models** (-1.0..1.5 on a
    moving frame). A static model's face order depends only on its
    camera-relative direction. Quantise yaw (2048/64 = 32 buckets) and
    pitch (8 buckets) and cache the sorted index run per (element,
    bucket) in the frame stream's resident windows; re-sort only when the
    element's bucket changes. Visual risk: an order that is right at the
    bucket centre and wrong at its edge. Gate: the parity harness extended
    with a camera sweep -- count faces out of order at bucket edges
    against the exact sort; accept only if no face crosses a priority band
    and the misordered pairs are coplanar-adjacent (the bucket sort's own
    tie class). If it fails that, halve the buckets and re-measure; if it
    still fails, drop 2b and accept ~10.3 on a moving frame.
2c. **Paint walk cached per (camera tile, level, cullspan)** (-0.9). The
    walk's static output changes only when those change (every ~600 ms of
    walking; never while the camera rotates in place). Cache the emitted
    static command list; entities are appended per tile from the roster,
    which the walk already does as separate commands. Pays on moving
    frames too, which 2a does not.
2d. **UI submission** (-1.0..1.2). From 0a's counts: one interleaved UI
    vertex stream per frame with one upload; batches become ranges of it
    (`glDrawArrays(first, count)`), so the three `glVertexAttribPointer`s +
    `glBindBuffer` per batch go, and with them `oxili_calc_vfd_regs`
    (7.6% of the driver). Pre-allocated 3-frame ring, no orphaning
    (`cpumempool_*`, `rb_vbo_free`). Merge the font atlas into the sprite
    atlas so text and sprites share a batch; clip in the vertex data
    instead of a scissor change where the clip is a plain rectangle.
    Target driver+kernel 2.4 -> 1.3.
2e. **Frame command bus** (-0.5). `ToriRS_FrameNextCommand` resolves every
    terrain command through `World_TerrainElementAt` (pool index math,
    active check, pointer) then `SceneElementIsLive` then the element:
    the walk knows the tile, so store the element id in the tile paint
    record and emit it. Hoist the per-command view-stack read. The
    scrollbar's dragger-mid is emitted as one tiled command per row
    every frame (`translate_scrollbar_v_skin_step`, 7% of the bus): emit
    one quad and let the renderer wrap the UV.
2f. **UI tree** (-0.4). `UITree_FrameSlotNode` scans every component per
    query (1.8% + `EmitWalk` 1.5%); cache slot -> node per tree revision.
    `ToriDraw_FontValidate` + markup parsing run per frame on text that
    did not change: cache glyph runs keyed by (text, font, width).

## Phase 3 -- the second core (wall time, not CPU)

Only one thread works. Pipelining the world CPU (walk + project + sort +
CPU index stream) for frame N+1 on the second Krait core while core 1
does UI and GL submission for frame N halves the wall time of the world
half at one frame of world latency (UI stays immediate). It does not
reduce the CPU number this plan is about and is listed as the
complement: if Phase 2 lands at 10.3 on a moving frame, this is what
makes the drag feel like 8. Prerequisite: 2a's double-buffered frame
stream, which is the same seam.

## Order and gates

1. Phase 0 (all four counts) -- one build, one profile.
2. 1a, 1b, 1c, 1d, 1e in that order, each A/B'd, each reverted if under
   0.1 ms. Expected: 15.1 -> ~12.9.
3. 2c, 2e, 2f (no visual risk, no new caches of GPU data). Expected -> ~11.2.
4. 2d after 0a's counts say where the draws come from. Expected -> ~10.1.
5. 2a (still frames). Expected still frame -> ~6-7, moving unchanged.
6. 2b, gated by the camera-sweep parity test. Expected moving -> ~9.5.
7. Phase 3 only if the moving frame is still over 10 after 6.

Every step's before/after goes into `ARMVX_KERNEL_STATE.md` (sort steps)
or here (the rest), with the A/B numbers, so a step that did not pay is
recorded as such and not tried twice.

## Phase 1 log

**1a (done, kept) — model-line prefetch instead of a bounds copy.** The
cull's miss was the model struct (element → model → cylinder), but copying
the cylinder into the element only moves the miss to the projection
kernel, which reads the same struct next; and the scene's three animation
paths re-set the model's bounds after binding, so a copy could go stale.
Instead the frame's emit loop, which already warmed the element node and
data one and two commands ahead, runs a deeper pipeline: node +3, data +2,
model struct +1 (`ToriDraw_SceneElementPrefetchModel`), with a four-slot
ring of resolved element ids so a terrain command's pool lookup runs once
(it used to run at its own turn; now at its prefetch turn). CPU ms/frame
(two runs each, interleaved): element-only 14.67 / 14.78 → +model 14.07 /
14.50 → +model+arrays 14.41 / 14.88. FastCull 0.81 → 0.29, kernel 0.76 →
0.70; the array step's six pointer reads + six PLDs per command cost the
emit loop 0.4 ms, more than they saved. `TORIRS_FRAME_PREFETCH_MODEL=0/1/2`
selects; 1 is the default. Net ≈ −0.45 ms.

**1d (done) — hash maps.** `hmap` and `ToriDraw_Map` hashed FNV-1a 64-bit
a byte at a time (three 32-bit multiplies per byte on armv7) and reduced
with a 64-bit `%` (`__aeabi_uldivmod`, 372 + 78 inlined call sites). Both
now select by target word: Murmur3 x86_32 a word at a time + Fibonacci
slot index (no divide) on 32-bit targets; the original on 64-bit hosts so
desktop placement and iteration order are unchanged. `hmap_test` passes in
both modes. `__udivmoddi4` 0.077 → ~0.05 ms/frame; the remaining 64-bit
divides are `ToriDraw_TriangleLerpPlaneProjecti` (112 sites) and a few
per-frame `ms` conversions.

**1b (re-aimed, not built yet).** No scalar tail cost exists; the kernel's
entry block is the per-model fixed cost (~450 cycles). After 1a's prefetch
the kernel is 0.70 ms for 1,331 models. An exact-four tile kernel would
address ~57% of those calls; estimated −0.15..0.2 ms. Deferred behind 2c/2e.

**1c (mostly measured, small).** strcmp sites are plugin config / chrome
lookups and revconfig refs (~700 calls/frame, ≈0.07 ms after the other
changes); memcpy is the index copy in `gles2_sequence_push_indexed`
(986/frame, 0.18 ms) — that one belongs to 2e (write indices straight into
the staging buffer). Deferred.

**1e (partly absorbed by 1a).** The sort's per-model reads of the model
struct are warm now; the remaining per-model overhead is ~30% of 1.8 ms
spread over the dispatcher, the caller and the emit. K16 gather (24% of
the sort) is the single largest remaining item inside the sort.

**2d (done, kept) — UI submission.** The UI vertex carries a sampler
select (0 sprite atlas on unit 0, 1 the batch's own texture on unit 1, 2
flat), the fragment mixes the two fetches, and every axis-aligned quad
(sprites, glyphs, fills, underlines, the clear) is clipped on the CPU in
logical space instead of through `glScissor`. Batch breaks per frame: atlas
19 + scissor 19 + texture 2 → texture 6 (font changes) only; draws 64 →
30. Driver share 11.2% → 7.5% of samples, CPU 14.07/14.50 → 13.98 ms.
Lesson: the Adreno 320 driver silently drops a draw whose program lacks an
attribute that is enabled as an array (the rotmask pass lost the minimap);
the texinfo array is disabled for that layout and `glGetError` is checked
after the rotmask draw. Rotated sprites, polygons and widget models keep
the GL scissor path.

**2c′ (done, kept) — paint walk row pre-fill.** Not the withdrawn cache:
the walk still runs in full every frame. Its per-tile init loop wrote six
byte fields and ran the whole test ladder for every tile of the 51×51×4
box (~10,400 tiles, ~1,300 painted) and was 30% of the walk. Each row of
the box is now pre-filled with the DONE pattern in one sequential pass and
the tests run only over the row's visible span. Walk 0.93 → 0.69 ms; pops
and commands per frame unchanged (1,295 / 1,640).

**2e′ (done, kept) — renderer peek-ahead prefetch.** `gles2_dispatch`
spent 39% on the first read of the static batch entry (pose table row →
track → entry, three dependent cold lines per model). The renderer's
consumer loop now warms them one line class per command ahead through
`ToriRS_FrameLookaheadElementId` (the frame's ring, resolved three
commands ahead): element row at +3, track at +2, entry at +1. Dispatch
0.80 → 0.56 ms. CPU 13.98 → 13.66 ms.

**2f (done, kept) — UI tree.** `UITree_EmitWalk` was 83% a per-frame
pointer chase over the whole component free list, feeding a perf counter
that is off in the client: now guarded by `g_torirs_perf_enabled` (0.21 →
0.02 ms). The `emit_visited` reachability scratch is stamped with a
per-walk epoch instead of cleared (the clear was small; the epoch is the
cheaper contract either way). `UITree_FrameSlotNode/MemberNode` (linear
scans, several calls a frame, 0.18 ms) remember their hits per tree; a hit
is re-validated against the node it names before it is returned and a
miss is never remembered — a first version keyed only on `generation`
returned stale nodes and the minimap orbs vanished, because sidebar
membership (`componentno >= 0`) is server state, not topology.
`gles2_painter_push_resident` writes indices straight into the IBO staging
(neutral: the memcpy that shows in the profile is the driver's own upload
copy, not ours).

## Where it stands (2026-09-01 evening, run p3a)

CPU per frame 15.1 → **13.7–13.9 ms** (runs drift ±0.3 with the phone's
temperature; compare within a session). By bucket now: face sort 2.0,
frame bus 1.2, GL driver+kernel ~1.4, projection kernel 0.86 + cull 0.37 +
shell 0.34, walk 0.8, UI tree+fonts ~0.9, dispatch 0.48, libc ~0.7,
animation/scripting/tail ~2.3.

What is left on the moving-frame ledger, and what each is worth:
- sort: a tile fast path that bypasses the dispatcher/kernel layers for
  the 763 two-face terrain models (~0.1–0.15), and the K16 eight-face
  int16 block for everything else (~0.3, a day's kernel work with parity).
- projection: exact-four tile kernel (~0.2) and shell trims (~0.1).
- bus: incremental lookahead instead of the per-command rescan, fused
  IsLive+Get (~0.2).
- driver: a second font unit so the 6 font breaks become ~2 (~0.1); world
  draws are page switches in painter order (15–17), a bake-order change.
- Sum ≈ 1.1–1.3 ms → ~12.5. **Under 10 on a moving frame is not reachable
  by per-frame trims alone once the still-camera levers are excluded**; the
  remaining gap is Phase 3 — the world CPU (walk, project, sort, emit) on
  the second Krait core, pipelined one frame ahead of GL submission. That
  is the next step to decide on.

**Sort K16 (done, kept).** Eight-face int16 block in the A32 lane for
models whose screen box is under 32K (details in ARMVX_KERNEL_STATE.md).
Bench −7.5% at 1000 faces, −18% at 2000; client sort 2.17/2.31 → 2.07/2.13
ms. Parity PASS with the block engaged on 522K fixture models.

## 2026-09-01 late: review of the aarch32 + GLES2 work, and the build-flag A/B

Five reviewers over the neon32 sort, the neon32 projection, the aarch32
raster asm, the GLES2 renderer and the frame bus/walk, checked against the
armv7 `.so`'s disassembly. Full notes were in the session scratchpad; what
survives is here.

**Build flags: measured flat.** The static facts were real -- 6,215 exported
functions and 1,930 internal symbols bound through the PLT, 1,548
`bl __divsi3` sites (`-march=armv7-a` has no hwdiv), frame pointers kept in
the release build, ARM-mode code with an 18 KB `ToriRS_FrameNextCommand` on
a 16 KB L1I -- but none of the four variants moved the frame. Interleaved
whole-client runs, CPU ms/frame:

| arm | runs |
|---|---|
| baseline | 13.64, 12.75, 12.22 |
| `-fvisibility=hidden` (exports 6215 → 16, .so 27.4 → 19.6 MB) | 13.57, 13.68 |
| `-mcpu=krait` (hwdiv + scheduling) | 13.73, 13.52 |
| `-fomit-frame-pointer` | 13.77, 13.13 |
| `-mthumb` | 13.54 |

The baseline's own spread (thermal, the population near the spawn) is wider
than any flag. Do not re-run these; anything under ~0.5 ms needs three
interleaved pairs and medians.

**Software pipelining of the neon32 sort is closed.** E1 (two block chains
per trip) is dead by register count -- the K16 gather is 24 D + 5 Q of
invariants = 34 D against 32 -- whichever binary the bench linked. The lever
that IS open is instruction count inside the chain: clang lowers the K16
block's `vtrn_s32` half-uses as **36 `vext.32`** per eight faces (and zero
`vtrn.32`); `vuzpq_s16` on Q pairs is -36 instructions a block (~-4.5/face,
~0.07-0.10 ms). The rest of the sort's remaining ~0.3 ms: the tile fast path
(~320 instructions per two-face tile incl. a 728 B frame + `vpush d8-d15`
paid by every model, `tile2_armed()` read twice, two outlined `one_abc`
calls), the K16 tail through scalar `one_abc` (~100 instr/face; run the
block on the overlapping window `num_faces - 8` with the already-emitted
lanes masked), a vectorised emit copy, `pld` on the three index streams,
the bitonic control loop (`if (i & j) continue` skips half the trips at 6
instructions each; masks reloaded from a pc-pool per vector).

**Projection.** The prepared kernel's per-model cost is the union frame: all
four specialisations inlined into one body (440 B frame, 251 `[sp]`
references), five constant vectors `vdup`'d from GPRs, stored to the stack
and reloaded 4-7 times per block, the eligibility test AFTER the prologue,
and -- contrary to the Phase 0 note -- 8 `bl __divsi3` in the scalar tails.
Levers: an exact-four tile kernel dispatched from the shell slot with its
own frame (~75 instructions vs ~210; -0.1..0.2 ms), resolving the model
handle once instead of ~11 kind switches per model, near_clip reading cot16
off the prepared block, `FastCull` inlined (it is out of line, 1.2 KB, a
9-register push per model), the bound fold stored with one `vst1` instead
of four NEON→ARM `vmov`s. Plan 1b's "run the last block at n-4, identical
results" is wrong: the tail is an exact divide, the block a reciprocal.

**Frame bus and walk.** Terrain element ids from the walk instead of
`World_TerrainElementAt` per command (2-3 dependent loads into a 43K-entry
pool, ×763; -0.15..0.25); every animated element re-posed every frame with
no (anim, frame, frame2) equality check (-0.2..0.3, camera-independent);
`ToriDraw_TextureMapAnimate` is dead work on the GLES2 lane (the shader
animates; ≥0.1); `app_ui_host_publish_inputs` byte-hashes its inputs each
frame; `luaV_execute` is the `on_frame` plugins. **Bugs:** the painter
scenery pool leaks ~1.4 KB/frame (`scenery_pool_count` is reset only in
`painter_new`); `TORIRS_FRAME_PREFETCH_MODEL=2` prefetches `cur+4` into
`cur`'s ring slot, so the 1a "+arrays loses" verdict was measured on a
broken arm.

**GLES2 renderer.** UI still uploads and rebinds per batch (~30
`glBufferSubData` + 4 `glVertexAttribPointer` each; one stream and range
draws is plan 2d's undone half, -0.25..0.35); the resident fast path walks
page → batch → chunk → vbo per model for a face limit the entry carries
(-0.1..0.2); `push_resident`'s triplet stores want `vst3q_u16` (-0.15..0.2);
the rotmask hashes the whole 512² minimap bake every 8th frame (0.76%;
a generation counter); `eglMakeCurrent` + two `eglQuerySurface` per frame.
**Unmeasured on a moving camera:** `hot_vbo` is written while prior frames'
draws read it (the ghosting pattern the stream sets avoid), and the
compaction (`draw_item_count > 48`) has no hysteresis -- both are invisible
camera-still. **Bugs:** `hot_head` serial compares are not wrap-safe;
`gles2_stream_set_append` growth discards `[0, offset)`; two `glGetError`
per frame in the rotmask path.

**aarch32 raster asm** (the soft3d lane). `tri.tex.aarch32.S` LERP8 does
eight `vmov.32 r1, dN[i]` NEON→ARM lane moves per block plus `vmrs` in
WRAPQ -- the same serialisation the sort found; stage the index vectors
through the frame instead. The alpha blends can fuse `vmovl+vmul+vshr+vmovn`
into `vmull.u8 + vshrn` bit-identically. The edge-slope ladder moves nine
words through `vmov` where a `vst1` to the contiguous F_SAC/F_SAB/F_SBC
slots would do. No door gap against AArch64; the makefile comment saying
aarch32 lacks the textured kernel is stale.

**Textured terrain is affine now.** `TORIDRAW_MODEL_FLAG_AFFINE_TEXTURES`
(bit 2), set by `world_decode_tile` on every textured tile, read once per
model next to the camera's `texture_affine`. The per-face path already had
the affine family on every span lane; the presorted-run kernels gained a
row lane (`TORIDRAW_TEXBATCH_LANE_AFFINE`) and an affine row walk
(`Ltex_affine_row`: u/v at the span's two ends, a double-precision exact
step divide, the shared block/tail bodies) on aarch64 and aarch32, scored by
four new doors in `toridraw_presorted_neon_test.c` on the M4 and on the
phone (3.2-5.0K ppm triangles, 290-550 ppm pixels, inside the tex budget).
`tri.tex.i686.S` does not read the lane yet, so that lane's batcher hands
affine faces to the per-face C family (`TORIDRAW_TEXTRI_PRESORTED_RUN_AFFINE`
gates it). `test-affine-flag` proves the flag draws what the camera's
affine route draws and not what the perspective walk draws, on both
batcher arms; `test-terrain-affine-flag` pins the decoder. GPU lanes
interpolate in hardware and are untouched.
