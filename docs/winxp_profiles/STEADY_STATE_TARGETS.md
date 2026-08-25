# Steady-state optimization targets — XP / soft3d lane

Triage of where the frame goes after world load, and what to do about it, in
order. Numbers are from the 2026-08-23 measurement set, all taken on the XP box
(Pentium 4 Prescott 2.8 GHz, SSE2 yes / SSE3 no, QPC backed by the 3.58 MHz
ACPI PM timer):

* **Time**: Very Sleepy 60 s capture minus a separately-captured boot profile
  (`TORIRS_MAX_FRAMES=40`, the first frame count that reaches every boot
  milestone), subtracted stack-by-stack with `subtract_folded.py`. Steady state
  is 40.6 s of the 60 s window — **boot was 45% of the naive capture**, which
  is why earlier tables overstated heap and file I/O.
* **Counts**: 1,000-frame `TORIRS_PERF` run, steady windows 2–9. Counters are
  trustworthy; the *stage timings* are not wall-clock — one `clock_gettime` on
  this box costs 3.8 µs (ACPI PM timer syscall) and the per-command bracket
  adds ~13.7 ms/frame of instrument. Wall-clock claims come from Sleepy runs
  with `TORIRS_PERF` unset, and from the benchmarking camera sweep once that
  lane lands.

Artifacts: `steady-only_stacks.folded`, `steady-only-flamegraph.svg` (plus
`steady-boot-*` / `steady-full-*`), `counters-culling.windows.csv`.

## The shape of a steady-state frame

| per frame | counter |
|---:|---|
| 6,335 | painter pushes |
| 2,826 | rejected by the painter gate (45%) |
| 1,639 | model commands submitted to the renderer |
| 674 | rejected by sphere/AABB cull (41% of submissions) |
| 79 | survive cull, project, sort to **zero** faces |
| 965 | models rasterized |
| 18,426 | faces filled — **~19 faces per model** |

Culling is correct and correctly ordered (capacity → rotation-invariant sphere
→ AABB, all before per-vertex projection; ~85% of pushes never reach the
rasterizer). The steady-state problem is not "too many models drawn", it is
**per-model fixed overhead against 19-face models**.

Steady-state exclusive CPU, grouped:

| share | subsystem |
|---:|---|
| ~32% | model render: projection 11.2, gouraud fill 8.3, memset 6.9, texture fill 4.2, TriangleGouraud 1.5 |
| ~14% | UI tree: `UITree_LayoutSetRootSize` 12.6 + emit walk |
| ~12% | syscalls: residual cache fread, GDI present, `__udivmoddi4`, waits |
| ~8% | heap: `RtlReAllocateHeap` 6.8 + alloc/critsec |
| 3.6% | `app_plugin_highlight_next` (Lua nxt-highlight plugin) |
| 2.5% | `msvcrt!bsearch` |
| 1.9% | `painter_cullmap_refresh_camera_key` |

---

## Tier 0 — hours of work, no design risk. Do first.

### 0.1 Bound the per-model depth-table clear — **DONE, −16% frame time**
`ToriDraw_ComputeProjectedFaceOrder` cleared `tmp_depth_face_count` whole —
all `depth_levels = 16384` entries (`TORIDRAW_SCENE_DEPTH_16K`, `faceint_t` =
int16 → **32 KB**) — once per model, to sort ~19 faces. 965 models/frame
≈ 30 MB/frame of zeroing, and every clear evicted 2× the P4's L1D, taxing the
projection/raster loops around it.

This was never bounded in this tree (full-width since the port commit
df036f331); the reference engine bounds it by model depth diameter. It was
cheap at 1500 levels (3 KB) and became the #5 steady-state symbol when the
16K tier landed (06caebceb, 5afae233c) without re-bounding it.

Fix shipped — clear-after-consume, using the sort's **returned** bounds
(`bucket_sort` gives back `min_d | max_d << 16`, updated on every accepted
bucket write) rather than the `2*bias+1` estimate, which animation can stretch
past (the merged QBD reaches a bias of 54,402). `toridraw.c` calloc's the
table so the all-zero invariant starts true, each sort re-zeroes exactly its
own range once its consumer has walked it, and both the no-priority drain loop
and `partition_and_accumulate_faces_by_priority` are read-only over that range.

Measured (2026-08-23, A/B of two binaries built from the same tree, 3 reps,
400 steady frames each after a 40-frame boot):

| | steady ms/frame | 440-frame wall |
|---|---:|---:|
| full-width clear | 42.6 | 31.3 s |
| bounded clear | **35.8** | **28.6 s** |

`msvcrt!memset` exclusive steady CPU fell 2.80 s → 0.23 s (6.9% → 0.5%).
Validated with `TORIDRAW_DEBUG_NDJSON=1` over 400 frames — zero
duplicate/out-of-range/`distinct != ordered` records from
`toridraw_dbg_check_face_order`, which is exactly the stale-bucket symptom —
and with an assert-live build (`OPT_RELEASE_CFLAGS=-flto`, i.e. `OPT=1` minus
`-DNDEBUG`) so the three range invariants actually execute.

The small-scene path (`ToriDraw_ComputeProjectedFaceOrderSmall`,
`sm_depth_offset`, int → 64 KB) has the same shape and was deliberately left
alone: `TORIDRAW_SCENE_SMALL` has no users under `src/`, only `v1/`.

### 0.2 Kill the remaining steady-state cache hydration (part of the 12%)
`fread` from `RSCache_Dat2DiskDat2FileReadArchive` is still visible after
boot. A pre-warmed `cache.osrs239.sparse` (or a larger read granularity)
removes it from the steady frame. Cheap; also makes every future measurement
cleaner. (Verify how much of the 5.04 s syscall bucket it is before claiming
the win.)

### 0.3 Find and delete the `__udivmoddi4`
64-bit divide helper visible in steady-state stacks. Standing rule: **no
64-bit arithmetic in the toridraw rasterizer.** Attribute it (it may be the
GDI/present or timer path, not the rasterizer) and replace with 32-bit or
shift math wherever it is.

## Tier 1 — UI layout — **no longer a target**

This was the #1 item at 36,160 layout nodes/frame and ~12.6% of steady CPU.
Another lane's panel work landed in between and fixed it: the counters now
read `uitree_layout_nodes` = 7,629/frame and `UITree_LayoutSetRootSize` is
0.50 s / 1.2% of steady CPU. Nothing to do here. Kept as an entry only so the
next person does not re-derive it from the stale table above.

## Tier 2 — attribute first, then fix

### 2.1 `RtlReAllocateHeap` 6.8% steady
A realloc storm surviving into steady state; the true caller is masked by
ntdll nearest-export symbols. Attribute with a cheap wrapper (count + bytes
per call site via a macro over `realloc`, or Dr. Memory-style hook — NOT
per-call clock reads). Prime suspect class: a per-frame growing buffer that
should be allocated lazily once and kept. Standing direction: lazily allocate,
never per-frame realloc.

### 2.2 `bsearch` 2.5%
Almost certainly the cache archive id lookup on a hot per-model/per-anim path.
Attribute, then either hash-map it or cache the handle at the call site.

### 2.3 Sort-empty models — 79/frame
8% of drawn models pay full projection + clear + bucket walk and emit zero
faces (all faces back-facing or off-bucket). After 0.1 lands, measure what is
left; a cheap "all faces back-facing" early-out only pays if these models are
big, and at ~19 faces they may not be worth more code.

## Tier 3 — hand SSE2 kernels for the software rasterizer

The lane already builds SSE2: `platform.mk` sets
`-march=pentium4 -mtune=generic -mfpmath=sse` as the win32 base flags
(load-bearing since R1 — it is what selects the textured span's SSE2 `#if`
variant), and every profile number in this document was measured on that
build. Hand kernels are therefore about **beating GCC's autovec**, not about
turning SSE2 on, and the disassembly of the shipping exe says where the
headroom is:

| hot loop | insns | xmm refs | today's codegen |
|---|---:|---:|---|
| gouraud fill (8.3%) | 5,870 | 944 | inner loop mostly scalar `mov`/`sar`/`add` |
| texture span (4.2%) | 3,092 | 216 | nearly all scalar |
| projection (11.2%) | 1,055 | 309 | already well vectorized (`pshufd`/`pmuludq`) |

Target pool ≈ 25% of steady CPU; the realistic hand-ASM win is what remains
*after* autovec, so order by scalar-ness, not by size:

1. **Gouraud scanline** (8.3%, mostly scalar today): 4-px steps interpolating
   the fixed-point HSL-lightness accumulator with `paddd`; palette lookup
   stays scalar (no gather on SSE2).
2. **Texture span** (4.2%, almost fully scalar): per-span divide and per-px
   indirect fetch limit the ceiling — prototype before committing.
3. **Projection** (11.2% but already SIMD): least headroom; attempt last,
   only if the sweep shows GCC left something on the table.

Sequenced after Tier 0/1 because those are cheaper per point, and the camera
sweep benchmark (other lane) is the right harness to score kernels against.

Constraints for whoever writes these: **no 64-bit arithmetic**; models average
19 faces so spans are short — kernel setup must be cheap and every kernel
keeps its scalar tail; unaligned loads are expensive on P4, keep the vertex
arrays 16-aligned (they're scene-owned, alignment is ours to set); asserts
stay live (`NDEBUG` only via `OPT=1`), so kernels get C reference twins and a
compare-mode test, not assert-free trust.

## Tier 4 — smaller, real, in order

* **`app_plugin_highlight_next` (5.0% on the current binary) — gated, worth
  1.8%; the rest needs a different fix.** `app_plugin_highlights_rebuild`
  walked the npc, player, scenery and obj_stack pools at the start of every
  walk, testing each entity against that kind's subject list. A pool whose
  subject list is empty makes the whole walk dead work, so each pool is now
  gated on having something to look for (`app_plugin_highlight_pools_wanted`).
  Not cacheable on `hl->revision`: the resolved list carries draw positions,
  which move every frame without anything being said.

  Measured 38.97 → 38.27 ms/frame (2 clean reps each; one baseline rep was
  discarded for a 3.6 s boot, i.e. a failed boot, against 13.4 s otherwise).
  That is **1.8%, not the 5% the profile suggested**, and the census run says
  why: the cache marks 3 locs and 4 loctypes at boot, so `want_loc` is true
  and the **scenery pool — ~23k entities — is still walked every frame** to
  resolve nothing. The other three pools are now skipped.

  The remaining fix is algorithmic, not another gate: the walk is
  O(entities × members) with the member list on the inside. Index the marked
  keys instead — a 256-byte direct-mapped filter on `loc_id & 255` rejects
  almost every entity with one L1 load, and only a hit pays the full compare.
  Same shape for npc (`npc_id`/`base_npc_id`) and obj.

  Equivalence is machine-checked, not argued: under `TORIRS_HIGHLIGHT_DEBUG`
  the rebuild runs a second time with every pool armed and prints
  `highlight-gate: MISMATCH` if the two resolve to different counts. A gate
  that wrongly skipped a pool would otherwise look exactly like a highlight
  the script never asked for. 400 frames, no mismatch.
* **`painter_cullmap_refresh_camera_key` 1.9%** — refresh only when the
  camera crosses into a new key/cell instead of per frame.
* **CS2 VM init/release ~1.3 ms/frame** (`cs2_vm_init_ns` 691 µs +
  `cs2_vm_release_ns` 619 µs) — pool/reuse the VM across scripts instead of
  init/release per run. (These two counters are ns-clock-read based; sanity
  check against a Sleepy diff before/after.)
* **GDI present** (`gdi_paint_latest` in the syscall bucket) — already
  improved this session; revisit only if the sweep shows it high.

## Measurement discipline

* Wall-clock claims: Sleepy capture with `TORIRS_PERF` unset, boot-subtracted
  via `subtract_folded.py`. Counters: `TORIRS_PERF=1` run, windows 2–9.
* One change per measured build; the camera sweep lane is the arbiter for
  raster-kernel scoring.
* The Aug-22 "baseline" tables predate 16 of the 42 stages and the crash fix;
  do not compare against them.
