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
   load-port floor (48 loads per 8 faces ≈ 6 cycles/face) and is ~15–20 % of the sort.
   Sections 3–4.
3. **They name specific, cheap, bit-exact edits** with a cycle number attached. The
   ranked list:

| # | change | kernel | est. gain | conf. | grounded in |
|---|---|---|---|---|---|
| 1 | Do the textured block-end divide (`RECIP`/`UQUOT`/`WRAPQ`) as one NEON vector instead of five ARM↔VFP crossings with VFP/NEON register mixing and two serial scalar chains | tex asm | −25..35 cycles per 8-px block (~20 %) | high | §1.3 |
| 2 | `ldm`/`stm` the textured row advance and row head (7 load-add-store triples → 4 `ldm` + 3 `stm`) | tex asm | −12..15 cycles/row | high | §4.1 |
| 3 | Keep `shift` in r7 (fold `have_cur` into control flow) — removes 3 frame reloads from the head of every block's dependency chain | tex asm | −6..9 cycles/block | high | §3.1, §1.3 |
| 4 | Stagger the six scene SoA arrays so they do not share L0/L1 sets (they are power-of-two ×4 bytes each, so jemalloc almost certainly returns them congruent mod 4 KB) | scene alloc | +3 cycles per conflicting load removed; ~30–50 µs/frame | med (verify alignment first) | §3.1–3.2 |
| 5 | `vmlaq_s32`/`vmlsq_s32` for the six rotation mul+add pairs if clang did not fuse them | projection | −6 NEON ops of ~45 per block; shortens the chain by ~9 cycles | med (check disasm; measure `vmla.i32` on Krait) | §1.1 |
| 6 | Sort prefetch distance: `+32` faces is exactly one line ahead; move to 2–4 lines and issue once per line, not per block | sort | L2-resident: 28.5 → 25.2 cyc/line; DRAM: 168 → 45–66 | high | §4.4 |
| 7 | Drop the two `mov`s from the flat/gouraud row loop's critical path (`mov` is 2 cycles and not eliminated) | flat/gouraud asm | −2..4 cycles/row | high | §1.4, §5 |
| 8 | Make the flat opaque fill's 4..7 / ≥8 split branchless (clamp the pair-loop stop) | flat asm | −1 mispredict (13 cycles) per width-threshold crossing per triangle | med | §2.1 |
| 9 | Prefetch the next textured block's first texel and the next row's framebuffer line | tex/alpha asm | hides one L2 miss (32) per block; one DRAM miss (355) per RMW row | med | §4.4, §3.1 |
| 10 | Two tiles per projection call (two independent 4-vertex chains in one window) | projection shell | halves the ~500-cycle shell per tile and overlaps two 65-cycle chains | med, structural | §1.5, §1.2 |
| 11 | Replace the scalar tail's `__aeabi_idiv` with an exact reciprocal-plus-correction | projection | ~35 µs/frame | high, small | §5.1 |

Items 1–3 and 7–9 are on the soft3d lane only. Items 4–6, 10–11 touch the client frame.

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

**P2 — Two tiles per call** (`est. −0.1..0.2 ms`, structural). §1.5 and §1.2: two
independent 65-cycle chains in one ~80-instruction window run in ~75 cycles, not 130, and
one shell serves two models. 57 % of projected models are 4-vertex tiles sharing one
prepared camera; the scene loop can hand the tile4 slot a pair (two positions, two yaw
rows, two output offsets). Register budget for two blocks: ~12 live Q each at peak is over
16 — so interleave at the *stage* level (both rotations, then both zdivs), which keeps peak
live vectors near 14 with point-of-use constants. Bit-identical by construction.

**P3 — `vmla`/`vmls` for the six rotation pairs** (`est. −6 NEON ops/block, −9 cycles
of chain`, check first). Each `(x*c + z*s) >> 16` is `vmul, vmul, vadd, vshr`; `vmul,
vmla, vshr` is bit-identical (wrapping int32). Clang fuses `vmulq_s32`+`vaddq_s32` into
`vmla.i32` only when the subtarget says the VMLA accumulator forwarding is not hazardous
(it is disabled for Cortex-A8/A9; unknown for the `-march=armv7-a` generic target). Read
the `.so` disassembly for `vmla.i32`; if absent, write `vmlaq_s32` explicitly. Then
measure `vmla.i32 q` latency/throughput and the `vmul → vmla` accumulator forward on
Krait (§8) — if the accumulator path is late-forwarded like ARM `mla` (2 cycles, §5), this
is a clean 12 % cut of the block's NEON ops.

**P4 — Exact tail without `__aeabi_idiv`** (`est. −35 µs/frame`). 264 models a frame
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

**S5 — Things the findings say to leave alone.** The compaction pass (branch-free, 1
load + 1 store per key — at the port limit already). The sentinel-store design (§2.3). The
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

## 7. Phase 3 (second core) — what the findings say before it starts

`FRAME_BUDGET_PLAN.md` promotes the second Krait core to a planned step. Three findings bear
on it directly:

1. **Barriers cost 50–80 cycles plus ~3 per pending store; `ldrex/strex` ~60 uncontended
   (§4.5).** The world→GL handoff must be one release/acquire pair per frame per direction.
   A per-model or per-run publish (a `dmb` per sort result, say) would cost ~1,300 × 80 ≈
   0.06 ms — small, but a per-face one would not be. No fine-grained locks anywhere in the
   pipeline.
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
| `vmla.i32 q` latency, throughput, and `vmul → vmla` accumulator forward | P3 |
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

## 10. Suggested order

1. **P1** (PMU decomposition of the projection shell) and **S2's alignment check** — both
   are measurements, half a day, and they decide the shape of the next month of work.
2. **S1, S3, P3-check, P4** — small, bit-exact, each A/B-able on the bench in an hour.
3. **R1, R2, R3, R6, R7** on the soft lane — the tex block-end rewrite is the one that
   moves a whole door by a double-digit percentage; the rest are single-digit each.
4. Add the §8 kernels to arch_fuzz; **R4** and **S4** wait on them.
5. **P2** (tile pairs) once P1 has shown how much of the 500 cycles is shell that pairing
   would amortise.
