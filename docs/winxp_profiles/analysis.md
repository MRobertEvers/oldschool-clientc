# Windows XP Soft3D profile

## Scope

This profile measures the 32-bit Windows XP client while the IO, game, and JS5
servers run on the host machine. The XP package contains no cache and no
embedded server. The client uses the sparse `cache.osrs239.sparse` directory,
filled progressively from JS5 at `10.10.10.1:43595`.

The instrumented build retained the release optimizer and SSE2 settings:
`-O3 -g -fno-omit-frame-pointer -march=i686 -mtune=generic -msse2
-mfpmath=sse -DNDEBUG -flto`. It targets PE32 and Windows subsystem 5.01. The
complete compiler/link line is in `effective_flags.txt`.

`cv2pdb-0.54` converted the GCC DWARF image to `torirs-profile.exe` plus
`torirs-profile.pdb`. The exact supplied Very Sleepy 0.7 installer was used on
XP. The raw sampling capture covers 59.968 seconds and 24,784 samples. A second
normal-exit run collected TORIRS_PERF data for 1,000 frames.

Conditions that shape the numbers:

- The client ran **silent**: the log records `audio: no device; running
  silent`, yet music decode and CS2 sound requests still execute (finding 6).
- The TORIRS_PERF run suffered a **mid-capture disconnect and reconnect**
  (`net: connection lost` … `RECONNECT_OK`) with a full world and interface
  reload. It lands in 100-frame window 1, so window 1 is not steady state.
- Five Lua plugins were enabled, including `performance_display`.
- TORIRS_PERF gauge sampling itself runs per logic tick and is visible in the
  data (finding 8).

## Result

The measured performance problem is real and remains after the initial cache
fill settles:

| Metric | Result |
|---|---:|
| Effective FPS, all 1,000 frames | 11.579 |
| Frames over 20 ms | 978 / 1,000 |
| Frame mean / p50 / p95 | 86.36 / 65.72 / 111.52 ms |
| Window 0 (launch) frame mean / p95 | 253.78 / 1,312.19 ms |

### Steady state is windows 2–9

Window 0 is launch/fill and window 1 contains the reconnect world reload, so
steady state is **windows 2–9: 800 frames, 2,631 logic ticks, 3.289
ticks/frame**. (An earlier revision of this document averaged windows 1–9;
that inflated opcodes/tick and host-ops/tick roughly 2× because the reconnect
re-ran every interface `onLoad`.)

Stage means over windows 2–9, in ms/frame. Stages are nested — `display`
contains `render`, `logic` contains `cs2`, `cs2` contains `cs2_settle` — so
columns must not be summed blindly:

| stage | ms/frame | notes |
|---|---:|---|
| frame | 65.58 | period 65.80 |
| display | 35.36 | contains render + emit + paint + present |
| render | 28.08 | world/Soft3D |
| logic | 24.06 | |
| cs2 | 23.40 | = 7.11 ms per logic tick; see finding 2 for what it contains |
| cs2_settle | 9.16 | inside cs2; 39% of it |
| emit | 2.94 | UI emit walk |
| build | 1.58 | |
| paint | 1.57 | |
| present | 1.48 | |
| frame_post | 1.45 | |
| window_sync | 1.03 | |
| tick_packets | 0.20 | |
| layout (stage) | 0.016 | the *stage*; settle-loop layout is inside cs2 |
| async | 0.021 | JS5 fill fully settled |

Steady-state CS2 counters (totals over 800 frames / 2,631 ticks):

| counter | per frame | per logic tick |
|---|---:|---:|
| scripts | 72.92 | 22.17 |
| opcodes | 3,982.6 | 1,211.0 (54.6 per script) |
| host operations | 573.4 | 174.4 (7.86 per script) |
| VM acquires (100% pool hits) | 72.92 | 22.17 |
| VM init time | 0.72 ms | 0.22 ms (9.94 µs per acquire) |
| frame-pool pushes | 106.4 | 32.3 (1.46 per script) |
| cc_deleteall / cc_create | 3.30 / 3.29 | 1.00 / 1.00 |
| apply_geo / apply_content / apply_hook | 20.7 / 8.4 / 4.0 | 6.29 / 2.56 / 1.23 |
| layout nodes visited (99.9% skips) | 23,527 | 7,154 |
| layout resolves | 6.55 | 1.99 |
| emit-walk visits / painter pushes | 2,843 / 6,087 | 864 / 1,851 |
| find_id lookups / probes | 273 / 1,257 | 83 / 382 |

Reproduce with `steady.py`-style extraction over
`winxp-soft3d-torirs-perf.csv.windows.csv` (filter `window` ∈ 2–9), and the
sampling aggregations below with `aggregate_stacks.py`.

## Findings

1. **CS2 steady-state cost is per-invocation overhead, not opcode volume.**
   The cs2 stage is 23.40 ms/frame for 72.9 scripts/frame — **0.321 ms per
   script** for a mean script of only 54.6 opcodes and 7.86 host ops
   (0.195 ms/script even excluding the settle loop). A generous i686 estimate
   of the VM work itself — 55 dispatches, 8 host calls, one 9.9 µs VM
   acquire+init — is ~40 µs, under a quarter of the measured cost. The
   remainder is fixed per-invocation machinery: a ~9.9 KB `calloc` of
   `struct Task_CS2Run` per hook invocation (`src/game/task_cs2_run.c:1367`;
   64 int args + a 16×512 string matrix + a 1,336-byte pending
   `CS2VM_HostRequest`), FIFO queue traffic, `CS2VM2_Acquire` + `CS2VM2_Init`
   (9.94 µs each, 0.22 ms/tick, on a ~2.9 MB pooled VM), hook-slot heap
   churn (finding 4), request-union clears (finding 5), and the settle loop
   (finding 2). The VM pool itself is not the problem: 58,336 of 58,336
   steady acquires were pool hits. Sampling agrees:
   `app_dispatch_clientscript` sits on 79% of stacks while `CS2VM2_RunOp` is
   only 9.9% inclusive (5.94 s) — time is around the VM, not in it.
   An earlier revision reported 2,340.91 opcodes/tick and 344.43 host
   ops/tick; those figures averaged in the launch and reconnect windows,
   whose interface `onLoad` scripts are ~40× larger than steady timer
   scripts. Steady scripts/tick (22.17 vs 23.07) barely moved — the quiet
   workload is the same ~22 timer hooks every tick, they are just small.

2. **The `cs2` stage is broader than VM execution, and 39% of it is the
   settle loop.** `TORIRS_PERF_STAGE_CS2` wraps each catch-up
   `app_logic_tick` *and* `app_settle_cs2_frame` (`src/app.c:22823`,
   `:22837`). The logic tick runs the whole serial task queue — world load,
   music decode, interface opens all bill to "cs2" during loading windows —
   and the settle loop (`src/app.c:10811`) alternates
   `TaskRunner_SettleFrame` with a **full-tree `UITree_LayoutResolve` every
   iteration** until the queue idles. Steady state shows 6.55 layout resolves
   and 23,527 layout-node visits per frame (tree ≈ 7,000 components), none of
   it in the near-zero `layout` stage. `cs2_settle` is 9.16 ms/frame; the
   per-iteration full-tree resolve and the followup pump are the first place
   to look, ahead of any interpreter work.

3. **The launch-heavy sampling capture is one-fifth heap, by OS tail.**
   Classifying every sample's CRT/OS tail over the 59.9 s capture: self
   43.1%, file-io 29.4% (17.6 s), **heap 20.6% (12.3 s)**, the rest
   waits/GDI/raw syscalls. Heap time by nearest client frame:
   `merge_column` (WorldBuilder) 2.44 s, Dat2Disk read paths ~1.75 s,
   `rs_cs2_host_exec_dispatch` 0.95 s + `rs_cs2_runtime_hook_slot` 0.77 s
   (≈1.7 s of CS2 hook-slot alloc/free/copy churn), `CS2VM2_StrPool_Alloc`
   0.18 s. Most heap cost is launch-time (world build, cache reads), but the
   hook-slot churn recurs whenever interfaces open or hooks re-register.

4. **`RS_CS2Host_Exec` spends half its time in the allocator and an eighth
   in `memset`.** Its subtree totals 3.895 s: 52% carries a heap OS-tail and
   0.46 s (11.8%) is `memset` — the 1,336-byte `CS2VM_HostRequest` clears.
   The interpreter side matches: the top `cs2vm2.c` source line by samples is
   the PUSHSCRIPT request clear (`src/cs2vm2/cs2vm2.c:1470`, 0.224 s), with
   the same whole-union `memset(&request, 0, sizeof(request))` pattern at the
   ENUM_LOOKUP (`:5947`), DB (`:6028`), varbit-read (`:816`), and CC_CREATE
   (`:1617`) builders.

5. **A silent client still pays for sound.** 18.8% of `CS2VM2_RunOp` (1.115 s
   sampled) is `sound_request` (`src/cs2vm2/cs2vm2.c:2060`) — the
   SOUND_SYNTH/SONG/JINGLE opcode builders, each clearing a fresh 1,336-byte
   request — plus 0.349 s of `App_PlaySound` in the frame loop, on a client
   whose log says `audio: no device; running silent`. Music is worse at
   launch: the Vorbis decode path is 5.55 s (9.45% inclusive) decoding music
   nothing will play.

6. **Launch cost is file handling, not bytes.** `PlatformXIO_Js5Pump` is
   21.79 s inclusive (36.4%) with a 16.9 s file-io tail. Of that, 6.85 s is
   `dat2disk_fopen_index` opening and closing the index/dat files **per
   read** (`fopen`→`CreateFileA` chains), and ~1.25 s is CRC32 of every
   fetched group (`3rd/rscache/src/checksum.c:51`, misattributed to
   `RSCache_ProfileZero` in the symbol view). `Task_WorldLoad_Run` adds
   7.72 s, 4.73 s of it heap (`merge_column`). On i686, libgcc 64-bit
   division helpers (`__udivmoddi4`, `__umoddi3`) are 9.1% exclusive
   (5.46 s): ~4.2 s under Vorbis decode, ~1.6 s under Dat2Disk offset math.

7. **Soft3D alone exceeds a 50 FPS frame budget.** Steady render mean is
   28.08 ms/frame before the rest of the client is considered.
   `ToriRS_Soft3D_Execute` (3.20 s sampled) is 69%
   `project_vertices_array_ortho_fused_clip` — which LTO has inflated by
   inlining lighting/raster work — 12.5% `memset` (surface clears), and 7.6%
   `BlitArgb`. The nested `display` stage (35.36 ms) minus `render`
   reconciles with emit 2.94 + paint 1.57 + present 1.48 + window_sync 1.03.
   SSE2 is enabled; D3D9 remains the answer when frame rate matters on this
   VM, with Soft3D for compatibility/debugging.

8. **The profiler measures itself.** With TORIRS_PERF enabled, every logic
   tick walks the whole interface tree in `UITreeIfaceStats_SampleGauges`
   (`src/ui/uitree_iface_stats.c:99`; 0.846 s sampled) and counts the layout
   free list by walking it (`src/ui/uitree_layout.c:315`; 0.254 s). That is
   roughly 0.4–0.5 ms of every measured tick — ~6% of the 7.11 ms/tick cs2
   figure — and it contaminates both captures. Gauge sampling should move to
   window boundaries before per-optimization comparisons are trusted at the
   sub-millisecond level.

9. **UI steady-state mutation volume is small; lookup traffic is not.**
   Per tick: 6.29 geometry writes, 2.56 content writes, 1.23 hook
   registrations, and **exactly one `cc_deleteall` + `cc_create` pair** —
   one timer script rebuilds a component every tick and is worth identifying
   by name (Gate 0). Component-id lookups run 83/tick with 382 probes/tick
   (4.6 probes per lookup). Emit-side volume behind the 2.94 ms emit stage:
   2,843 emit-walk visits and 6,087 painter push/pops per frame.

## Symbol reliability (LTO)

The build keeps `-flto`, so the PDB's inlined/LTO function boundaries are
approximate. Verified misattributions to read around:

- `App_WorldApplyNpcType` shows 88.6% inclusive — a bogus LTO parent that
  swallowed most of the frame loop; ignore its inclusive number.
- `PlatformWindow_SetWindowSize` (5.65%) is really `load_cache_item_dat2`
  cache-load work.
- `UITree_InvViewGridHitTest` (2.07 s) appears under CS2 dispatch parents;
  its only real callers are `uitree_obj_cell.c:76` and `uitree_input.c:40`.
- `RSCache_ProfileZero` (1.26 s) is the CRC32 loop at
  `3rd/rscache/src/checksum.c:51`.
- msvcrt/ntdll frame names (`wscanf`, `mktemp`, `putch`) are nearest-export
  guesses for CRT internals; classify them by chain, as
  `aggregate_stacks.py` does, not individually.

Source-line totals, leaf costs, TORIRS stage timings, and counters are more
reliable than any high-level parent name.

## Local Win64 baseline status

The iteration-platform baseline capture **has not run**:
`local-soft3d-baseline.log` shows the Win64 client exiting with
`cannot create/open incremental dat2 cache at ..\..\cache.osrs239.sparse
(the directory must already exist)`. Create the sparse cache directory (or
point `--cache` at an existing one) and recapture before any before/after
comparison; every optimization gate in `../CS2_OPTIMIZATION_TARGETS.md`
needs the local column.

## Artifacts

- `winxp-soft3d-60s.sleepy` — raw Very Sleepy archive
- `winxp-soft3d-60s-raw/` — extracted raw text payload
- `winxp-soft3d-60s-flamegraph.svg` — interactive/hoverable flame graph
- `winxp-soft3d-60s-flamegraph.png` — static overview
- `verysleepy_hotspots.csv` — inclusive and exclusive symbol totals
- `verysleepy_source_hotspots.csv` — source-line totals
- `verysleepy_top_stacks.csv` and `verysleepy_stacks.folded` — stack data
- `aggregate_stacks.py` — folded-stack aggregation used for the findings
  above: OS-tail classification (heap/file-io/wait/GDI), heap attribution to
  nearest client frame, per-subtree child splits, top exclusive leaves. Run
  as `python aggregate_stacks.py verysleepy_stacks.folded`.
- `winxp-soft3d-torirs-perf.csv` — aggregate TORIRS_PERF report
- `winxp-soft3d-torirs-perf.csv.windows.csv` — 100-frame windows
- `winxp-soft3d-torirs-perf.log` — run log (reconnect and silent-audio
  evidence)
- `local-soft3d-baseline.log` — failed Win64 baseline attempt (see above)
- `torirs-winxp-sse2-profile-dwarf.exe`, `torirs-profile.exe`, and
  `torirs-profile.pdb` — symbol-bearing profiling build

The implementation backlog these findings feed is
`../CS2_OPTIMIZATION_TARGETS.md`; the architecture behind it is
`../CS2_OPTIMIZER_PLAN.md`.

The XP VM reports a 2005 wall-clock date, so the date embedded in `Stats.txt`
is not a trustworthy capture timestamp; duration and sample counts are valid.
