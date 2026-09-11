# Krait 300 findings applied to the armv7 kernels

What `arch_fuzz/FINDINGS.md` (measured on the same Moto X the client is
profiled on) says about `projection`, `face sort + cull` and `raster`, and
which of it is actionable.

Companion to `ARMVX_KERNEL_STATE.md` (the sort's own log) and
`FRAME_BUDGET_PLAN.md` (the 15.1 → 9.55 ms ledger). Every claim below is
either a quoted FINDINGS measurement or an instruction count taken from the
shipped `android/src/main/jniLibs/armeabi-v7a/libtorirs.so` (2026-09-03
build, `llvm-objdump` from NDK 27). Nothing here has been measured on the
phone yet; the last section says what to measure first.

---

## 0. Scope, and one honest caveat about raster

The Moto X client renders through GLES2 (`libGLESv2_adreno` is 16.2% of the
ab3 profile). **The software raster kernels — `impl/raster/asm/tri.*.aarch32.S`,
`impl/raster/span/span.tex.neon32.u.c` — do not appear in that profile at
all.** They are the desktop / no-GL-path lane. So the raster section below
is real but it is not on the Moto X's critical path; do not spend the
frame budget there. Projection, cull and sort are.

---

## 1. The number that reframes all three kernels

> §1.1: *"Exactly one NEON/VFP pipe. Every 64-bit **and** 128-bit NEON
> operation saturates at 1.00/cycle."*

Krait 300 decodes 3-wide and sustains ~2.55 IPC on integer ALU work, but
every NEON instruction — arithmetic, load, store, transpose, dup — retires
at **one per cycle**. That gives a hard cycle floor for any vector kernel:
count the NEON instructions in its loop and divide by the elements per trip.
The ARM-side instructions in the same loop are essentially free (three ALU
pipes at 2.55 IPC hide under the NEON stream).

Counted off the shipped `.so`:

| loop | insns/trip | NEON | of which ld/st | ARM | elements | **NEON floor** |
|---|---:|---:|---:|---:|---:|---:|
| `..._prepared_neon32_tex_yaw_noclip` vector block | 74 | 59 | 13 ld / 6 st | 15 | 4 vertices | **14.8 cyc/vertex** |
| `block8_k16` loop (in `..._face_order_small_general`) | 139 | 74 | 27 ld / 2 st | 65 | 8 faces | **9.25 cyc/face** |

Against the bench: the K16 arm measures 33.9 ns/face at 2000 faces = **58.6
cycles/face for the whole sort**, of which the block is (per the 2026-09-01
per-line profile) roughly 40% ≈ 23 cycles/face. That is **2.5× the issue
floor**, so the block is *latency- or memory-bound, not instruction-bound*.

This is the diagnostic the sort work has been missing. It tells you which
lever to pull:

- **ratio near 1.0** → the kernel is issue-bound; cut vector instructions.
- **ratio ≥ 2** → cutting instructions buys nothing; go after latency
  (prefetch, cache placement, independent chains).

E1 ("two independent block chains per trip") failed on register spills, and
FINDINGS explains why that route was the wrong one anyway: §1.2 shows the
1.667-cycle dependent-`add` latency comes from *pipe* round-robin, not from
the register file — and with only **one** NEON pipe, two vector chains cannot
issue in parallel. They can only cover each other's *latency*. The cheaper
way to cover NEON latency here is to remove the misses (§3, §4 below), not
to double the live register set.

---

## 2. Projection — three concrete levers

### 2.1 Ten of 59 NEON slots per block are constant re-loads (17%)

The `TORIDRAW_PROJ_PREP_POINT_OF_USE` comment says hoisting the prepared
block's vectors into registers "starved the allocator". The disassembly shows
exactly what point-of-use costs instead. Per 4-vertex block the loop issues:

- **3** real vertex loads (`vld1.16 {d26}, [r3:64]` etc.)
- **10** constant re-loads: `vld1.32 {d28[],d29[]}` × 5 (the `vld1q_dup`
  splats of `yaw_row[0/1]`, `position->x/y/z`) and
  `vld1.64 {dN,dN+1}, [rM:128]` × 5 (the prepared block's
  `cos_yaw/sin_yaw/cos_pitch/sin_pitch/cot15`)
- plus **~7 ARM `ldr rN, [sp, #0x38..0x4c]`** just to re-materialise the
  *pointers* to those constants.

At 1 NEON op/cycle that is 10 of 59 cycles — **17% of the projection's vector
loop spent reloading values the frame fixed once**.

**The fix FINDINGS points at: use the by-scalar NEON multiply forms.**
All ten constants are *splats* — every lane the same value — and they are
splats only because the operand is Q-wide. A32 has `VMUL.I32 Qd, Qn, Dm[x]`,
`VMLA.I32` and `VMLS.I32` in the same by-scalar form
(`vmulq_lane_s32` / `vmlaq_lane_s32` / `vmlsq_lane_s32`), at the same
1/cycle throughput. The seven trig constants (`cos/sin` model yaw, `cos/sin`
camera yaw, `cos/sin` pitch, `cot15`) collapse from **7 Q registers of
splats into 4 D registers** — and 32-bit by-scalar operands must live in
d0–d15, which d0–d7 satisfies without touching the callee-saved bank.

Budget after the change: 4 D (trig, by-scalar) + 3 Q (`scene_x/y/z`, which
`vadd` cannot take by scalar) + 4 Q (bound accumulators) + 1 Q (`v_mid`) =
**10 Q live, 6 free for scratch**. That is the register budget the current
code could not find, and it is what forced the point-of-use reload.

Expected: 59 → **49 NEON ops/block, −17%** on the projection's vector loop,
with the ARM-side frame reloads going away too. The 440-byte spill frame the
header describes should not return, because the constants now occupy 2.5 Q
registers instead of 10.

Gate: `toridraw_projection_kernel_test` demands bit-exactness with the
portable ladder. By-scalar `VMUL.I32` is the same integer multiply, so this
is a register-allocation change, not an arithmetic one — the test is the
proof.

### 2.2 The scalar tail's divides — and why the `armv7ve` idea should be dropped

`FRAME_BUDGET_PLAN.md` 1b has two steps. FINDINGS kills the second one.

Confirmed in the `.so`: `toridraw_projection_prepared_neon32_tex_yaw_noclip`
ends with **two `bl __divsi3`** per tail vertex, and the whole binary has
**1,559 `__divsi3` call sites and zero `sdiv`/`udiv` instructions** — as
expected for `-march=armv7-a`.

The plan's step 2 is *"an `-march=armv7ve` (idiv) compile of the projection +
sort unity, chosen at load by HWCAP_IDIVA — the Krait has it, the toolchain
flag hides it."* §5.1 says what that would actually buy:

> *"Latency tracks the significant-bit count of the dividend, not the
> divisor: 4 bits → 2 cyc, 16 bits → 17 cyc, 31 bits → 32 cyc. This is a
> radix-2 (1 bit/cycle) iterative divider."*

`screen_x = (x_scene * cot15) >> 6` routinely carries 20+ significant bits,
so hardware `sdiv` there is **20–32 cycles**, against maybe 40 for the
`__divsi3` call. A build-variant + HWCAP dispatch + a second unity object,
for ~30%.

**Recommendation: do step 1 only** — run the last vector block at
`i = num_vertices - 4` and delete the tail. That takes the divide to zero, it
recomputes at most three vertices with identical results, and the arrays are
all `num_vertices` long. Then drop the `armv7ve` variant from the plan for
the projection. (Keep it on the list only if the 64-bit divides in §2.3
cannot be removed algebraically — a 64-bit software divide is far worse than
a 32-bit one, and there `sdiv` is not even applicable.)

Secondary benefit, from §3.9: the four `*_clip` entry points are **2,656 to
4,084 bytes each** against 796–1,036 for the `*_noclip` ones. The clip
bodies are 3–4× larger, and the tail with its two divide calls and their
setup is a large part of that difference. Deleting the tail shrinks the
hottest 8 KB of the projection's 18 KB footprint.

### 2.3 Software 64-bit divides are worse than the plan assumes

Whole-binary census (`llvm-objdump` + grep on call targets):

| helper | call sites |
|---|---:|
| `__divsi3` | 1,559 |
| `__aeabi_ldivmod` | 204 |
| `__udivsi3` | 106 |
| `__aeabi_uldivmod` | 94 |
| `__aeabi_uidivmod` | 90 |
| `__aeabi_idivmod` | 77 |

Plan 1d sizes the 64-bit ones at −0.1 ms. §5.1's "~1 cycle per dividend bit"
applied to a 64-bit dividend, plus the call, puts a `hmap_search` modulo at
**~65–80 cycles each**. Plan 1d's fix (mask, or 32-bit modulo) is right; the
finding is that it is worth more than 0.1 ms if those maps are hit per model
or per face. Count the calls with the shim before sizing it.

---

## 3. The two cache facts nobody has applied yet

These are the highest-value items in this document, and both are close to
free to try.

### 3.1 The six scene vertex arrays are power-of-two sized and consecutively malloc'd — they will alias in L0

`toridraw.c:411-416`:

```c
scene->screen_vertices_x = malloc((size_t)caps->max_vertices * sizeof(int));
scene->screen_vertices_y = malloc(...);   /* x6, consecutive */
```

`max_vertices` is **2048 / 4096 / 8192 / 16384** — so each array is exactly
8 KB / 16 KB / 32 KB / 64 KB, and six of them are allocated back to back.

FINDINGS §3.1–§3.2:

> **L0 D: 4 KB, 3 cyc, 64 B line, direct-mapped (1 way), 64 sets indexed by VA[11:6].**
> **L1 D: 16 KB, 6 cyc, 4-way, same 64 sets.**
> §3.2, stride 4 KB+: W=1 → 3.0 cyc, W=2 → 5.0, W=3/4 → 6.0, W=5 → 19.9, **W=6 → 32.1 (L2)**.
> §3.4: replacement is pseudo-random, not LRU — cycling 6 lines in a 4-way set misses **100%**.

Any allocation stride that is a multiple of 4096 gives every array the
*same* VA[11:6], so all six map to one L0 set and one L1 set. The projection
block touches nine streams per trip (3 model `vertexint_t` arrays + 6 scene
`int` arrays). If the six scene arrays collide, §3.2 says the sixth stream
costs **32 cycles** — L2 — on every new line.

Stores are absorbed by the write buffer (§3.5: write bandwidth flat at
18.5 GB/s from L0 through L2), so the projection itself may not stall. **The
face sort is where the bill arrives**: `lane_blocks` reads
`vx/vy/vz` = `screen_vertices_x/y/z` sequentially in the K16 interleave, and
`block4` reads `xyz` randomly by face index. Random access gets no help from
the stride prefetcher (§3.6: 355 cycles random vs 35 sequential at 16 MB).

**The fix is a stagger, and §4.3 says it is free:**

> *"Misalignment itself costs nothing, for scalar and NEON alike. The only
> penalty is +1 cycle when the access crosses a 64 B line. Aligning to 4
> bytes is pointless here; aligning to 64 is what matters."*

So allocate the six from one block with a **64-byte stagger per array**
(array *k* starts at `base + k*(size + 64)`), or equivalently
`malloc(size + 64*k)` and offset. Every array stays 64-byte aligned, no
access crosses a line that did not before, and the six land in six different
L0 sets.

Note the sort's own scratch is *already accidentally coloured* — `malloc(((size_t)caps->max_vertices + 4) * 4 * sizeof(int))` at `toridraw.c:527`
is 64 bytes past a power of two. The projection's six are not.

**Decisive 5-minute measurement:** on the phone, log
`((uintptr_t)p >> 6) & 63` for all six pointers plus `sm_vertex_xyz`,
`sm_vertex_xyz16`, the keys and the tmp buffer. Any two equal values are a
guaranteed L0 conflict on every block.

### 3.2 The cross-model array prefetch is written, gated, and off by default

`src/render/torirs_frame.c:2438`:

```c
int const depth = 2 + frame_prefetch_model_mode();   /* 2, 3 or 4 */
...
if( depth >= 4 && reach >= cur + 1 )
    ToriDraw_SceneElementPrefetchArrays(...);        /* vertices_x/y/z, faces a/b/c */
```

`frame_prefetch_model_mode()` defaults to **1**, so `depth` is 3 and the
**arrays step — the one that warms exactly the six streams the projection
and the sort are about to walk — never runs**. It only runs at
`TORIRS_FRAME_PREFETCH_MODEL=2`.

Why this matters, from §4.4 and §3.6:

> *"`pld` at 8 lines (512 B) ahead gives a 5.7× speedup on a DRAM stream;
> 2 lines ahead gives 2.2× on an L2-resident stream. This is the
> highest-value single optimisation on this core."*

The painter walks in depth order, so every model's arrays are a cold line
(the comment at 2420 says exactly this). 1,641 models × ~6 streams ≈ 5,000
first-touch misses a frame that no *intra*-model prefetch can cover. At
32 cycles (L2) to 355 (DRAM) that is somewhere between 0.09 ms and 1.0 ms
per frame, and the code to fix it already exists.

**Two things to try, in this order:**

1. `TORIRS_FRAME_PREFETCH_MODEL=2` as-is, A/B'd by the standard method.
2. If that is flat or negative, trim the fan-out. §3.7 measures **4–5
   outstanding misses supported**; `PrefetchArrays` fires **six** `pld`s at
   once and `PrefetchModel` three more. Over-subscribing the miss queue is a
   plausible reason a naive `depth=4` loses. Split it: the three vertex
   arrays at `cur+1` (the projection needs them first), the three face
   arrays at `cur+1` too but only for models the cull will not reject.

### 3.3 The sort's own `pld` is one line ahead, on arrays two to five lines long

`facesort.bitonic_radix.small.neon32.u.c:847`:

```c
__builtin_prefetch(face_a + f + 32);   /* faceint_t is int16_t -> 64 B = 1 line */
```

§4.4's distance table: **D=1 buys 1.1× on a DRAM stream and 1.9× on an L2
one; D=2 buys 2.2× on L2, D=8 buys 5.7× on DRAM.** D=1 is the weakest useful
distance.

But raising D here is not the answer either: a typical model has 60–150
faces = 120–300 bytes = **2–5 lines total**, so an intra-model prefetch at
any distance covers at most a couple of lines and runs off the end. The
lever is §3.2's cross-model prefetch, not this one. Consider deleting the
`if( pld )` test from the block loops afterwards — it is a branch and three
`pld`s per block buying almost nothing, and removing it shrinks the four
folded loop bodies (see §4).

---

## 4. Instruction-cache footprint — the counter-pressure on specialisation

§3.9, measured:

| code footprint | 1–4 KB | 6 KB | 8–16 KB | 24–64 KB |
|---|---:|---:|---:|---:|
| cyc/instruction | **0.378** | 0.542 | **0.627** | **1.692** |
| L1i miss/instruction | 0.000 | 0.00001 | 0.00001 | **0.0314** |

> *"L0 I-cache = 4 KB, sustaining 2.65 instructions/cycle — this is the real
> fetch limit. L1 I-cache = 16 KB at 1.6 instr/cycle. Keeping a hot loop
> under 4 KB is worth 1.66× fetch bandwidth; under 16 KB, 2.7×."*

Sizes from the shipped `.so`:

| symbol | bytes |
|---|---:|
| `App_RunOnce` | 77,536 |
| `frame_loop_step` | 21,640 |
| `ToriRS_FrameNextCommand` | 17,428 |
| `toridraw_project_vertices_clip_portable` | 16,544 |
| `toridraw_compute_projected_face_order_small_general` | **16,356** |
| `bucket_paint_world` | 15,244 |
| the 12 `..._prepared_neon32_*` entry points | ~18,200 |

The face sort alone is one 16 KB function — 4,089 instructions, containing
**4 inlined copies of `block4`** (16 `vmull.s32` at 4 per copy) and **4 of
`block8_k16`** (8 `vmull.s16` at 2 per copy), and 131 backward branches. It
exactly fills the 16 KB L1-I on its own; the hot per-frame path is an order
of magnitude past it.

**This is the best available explanation for the gap the log already
records**: the specialisation steps each measured positive in the bench and
then barely moved in the client —

> *"Client sort share: 12.3 (pre-spec) → 11.65 (e34) → 12.06 (e2) → 12.12
> (prio)."*

The bench runs one kernel in a tight loop with a hot L0-I. The client
interleaves it with the walk, the renderer, the bus and the UI tree, so each
model re-fetches cold code. Every folded loop that made the bench faster
made the client's I-footprint larger. That is not proof — it is a hypothesis
with a cheap test (§6).

**What follows if it holds:**

- **Retire the settled A/B control arms.** Each `TORIDRAW_*_armed()` env
  toggle keeps *both* arms compiled in: `pld`, `k16`, `k16_uzp`, `k16_tail`,
  `bitonic2`, `bitonic_max`, `radix_legacy`, `tile2`, `tile_fast`,
  `emit_vec`, `prio_uniform`. Every one that has been measured and kept
  should have its control arm deleted, not left live. This is the cheapest
  I-cache reduction available and it costs no correctness.
- **Weigh the next specialisation against its code size**, not only its
  bench delta. K16's remaining "still open" items (the K16 tail, a masked
  final block, the tile fast path) each add another inlined body.
- The projection's twelve entry points are the same shape. The four
  `*_clip` bodies at 2.6–4.1 KB each are the ones to shrink first (§2.2:
  deleting the divide tail is most of it).

---

## 5. FastCull, and the raster kernels

### 5.1 Cull

`ToriDraw_FastCull` is already free of divides — the four perspective
extents are compared as multiplies with the divide cancelled across the
inequality, which is exactly right on a core where §5.1 prices a divide by
its dividend's bit count. Nothing in FINDINGS argues against the current
shape.

Plan 1a (copy `radius`, `center_to_top/bottom_edge`,
`min_z_depth_any_rotation` into `struct ToriDraw_SceneElement` so the cull
never chases `element → model → bounds_cylinder`) is *strengthened* by
§3.1: the cylinder sits ~270 bytes into the model struct, so it is a
separate line, and the chase is `element` (3 cyc if L0-hot, 32 if L2) →
`model` → `cylinder` — a **dependent** chain, which §3.6 shows the
prefetcher cannot help with when the addresses are pointer-chased. Three
dependent misses × 1,641 models is the worst-shaped access pattern this core
has. 1a should be first, ahead of anything in §2.

### 5.2 Raster — one thing to check, one thing to stop doing

Reminder from §0: not on the Moto X's hot path. Both items are cheap.

**(a) `span.tex.neon32.u.c` builds each texel quad with eight *dependent*
`vld1q_lane_u32`s.** `raster_linear_opaque_blend_lerp8_v3` writes lanes
0–3 of `t0` then 0–3 of `t1`, each load reading and rewriting the same
register. That is two chains of four partial-register writes. §1.1 prices
NEON at 1/cycle throughput but the *latency* of a lane insert is not
measured in FINDINGS — this is a question for arch_fuzz (§7). If a lane
write does carry a dependency on the register's prior value, splitting into
four independent d-registers and combining with `vtrn`/`vzip` would break
the chain at no instruction cost.

**(b) `tri.flat.aarch32.S`'s `FILLOPAQUE` aligns the store cursor to 16
bytes. §4.3 says that buys at most 25%, and §3.5 says the store buffer has
already spent it.**

```asm
vst1.32 {d0, d1}, [r12]      /* one unaligned store */
add     r12, r12, #16
bic     r12, r12, #15        /* round up to 16 */
...
vst1.32 {d0, d1}, [r12:128]! /* aligned pairs */
```

§4.3 measures an unaligned 16-byte NEON access at **1.00 cycle** unless it
crosses a 64 B line, in which case **2.04**. A run of 16-byte stores from an
arbitrary offset crosses a line on 1 store in 4 → 1.25 cyc/store unaligned
vs 1.00 aligned. But §3.5 measures **write bandwidth flat at 18.5 GB/s
(10.7 B/cycle) from L0 through L2** — 16 bytes per store is **1.5
cycles/store at the buffer's ceiling**, which is above both. The alignment
dance is below the floor that limits it.

The A/B arm already exists (`TORIDRAW_FLAT_FILL_V1` is the simple
unaligned-store loop). Run it; if it is flat, take the simpler code and the
smaller function — which, per §4, is itself worth something.

---

## 6. Measure these first — all cheap, all decisive

In priority order. Each answers a question that changes what gets built.

1. **Array colouring** (5 min, no build): log `(p >> 6) & 63` for
   `screen_vertices_{x,y,z}`, `orthographic_vertices_{x,y,z}`,
   `sm_vertex_xyz`, `sm_vertex_xyz16`, the sort keys and tmp. Equal values =
   guaranteed L0 conflict. If they collide, §3.1's stagger is a ~10-line
   change.
2. **`TORIRS_FRAME_PREFETCH_MODEL=2`** (no build): the standard client A/B
   (`scratchpad/client_ab.sh`). This turns on a prefetch step that is
   written, tested and currently dead.
3. **I-cache miss rate** (one simpleperf run, ≤2 PMU events):
   `L1-icache-load-misses` and `instructions` over a 10 s Lumbridge window.
   §3.9's model says the frame is fetch-bound if misses/instruction
   approaches 0.03. That number decides whether §4 is the top of the list or
   a footnote.
4. **NEON-op count vs measured cycles**, per kernel, using the §1 table as
   the template. Extend it to `block4`, the bitonic and the tile kernel. The
   floor-to-actual ratio tells you per kernel whether to cut instructions or
   cut misses, and it is the number the sort log has been missing.

## 7. What arch_fuzz should measure next

Three questions block decisions above, and none is answerable from the
current FINDINGS:

- **Do NEON loads and NEON arithmetic share the single 1/cycle pipe?**
  Interleave `vld1.32 {q}` with `vadd.i32 q` at k=4 independent chains. If
  the mix sustains 1.00/cycle total, §2.1's saving is a full 17%; if loads
  issue on the LSU alongside NEON arithmetic, it is smaller and §2.1 becomes
  a register-pressure fix rather than an issue-slot one. **This is the single
  most valuable addition**, because the whole "point of use vs hoist"
  question in the projection turns on it.
- **Latency of a partial-register NEON lane write** (`vld1.32 {d0[0]}, [r]`
  chained on the same register vs four independent registers). Decides
  §5.2(a).
- **By-scalar NEON forms**: `vmul.i32 q, q, d[0]` latency and throughput
  against `vmul.i32 q, q, q`. FINDINGS covers the register-register form
  only. If by-scalar is also 1/cycle (it should be), §2.1 is free; if it is
  2/cycle, §2.1 is a wash and the constants must be hoisted as splats
  instead.

---

## Ranked summary

| # | lever | FINDINGS basis | size | cost | confidence |
|---|---|---|---|---|---|
| 1 | Stagger the six scene vertex arrays by 64 B | §3.1/§3.2 direct-mapped 4 KB L0; §3.4 pseudo-LRU; §4.3 misalign free | 0.1–0.5 ms? | ~10 lines | med — measure §6.1 first |
| 2 | Turn on the cross-model array prefetch (`=2`), then trim its fan-out to ≤4 | §4.4 (5.7×/2.2×), §3.7 (4–5 outstanding) | 0.1–1.0 ms? | env var, then a split | med |
| 3 | Plan 1a: bounds cylinder into the scene element | §3.1 dependent pointer chase; §3.6 no prefetcher help | 0.4–0.5 ms (plan's own) | as planned | high |
| 4 | Projection constants as by-scalar `vmul/vmla/vmls` | §1.1 one NEON pipe; 10 of 59 slots measured | −17% of the vector loop | rewrite of one block | high, pending §7 |
| 5 | Plan 1b step 1 only: delete the projection tail (last block at `n-4`) | §5.1 divide is ~1 cyc/dividend bit | 0.3–0.4 ms (plan's own) | as planned | high |
| 6 | Retire settled A/B control arms; weigh new specialisations by code size | §3.9 4 KB / 16 KB fetch cliffs; 16 KB sort function measured | unknown, possibly large | deletions | med — measure §6.3 |
| 7 | **Drop** plan 1b step 2 (`armv7ve` idiv build) for the projection | §5.1: hw `sdiv` on a 20-bit dividend is still ~20–32 cyc | saves the work | delete a plan item | high |
| 8 | A/B `TORIDRAW_FLAT_FILL_V1`; if flat, take the simpler fill | §4.3 misalign free; §3.5 18.5 GB/s store ceiling | small, off hot path | existing toggle | high |
