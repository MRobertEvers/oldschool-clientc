# Krait 300 measurements vs. the armv7 kernels: what the arch_fuzz findings buy

Source: `arch_fuzz/FINDINGS.md` (Krait 300, Moto X XT1060, 1.728 GHz, PMU cycles).
Targets: the three armv7 kernel families under `3rd/toridraw/`:

| family | files | in the Moto X client frame? |
|---|---|---|
| projection | `impl/projection/projection.perspective.prepared.neon32.impl.h`, `zdiv/projection.zdiv.neon32.u.c`, `projection.bound.neon32.u.c` | yes — **1.68 ms/frame** of CPU across both threads (kr1, §M) |
| face sort + cull | `impl/facesort/facesort.bitonic_radix.small.neon32.u.c`, `…dispatch.u.c` (radix, tile leaf, compaction) | yes — **2.88 ms/frame** across both threads (kr1, §M) |
| raster | `impl/raster/asm/tri.{flat,gouraudhsllightness,tex}.aarch32.S`, `graphics/raster/edge_slopes_aarch32.inc` | **no** — the client renders through GLES2; these run only on the soft3d lane and in `toridraw_presorted_neon_test` |

1 ms = 1.728 M cycles. Sections 1–10 were written from `FINDINGS.md` and the source
before anything was measured; **§M (below §0) is the 2026-09-03 phone profile** of the
current `--gles2-dualcore` client, with PMU counters, and each later section carries a
`Measured:` note where the profile confirms, sizes, or retires its estimate. Where the
two disagree, §M wins. Older frame numbers quoted in §§3–4 are from `FRAME_BUDGET_PLAN.md`
(p3a, single-threaded, 13.7 ms/frame) and are labelled as such.

---

## 0. Short answer

Yes. The findings do three things for this tree:

1. **They close four open questions the tree measured but could not explain.** The failed
   staging experiments (`TORIDRAW_EDGE_STAGE_IN/OUT`, the LERP8 texel-index staging), the
   gouraud `vdup`-from-GPR loss, and the sort's NEON→ARM pack serialisation all reduce to
   two measured facts: **store-to-load forwarding fails on any size mismatch (+4 cycles)**
   and **an ARM↔NEON crossing costs ~3.5 cycles each way, and VFP-scalar-then-NEON on the
   same register costs +5 more**. Section 2.
2. **They put hard floors under each kernel**, which says where further work can and
   cannot pay. The projection block is latency-bound (≈65-cycle chain vs ≈45 NEON issue
   slots) and for a four-vertex tile the vector body is ~12 % of the measured per-model
   time — the remaining ~500 cycles per tile are shell. The K16 sort block is at its
   load-port floor (48 loads per 8 faces ≈ 6 cycles/face); **measured at 29 % of the
   sort's cycles and 39 % of its D-cache refills** (§M.3), more than the 15–20 %
   estimated. Sections 3–4.
3. **They name specific, cheap, bit-exact edits** with a cycle number attached. The
   ranked list, re-ordered by what the profile says is actually there:

| # | change | kernel | est. gain | conf. | grounded in |
|---|---|---|---|---|---|
| 1 | Run `ToriRS_FrameNextCommand` once, not once per thread: the worker replays the frame bus to find its models (2.32 ms/frame of CPU across both threads, 20 % of the worker's D-refills and I-misses). Publish the translated command with the worker's result, as `platform_quirks.md` already proposes | dual-core lane | −1.0..1.3 ms/frame of worker CPU; shortens the draw's wait (1.45 ms/frame) by the same | high | §M.1, §M.2 |
| 2 | Take the two `dmb ish` out of the draw thread's poll loop in `dualcore_source_take` (each poll = 2 barriers ≈ 100–160 cycles; ≤ 4096 polls before it yields). Poll with a plain load, barrier once on success | dual-core lane | draw-thread cycles only; the wall-clock wait is the worker's to fix (item 1) | high | §4.5, §M.5 |
| 3 | Priority partition + bitonic network: 27 % of the sort's cycles and **56 % of its branch mispredicts** (short data-dependent inner loops; ~4.7 K mispredicts/frame on the worker alone). Fixed-N network bodies and a branch-free bucket write | sort | −0.2..0.3 ms/frame | high (measured), med (fix) | §M.3, §2.1 |
| 4 | Stagger the scene SoA arrays (or accept a write-through L1 and prefetch): the sort's interleave pass reading `screen_{x,y,z}` is **23 % of the sort's D-refills** on its own | scene alloc / sort | one L2 hit (32) per 4 vertices ≈ 0.1 ms/frame | med — mechanism still to be split (§M.3) | §3.1–3.2, §3.5 |
| 5 | Sort prefetch distance: `+32` faces is exactly one line ahead; move to 2–4 lines and issue once per line, not per block. The K16 gather now measured at 39 % of the sort's refills | sort | L2-resident: 28.5 → 25.2 cyc/line; DRAM: 168 → 45–66 | high | §4.4, §M.3 |
| 6 | Do the textured block-end divide (`RECIP`/`UQUOT`/`WRAPQ`) as one NEON vector instead of five ARM↔VFP crossings with VFP/NEON register mixing and two serial scalar chains | tex asm | −25..35 cycles per 8-px block (~20 %) | high | §1.3 |
| 7 | `ldm`/`stm` the textured row advance and row head (7 load-add-store triples → 4 `ldm` + 3 `stm`) | tex asm | −12..15 cycles/row | high | §4.1 |
| 8 | Keep `shift` in r7 (fold `have_cur` into control flow) — removes 3 frame reloads from the head of every block's dependency chain | tex asm | −6..9 cycles/block | high | §3.1, §1.3 |
| 9 | Drop the two `mov`s from the flat/gouraud row loop's critical path (`mov` is 2 cycles and not eliminated) | flat/gouraud asm | −2..4 cycles/row | high | §1.4, §5 |
| 10 | Make the flat opaque fill's 4..7 / ≥8 split branchless (clamp the pair-loop stop) | flat asm | −1 mispredict (13 cycles) per width-threshold crossing per triangle | med | §2.1 |
| 11 | Prefetch the next textured block's first texel and the next row's framebuffer line | tex/alpha asm | hides one L2 miss (32) per block; one DRAM miss (355) per RMW row | med | §4.4, §3.1 |
| 12 | Two tiles per projection call (two independent 4-vertex chains in one window) | projection shell | shell (`ToriDraw_ProjectWithVTable`, 0.51 ms/frame) is now measured at 42 % of the projection family's time | med, structural | §1.5, §1.2, §M.4 |
| — | ~~`vmlaq_s32`/`vmlsq_s32` for the rotation pairs~~ | projection | **retired**: the shipped kernel already has `vmla.i32`/`vmls.i32` — clang fused them (§M.4) | — | — |
| — | ~~Exact tail without `__aeabi_idiv`~~ | projection | **demoted**: the scalar tail is ~6 % of the kernel's samples; `__divsi3`+`__udivsi3`+`__aeabi_idiv` total 0.11 ms/frame process-wide and the tail is only part of that | low | §M.4 |

Items 6–11 are on the soft3d lane only. Items 1–5 and 12 touch the client frame; items
1–3 did not exist as estimates before the profile.

---

## M. Measured on the phone, 2026-09-03

### M.1 Method

- Device: XT1060, Android 5.1, both Krait cores online at 1.728 GHz for the whole run
  (`/sys/devices/system/cpu/online` = `0-1`; cpu1 came up when the client started).
- Binary: the OPT=1 `libtorirs.so` in `android/src/main/jniLibs/armeabi-v7a` (built
  2026-09-03 00:18, `.symtab` present, no frame pointers — flat histograms only). The
  PROFILE=1 build of HEAD **does not boot on this phone** — see §M.6.
- Scene: `manifest_osrs239_phone.ini` against the local `torirsserver`, account `testc`,
  Lumbridge, camera still, no input. **Plugins off** (`TORIRS_PLUGINS=0`) and the saved
  15 fps "Limit Framerate" option (`preferences.ini` `5=15`) cleared so the loop runs at
  the pacer's 20 ms as in every earlier profile (49.06 fps by the `swap:` cadence, 300
  swaps per 6.115 s → **490 frames per 10 s window**). `--gles2-dualcore` on, as shipped.
- Recorder: `simpleperf` via `run-as`, whole process, 10 s each, ≤ 2 PMU events per run:
  `kr1` cpu-clock 1 kHz (10,145 samples, 0 lost); `kr2` `L1-dcache-load-misses` +
  `branch-misses`; `kr3` `L1-icache-load-misses` + `cpu-cycles`; four `stat --per-thread`
  passes for the totals below. Note the Krait event `L1-dcache-load-misses` counts the
  same thing as `L1-dcache-store-misses` (61.5 M vs 60.9 M over 10 s): it is
  `L1D_CACHE_REFILL`, total refills, not load misses.
- ms/frame = samples / 490 at 1 kHz.

### M.2 The frame

The process used 1.01 cores (10,145 samples in 10 s). Two threads matter:

| thread | samples | **CPU ms/frame** | cycles/frame | instr/frame | **IPC** |
|---|---:|---:|---:|---:|---:|
| draw (`Thread-8296`) | 6,805 | **13.9** | 22.35 M (12.9 ms @1.728) | 10.50 M | **0.47** |
| worker (`gles2-stage`) | 3,023 | **6.2** | 8.42 M (4.9 ms) | 5.35 M | **0.64** |
| AudioTrack | 297 | 0.6 | | | |

Against `FINDINGS §1.1`'s 2.55 sustained IPC, the draw thread issues at 18 % of the
core's rate and the worker at 25 %. The PMU says where the rest went:

| per frame | draw | worker | cost model (FINDINGS) | draw cycles | worker cycles |
|---|---:|---:|---|---:|---:|
| instructions at 2.5 IPC | 10.50 M | 5.35 M | §1.1 | 4.2 M | 2.1 M |
| branches / mispredicts | 843 K / **72.3 K (8.6 %)** | 367 K / **25.8 K (7.0 %)** | 13.4 each (§2.1) | 0.97 M | 0.35 M |
| L1D loads / refills | 4.66 M / **125.6 K (2.7 %)** | 2.08 M / **44.6 K (2.1 %)** | 32 if L2 hit (§3.2) | ≥ 4.0 M | ≥ 1.4 M |
| L1I misses | 54.6 K | 30.2 K | 32 if L2 hit (§3.9) | 1.7 M | 1.0 M |
| **explained** | | | | **10.9 M of 22.35 M** | **4.9 M of 8.42 M** |

The unexplained half is (a) refills that go to DRAM at 355 rather than L2 at 32 — 10 K of
the worker's 44.6 K would close its gap alone — and (b) on the draw thread, the poll loop
in §M.5 (2.5 M cycles). I-miss rate is 0.005 per instruction on both threads, well under
the 0.03 where `§3.9` says fetch becomes the limiter: **the frame is data-miss and
branch-miss bound, not fetch bound.** `stalled-cycles-frontend/backend` read 0 on this
PMU; they are not implemented.

**Draw thread, top of the flat histogram (ms/frame):**

| symbol | ms | note |
|---|---:|---|
| `bucket_paint_world` | 1.89 | 18 % of the draw's branch mispredicts on its own |
| `dualcore_source_take` | **1.45** | the draw waiting for / claiming the worker's results — §M.5 |
| `ToriRS_FrameNextCommand` | 1.06 | 11.5 % of the draw's I-misses |
| `World_CycleRegisterPainterDynamics` | 0.74 | 9.7 % of D-refills |
| `emit_walk_node` | 0.72 | 7.5 % of D-refills |
| face sort family (draw-side share) | 0.65 | models the draw claimed because the worker was behind |
| `gles2_dispatch` + `gles2_render_frame_commands` | 0.83 | 12 % of the draw's I-misses |
| projection family (draw-side share) | 0.47 | |
| `__memcpy_base` | 0.37 | **15 % of the draw's D-refills** — the largest single refill source |
| `gles2_bake_pose_vertices` + `trspk_toridraw_bake_face` + `gles2_painter_push_resident` | 0.78 | |
| kernel | 0.64 | futex / scheduler |
| `libGLESv2_adreno` | 0.62 | |
| `__findenv` | 0.035 | but 3.5 % of the draw's branch mispredicts: something `getenv`s every frame |

**Worker, top of the histogram (ms/frame):**

| symbol | ms | note |
|---|---:|---|
| face sort + cull family | **2.23** | `…small_general` alone 2.01; 41 % of the worker's D-refills, 33 % of its mispredicts, 26 % of its I-misses |
| `ToriRS_FrameNextCommand` | **1.26** | the worker replays the frame bus; 20 % of its D-refills, 15 % of mispredicts, 19 % of I-misses |
| projection family | **1.21** | kernel 0.52 (`notex_noyaw_noclip`), shell `ToriDraw_ProjectWithVTable` 0.35, tile4 0.09, clip 0.09, rest 0.16 |
| `GLES2DualCoreStage_ComputeModel` | 0.49 | |
| `ToriDraw_AnimApplyTransform` (+ animate) | 0.47 | 13 % of the worker's mispredicts |
| `__udivsi3` + `__aeabi_idiv` | 0.07 | |

**Kernel families, both threads together:** sort + cull **2.88 ms/frame** (13.9 % of all
samples; the 09-01 single-thread figure was 12.3 %), projection **1.68 ms/frame**
(kernel 0.70, shell 0.51, tile4 0.13, clip 0.13, rest 0.21). `ToriRS_FrameNextCommand`
**2.32 ms/frame** — run once per thread, it is now the largest single line item in the
process.

### M.3 Inside the sort (`toridraw_compute_projected_face_order_small_general`, 16.4 KB)

No line tables in this build, so the function was bucketed by address and each hot range
read back from the disassembly (`kr3` cycles; `kr2` refills and mispredicts; shares are
of the function's own samples):

| phase | address range (in `.so`) | cycles | D-refills | branch-miss | what the disassembly shows |
|---|---|---:|---:|---:|---|
| K16 gather + winding block | `4e36f0–4e3aef` | **29 %** | **39 %** | 7 % | 24 `ldrsh` + 24 `vld1.16 {d}` gathers, `vuzp`, `vmull/vmlsl.s16`, `vcgt/vbsl`, 2 `vst1` — the block of §4.1, as counted |
| interleave + bound pass (and K16 rebuild) | `4e32f0–4e36ef` | 9 % | **23 %** | 5 % | `vld1 ×3` from `screen_{x,y,z}` → `vsub`, `vmovn`, `vst4.16`, `vmin/vmax`; the three loads take 13 % of the whole function's refills |
| compaction + radix (count, prefix, scatter) | `4e42f0–4e46ef` | 21 % | 19 % | 14 % | compaction loop `ldr/str/cmn/addne/subs/bne` ≈ 9 % of cycles by itself (hottest single instruction in the function); radix `ldr/add/str count[]` chains |
| priority partition | `4e48f0–4e4cef` | **20 %** | 6.5 % | **21 %** | per key: `ldrb` packed priority, two `ldr/add/str` read-modify-writes on `count[]`/`depth_sum[]`, `strh`, one data-dependent `bhi` |
| bitonic network (N ≤ 64) | `4e4cf0–4e4eef` | 7.5 % | 1.5 % | **35 %** | per-layer `vld1/vmin/vmax/vbit/vst1` in memory; inner loops of 1–4 trips whose counts change with every model |
| per-model setup, dispatch, emit | rest | ~14 % | ~11 % | ~18 % | includes the `getenv`-cached static checks (settled, cold) |

What this changes:

- **The block is bigger than estimated** (29 % vs 15–20 %) and its cost is refills, not
  issue slots: 39 % of the function's refills land on the 24 `vld1.16` gathers from
  `sm_vertex_xyz16` (`r10 + idx*8`). S1 (prefetch distance) and the index-load halving
  (S3) are both still right, but S1 is the one that touches the measured cost.
- **The interleave pass is the S2 evidence** — 13 % of the sort's refills are its three
  `screen_{x,y,z}` loads, reading what the projection wrote microseconds earlier. Two
  mechanisms fit: the L0/L1 set conflict of §4.2-S2, or a write-through, no-allocate L1
  (`FINDINGS §3.5`'s inference) under which a just-written array is *never* in L1 for its
  first reader regardless of alignment. They are told apart by one experiment: pad the
  three allocations by `k*64` and re-record `kr2`; if the interleave's refills do not
  move, it is write-no-allocate and the fix is a `pld` two lines ahead in the interleave
  loop (or having the projection kernel write the interleaved form directly).
- **Two phases the estimates ignored carry 27 % of the cycles and 56 % of the
  mispredicts:** the priority partition and the bitonic network. At 13.4 cycles each
  (`§2.1`), the sort's 32.5 % share of the worker's 25.8 K mispredicts is 8.4 K
  mispredicts/frame ≈ 112 K cycles ≈ 0.065 ms — small in cycles, but they sit on the
  per-model critical path of a function that runs ~1,300 times a frame. The bitonic's
  inner loops (`blo`/`blt` on 1–4 trips) are the textbook case `§2.2` describes: the
  8-deep global history cannot learn trip counts that change every model. Fixed-N
  straight-line network bodies for N ∈ {8, 16, 32, 64} remove the loops entirely.
- **Compaction is not free** (S5 said leave it alone): ~9 % of the function on a 6-instruction
  loop with a serial `r11` chain. At ~3 cycles per key it is at the `§1.2` dependent-add
  rate, not the port limit. Folding the compaction into the radix count pass (count only
  non-sentinel keys; scatter skips them) removes one full pass over the keys.

### M.4 Inside the projection kernel (`…prepared_neon32_notex_noyaw_noclip`, 199 instructions)

- **Clang already emits `vmla.i32`/`vmls.i32`** for the rotation pairs (2 + 2 in the
  vector loop), so P3 is retired without a measurement.
- The vector loop (`4f023c–4f0314`) is 55 instructions: 3 `vld1.16` vertex loads, 5
  `vld1.64 [:128]` constant loads, 3 `vld1.32 {d[],d[]}` broadcasts, 3 `vst1.32`, ~30
  NEON ALU — consistent with the §3.1 count. It takes ~93 % of the kernel's samples;
  the scalar tail (`__divsi3` twice per vertex) ~6 %, so **P4 is demoted**.
- Where the samples sit inside the loop: ~25 % on the consumers of the vertex/constant
  loads (`vaddw` right after the first `vld1.16` is the hottest instruction — the input
  arrays are not in L1), ~33 % on the `vcvt → vrecpe → vrecps → vmul → vcvt → vst1`
  chain, ~23 % on the rotation `vmul/vmla/vshr` chain. That is the latency-bound picture
  §3.1 predicted, with a memory component on top that the source count could not show.
- **The shell is 42 % of the family** (`ToriDraw_ProjectWithVTable` 0.51 of 1.68 ms) and
  carries 10.6 % of the worker's I-misses — the ~500-cycle-per-tile shell of §3.1 is real
  and P1's decomposition (I-misses first) is confirmed as the right next measurement.

### M.5 The dual-core lane, as it runs today

`platform_quirks.md` ANDROID-GLES2-002 measured 12.26 → 11.66 ms/frame on the draw thread;
this profile has the draw thread at 13.9 ms of CPU per 20.4 ms frame plus a 6.2 ms
worker. The structural facts the kernels now live inside:

1. **The worker re-runs the frame bus.** `ToriRS_FrameNextCommand` costs 1.26 ms on the
   worker and 1.06 on the draw; the quirks entry names publishing the translated command
   as the next lever, and the profile says it is worth ~1.2 ms of worker time and
   most of the draw's wait.
2. **The draw waits 1.45 ms/frame in `dualcore_source_take`**, and the wait loop pays
   `FINDINGS §4.5` twice per poll: `ldr; dmb ish; cmp` on the publish counter and again
   on the abort flag, up to 4096 polls before a syscall. At 50–80 cycles per `dmb` that is
   ~100–160 cycles per poll, so the loop polls the counter roughly every 100 cycles
   instead of every 3, and each barrier drains the draw core's own store buffer
   (`§4.5`: +3 cycles per pending store) for nothing. Whether the barrier traffic also
   slows the worker's publishing stores is not measured (`§3.10`'s false-sharing result
   was inconclusive). Poll with a plain load and issue one `dmb` after the value is seen;
   the result is the same acquire. The wall-clock wait itself is the worker's per-model
   cost (item 1), not the poll.
3. **cpu1 residency was fine here** — both cores online throughout, worker at 1.33 GHz
   effective (`cycles / runtime`; it was scheduled at reduced frequency part of the time,
   the draw at 1.62). §7's warning stands for low-load scenes.
4. **Result ownership is split at runtime:** 0.65 ms of sort and 0.47 ms of projection
   ran on the draw thread because the draw reached those models first. Any kernel A/B on
   this binary must sum both threads (as §M.2 does), or the split moves and the A/B lies.

### M.6 The PROFILE=1 build does not boot on this phone

`make PLATFORM=android ANDROID_ABI=armeabi-v7a OPT=1 PROFILE=1` of HEAD (28e7cd51c) dies
deterministically during login on a worker thread:

```
signal 7 (SIGBUS), code 1 (BUS_ADRALN)
#00 pc 003efb0c  libtorirs.so  RSCache_Dat2SkeletalBaseBakePalette (dat2_skeletalbase.c)
```

`/proc/cpu/alignment` on this device reads `User faults: 4 (signal)` — the kernel
delivers SIGBUS for any user-mode alignment trap rather than fixing it up. The faulting
loop is the NEON matrix multiply (`vld1.32 {dN[],dN[]}, [rX:32]!` broadcasts with an
explicit `:32` alignment qualifier; `vst1.32`), and the crash site's registers are all
4-byte aligned, so the exact trapping instruction is not yet identified. The OPT=1 build
of the same tree boots and was used for §M. Until this is understood, profiles are flat
histograms only (no frame-pointer call graphs), and any A/B that needs stacks needs the
crash fixed first. Not chased here; recorded so the next person does not lose an hour
to it.

### M.7 What is still unmeasured

- The sort and projection **micro-benches** (`face_sort_bench`, `presorted_neon_*`)
  were not re-run; the binaries on the phone are from 09-01. Their per-face and per-tile
  numbers in §§3–4 are the last known.
- Per-frame **model and face counts** (the `gles2 …/frame` logcat lines) did not print
  on this build with `TORIRS_FRAME_DEBUG=1`, so cycles-per-face for the sort is not
  derivable from this run. The 09-01 bench figure (38–43 cycles/face all-in) is the
  reference.
- **S2's alignment log** (the six pointers `& 4095`) — the `kr2` interleave refills are
  the symptom; the log is the diagnosis and takes one `fprintf`.
- Raster: nothing new; not in this frame.

---

## 1. The findings distilled into a cost sheet for these kernels

Everything in this table is measured in `FINDINGS.md`; the right-hand column is what it
means for code in this tree.

| fact | value | consequence here |
|---|---|---|
| 3 ALU pipes, ~2.55 IPC sustained; **one** NEON/VFP pipe at 1 op/cycle, Q = D cost | §1.1 | A NEON kernel's floor is its NEON op count. Q-form everywhere (already done). Loads (1/cycle) and NEON ALU are separate resources. |
| dependent `add` chain = 1.667 cycles; 3 independent chains = 2.62 IPC | §1.2 | Serial scalar bookkeeping (edge step → shift → compare → address) runs at 0.6 IPC. |
| `mov rX,rY` = 2.0 latency, **not** eliminated; `eor r,r,r` does **not** break a dependency | §1.4 | A `mov` on a critical path costs more than an `add`. Zero with `mov #0` / `vmov.i32 #0` (the asm already does). |
| `add` with shifted operand = 3 latency | §5 | `add r12, r5, r9, lsl #2` (row address) is 3 cycles on the row chain. |
| `mul` 4 / `mla` accumulator 2; `umull` 4/6 | §5 | `mla` chains at 2/step; the sort's `smull` winding is 4–6. |
| `sdiv` = ~1 cycle per dividend bit, 0.5/cycle | §5.1 | Three edge-slope `sdiv`s ≈ 60–80 cycles serial; the NEON ladder wins (matches the 45 vs 54 ns measured in `edge_slopes_aarch32.inc`). |
| `vdiv.f64` = **31** latency, 30 throughput; `vdiv.f32` 5/4 | §5 | Two `DIVEXACT64` per affine textured row = ~62 cycles, serialized. |
| `vmul.f32` 5, `vadd.f32` 3, `vrecpe` 4, `vrecps` 8 | §5 | A two-step reciprocal ladder is ~28 cycles of latency. |
| **ARM→VFP→ARM round trip 7.0; NEON lane↔ARM round trip 7.0** | §1.3 | ~3.5 cycles per crossing. Not a pipeline drain — but the tex kernel does ~12 crossings per block end. |
| **VFP scalar → NEON view of the same register: +5** | §1.3 | `RECIP` (`vcvt.f32.s32 s0` then `vrecpe.f32 d30, d0`) and `UQUOT` (`vmul.f32 s2` then `vmax.f32 d1`) pay this every block. |
| Reorder window ≈ 80 instructions | §1.5 | A block of ≥ 80 instructions cannot overlap with the next one. The projection block is ~65, the K16 sort block 138. |
| Predicted branch free when there is other work; taken back-to-back 2.07; **mispredict 13.4**; indirect mispredict 12.6 | §2.1, 2.4 | Data-dependent span-width branches cost 13 cycles per direction change. |
| Global history 8 deep, correlates across sites; indirect predicts only 1/2/4-target cycles | §2.2–2.4 | An indirect call site with > 4 hot targets (the 12 projection entry points via a vtable) pays ~18 cycles per call. |
| RAS 8 entries | §2.5 | Not a concern for these kernels (leaf depth ≤ 4). |
| **L0 4 KB direct-mapped, 3 cycles**; L1 16 KB 4-way, 6; L2 512 KB, 32; DRAM 330–360 | §3.1–3.2 | Six same-index SoA arrays at 4 KB-multiple strides collide in L0 (1 way) **and** L1 (4 ways). |
| L1 replacement not LRU (50 % miss at ways+1) | §3.4 | Five lines in one set still hit half the time. |
| NEON loads 24 GB/s from L1; `ldr` 6.6; `ldm` 2 regs/cycle | §3.5, §4.1 | Frame reloads want `ldm`; bulk copies want `vld1 {d0-d3}`. |
| **`pld` 8 lines ahead = 5.7× on a DRAM stream; 2 lines ahead = 2.2× on L2; >8 harmful** | §4.4 | Prefetch distances must be chosen per level; `+64 B` is the wrong distance for both. |
| **Store→load forwarding 2 cycles on exact address+size; any mismatch = 6 (full L1)**; no 4 K aliasing | §4.2 | Every "stage through the stack" trick with a NEON store and ARM loads (or vice versa) is a size mismatch. |
| Unaligned free; only a 64 B line crossing costs +1 | §4.3 | 16-byte alignment is not a goal; 64 is. |
| L0-I 4 KB (2.65 IPC fetch), L1-I 16 KB (1.6), 128 B I-lines | §3.9 | Hot code should sit inside 4 KB per loop and 16 KB per frame stage. |
| `dmb` 50–80 cycles (+3 per pending store); `ldrex/strex` ~60; `isb` 5.7 | §4.5 | Phase 3 (second core): one barrier per frame handoff, never per model. |
| No denormal penalty | §5.2 | No FZ mode needed for the float reciprocal paths. |

---

## 2. What the findings explain about past A/Bs in this tree

These close questions that `FRAME_BUDGET_PLAN.md` and the asm headers left as
"measured, not understood". Nothing to change; but the mechanism decides what to try next.

**2.1 The three stack-staging experiments were size-mismatch forwarding failures.**
`FINDINGS §4.2`: forwarding succeeds only when the load's address *and size* match the
store's; otherwise the load waits for the store to reach L1 and pays a full 6-cycle access.

- `TORIDRAW_EDGE_STAGE_IN`: six 4-byte `str` then one 16-byte `vld1` — mismatch.
- `TORIDRAW_EDGE_STAGE_OUT`: one 16-byte `vst1` then three 4-byte `ldr` — mismatch.
- LERP8 index staging: two 16-byte `vst1` then eight 4-byte `ldr` — mismatch, ×8 per block.
  Lost 2.5 % of the whole test; reverted.

All three replaced ~3.5-cycle lane moves with 6-cycle (plus store-commit) loads. The
tex header's explanation ("an ARM load that hits a NEON store still in flight waits for
the NEON queue to drain") is the same effect seen from the other side.

What this predicts, untested: **4-byte single-lane NEON stores** (`vst1.32 {dN[i]}`)
followed by 4-byte `ldr` at the same address are an exact size match. Whether the LSU
forwards NEON stores to ARM loads at all was not measured by arch_fuzz (only `str`→`ldr`
was). That is the one experiment that could still make staging pay; see §8.

**2.2 The gouraud `vld1 {d0[],d1[]}` fix (0.68× → 1.36×) is the ARM→NEON crossing.**
`FINDINGS §1.3`: ~3.5 cycles per direction, ~7 round trip. In the four-pixel block loop the
`ldr` + `vdup.32 q0, r10` put a crossing on the store's dependency chain every block; the
memory broadcast keeps the chain inside the load and NEON domains. The header's model of
"a queue that drains" overstates the cost per crossing (it is a fixed ~3.5 cycles, not a
flush), which matters for LERP8 below: eight independent lane moves are ~3.5 cycles of
latency each, overlappable, not eight drains.

**2.3 The sort's block IPC of 0.4 with the ARM-side pack.** Five NEON→ARM moves at the end
of the block's chain: each waits for the vector result (chain ≈ 30 cycles) then adds 3.5,
and the pack that consumed them fed the next block's store cursor. §1.3 and §1.5 together:
the pack made the loop latency-bound at one chain per block with an 80-instruction window
that could not see the next block. The sentinel store + compaction fix is exactly what
these numbers prescribe.

**2.4 E1 (two sort blocks per trip) died of registers, and the findings say the window
would not have saved it anyway.** The K16 block is 138 instructions; the window is ~80.
Two blocks per trip would still be issue-serialised past the first 80 instructions. The
only pipelining that pays on this core is one that keeps each trip under ~80 instructions
with two independent chains inside it — which a 16-Q-register file cannot hold for this
block. Closed, on two grounds now.

**2.5 The `-mcpu=krait` / `-mthumb` / `-fomit-frame-pointer` build A/Bs were flat.**
Consistent: `sdiv` at 1 cycle/bit is only ~2× a good software divide on 20-bit dividends,
and there are ~1.6 K divides a frame (§5 below); Thumb halves code size but §3.9 says
the L1-I cliff is at 16 KB and the *hot* set per stage is probably already inside it.

**2.6 The edge ladder's reciprocal arm beat three `sdiv`s by 1.2×.** §5.1: the dividend
`dx << 16` has 17–27 significant bits → 20–30 cycles per `sdiv`, three of them serialised
on one divider (0.5/cycle) ≈ 75 cycles; the NEON ladder is ~45 cycles of chain plus nine
crossings (~30) ≈ 78. The two measured numbers (44.9 vs 54.5 ns = 78 vs 94 cycles) match
this decomposition. It also says where the ladder's remaining cost is: **nine ARM↔NEON
crossings** (six `vmov.32 dN[i], r` in, three `vmov.32 r, dN[i]` out), not the arithmetic.
`vmov dN, rA, rB` moves two GPRs into one D register in one instruction and
`vmov rA, rB, dN` the reverse; that is 4 in + 2 out = 6 crossings, if the paired form
costs one crossing on Krait (unmeasured — §8).

---

## 3. Projection

### 3.1 Where the cycles are

Per 4-vertex block, `toridraw_projection_prepared_neon32_core` (tex, yaw, noclip), counted
from the source:

| resource | ops | floor (cycles) |
|---|---|---|
| NEON ALU: 3 `vmovl` + 8 model-yaw + 3 position adds + 8 camera-yaw + 8 pitch + 4 cot + 11 zdiv (+4 bound min/max in the loop form) | 45–49 | **45–49** (1/cycle) |
| loads: 3 `vld1` vertices + up to 19 point-of-use constant `vld1q` (each macro use reloads; the output stores may alias the block, so clang cannot CSE them) | ≤ 22 | ≤ 22 |
| stores: 6 (tex) or 3 (notex); tile4 adds 4 bound stores | 6–10 | 6–10 |
| dependency chain x → rotations → z_final → reciprocal → x/z → store | — | **≈ 65** (§1.3/§5 latencies: `vmul` 4, `vadd`/`vshr` 3, `vrecpe` 4, `vrecps` 8, `vcvt` assumed 3) |

So a single block is **latency-bound at ~65 cycles against ~45 issue slots**, and with a
~65-instruction block in an ~80-instruction window, consecutive blocks overlap only
partially. For a **four-vertex tile the block runs once**: ~65–70 cycles ≈ 40 ns. The
bench measures 327 ns per tile after `TORIDRAW_PROJ_TILE4`. **The vector body is ~12 % of
the tile's time; ~500 cycles per tile are shell** (dispatch, eligibility, handle
resolution, FastCull, argument marshal). Over 763 tiles that is ~0.22 ms; over all 1,331
models at the p3a kernel cost (0.86 ms → ~1,100 cycles/model average) the non-vector share
is larger still.

This confirms the plan's re-aim ("the entry block is the per-model fixed cost") and adds
the ceiling: no edit inside the block can move the client by more than ~0.05 ms.

*Measured (§M.4):* the family is 1.68 ms/frame across both threads, of which the
`notex_noyaw_noclip` kernel is 0.70 and the `ToriDraw_ProjectWithVTable` shell 0.51. Inside
the kernel ~93 % of samples are in the vector loop and ~6 % in the scalar tail; inside
the loop a quarter of the samples sit on the first consumers of the vertex and constant
loads, so the loop has a memory component the source count above does not show (the
int16 vertex arrays are not L1-resident when the kernel reaches them). The
`vmla.i32`/`vmls.i32` fusion below (P3) is already in the shipped code.

The point-of-use constant loads (`TORIDRAW_PROJ_PREP_POINT_OF_USE`) are the right call
by these numbers: they cost up to 19 slots on the load port, which still has ~25 spare
per block against the NEON pipe's ~45, and zero on the NEON pipe, which is the binding
resource. (This assumes `vld1` and a NEON ALU op can issue in the same cycle — §8.)

### 3.2 Levers

**P1 — Decompose the 500-cycle shell with the PMU (do this first).** FINDINGS gives the
unit costs; three single-counter passes on the projection bench (`cycles`, `instructions`,
`branch-misses`; §7 says one counter per pass) split 500 cycles into: instruction volume
(at 2.5 IPC, 500 cycles = 1,250 instructions), mispredicts (13.4 each — 10 mispredicts is
27 % of the shell), and the remainder (memory: an L2 hit is 32, a DRAM miss 355 — one
cold model-struct line is 70 % of the shell on its own). `L1-icache-load-misses` in a
fourth pass tells whether the shell is fetch-bound (§3.9: past 16 KB of hot code, fetch
drops to 1.6 IPC; `ToriRS_FrameNextCommand` alone is 18 KB and runs between every model).
Which of the four dominates decides whether the fix is fewer switches, a predictable
dispatch order, more prefetch, or code layout — they are different work.

*Measured (§M.2, §M.4), process-level rather than per-shell:* `ToriDraw_ProjectWithVTable`
carries 10.6 % of the worker's I-misses against 3.3 % for the kernel it calls, and 5 %
of its mispredicts; `ToriRS_FrameNextCommand`, which runs between every model on both
threads, is the largest I-miss source on either. The whole-thread I-miss rate (0.005 per
instruction) is below the fetch-bound threshold, so the shell's cost is more likely the
per-model D-refills and mispredicts than fetch — but the per-shell bench pass above is
still the way to split it.

**P2 — Two tiles per call** (`est. −0.1..0.2 ms`, structural). §1.5 and §1.2: two
independent 65-cycle chains in one ~80-instruction window run in ~75 cycles, not 130, and
one shell serves two models. 57 % of projected models are 4-vertex tiles sharing one
prepared camera; the scene loop can hand the tile4 slot a pair (two positions, two yaw
rows, two output offsets). Register budget for two blocks: ~12 live Q each at peak is over
16 — so interleave at the *stage* level (both rotations, then both zdivs), which keeps peak
live vectors near 14 with point-of-use constants. Bit-identical by construction.

**P3 — `vmla`/`vmls` for the six rotation pairs** — **retired.** *Measured (§M.4):* the
shipped `notex_noyaw_noclip` loop already contains 2 `vmla.i32` + 2 `vmls.i32` (the
`noyaw` variant has four pairs, not six); clang fused them for the generic `armv7-a`
target. Kept for the record of why it was expected to matter:
(`est. −6 NEON ops/block, −9 cycles
of chain`). Each `(x*c + z*s) >> 16` is `vmul, vmul, vadd, vshr`; `vmul,
vmla, vshr` is bit-identical (wrapping int32). Clang fuses `vmulq_s32`+`vaddq_s32` into
`vmla.i32` only when the subtarget says the VMLA accumulator forwarding is not hazardous
(it is disabled for Cortex-A8/A9; unknown for the `-march=armv7-a` generic target). Read
the `.so` disassembly for `vmla.i32`; if absent, write `vmlaq_s32` explicitly. Then
measure `vmla.i32 q` latency/throughput and the `vmul → vmla` accumulator forward on
Krait (§8) — if the accumulator path is late-forwarded like ARM `mla` (2 cycles, §5), this
is a clean 12 % cut of the block's NEON ops.

**P4 — Exact tail without `__aeabi_idiv`** (`est. −35 µs/frame`; *measured (§M.4):
demoted* — the tail is ~6 % of the kernel's samples ≈ 0.04 ms/frame, and all software
divides in the process (`__divsi3`/`__udivsi3`/`__aeabi_idiv`) sum to 0.11 ms/frame, of
which the projection tail is a part; the estimate below is about the right size but the
item is no longer worth its own change). 264 models a frame
have a 1..3-vertex tail; two software divides each ≈ 1.6 K divides × ~60 cycles ≈ 55 µs.
The plan's "run the last block at n−4" is ruled out (reciprocal ≠ exact). An exact
alternative that needs no ISA extension: `q = trunc(x * (1/z))` in VFP single, then one
integer correction `if ((q+1)*z <= x) q++; if (q*z > x) q--` (for `x` up to 2^22 and
`z ≥ near_plane`, the f32 reciprocal error is < 1 in `q`, so one step suffices). ≈ 25
cycles including two crossings, bit-exact with `x / z`, gated by
`toridraw_projection_kernel_test`. Alternative: `-march=armv7ve` for the projection unity
behind an `HWCAP_IDIVA` check — `sdiv` on a 22-bit dividend is ~23 cycles (§5.1) — but it
forks the build for ~35 µs.

**P5 — Indirect dispatch.** §2.4: the slot → 12 entry points is a direct call after a
switch (good). `ToriDraw_ProjectWithVTable`'s function pointer and the renderer's kernel
vtable are indirect sites whose target sequence follows the model mix (tile/yaw/clip) —
not a 1/2/4-cycle, so ~18 cycles per model ≈ 25 K cycles ≈ 15 µs/frame. Not worth
restructuring for; listed so it is not chased.

**P6 — Vertex-array prefetch.** For models over ~64 vertices the three int16 arrays stream
at 32 vertices per line. If they are L2-resident (likely: ~160 KB of vertex data a frame
against 512 KB, but the GL driver runs in between), §4.4 says one `pld` 2 lines ahead per
axis per 32 vertices takes 55 → 25 cycles per line: ~1 cycle/vertex ≈ 26 K cycles ≈ 15
µs/frame. Cheap; small.

---

## 4. Face sort + cull

### 4.1 Where the cycles are

Bench: 22–25 ns/face at 1,000–2,000 faces ≈ 38–43 cycles/face all-in (block, compaction,
network/radix, partition, emit; per-model cost amortised away at this size).

K16 block (`block8_k16_neon32`, 8 faces, 138 instructions):

| resource | per block | per face |
|---|---|---|
| loads: 24 `ldrh` indices + 24 `vld1` 8-byte quads | 48 | **6.0 cycles** (1 load/cycle) |
| NEON ALU: 12 `vuzp` + 4 `vsub` + 4 `vmull/vmlsl` + 2 `vshr` + 4 `vaddl/vaddw` + 2 `vmul` + 2 `vshr` + 2 `vadd` + 2 `vclt` + 2 `vand` + 2 `vshl` + 2 `vsub` + 2 `vbsl` | ~44 | 5.5 cycles |
| ARM: address arithmetic, loop, `pld` | ~40 | ~2 cycles at 2.5 IPC |

**The block's floor is ~6 cycles/face on the load port**, with the NEON pipe a close
second, i.e. ~15–20 % of the sort — the same share the per-line profile attributes to
"gather + transposes". Every remaining lever inside the block is bounded by that.

The rest per face (estimates, bench mix): compaction 2–3 (1 load + 1 store + 2 ALU,
branch-free — correct for this core), radix 8–16 (two or four passes of a
load-increment-store on `count[]`, a 2-cycle forward when consecutive keys share a bucket),
bitonic for N ≤ 64 ~10–15 (2 `vld1q` + `vmin` + `vmax` + 2 `vst1q` per vector per layer,
21 layers at N=64), priority partition + merge, emit ~1.

*Measured (§M.3), shares of the function's own cycles in the client, both threads:* K16
block **29 %** (estimate above: 15–20 %), compaction + radix 21 %, **priority partition
20 %**, interleave/bound pass 9 %, bitonic 7.5 %, per-model rest ~14 %. The refills are
in the block (39 %) and the interleave pass (23 %); the mispredicts are in the bitonic
(35 %) and the partition (21 %). The per-face bench number above is not reproducible from
this run (no face count printed); the client's mix is ~1,300 models of mostly small N,
so the per-model phases (partition, network, setup) weigh more here than in a
1,000–2,000-face bench fixture, which is where the estimate's under-count came from.

### 4.2 Levers

**S1 — Prefetch distance** (`free`). `+32` faces of int16 is +64 B: one line ahead. §4.4
measured one line ahead at 168 (DRAM) / 28.5 (L2) cycles per line against 33–66 / 25.2 at
2–8 lines. Set the distance to 4 lines (`+128` faces) — right for L2, most of the way for
DRAM — and issue it once per line (`if ((f & 31) == 0)`) instead of once per block: today
three `pld` per block is 12–24 `pld` per line consumed, each an issue slot. A/B on the
bench with a cold-L2 fixture (the current fixtures are hot).

**S2 — Scene array set conflicts** (`verify, then fix; est. 30–50 µs/frame`). §3.1–3.2:
L0 is 4 KB **direct-mapped**, indexed by VA[11:6]; L1 is 4-way on the same index.
`toridraw.c:411` allocates `screen_vertices_{x,y,z}` and `orthographic_vertices_{x,y,z}`
as six separate `malloc(max_vertices * 4)`; every profile's `max_vertices` is a power of
two ≥ 2048, so each array is 8/16/32/64 KB. Under Android 5.1's jemalloc, six requests of
one such size come back at the **same offset modulo 4 KB**: ≥ 16 KB is a large class and
page-aligned; 8 KB is a small class whose regions sit at a fixed header offset plus
multiples of 8 KB inside page-aligned runs. Either way `screen_x[i]`, `screen_y[i]`,
`screen_z[i]` and the three ortho arrays all map to **one L0 set and one L1 set**. Readers that touch the three screen arrays at the
same index — the sort's interleave pass (3 loads per 4 vertices), the K16 rebuild, the
tile leaf (8 corner reads × 3), `toridraw_projected_bound` — then miss L0 on every access
(+3 cycles each, L1 hit) and, where the ortho arrays are read too, spill the 4-way L1 set
(+26). Check: log the six pointers `& 4095` on the phone once. Fix: one allocation with a
64–192 B stagger between arrays (`base + k * (size + 64)`), or pad each `malloc` by
`k * 64`. Same check for `sm_sort_keys`/`sm_sort_tmp` (equal sizes; the radix's read
stream and scatter target).

*Measured (§M.3):* the interleave pass's three `vld1` from `screen_{x,y,z}` are 13 % of
the sort's D-refills (23 % with the rest of that range), which is the symptom this lever
predicts — but a write-through, no-allocate L1 (`§3.5`'s inference) produces the same
symptom without any set conflict, because the projection's stores would never have put
those lines in L1 in the first place. The `& 4095` log plus one padded re-record of `kr2`
separates them; if it is write-no-allocate, the fix is a `pld` in the interleave loop or
having the projection kernel emit the interleaved `xyz16` directly (the sort's K16 rebuild
then disappears too).

**S3 — Halve the index loads** (`est. −1.5 cycles/face in the block, ~4 % of the sort`).
Indices are int16 in three arrays; `ldr` fetches two per load, `uxth`/`lsr` split them
(`uxth` is on the 1/cycle DSP unit, `lsr` 2 cycles on an ALU — both have slack). 24 → 12
index loads per block moves the floor from the load port (48) to the NEON pipe (44). Small,
mechanical, bit-identical.

**S4 — Register-resident bitonic for N ≤ 32** (`est. 2–4 % of the sort`, conditional). N
≤ 32 is 8 Q registers with 8 scratch; the network's 15 layers then do no loads or stores
at all. Today each layer round-trips memory: `vst1q` then `vld1q` of the same 16 bytes.
Whether that forwards (NEON→NEON, exact size) is not in FINDINGS — if it does at 2
cycles the win is the 4 memory ops per vector per layer (issue slots); if it does not, each
layer also eats a 6-cycle L1 latency and the win is larger. Measure first (§8).

*Measured (§M.3):* the network is 7.5 % of the sort's cycles but **35 % of its branch
mispredicts** — the per-layer inner loops run 1–4 trips and the trip count changes with
every model's N, which is exactly what an 8-deep global history cannot learn (`§2.2`).
That makes the register-resident form worth more than the issue-slot count says: a
straight-line body per N ∈ {8, 16, 32, 64} has no inner-loop branches at all. The
forwarding question above still decides how much of the remaining 7.5 % goes with it.

**S6 — Priority partition** (*new from the profile*, `20 % of the sort's cycles, 21 % of
its mispredicts`). Per sorted key: `ldrb` the packed 4-bit priority, `ldr/add/str` on
`count[prio]`, `ldr/add/str` on `depth_sum[prio]`, `strh` the face into its bucket, and a
`bhi` on `prio > 9`. Two read-modify-write chains on tables indexed by a data-dependent
byte are `§4.2`'s 2-cycle forward when consecutive keys share a priority (the common
case: most models are all-priority-0) and a 6-cycle L1 round trip when they do not. Cheap
shapes: keep the ten counters in registers for models whose priority set is a single value
(known from the model's priority table before the loop), and make the `> 9` test a
`cmp; movhi` on the accumulate rather than a branch.

**S5 — Things the findings say to leave alone.** The compaction pass (branch-free, 1
load + 1 store per key — at the port limit already; *measured: ~9 % of the sort's
cycles on a 6-instruction loop, ≈ 3 cycles/key, i.e. at the `§1.2` dependent-add rate
on the `r11` write-cursor chain rather than the port limit — folding it into the radix
count pass would remove the pass, see §M.3*). The sentinel-store design (§2.3). The
`vst4q`/`vst4_s16` interleave (once per vertex per frame; `vld4` costs 2 vs 1 and the
store is likely similar — ~0.5 cycle/vertex). The winding's `vmull.s32` (4-ish cycles; no
faster exact form exists on A32). The K16 tail and tile leaf (already at their per-model
floor; the remaining per-model cost is the dispatcher, which §3.1 of the projection
section's PMU method also applies to).

---

## 5. Raster (soft3d lane)

Not in the Moto X client frame; relevant to the soft-render lane and to any other armv7
device. Ordered by size.

### 5.1 Textured: the block-end divide is a domain-crossing storm (`R1`)

Per 8-pixel block, `Ltex_blk` runs `RECIP` once (twice when `have_cur` is 0), `UQUOT`
once, `WRAPQ` once, each of which:

```
RECIP:  vmov s0, r      ; ARM→VFP           3.5
        vcvt.f32.s32 s0 ; VFP scalar
        vrecpe.f32 d30, d0    ; NEON reads d0 = {s0,s1}  → +5 (§1.3 VFP→NEON same reg)
        vrecps / vmul / vrecps / vmul  (NEON, 8+4+8+4 = 24 latency)
UQUOT:  vmov s2, r ; vcvt ; vmul.f32 s2,s2,s0   ; VFP scalar
        vmax.f32 d1 ; vmin.f32 d1               ; NEON on d1 = {s2,s3} → +5
        vcvt.s32.f32 s2 ; vmov r, s2            ; VFP→ARM 3.5
WRAPQ:  vmov s2, r ; vcvt ; vmul.f32 ; vcvt ; vmov r, s2   ; two crossings
```

In steady state (`have_cur` set) one end is computed per block: **5 ARM↔VFP crossings
(~17 cycles), 2–3 same-register VFP/NEON penalties (~10–15; the NEON→VFP direction in
`UQUOT`'s final `vcvt` is unmeasured but the same mechanism), and two scalar chains run
serially (`UQUOT` ≈ 17 cycles, `WRAPQ` ≈ 11) where one vector op would do both** — all on
the dependency chain that gates `FITS` and `LERP8`, inside a block whose vector body is ~50
NEON ops. Estimated 45–60 cycles of a ~130–160-cycle block; the vector form below removes
~25–35 of them.

The fix keeps everything in one domain: `vmov d0, r4, r5` and `vmov d1, r6, rZ` (two
paired ARM→NEON moves = the whole `{au, bv, cw, 0}`), `vcvt.f32.s32 q0, q0`, `vdup.32 q1,
d1[0]` (w broadcast), `vrecpe/vrecps ×2` on q1, `vmul.f32 q0, q0, q1`, clamp lane 0 with
`vmax/vmin` against a constant vector, `vcvt.s32.f32 q0, q0`, `vmov r8, r9, d0` (one
paired NEON→ARM move). Three crossings, no VFP/NEON mixing. NEON `vmul.f32` on normal
inputs (these are converted integers) is IEEE round-to-nearest like VFP's, so the result
is bit-identical to today's scalar sequence — `toridraw_presorted_neon_test`'s ppm numbers
must not move. The `WRAPQ` range test stays on the ARM side as written (its comment is
already right about `vmrs`).

### 5.2 Textured: the row bookkeeping is load-port bound (`R2`, `R3`)

`Ltex_rowadv` is seven `ldr / ldr / add / str` triples (21 loads, 7 stores) and `Ltex_row`
reloads ~14 frame words; each block reloads `F_SHIFT` up to three times plus `F_SAU8`,
`F_SBV8`, `F_SCW8`, `F_SHX8`, `F_TWM1`, `F_TEXELS`, `F_GATE`. At one load per cycle
(§4.1) a 16-pixel row costs **~40 load-port cycles of bookkeeping before any texel**, and
the 8 texel gathers per block share the same port.

- **R2:** at `Ltex_rowadv` every GPR is dead. `F_AU/F_BV/F_CW` (108–116) and
  `F_YAU/F_YBV/F_YCW` (120–128) are contiguous: one `ldm` of six (3 cycles, §4.1: 2
  regs/cycle), three `add`, one `stm` of three. `F_LX/F_LS/F_RX/F_RS` (92–104): one `ldm`
  of four. `F_SHADE/F_SHY`, `F_ROWPTR/…`: pairs. ~28 port cycles → ~14.
- **R3:** `F_SHIFT` is read at the head of every block's chain (`ldr → asr r1, r6, r0 →
  cmp → RECIP …`): 3 cycles of L0 latency plus a slot, three times per block. r7 holds
  `have_cur`, a one-bit flag; encode it as two loop entry labels (or in the sign of an
  existing value) and keep `shift` in r7 for the whole triangle. `q10` already holds
  `-shift` for the vector side.

### 5.3 Textured: the gather and the eight lane moves (`R4`, measure before building)

Eight `vmov.32 r1, dN[i]` per block: §1.3 puts them at ~3.5 cycles latency each and they
are independent — so ~28 cycles of latency, overlappable, if the transfer unit pipelines
(throughput unmeasured — §8). Two cheaper shapes, both needing one arch_fuzz kernel each:

- `vmov r1, r2, dN` moves two lanes in one instruction (r2 is free during the gather;
  `F_GATE` is loaded after). 8 → 4 transfer instructions if the paired form is one crossing.
- Eight 4-byte `vst1.32 {dN[i]}` to the frame, eight 4-byte `ldr` back: exact size match
  (§2.1). Pays only if NEON-store→ARM-load forwarding exists on this LSU.

The texel loads themselves: a 128×128×4 texture is 64 KB — L2-resident, not L1. A block
of 8 pixels advances u by ~8 texels along one texture row (one or two 64 B lines) at one
v; each new line is a 32-cycle L2 hit and the 4–5-miss MLP (§3.7) overlaps only what the
80-instruction window exposes. **`pld` the next block's first texel** as soon as `nxt_u`,
`nxt_v` are known (they are this block's far end): 3–4 instructions per block, ~100
cycles of lead — enough for L2 (32), not DRAM. `est. −20..30 cycles/block` when the
texture is L2-resident.

### 5.4 Affine rows: two `vdiv.f64` per row (`R5`, note only)

`DIVEXACT64` twice per affine row = 2 × 31 latency on a 30-throughput divider = **~62
cycles serialised per row**, plus four crossings. Every textured terrain tile is affine
now. The comment's argument that `n * (1.0/w)` is inexact holds in double too (`49 *
fl(1/49)` is `0.99999999999999989` and truncates to 0), so a shared reciprocal is out. An exact alternative is `sdiv`
(~24 cycles on a 24-bit dividend, §5.1, and no crossings) behind `HWCAP_IDIVA` — a fork
of the shipping asm for ~40 cycles/row. Recorded; not recommended unless the affine lane
shows up in a profile.

### 5.5 Flat / gouraud row loop (`R6`, `R7`)

```
mov  r9, r0          ; 2 cycles, NOT eliminated (§1.4)
mov  r10, r1
add  r0, r0, r2
add  r1, r1, r3
cmp  r9, r10 ; beq
asr  r9, r9, #16     ; depends on the mov: 2 + 2
asr  r10, r10, #16
```

- **R6:** `cmp r0, r1; asr r9, r0, #16; asr r10, r1, #16; add r0, r0, r2; add r1, r1,
  r3; beq 64f` — the `cmp` sets the flags, the non-S `asr`/`add`s leave them alone, and
  the edges are still stepped on a skipped row exactly as today. Same semantics, two
  fewer instructions, two `mov`s (4 cycles) off the row's critical path. The chain is then
  `asr (2) → cmp (1.7) → sub → add-shifted (3) → store`.
  For the 59.5 % of spans under four pixels the row overhead *is* the cost, so this is
  worth ~10–15 % of a narrow row. Same edit in the gouraud `SEGMENT`.
- **R7:** `FILLOPAQUE` has two data-dependent width branches (`< 4`, `< 8`). Widths across
  a triangle's rows are monotone-ish, so the global predictor (§2.2–2.3) learns them and
  misses at the threshold crossings: ~2 mispredicts (27 cycles) per branch per triangle.
  The `< 8` split exists only because for 4..7 pixels `end-32` lies before the span; clamp
  it before r12 is advanced (`sub r9, lr, #16; cmp r9, r12; movlo r9, r12`) and the 4..7
  case falls out of the ≥ 8 path: the pair loop runs zero trips (`r12` aligned up is ≥ `r9
  = start`) and the two closing stores at `r9 = start` and `end-16` stay inside the span
  for any n ≥ 4. One branch fewer per row. The `< 4` split stays
  (the three-store tail has no overlap-safe alternative).
- The `[r12:128]` alignment dance in the pair loop is validated by §4.3: aligned 16-byte
  stores never cross a 64 B line; unaligned ones do 23 % of the time at +1 each. Keep.

### 5.6 Alpha spans: prefetch the next row (`R8`)

`FILLALPHA` and the keyed texture path read the framebuffer (RMW). A 1280×720×4
framebuffer is 3.6 MB — DRAM. The hardware stride prefetcher (§3.6) trains only across a
long sequential run; a short span is 1–2 lines. At the top of each row the next row's
address is known (`r5 + r6`): one `pld [r5, r6]` per row, ~a row's work (≥ 355 cycles
for any span over ~15 pixels) ahead of use. §4.4: a missing line is 188 cycles per line
consumed against 33 with a well-placed prefetch. Opaque stores need no line under a
write-through L1 with a store buffer (§3.5's inference), so this is for the blend and
keyed doors only.

### 5.7 Gouraud per-triangle setup

`vdiv.f64` (31) + 2 `vmul.f64` (6) + converts + 4 crossings ≈ 60–70 cycles per triangle
for the colour gradient. The C reference does it in double; nothing to change without
changing the reference. Recorded so it is not re-derived.

---

## 6. Memory layout facts to carry into any future kernel

- **Direct-mapped L0 + power-of-two SoA = conflict.** Any set of arrays sized to a
  power-of-two multiple of 4 KB and indexed together lands in one L0 set. The scene's six
  projection arrays (S2) are the instance in this tree; the general rule is to stagger by
  64–192 B, and to prefer interleaved records (`sm_vertex_xyz`) where a kernel reads all
  components anyway — which the sort already learned for other reasons.
- **Align to 64, not 16.** `_Alignas(16)` on the batch row buffers is for SSE `movdqa`;
  on this core it buys nothing (§4.3), and 48-byte records straddle lines every other
  record regardless (harmless: the A32 kernels read them with 4-byte `ldr`).
- **`pld` distance is per level:** 2 lines for L2-resident streams, 8 for DRAM, never
  more than 8 (§4.4 shows the benefit collapsing past 8 — the line is evicted before use).
  Issue one `pld` per line consumed, not one per loop trip.
- **NEON loads for bulk copies:** 24 GB/s vs 6.6 for `ldr` and 10.3 for `ldm` (§3.5). The
  sort's emit is already vectorised; the compaction pass cannot be (no A32 left-pack).
- **Stores are cheap and flat to L2** (18.5 GB/s L0..L2); reads fall off a 4–6× cliff
  leaving L1. Design for read locality, not store locality.

---

## 7. Phase 3 (second core) — what the findings say about it

`FRAME_BUDGET_PLAN.md` promoted the second Krait core to a planned step; it has since
shipped as `--gles2-dualcore` (`platform_quirks.md` ANDROID-GLES2-002) and §M.5 is how it
runs today: draw thread 13.9 ms CPU/frame, worker 6.2, the draw waiting 1.45 ms/frame in a
poll loop that pays two `dmb`s per iteration, and the frame bus run once on each thread.
Three findings bear on it directly, and item 1 is no longer hypothetical:

1. **Barriers cost 50–80 cycles plus ~3 per pending store; `ldrex/strex` ~60 uncontended
   (§4.5).** The world→GL handoff must be one release/acquire pair per frame per direction.
   A per-model or per-run publish (a `dmb` per sort result, say) would cost ~1,300 × 80 ≈
   0.06 ms — small, but a per-face one would not be. No fine-grained locks anywhere in the
   pipeline. *Measured:* the lane publishes per model (a CAS per slot, release/acquire
   per result) — that side is within the 0.06 ms — but the *consumer's* poll loop
   (`dualcore_source_take`) issues two barriers per poll for up to 4096 polls; see §M.5.
2. **cpu1 is hot-unplugged under low load and `sched_setaffinity` to an offline CPU fails
   with `EINVAL`, leaving the thread on cpu0 (§3.10).** A worker "pinned" to cpu1 may be
   silently running on cpu0 and *serialising* with the main thread. The design needs a
   keep-alive or a sustained-load contract, and every A/B of Phase 3 must report measured
   cpu1 residency next to its ms/frame, exactly as arch_fuzz had to.
3. **L2 sharing showed no measurable contention (+0.0 %) with a sibling streaming 512
   KB (§3.10).** The second core can stream the scene's model data without slowing the
   GL thread's L2 use. False-sharing cost is *inconclusive* (residency problem); keep the
   two threads' written lines apart by ≥ 64 B and do not rely on the null result.

---

## 8. Gaps: arch_fuzz kernels that would settle open questions here

FINDINGS covers ARM-side forwarding and cross-domain latency but not the NEON-side
shapes these kernels lean on. Each of these decides one item above:

| kernel to add | decides |
|---|---|
| `vst1.32 {dN[i]}` (4 B) → `ldr` same address: forwards? latency | §2.1 / R4: whether any stack staging can work |
| `vst1q` → `vld1q` same 16 B address (NEON→NEON): forwards? | S4: register-resident vs in-memory bitonic |
| `str` ×4 → `vld1q` covering them (4 small stores, one big load) | closes the EDGE_STAGE_IN post-mortem |
| `vmov dN, rA, rB` and `vmov rA, rB, dN`: one crossing or two? throughput | §2.6 edge ladder (9 → 6 crossings); R1; R4 |
| back-to-back independent `vmov.32 r, dN[i]`: throughput | R4: are eight lane moves 28 cycles or 8 |
| `vmla.i32 q` latency, throughput, and `vmul → vmla` accumulator forward | the projection chain estimate in §3.1 (the shipped loop already uses `vmla`/`vmls`, §M.4 — this now sizes the chain rather than deciding a change) |
| `vmull.s32`, `vmlsl.s32`, `vaddl.s16`, `vuzp.16`, `vtrn.32`, `vext`, `vst4.32`, `vcvt.f32.s32 q`: latency/throughput | sort block and projection chain estimates above (currently assumed 3–4) |
| does `vld1q` dual-issue with a NEON ALU op in the same cycle? | whether point-of-use constant loads are truly free (P-section assumes yes) |
| `pld` into L0 vs L1: does a prefetched line land in the 4 KB L0? | whether S1/R8 give 3- or 6-cycle hits |
| `vdiv.f64` back-to-back independent: is the 30-cycle throughput per divider or overlappable? | R5 |

---

## 9. What not to do (findings-backed)

- Do not stage NEON vectors through the stack for ARM consumption, or vice versa, with
  mismatched access sizes. Every such attempt in this tree lost, and §4.2 says why.
- Do not mix VFP-scalar and NEON operations on the same D/Q register (+5 cycles). The
  tex kernel's `RECIP`/`UQUOT` do; R1 removes it.
- Do not chase 16-byte alignment. Only 64 B line crossings cost.
- Do not use `eor r,r,r` or `mov r,r` as idioms; neither is free (§1.4).
- Do not add `S`-suffixed forms where the flags are unused (2 pipes instead of 3).
- Do not software-pipeline a block that is already ≥ 80 instructions; the window cannot
  hold two (§1.5). Cut instructions first.
- Do not prefetch more than 8 lines ahead (§4.4).
- Do not build a "two-arm" runtime dispatch on the indirect predictor: > 4 hot targets at a
  site mispredict every time (§2.4). Direct calls after a switch, as the projection slot
  does, are right.
- Do not trust a second-core measurement without its cpu1 residency (§3.10).

---

## 10. Suggested order (revised after §M)

1. **Fix the PROFILE=1 boot crash (§M.6)** so call-graph profiles are possible again;
   then **S2's alignment log + padded re-record** (half a day together). Both are
   prerequisites for measuring anything else properly.
2. **Dual-core lane: publish the translated command with the worker's result** (ranked
   #1; the quirks entry's own next lever) and **drop the barriers from the poll loop**
   (#2). These are the largest measured items in the process and are not kernel work.
3. **Sort: S6 (priority partition), S4 as fixed-N straight-line bodies, compaction folded
   into the radix count, S1 (prefetch distance)** — in that order, each A/B'd on the
   client with both threads summed (§M.5 item 4), since the bench mix under-weights the
   per-model phases that the profile put at the top.
4. **P1** on the projection shell with the per-shell counters; **P2** (tile pairs) once P1
   says how much of the shell pairing would amortise. P3 is done by the compiler; P4 is
   not worth a change.
5. **R1, R2, R3, R6, R7** on the soft lane — unchanged: the tex block-end rewrite is the
   one that moves a whole door by a double-digit percentage; the rest are single-digit each.
6. Add the §8 kernels to arch_fuzz; **R4** and the forwarding half of **S4** wait on them.
