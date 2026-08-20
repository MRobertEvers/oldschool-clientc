# Performance harness

Entry point for measuring and iterating on torirs client frame time,
especially under `manifest_osrs230.ini` / `manifest_osrs230_embed.ini`.

## Gate

**p95 frame work under 20 ms** (50 fps) at `-O0` for the client, with Soft3D
compiled at `-O2` via `TORIDRAW_OPT=1`. Measured by this harness in
`--uncapped` mode so the number is work time, not the 50 fps sleep.

That is the portable development gate. Windows renderer investigations also
use the stricter main-thread CPU gate registered in
[WIN32-PERF-001](platform_quirks.md#win32-perf-001---main-thread-cpu-time-is-the-non-waiting-frame-metric).
Keep platform thresholds and clock caveats in the quirks registry rather than
forking them into this harness guide.

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
./tools/perf/run_perf.sh drift-ui 30000     # sidebar + bank open/close churn (one pack)
./tools/perf/run_perf.sh soak-ui 60000      # multi-panel residency soak

# Compare two CSVs (also gates main-thread CPU p95 at 10 ms when present)
python3 tools/perf/compare.py before.csv after.csv

# Windowed idle-drift guard (reads TORIRS_PERF_CSV.windows.csv)
python3 tools/perf/compare.py --drift tools/perf/results/<rev>-drift.csv.windows.csv
```

Use `--cpu-budget-ns` only when a platform entry registers a different
non-waiting CPU budget; the Windows contract is 10,000,000 ns.

Env: `TORIRS_PERF=1` enables stage timers/counters; `TORIRS_PERF_CSV=<path>`
writes the machine-readable report; `TORIRS_PERF_WINDOW=<N>` (default 1000,
500 for drift/soak) also appends `<csv>.windows.csv` with per-window stage
percentiles and counter deltas / gauge snapshots. `TORIRS_IFACE_STATS=1`
prints a per-group open/close ledger every 250 logic ticks. `TORIRS_NET_CHEAT_ROTATE=1`
fires one semicolon-separated cheat per EVERY cycle (soak-ui). Embed transport
requires `EMBED_SERVER=1`.

**Frame work vs pacing:** `TORIRS_PERF_FRAME_END` runs *before* the native
capped absolute-deadline wait. `--uncapped` performs no artificial delay. The
capped deadline begins before `TORIRS_PERF_FRAME_BEGIN`, so pre-instrumentation
frame work still consumes the 20 ms budget, but the pacing wait is excluded
from stage timings. The CSV `cpu` row is the main-thread non-waiting
distribution; `cpu_raw` is the calibration interval's aggregate clock. On
modern Windows, cycles sampled exactly at frame begin/end define work, while
cycles over the delayed `GetThreadTimes` interval provide the scale. This
excludes the limiter's final spin and post-frame loop work. On XP,
`GetThreadTimes` remains available but is too coarse for a meaningful
one-frame percentile, so the comparison gate uses its longer-window raw mean.
Measure wall-clock effective fps separately.

## Current Windows renderer architecture (2026-08-06)

The optimized Windows renderer has one D3D9 architecture on both build lanes.
Win64 does not select a GL3-style or 32-bit-index path based on OS or adapter
capabilities; it deliberately behaves like the XP-compatible backend:

- All GPU index resources are unconditionally `D3DFMT_INDEX16`.
  `TRSPK_Batch16` repacks static/pre-baked poses once into chunks of at most
  65,535 vertices, and D3D9 assigns those chunks stable pages in one managed
  static vertex buffer. There is no `INDEX32` or capability-selected path.
- Every visible frame emits exact painter-ordered, page-local U16 indices into
  one dynamic IBO. Draw ranges split on page/dynamic binding, texture/config,
  and `MaxPrimitiveCount`; `BaseVertexIndex` selects a static page without
  widening its indices. This is the one normal path, not a fallback.
- `--d3d9-zbuffer` is the opt-in alternative submission policy on the same
  XP-compatible renderer. It skips the tile painter and opaque face sort,
  depth-tests/writes opaque and binary-cutout faces, then sorts and blends only
  models containing true per-face translucency. The final baked alpha and
  texture-opacity result is cached per element/track/pose, so retained poses do
  not repeat material classification each frame. It still rebuilds page-local
  U16 IBO chains; it does not change retained static vertex or texture data.
- Static vertex pages upload only after a batch changes or their managed buffer
  is recreated. The per-frame IBO upload is expected because painter order can
  change; genuinely dynamic geometry alone uses the dynamic vertex buffer.
- Static world atlases/buffers, exact-sized animated texture buffers, and UI
  sprite/font/variant textures upload only on their explicit dirty or
  replacement seam. Animation through texture coordinates is not a reason to
  reupload pixels. Rotated masks use a native fixed-function `TEX2` source+mask
  draw, with retained textures rather than a per-frame CPU composite.
- D3D9 uses `D3DPRESENT_INTERVAL_IMMEDIATE` on both lanes. The client already
  owns the 50 Hz absolute deadline; display synchronization inside `Present`
  would add a second wait and corrupt both pacing and uncapped attribution.

Soft3D's plain, scaled, and tiled ARGB blitters now clip once and use row
pointers with the same integer blend result. Its global-alpha path applies
command opacity during the destination blend, so a plain translucent sprite no
longer allocates/copies a temporary image or scans it merely to rewrite alpha.
Transformed/outlined sprites keep bounded specialized paths.

Final Win64 `-O3` measurements on 2026-08-06 used the pristine revision-239
offline scene (`manifest_osrs239.ini`) and `--uncapped`. Every run captured
6,000 frames as twelve 500-frame windows; every steady window held exactly
1,072 UI components and 4,946 painter commands. Values below are milliseconds
and exclude the client frame limiter. The aggregate rows are the profiler's
retained last 2,048 frames; the window CSVs cover the full run.

| Renderer / canvas | CPU mean | CPU p95 | Frame p95 | Build p95 | Render p95 | Present p95 |
|---|---:|---:|---:|---:|---:|---:|
| D3D9, 765x503 | 3.45 | 5.19 | 5.80 | 1.42 | 3.67 | 0.74 |
| Soft3D, 765x503 | 6.08 | 8.53 | 8.71 | 1.50 | 6.94 | 0.16 |
| D3D9, 1440x900 resizable | 3.45 | 4.73 | 5.34 | 1.43 | 3.32 | 0.69 |
| Soft3D, 1440x900 resizable | 9.98 | 14.72 | 14.86 | 1.59 | 12.52 | 0.48 |

D3D9 passes the 10 ms CPU gate in every window at both sizes. Soft3D's retained
765x503 aggregate passes, but three of twelve window p95 values exceed 10 ms
(maximum 10.96 ms); 1440x900 fails in every window. A 1440x900 diagnostic with
world painter output suppressed measured only 0.41 ms render p95, locating the
remaining resolution-scaled cost in world projection/sort/triangle raster,
not in GDI presentation, a second canvas raster, or a canvas-alpha scan.

`surface_sync` stayed below 0.001 ms p95 in all four runs. After window-zero
bootstrap, unchanged world/animated/UI textures and the static VB produced zero
uploads. Steady D3D9 at 765x503 uploaded only one painter-order U16 IBO per
visible frame (227.46 KiB) and averaged 2,459 draw calls, 2,433 page switches,
27 texture switches, and 40.08 static/dynamic stream switches. Those page
boundaries select `BaseVertexIndex` within one static VB; they do not rebind a
per-page VB. No D3D9 software UI fallback or full-canvas alpha reconstruction
occurred. Keep these architectural counters beside timing results; they catch
regressions that a fast host can conceal.

The opt-in z-buffer path was re-measured on 2026-08-07 with the same Win64
`-O3`, revision-239 offline, `--uncapped` setup. Each canvas ran 6,000 frames
in twelve 500-frame windows. These values are non-waiting milliseconds:

| Renderer / canvas | CPU mean | CPU p95 | Frame p95 | Build p95 | Render p95 | Present p95 |
|---|---:|---:|---:|---:|---:|---:|
| D3D9 z-buffer, 765x503 | 3.59 | 4.56 | 5.09 | 0.84 | 3.41 | 1.11 |
| D3D9 z-buffer, 1440x900 resizable | 3.15 | 4.02 | 4.62 | 0.79 | 3.01 | 1.10 |

Both aggregates and all twelve 765x503 windows pass the 10 ms CPU gate. The
1440x900 window CPU p95 range was 3.76--5.87 ms, also entirely below the gate.
Steady frames performed two page-local U16 IBO uploads (opaque/cutout and
blended) and no unchanged static VB or world/animated/UI texture upload. At
765x503 only about 30 models/frame entered legacy sorting (about 598 blended
triangles versus 34,900 opaque/cutout); at 1440x900 it was about 32 models
(913 versus 44,865 triangles). Plain `--d3d9` retained painter submission and
reported no z-buffer counters in a separate regression run.

## Flamegraphs

```bash
./profile-mac.sh manifest_osrs230_embed.ini 25
# builds EMBED_SERVER=1 TORIDRAW_OPT=1 automatically for transport=embed
```

## Stages timed

```
frame → input_prep/platform_poll → command_drain → surface_sync → app_run
      → async/logic/cs2/layout/interact/emit/paint/build
      → display(render/pick_finish/present) → window_sync → frame_post → server
```

`server` wraps `ToriRSServer_EmbedPump` (and therefore `ToriRSServer_WorldTick` when
the 600 ms schedule fires). Residual = frame_mean − sum(stage means). Nested
stages (cs2 inside logic) can make residual negative; read stage columns, not
the residual, for attribution. The historical sections below describe the
2026-08-03 Soft3D effort; the current Windows renderer architecture is recorded
above and must be measured as its own A/B.

Note that `cs2` spans the whole of `app_logic_tick`, not script execution
alone. Its two nested stages split it: `tick_packets` is the serial packet
pipeline (`TaskRunner_SettleFrame` plus every `GameProtoExec` task it drives)
and `cs2_settle` is `app_settle_cs2_frame`. `ui_icon` covers one obj inventory
icon rasterization wherever it is asked for.

## Measuring stutter rather than frame rate (2026-08-12)

A mean frame time cannot show a hitch, and `frame` stops at the pacing wait.
Three additions close that gap:

* `period` — wall time between consecutive frame starts. Work plus pace plus
  the loop overhead in neither, so it is the only stage that answers "what does
  the player actually see". Measured before `FRAME_BEGIN` and carried into the
  frame it opens.
* `pace` — the 50 fps wait itself, carried the same way (frame N's wait is
  reported with frame N+1). Both use `TORIRS_PERF_CARRY`, which survives the
  `FrameBegin` memset that would otherwise wipe a post-`FrameEnd` sample.
* `logic_ticks` — ticks run in one frame. The scene advances once per tick and
  the renderer does not interpolate, so a 0-tick or 2-tick frame stutters at a
  perfectly steady frame rate. Read the histogram, never the mean.

`TORIRS_PERF_WINDOW=1` writes one row per stage per frame — a per-frame trace,
which is what hitch hunting needs. Two diagnostic env knobs go with it:

```
TORIRS_PACE_SPIN=1     burn the pacing wait instead of sleeping it. Pins a core;
                       isolates render cost from the cost of resuming a CPU that
                       Windows parked during the sleep (worth 12-20% here).
TORIRS_PKT_SLOW_MS=<n> print `pkt_slow: type=<id> <ms> cycle=<n>` for any server
                       packet whose handler ran longer than n ms. The exec
                       pipeline is serial, so the settle timed is that packet
                       and nothing else. Ids are the PKT_NAME_* enum ordinals in
                       src/net/rev/pktnames.h.
```

## Historical Soft3D baseline (measured 2026-08-03, rev `9175a425`)

Build: `-O0` client + `TORIDRAW_OPT=1` Soft3D, `EMBED_SERVER=1`,
`manifest_osrs230_embed.ini`, `--uncapped`, Soft3D, 900 frames.
CSV: `tools/perf/results/9175a425-idle.csv`. Flamegraph:
`tools/perf/results/flame_now.svg` (30 s sample after 8 s warmup).

### idle

| metric               |                 value |
| -------------------- | --------------------: |
| frame p50            |               4.13 ms |
| frame p95            |           **7.16 ms** |
| frames over 20 ms    | 14 / 900 (1.6%, boot) |
| eff fps (1/mean)     |                 100.8 |
| render p95           |               3.90 ms |
| paint p95            |               0.89 ms |
| emit p95             |               0.25 ms |
| logic p95            |               0.59 ms |
| cs2 scripts/frame    |                  ~9.9 |
| cache_model_hit/miss |           13.5 / 2.4 |
| painter_commands/fr  |                 ~4353 |

Gate: **PASS** (p95 ≪ 20 ms).

### Collection Log open (`collection` / `collectionbig`, measured 2026-08-03)

`TORIRS_NET_CHEAT=collection` or `collectionbig` overrides the `ui` scenario's
default bank cheat (`run_perf.sh` respects `TORIRS_NET_CHEAT`).

| scenario | before frame p95 | after frame p95 | before render p95 | after render p95 |
| --- | ---: | ---: | ---: | ---: |
| `collection` (Abyssal Sire, 9 icons) | 5.02 ms | 5.21 ms | 3.41 ms | 3.60 ms |
| `collectionbig` (Hard Trails, 134 icons) | **14.46 ms** | **5.99 ms** | **9.22 ms** | **4.23 ms** |

Cause: script 2732 `cc_setoutline(1)` + `cc_setobject` → emit used Soft3D
draw-time `SpriteNewGraphicOutline` with a 32-entry LRU. Fix: bake bordered
icons once (`EnsureObjIconBordered`) when `outline==1 && graphic_shadow==0`;
Soft3D outline LRU 32 → 256 for remaining chrome. Open CS2 hitch (~120 ms max)
is one-shot (mount + 7798 + timer `if_callonresize` redraw), same class as bank.

CSV: `tools/perf/results/collectionbig-ui-{before,after}.csv`.

### Flamegraph hotspots (`flame_now`, main thread self %)

| leaf | self % | ~ms @ p95 7.16 |
| --- | ---: | ---: |
| `raster_gouraud_screen_opaque_bary_branching_s4_ordered` | 10.9 | 0.78 |
| `draw_texture_scanline_opaque_blend_branching_lerp8_v3_ordered` | 10.7 | 0.76 |
| `ToriDraw2D_BlendArgbPixel` | 8.0 | 0.57 |
| `ToriDraw_RenderModel2SortFaces` | 5.9 | 0.42 |
| `ToriDraw_RenderModel1Project` | 3.9 | 0.28 |
| `UITreeIfaceStats_SampleGauges` | 3.6 | 0.25 |
| `scene_occluders_point_hidden` | 3.4 | 0.24 |
| `UITree_LayoutResolve` | 3.3 | 0.24 |
| `painter_paint_bucket` | 2.5 | 0.18 |
| `World_EntityPoolNext` | 1.9 | 0.14 |

This table is historical attribution, not a post-2026-08-06 Windows profile.
In particular, the current clipped blitters no longer route every visible
plain/scaled/tiled pixel through the public `ToriDraw2D_BlendArgbPixel` clip
check, so that row must be resampled before it is ranked again.

Inclusive: `ToriRS_Soft3D_RenderFrame` **60.7%**, `soft3d_draw_model` **43.8%**,
`App_BuildFrame`/`painter_paint_bucket` ~**12.8%**. Raster-ish self cluster
(gouraud + texture scanlines + blend + sort/project/triangle/raster) ≈ **48%**.

Artifacts / tax (discount when ranking product opts):

- `UITreeIfaceStats_SampleGauges` — runs whenever `TORIRS_PERF=1` (full CC walk)
- `Blit4to4MaskAlpha` / software SDL present — headless dummy only
- `TorirsPerf_Count` ~1.4%

Earlier observations that still hold:

- Soft3D render dominates attributed steady-state work (~3.9 ms p95).
- UITree emit_skip is high — dirty filtering works; walk still strides live nodes.
- CS2 VM pool hits ~100% after warm-up (`cs2_vm_pool_hit` 6785 / miss 1).
- Model/sprite provider caches hit well after boot; config caches remain
  session-unbounded by design (see `cache_provider.c` comment).

## Optimizations landed this pass

1. **Harness** — `src/perf/torirs_perf.{h,c}`, stage scopes in `app.c`/`main.c`,
   `tools/perf/run_perf.sh`, `tools/perf/compare.py`, embed-aware `profile-mac.sh`.
2. **`TORIDRAW_OPT=1`** — Soft3D at `-O2` in a `-O0` client (`_tdo` objdir).
3. **UITree live node sets** — dense slot lists (`models`, `timer_hooks`,
   `key_hooks`, …) maintained at Push/reclaim/predicate writers; no lazy
   full-array rebuild. Consumers walk `set.slots[0..count)`.
4. **CS2 VM pool** — `CS2VM2_POOL_MAX` 4 → 16 (fewer Init cold paths).
5. **`TorirsLru` + `TorirsModelInstCache`** — spotanim lit bases cached (size 30,
   Client-TS SpotType.modelCache), cleared at world-load seam; hit/miss/evict
   counters wired.
6. **CacheProvider counters** — model/sprite hit/miss/evict on the derived
   caches; config caches documented as intentionally session-unbounded.

## Planar occluders (2026-08-03)

Occlusion culling (`docs/OCCLUDER_SYSTEM.md`) is on by default. Kill switch
`TORIRS_OCCLUDERS=0`.

Headless Lumbridge (`tele 0,50,50,16,14`, pitch 167, steady frame):

| | off | on |
|---|---:|---:|
| painter commands | 5853 | 5021 (−14%) |

`tools/perf/run_perf.sh idle 300` with occluders on still clears the p95 < 20 ms
gate (build `-O0` + `TORIDRAW_OPT=1`). Re-measure before claiming a paint-stage
delta — outdoor frames often have `active_count == 0` and cost one load per
emit site. Flame_now (2026-08-03 evening) puts
`scene_occluders_point_hidden` at **3.4% self** — the query cost is now visible
even when command count drops.

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
| windowed perf + `server` stage + FRAME_END before pacing wait                       |   harness only | keep                             |
| `SceneAnimatedElements` walks live intrusive chain (not high-water slots)            |   structural   | keep                             |
| Soft3D outline/shadow LRU (stops per-frame `SpriteNewGraphicOutline` calloc)         |   see below    | keep                             |
| Pre-baked `cc_setoutline(1)` obj icons + Soft3D outline LRU 32→256 (collection log)  |   see above    | keep                             |

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

Menu-churn follow-up (`./tools/perf/run_perf.sh drift-ui`): cycles sidebar
tabs (f1–f12) + Escape, and re-opens `::bank` every 40 logic ticks via
`TORIRS_NET_CHEAT_EVERY`. Uncapped 30k frames / window=500: steady frame p95
4.85 → 5.08 ms (+4.9%, under the 5% guard). `uitree_components` flat at 7092;
only `zone_map_count` rose (46→63). Mid-run windows spiked to ~13 ms then
recovered — noise / one-shot mount cost, not a leak slope.

**`drift-ui` covers one resident pack only.** It remounts the bank over and
over (`iface_bake_reuse`), which is the case that stays flat. Multi-panel
open/close — the interactive "open lots of different interfaces" case — is
`soak-ui`.

**UITree open/close path (follow-up):** the earlier `drift-ui` run never
actually opened the bank — `TORIRS_NET_CHEAT="::bank"` was sent with the
leading `::`, and the cheat handler matches `bank` without it (harness now
strips `::`). With a real bank open/close cycle:

- `IF_CLOSESUB` **hides** packs rather than reclaiming them so a remount can
  reuse dynamic children (`already baked; reusing it`). After one bank open
  the tree sits at ~10k components with ~4400 hidden (~43%). The gameframe
  boot itself already parks ~33 interface groups in the array.
- Each remount re-runs onload (`cc_deleteall` + `cc_create` with fresh dynamic
  uids) and re-registers `if_setonvartransmit`. Dead hook entries for the
  reclaimed uids were only compacted when the 512-slot array filled, so
  `var_hooks` sawtoothed 220→512→220 every few minutes of open/close. Fixed in
  two layers: (1) compact dead inv/var/stat hooks before every append;
  (2) `RS_CS2Host_ClearHooksForInterfaceGroup` on IF_CLOSESUB / replacing
  mount drops host inv/var/stat entries and clears *reactive* component
  listeners (timer/key/*transmit/resize/sub_change) for that pack, including
  same-group dynamic children. Interaction hooks (click/op/drag) stay on the
  reused bake — the compass `on_op` is installed once by gameframe onload and
  is not rebuilt when a sidebar closes. A block with no interaction slots left
  is still freed. Misc/friend transmit walks also skip hidden ancestors.
- `onTimer` did not skip hidden ancestors (inv/var/stat already did). Closed
  panels' timers kept firing; gated with `UITree_ComponentOrAncestorHidden`.

### Multi-panel soak (`soak-ui`, measured 2026-08-03)

`./tools/perf/run_perf.sh soak-ui 60000` rotates `bank;equipstats;xptracker;
loottools;hiscores;farmkit` via `TORIRS_NET_CHEAT_ROTATE=1`, with Escape +
sidebar F-keys interleaved. Diagnosis from the new counters:

| gauge / counter | before fix (60k) | after fix (12k) | reading |
| --- | ---: | ---: | --- |
| `uitree_components` | ~10119 flat | ~10119 flat | residency set at boot, not a leak |
| `iface_groups_resident` | 33 | 33 | hide-not-destroy; packs stay |
| `uitree_anim_scan_nodes` / window | ~2.7M | ~650–950 | was full-array ×2 / logic tick |
| `uitree_anim_model_nodes` / window | ~800 | ~650–950 | now equals scan (live model set) |
| `uitree_hook_index_rebuild_nodes` | present | **0** (omitted) | no lazy rebuild path left |
| `iface_group_scan_nodes` / 3k frames | n/a (full tree ×4) | ~16k total | open/close proportional to group size |
| `uitree_hook_blocks` | ~2872 | ~844 | close clears reactive hooks; interaction blocks may remain |

Fixes that landed:

1. **Live node sets (slot indices)** — models / timer / key / wheel / opkey /
   client_code / resize / sub_change / scroll_layers / per-group map, maintained
   at Push, reclaim, SetBehavior, ApplyRuntimeHook, ApplyOpKey. No
   `Ensure*Index` rebuild; consumers drop per-entry `FindByComponentId`.
   Open/close group walks are O(group) via `UITree_GroupNodes`
   (`iface_group_scan_nodes` counter).
2. **Clear reactive `runtime_hooks` on unmount** — `ClearHooksForInterfaceGroup`
   zeros timer/key/transmit/resize/sub_change and frees the block only when no
   interaction slots remain. Click/op/drag stay so reused gameframe chrome
   (compass `on_op`) still responds; remount onLoad re-arms reactive listeners.
3. **Telemetry** — `iface_open/close/bake/reuse`, hitch ns, growth gauges,
   scan-cost counters, and `TORIRS_IFACE_STATS=1` per-group ledger.
4. **Guard** — `test-uitree` `test_open_close_steady` +
   `test_clear_hooks_preserves_sibling_on_op` + `test_live_node_sets`;
   soak-ui under `compare.py --drift`.

Pack residency (destroying hidden groups) was **not** required for frame
stability once the scans and hook blocks were fixed; the remount-reuse
invariant stays.

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

## Not done / next candidates (ranked by `flame_now` + idle counters)

1. **Loc/npc/player instance caches** on `TorirsModelInstCache` (spotanim done).
   `cache_model_hit` 13.5/frame vs `cache_model_miss` 2.4/frame — decode is fine;
   `RenderModel2SortFaces` (5.9%) + `RenderModel1Project` (3.9%) still pay every
   draw. Highest non-raster structural win.
2. **Occluder query cost** — `scene_occluders_point_hidden` 3.4% self (inclusive
   via `scene_occluders_ground_tile_hidden` ~4.2%). Commands drop ~14% when
   occluders are active, but the point test itself is now a leaf hotspot.
   Profile `TORIRS_OCCLUDERS=0` A/B before investing; if off is faster on this
   outdoor idle scene, gate denser queries or cache tile results per frame.
3. **`UITreeIfaceStats_SampleGauges` under `TORIRS_PERF=1`** — 3.6% self, full
   component_count walk every frame. Measurement tax, not product cost. Sample
   every N frames or only when `TORIRS_STATS`/`TORIRS_IFACE_STATS` is set.
4. **`World_EntityPoolNext` / paint registration** — 1.9% self (down from ~3%
   on older flames). Keep watching under `world` scenario; live-chain work
   already landed for animated elements.
5. Soft3D scanline micro-opts — **do not chase** without a new leaf. Kernels are
   already `-O2` via `TORIDRAW_OPT`; see `docs/TORIDRAW_RASTER_OPTIMIZATIONS.md`.
6. Enum/param host-op hash indexes — dropped (never in profile).
7. `cs2vm2_thread_init` — **not a frame-time target** on these numbers
   (`cs2_vm_init_ns` ≈ 6.9 ms total over 900 frames ≈ 7.7 µs/frame).

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
- Live node sets are maintained at ApplyRuntimeHook / Push / reclaim /
  SetBehavior / ApplyOpKey; `UITree_SyncHookMembership` covers direct hook
  writes (tests). There is no Ensure*Index rebuild path.
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
| `ToriRSServer_Pack --check-only`                                            | **0 errors**, 15 warnings                                       |
| `test-uitree`, `bench-uitree`, `test-cache-trim`, `test-torirsserver-embed` | green                                                           |
| `test-torirsserver-coverage`                                                | green                                                           |
| `make -C 3rd/rscache test`                                             | green (`cachepack-fidelity: all bars met`)                      |
| `readme.md` UITree performance section                                 | points at this doc; historical 84% ToriDraw figure marked stale |

### Second close-out (2026-08-03, after the dat2/layout/emit round)

| check                                                                                                                                                                          | result                                                                                                                             |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------- |
| idle / ui / world harness p95 &lt; 20 ms                                                                                                                                       | PASS at 5.67 / 5.71 / 6.21 ms (`tools/perf/results/61548478-*.csv`)                                                                |
| incremental layout vs forced full resolve, 900 frames × 3 scenarios                                                                                                            | 0 box mismatches                                                                                                                   |
| `ToriRSServer_Pack --check-only`                                                                                                                                                    | **0 errors**, 15 warnings                                                                                                          |
| `test-uitree`, `test-uitree-builder`, `test-uitree-builder-dat1`, `test-chat-widgets`, `test-minimap`                                                                          | green                                                                                                                              |
| `test-cs1`, `test-cs1vm`                                                                                                                                                       | green (`test-cs1` needed `perf/torirs_perf.c` added to its hand-picked link list — the harness counters had broken it)             |
| `test-db`, `test-cs2-{math,string,component-param,triggerop,dialect}`, `test-cache-trim`, `test-task-order`, `test-world`, `test-inv`, `test-varp`, `test-varc`, `test-social` | green                                                                                                                              |
| `test-torirsserver-embed`                                                                                                                                                           | green                                                                                                                              |
| `test-ui-slots`                                                                                                                                                                | fails on `manifest must state [cache:boot] identity` — pre-existing, the target passes a bare `../cache254` rather than a manifest |

Windowed eye-check is left to the operator (`./run-live.sh manifest_osrs230_embed.ini`
after an `EMBED_SERVER=1 TORIDRAW_OPT=1` build). Pixel A/B against a pre-cache
baseline was not retained in-tree; re-capture with `TORIRS_EXIT_BMP` if a visual
regression is suspected after further cache work.
