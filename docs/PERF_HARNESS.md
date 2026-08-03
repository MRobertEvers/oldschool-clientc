# Performance harness

Entry point for measuring and iterating on torirs client frame time,
especially under `manifest_osrs230.ini` / `manifest_osrs230_embed.ini`.

## Gate

**p95 frame work under 20 ms** (50 fps) at `-O0` for the client, with Soft3D
compiled at `-O2` via `TORIDRAW_OPT=1`. Measured by this harness in
`--uncapped` mode so the number is work time, not the 50 fps sleep.

## Build knobs

```bash
# Soft3D/ToriDraw at -O2 while the rest of the client stays -O0.
# Own OBJ_DIR suffix (_tdo) so objects never mix with a plain -O0 Soft3D build.
make -C src PLATFORM_OBJ_BASE=build_perf EMBED_SERVER=1 TORIDRAW_OPT=1 torirs
```

`TORIDRAW_OPT` follows the existing `COMPRESS_CFLAGS` / `tommath.o` precedent:
hot kernels get their own flag without forcing a full-client release build.

## Running scenarios

```bash
./tools/perf/run_perf.sh idle 900   # logged in, world visible, no input
./tools/perf/run_perf.sh ui 900     # bank open (::bank) — UITree rebuild pressure
./tools/perf/run_perf.sh world 900  # npc spawn pressure for model-instance cache
./tools/perf/run_perf.sh drift 30000        # long idle uncapped (work-time drift)
./tools/perf/run_perf.sh drift-capped 30000 # long idle at 50 fps (wall-clock ticks)

# Compare two CSVs (exit 1 on p95 regression >5% or frame p95 over 20 ms)
python3 tools/perf/compare.py before.csv after.csv

# Windowed idle-drift guard (reads TORIRS_PERF_CSV.windows.csv)
python3 tools/perf/compare.py --drift tools/perf/results/<rev>-drift.csv.windows.csv
```

Env: `TORIRS_PERF=1` enables stage timers/counters; `TORIRS_PERF_CSV=<path>`
writes the machine-readable report; `TORIRS_PERF_WINDOW=<N>` (default 1000,
500 for drift) also appends `<csv>.windows.csv` with per-window stage
percentiles and counter deltas / gauge snapshots. Embed transport requires
`EMBED_SERVER=1`.

**Frame work vs pacing:** `TORIRS_PERF_FRAME_END` runs *before* the 50 fps
`Delay` / uncapped `Delay(1)`. Capped runs that timed the sleep used to report
a flat ~20 ms residual and could not see work drift.

## Flamegraphs

```bash
./profile-mac.sh manifest_osrs230_embed.ini 25
# builds EMBED_SERVER=1 TORIDRAW_OPT=1 automatically for transport=embed
```

## Stages timed

```
frame → async → logic → cs2 → layout → interact → emit → paint → build → render → present → server
```

`server` wraps `mock230_embed_pump` (and therefore `mock230_world_tick` when
the 600 ms schedule fires). Residual = frame_mean − sum(stage means). Nested
stages (cs2 inside logic) can make residual negative; read stage columns, not
the residual, for attribution. Render is measured but not optimized
algorithmically in this effort (see TORIDRAW_OPT).

## Baseline (measured 2026-08-03)

Build: `-O0` client + `TORIDRAW_OPT=1` Soft3D, `EMBED_SERVER=1`,
`manifest_osrs230_embed.ini`, `--uncapped`, Soft3D, 900 frames.

### idle

| metric               |                 value |
| -------------------- | --------------------: |
| frame p50            |               7.05 ms |
| frame p95            |           **8.25 ms** |
| frames over 20 ms    | 15 / 900 (1.7%, boot) |
| eff fps (1/mean)     |                  59.4 |
| render p95           |               3.64 ms |
| paint p95            |               0.92 ms |
| emit p95             |               0.45 ms |
| cs2 scripts/frame    |                 ~13.5 |
| uitree find_id/frame |                  ~682 |

### ui

| metric            |           value |
| ----------------- | --------------: |
| frame p50         |         7.36 ms |
| frame p95         |     **8.21 ms** |
| frames over 20 ms | 14 / 900 (1.6%) |
| eff fps           |            59.2 |

Gate: **PASS**.

### world

| metric            |                             value |
| ----------------- | --------------------------------: |
| frame p50         |                           7.07 ms |
| frame p95         |                       **8.56 ms** |
| frames over 20 ms | 14 / 600 (2.3%, boot-skewed mean) |

Gate: **PASS** on p95.

- Soft3D render dominates attributed steady-state work (~3.6 ms p95).
- UITree walks still visit every node twice per emit (normal + drag) plus
  hit/hover; emit_skip is high — dirty filtering works, but the walk itself
  still strides the array.
- CS2 VM pool hits ~100% after warm-up (pool size raised 4 → 16).
- Model/sprite provider caches hit well after boot; config caches remain
  session-unbounded by design (see `cache_provider.c` comment).

## Optimizations landed this pass

1. **Harness** — `src/perf/torirs_perf.{h,c}`, stage scopes in `app.c`/`main.c`,
   `tools/perf/run_perf.sh`, `tools/perf/compare.py`, embed-aware `profile-mac.sh`.
2. **`TORIDRAW_OPT=1`** — Soft3D at `-O2` in a `-O0` client (`_tdo` objdir).
3. **UITree hook indexes** — `timer_hook_ids` / `key_hook_ids` rebuilt lazily;
   logic ticks and key collection no longer scan every component every time.
4. **CS2 VM pool** — `CS2VM2_POOL_MAX` 4 → 16 (fewer Init cold paths).
5. **`TorirsLru` + `TorirsModelInstCache`** — spotanim lit bases cached (size 30,
   Client-TS SpotType.modelCache), cleared at world-load seam; hit/miss/evict
   counters wired.
6. **CacheProvider counters** — model/sprite hit/miss/evict on the derived
   caches; config caches documented as intentionally session-unbounded.

## Attempt log

| change                                                                               | idle frame p95 | keep?                            |
| ------------------------------------------------------------------------------------ | -------------: | -------------------------------- |
| harness + TORIDRAW_OPT + hook indexes + VM pool 16 + spotanim inst cache             |    **8.25 ms** | keep (gate met)                  |
| lazy `runtime_hooks` side-allocation (node 11–12 KB → 1720 B)                        |    **7.43 ms** | keep                             |
| `LayoutResolve` early-out when nothing layout-affecting changed                      |    **7.24 ms** | keep                             |
| per-record dat2 config loaders → `Dat2GroupCache`                                    |    **7.31 ms** | keep (logic p95 4.96 → 0.30 ms)  |
| incremental `LayoutResolve` + memoized depth pass + mount-scan hoist + CS1 scan skip |    **6.30 ms** | keep                             |
| emit drag pass only when something is being dragged                                  |    **5.67 ms** | keep (emit p95 0.201 → 0.075 ms) |
| CS2 call stack grown on demand instead of reserved inline                            |   *no change*  | keep for footprint, not for time |
| windowed perf + `server` stage + FRAME_END before Delay                              |   harness only | keep                             |
| `SceneAnimatedElements` walks live intrusive chain (not high-water slots)            |   structural   | keep                             |
| Soft3D outline/shadow LRU (stops per-frame `SpriteNewGraphicOutline` calloc)         |   see below    | keep                             |

### Idle FPS drift investigation (2026-08-03)

Symptom report: embed client FPS degrades while sitting idle for several
minutes. Measurement:

- `TORIRS_STATS=1` for ~3 min capped: `components=7092` / `free_head=7091`
  flat — not a UITree CC leak.
- Uncapped `drift` 45k frames (~3 min wall, window=500): frame work p95
  first-half 4.84 ms → second-half 4.74 ms (**no upward slope**).
  Gauges flat except `zone_map_count` 46→63 (wandering NPCs touching zones;
  ZoneMap never evicts — memory/rebuild cost, not per-frame yet).
- Flamegraph A/B on one process at ~1.5 min vs ~7.5 min: leaf shares noise
  only; early Soft3D put `ToriDraw_SpriteNewGraphicOutline` at ~2.1–2.5%.
- Capped runs previously could not see work drift: `FRAME_END` ran *after*
  the 20 ms pacing sleep.

Fixes applied anyway (structural / Soft3D allocator churn):

1. Anim-list rebuild iterates the live element chain.
2. Soft3D caches outlined/shadowed sprite pixels (32-entry LRU) and reuses a
   clamp scratch buffer.

Drift guard: `python3 tools/perf/compare.py --drift <csv>.windows.csv`
(fails if last steady frame p95 > first by 5%).

The last row is a footprint change, not a speed change, and it is in the table so
nobody re-derives it from the frame time. `struct CS2VM2` held
`frames[128] x 12,352 B` inline per thread — 6.03 MB of a 7.10 MB VM, reserved on
every acquire for a stack that the counters show peaking at **11 frames**. Making
the slots pointers off a shared free list puts the VM at **1.07 MB** (and the pool's
retention at 17 MB instead of 113 MB), while `cs2_frame_pool_miss` totals **11 for a
900-frame run** — i.e. growth allocates once and then recycles. Interleaved A/B in
one worktree: HEAD p95 6.65 / 6.43 ms, lazy 6.68 / 5.96 ms. That spread is noise,
which is the expected result given `cs2vm2_thread_init` never touched `frames[]`.

Absolute p95 across rows is only comparable within a row's own session: the
machine drifts warmer over a run of measurements, and `render` (untouched, and
already `-O2`) moved 3.09 → 4.09 ms p95 across this session on its own. The dat2
row looks like a regression against the row above it for exactly that reason —
its own before/after showed `logic` p95 dropping 4.96 → 0.30 ms. A/B within one
session, or read the flamegraph, before believing a delta.

### Final state (all three scenarios, one session)

| scenario | frame p95 | eff fps | layout p95 | emit p95 |
| -------- | --------: | ------: | ---------: | -------: |
| idle     |   5.67 ms |    93.5 |   0.001 ms | 0.075 ms |
| ui       |   5.71 ms |    93.3 |   0.001 ms | 0.076 ms |
| world    |   6.21 ms |    89.5 |   0.001 ms | 0.086 ms |

`over_20ms` is 12 frames in every scenario, all of them the world-load/login
frame (`max` ≈ 4.6 s); steady-state frames never approach the budget.

### What the flamegraph said at each step

Sampled with `OUT=... TORIDRAW_OPT=1 ./profile-mac.sh manifest_osrs230_embed.ini 30`,
main-thread leaf shares:

| leaf                                                                 | before | after |
| -------------------------------------------------------------------- | -----: | ----: |
| `RSCache_BufferReadto` (per-record group re-decode)                  |  15.4% |  gone |
| `layout_compute_node` + `layout_parent_box` + `UITree_LayoutResolve` |   7.8% |  1.8% |
| `UITree_ChildMountType`                                              |   6.0% |  gone |
| `task_cs1_component_has_scripts`                                     |   3.9% |  gone |
| `emit_walk_node`                                                     |   3.4% |  gone |

What is left on top is the rasterizer (~35% across
`draw_texture_scanline*`/`raster_gouraud*`/`ToriDraw2D_BlendArgbPixel`/
`RenderModel2SortFaces`), which is already built `-O2` via `TORIDRAW_OPT`.

Two traps this session, both worth remembering:

- A per-node `TORIRS_PERF_COUNT` on the new layout skip path cost 3.0% of
  samples — more than the work it was measuring. Accumulate into a local and
  count once per call.
- The depth recompute walked each node's whole parent chain, so shared ancestors
  were re-walked thousands of times (2.8% of samples, all cache misses into
  1.7 KB structs). Memoizing so each parent link is followed once removed it.

## The four DFS walks

There are five instrumented node visits over three traversal implementations,
and they are **not** interchangeable — the prune rules differ on purpose:

| walk                                             | visits/frame (rev230) | when                                       | hide rule                                                                                                                 |
| ------------------------------------------------ | --------------------: | ------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------- |
| emit, draw pass                                  |                  2478 | when `need_redraw`                         | prunes hidden subtrees **unless** the node is the hovered one (same-frame reveal)                                         |
| emit, drag pass                                  |          0 (was 3033) | only with a drag active                    | same                                                                                                                      |
| hit (`UITree_HitTestInteractive`)                |                   618 | every non-minimenu frame, twice on a click | any `behavior.hide` prunes                                                                                                |
| hover (`UITree_FindHoveredComponentIdForRegion`) |                    95 | every non-minimenu frame                   | prunes only hidden RS_LAYER/SIDEBAR, so hidden widgets are still discovered and can drive emit's reveal in the same frame |
| drop (`UITree_FindDropTarget`)                   |                    ~0 | only during an active drag                 | any `behavior.hide` prunes                                                                                                |

The drag pass was the big one and it is now gated on `UITree_HasActiveDrag`. It
existed only to redraw deferred (picked-up) drag subtrees on top of everything
else, so on an ordinary frame every node it reached took the descend-only branch
and emitted nothing — and it visited _more_ nodes than the draw pass, because
descend-only bypasses the collapsed-layer prune.

**Folding hit into hover is not worth doing.** Together they are 713 visits a
frame inside an `interact` stage that measures ~20 µs p95 (0.3% of the frame),
neither appears in the profile, and their hide rules are deliberately opposite:
hover has to see hidden widgets that hit must not. Merging them buys nothing
measurable and puts the same-frame reveal path at risk.

## Not done / next candidates (ranked by counter evidence)

1. Loc/npc/player instance caches on the same `TorirsModelInstCache` (spot done).
   `cache_model_hit` 27.8/frame vs `cache_model_miss` 2.4/frame, so the win here
   is in `RenderModel2SortFaces` (5.2%), not in decode.
2. Enum/param host-op hash indexes — dropped: `rs_cs2_host` linear scans never
   appeared in the profile under any scenario.
3. `cs2vm2_thread_init` — **not a frame-time target, do not chase it on these
   numbers.** It reads as 0.02–0.03% of samples in four of the five flamegraphs
   and 0.75% in the fifth, and `cs2_vm_init_ns` puts it at 5.9 µs/frame over 8.3
   acquires, i.e. ~0.1% of a 5.7 ms frame. An earlier revision of this section
   claimed 4.8%; that was wrong. The lazy call stack below was done for the
   memory footprint, not for the frame.

### Done (round 3 painter / present) — attribution corrections

An earlier note bundled `painter_paint_bucket` 5.9% with `SDL_FillRect4` 3.3% and
`_platform_memset` 2.5% as “2D/present”. Re-check of `src/out.sample.txt`:

- `_platform_memset` (91 samples, 0.36%) is mostly `ToriDraw_ComputeProjectedFaceOrder`
  and `try_emit_world_draw_model`, **not** framebuffer clears. Soft3D clear was a
  scalar store loop (now `memset_pattern4` / word fill).
- `SDL_FillRect4` is a **harness artifact**: production uses
  `SDL_RENDERER_ACCELERATED` (Metal). The software-renderer FillRect only appears
  under the headless SDL backend. Present now skips `SDL_RenderClear` when the
  letterbox covers the window.
- `painter_paint_bucket` itself was optimized (merged `TilePaint` layout, hoisted
  aliasing, emit cursor, contiguous setup, production frustum cullmap). See
  [painter_bucket_vs_world3d.md](painter_bucket_vs_world3d.md) round 3: mean ratio
  bucket/world3d **0.755**, **0/500** seeds slower. Re-profile before ranking it
  again.

**Round-3 cullmap follow-up:** the first production bake blanked the world
(`painter_*` counters all 0 at `1fcf825c`). Root cause was
`painters_cullmap_build_toridraw` feeding `TrigFns*` to a sin callback that
expects the raw tables, plus near-clip / pitch-domain mismatches. Fixed; fail-
safe keeps nocull if a slice is empty. Re-measured idle (`518c0c3a`, 900 frames):
one bake (`near=50`, `slice_vis=1226`), `painter_commands` ≈3739/frame (non-zero),
paint p50 **257 µs** / render p50 **2509 µs** (broken was paint 63 / render 742
with empty world; nocull baseline paint 745 / render 2716). Paint `max` still
includes the one-shot ~2 s bake. Obj-icon double-shadow from stacking runtime
`graphic_shadow` on a SHADOW-baked `item_scene_id` is fixed by flavour-select
emit (see [skill_guide.md](skill_guide.md) §9).

## Correctness

- Spotanim instance cache returns `ToriDraw_ModelCopy` of the cached base —
  scene owns a mutable copy; animation still applies per frame.
- Hook indexes rebuild on ApplyRuntimeHook / Push / reclaim; EnsureHookIndexes
  is the only reader contract.
- The incremental `LayoutResolve` recomputes a node only when its own box was
  invalidated or its parent's box moved in the same pass, so **every write to a
  layout input must clear that node's `position.layout_resolved`** — the resolve
  reads a set flag as "this box is already correct". The geometry setters,
  reparenting, `UITree_Push` and `UITree_CcCopy` all do. A JIT chain resolve
  (`UITree_EnsureLayoutFor`) leaves its nodes reading as resolved while their
  descendants are still stale, so it hands the change to the resolve through
  `layout_changed`, or sets `layout_force_full` if there is nowhere to record it.
  Verified by recomputing every node unconditionally at the end of each resolve
  and diffing the boxes: 0 mismatches across all three scenarios, 900 frames each.
- `layout_resolved_root_valid` is deliberately separate from
  `layout_resolved_valid`: an invalidation means some node's box changed, not that
  the canvas resized, so letting it clear the root-box comparison made every
  root-level node recompute on any mutation anywhere. `test-uitree` pins this
  (an untouched sibling branch keeps a poisoned box across a resolve that moves
  another subtree).
- `cs1_script_nodes` gates the per-tick CS1 whole-tree scan. It is maintained in
  `UITree_SetBehavior` and slot reclaim, which are the only two places a tree
  node's `behavior.scripts_count` changes; `test-cs1` covers the non-zero path.
- `drag_active_nodes` gates the emit drag pass. Write `drag_active` through
  `UITree_SetComponentDragActive` — the drag start, the drag-end UP, the CS2
  `DRAGPICKUP` op and slot reclaim all do. A missed writer would skip the pass
  and freeze a picked-up widget at its stale visual; `test-uitree`'s composite
  drag and emit-golden cases both exercise the pass actually running.

## Verification close-out (2026-08-03)

| check                                                                  | result                                                          |
| ---------------------------------------------------------------------- | --------------------------------------------------------------- |
| idle / ui / world harness p95 &lt; 20 ms                               | PASS (CSVs in `tools/perf/results/19e81d70-*.csv`)              |
| headless embed `TORIRS_EXIT_BMP`                                       | wrote successfully (`EMBED_SERVER=1 TORIDRAW_OPT=1`)            |
| `mock230_pack --check-only`                                            | **0 errors**, 15 warnings                                       |
| `test-uitree`, `bench-uitree`, `test-cache-trim`, `test-mock230-embed` | green                                                           |
| `test-mock230-coverage`                                                | green                                                           |
| `make -C 3rd/rscache test`                                             | green (`cachepack-fidelity: all bars met`)                      |
| `readme.md` UITree performance section                                 | points at this doc; historical 84% ToriDraw figure marked stale |

### Second close-out (2026-08-03, after the dat2/layout/emit round)

| check                                                                                                                                                                          | result                                                                                                                             |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------- |
| idle / ui / world harness p95 &lt; 20 ms                                                                                                                                       | PASS at 5.67 / 5.71 / 6.21 ms (`tools/perf/results/61548478-*.csv`)                                                                |
| incremental layout vs forced full resolve, 900 frames × 3 scenarios                                                                                                            | 0 box mismatches                                                                                                                   |
| `mock230_pack --check-only`                                                                                                                                                    | **0 errors**, 15 warnings                                                                                                          |
| `test-uitree`, `test-uitree-builder`, `test-uitree-builder-dat1`, `test-chat-widgets`, `test-minimap`                                                                          | green                                                                                                                              |
| `test-cs1`, `test-cs1vm`                                                                                                                                                       | green (`test-cs1` needed `perf/torirs_perf.c` added to its hand-picked link list — the harness counters had broken it)             |
| `test-db`, `test-cs2-{math,string,component-param,triggerop,dialect}`, `test-cache-trim`, `test-task-order`, `test-world`, `test-inv`, `test-varp`, `test-varc`, `test-social` | green                                                                                                                              |
| `test-mock230-embed`                                                                                                                                                           | green                                                                                                                              |
| `test-ui-slots`                                                                                                                                                                | fails on `manifest must state [cache:boot] identity` — pre-existing, the target passes a bare `../cache254` rather than a manifest |

Windowed eye-check is left to the operator (`./run-live.sh manifest_osrs230_embed.ini`
after an `EMBED_SERVER=1 TORIDRAW_OPT=1` build). Pixel A/B against a pre-cache
baseline was not retained in-tree; re-capture with `TORIRS_EXIT_BMP` if a visual
regression is suspected after further cache work.
