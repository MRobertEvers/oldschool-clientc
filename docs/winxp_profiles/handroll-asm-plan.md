# Plan: hand-rolled i686 SSE2 for the hot raster kernels

Status: proposal. Nothing here is implemented.

Question asked: can hand-written 32-bit x86 SSE2 beat GCC's output for (a) the
opaque shade-blended texture span and (b) the fused projection?

Short answer: **for projection, yes by a lot — but almost none of that win
needs assembly.** For the texture span the headroom is real but smaller, and
again mostly reachable from C. The compiler is not losing on instruction
selection; it is faithfully compiling an algorithm that asks for `pmulld` on a
machine that has no `pmulld`, and spilling because the source keeps more than
eight vectors live. Assembly fixes neither of those. Changing the math does.

Everything below is measured against the release lane's own flags
(`gcc 15.2.0 -O3 -march=i686 -mtune=generic -msse2 -mfpmath=sse -DNDEBUG`,
`toolchains/mingw32`), disassembled from standalone probe TUs.

---

## Gate 0 — the profile cannot currently rank raster kernels. Fix that first.

Two facts from `verysleepy_hotspots.csv` / `verysleepy_stacks.folded` that
change what is worth writing:

1. **No texture kernel appears anywhere in the profile.** Not as a symbol, not
   as a source line, not in a single folded stack — `grep -c` for
   `texshadeblend|tex_span|TriangleTexture` over both stack files returns 0.
   The kernel this plan was asked about has **zero recorded samples**.

2. **`-flto` destroyed the attribution.** `analysis.md` finding 7 already flags
   it: `ToriRS_Soft3D_Execute` is 3.20 s inclusive and 69% of that is charged
   to `project_vertices_array_ortho_fused_clip`, "which LTO has inflated by
   inlining lighting/raster work". Every span kernel is `static inline`. The
   texture spans are almost certainly inside that 2.2 s blob, not absent.

So the first number in this plan is not trustworthy, and neither is its
absence. Before any kernel is rewritten:

- Rebuild the XP profiling image with `-fno-lto` (keep `-O3`), or add
  `__attribute__((noinline,noclone))` to the ~8 span/triangle entry points, and
  re-capture. One 60 s Very Sleepy run.
- Also capture a **render-bound** scene. The existing capture is 25%
  `KiFastSystemCallRet` and 13% `RtlReAllocateHeap`; Soft3D is 5.3% of
  wall-clock because the run is dominated by JS5 cache fill, Vorbis decode and
  CS2. Ranking raster kernels off it is ranking noise. Stand still in a
  texture-heavy spot with the cache warm.

Deliverable: a per-kernel table with texture / gouraud / flat / lighting /
memset separated. **If the texture span really is near-zero in a warm
texture-heavy scene, item 2 below drops out of the plan entirely.**

### What the profile *can* support today

Cleanly attributed, exclusive seconds over the 59.97 s capture:

| symbol | excl s | % |
|---|---:|---:|
| `raster_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered` | 0.791 | 1.32 |
| `project_vertices_array_ortho_fused_clip` (LTO-inflated) | 0.793 | 1.32 |
| `lighting_flat_face_normal` | 0.806 | 1.34 |
| `raster_gouraudhsllightness_screen_alpha_bary_branching_s4_ordered` | 0.457 | 0.76 |
| `ToriDraw_ApplyLighting` | 0.490 | 0.82 |
| `ToriDraw2D_BlitArgb` | 0.242 | 0.40 |
| any texture span | **0.000** | **0.00** |

Note lighting (`flat_face_normal` + `ApplyLighting` +
`ModelCalculateVertexNormals`, 0.73 s inclusive) is roughly the size of *all*
raster combined, and `memset` of the surface clears is 12.5% of Soft3D on its
own. Neither is in scope here but both outrank the texture span as it currently
measures.

---

## Item 1 — fused projection (highest confidence, biggest win, no asm needed)

`project_vertices_array_fused_noclip`, vector loop, measured:

**152 instructions per 4 vertices = 38 per vertex.** Census of the loop body:

```
28 pmuludq   28 pshufd   24 movdqa   14 punpckldq   8 psrldq   8 psrad
 6 paddd      6 movups     6 mov      4 psubd       3 punpcklwd 3 psraw
 3 movq       3 cvtdq2ps   2 mulps    2 cvttps2dq   1 rcpps     ...
31 operands referencing (%esp)
```

`28 pmuludq + 28 pshufd + 14 punpckldq + 8 psrldq` = **78 of 152 instructions
(51%) are `mullo_epi32_sse2` emulation** — the six-instruction
`pmuludq/pmuludq/pshufd/pshufd/punpckldq` sequence in
`graphics/sse2_41compat.h`, twelve times per iteration. Useful work is ~34
instructions. The 24 `movdqa` and 31 `%esp` operands are spill: the loop wants
twelve broadcast constants (six sin/cos values, each also needing its
`>>4-byte` twin for the emulation) live in eight XMM registers.

Hand-written asm cannot fix this. The emulation is mandatory for `pmulld` on
SSE2, and no register allocator turns twelve live vectors into eight. What
fixes it is not multiplying 32×32.

**Approach A: `pmaddwd` for the model-yaw stage.**
`x_rot = (x*cos + z*sin) >> 16` with `x`, `z` already `int16`
(`vertexint_t`) is precisely what `pmaddwd` computes — one instruction for a
multiply-pair-and-add that currently costs two emulated mullos plus a `paddd`
(13 instructions). The obstacle is that the sin/cos table is 16.16 and reaches
±65536, which does not fit `int16`. Split it exactly:

```
cos = 4*(cos>>2) + (cos&3)        /* cos>>2 in [-16384,16384], fits int16 */
x*cos + z*sin = ((x,z)·(cos>>2, sin>>2) << 2) + ((x,z)·(cos&3, sin&3))
```

Two `pmaddwd` + `pslld` + `paddd` = 4 instructions, **bit-exact** including the
32-bit wrap the current code relies on (`paddd`/`pslld` wrap identically).
Inputs interleave with one `punpcklwd(xv, zv)` straight off the `int16` vertex
arrays — which also deletes the current `psraw $0xf` + `punpcklwd` sign-extend
pair. Range check: |x·(cos>>2)| ≤ 2^29, pair sum ≤ 2^30, no overflow before the
deliberate `<<2` wrap.

Stages 2 and 3 (camera yaw, camera pitch) operate on 32-bit scene coordinates
and cannot use this directly; they need either a 16-bit hi/lo split of the
*coordinate* or to stay on `pmuludq`. Do stage 1 first and re-measure before
deciding — stage 1 alone removes 4 of 12 mullos and 4 broadcast constants from
the live set, which should relieve the spill disproportionately.

**Approach B: cut the live constant set.** Even without `pmaddwd`, hoisting the
`_mm_srli_si128(b,4)` halves of each broadcast out of `mullo_epi32_sse2` (they
are loop-invariant; GCC already tries, and spills) and restructuring so at most
four are live at once should remove most of the 24 `movdqa`.

Expected: **1.6–2.2× on the vector loop.** All from intrinsics.

**Z-divide** is already float-reciprocal in the SSE2 vector path
(`projection_zdiv_sse2_apply_noclip`: `cvtdq2ps`/`rcpps`/`mulps`/`cvttps2dq`).
Per the standing instruction that float reciprocal wins wherever it can be
used, the remaining integer divides to convert are:

- the **scalar tail** of every fused variant (`screen_x / z_final_scene`) —
  12 `idiv` in the probe object, one pair per tail vertex. Use `rcpss`/`divss`
  matching the vector path so tail and body agree bit-for-bit.
- `projection_zdiv_*_tail_*` paths — check each.
- **`gouraudhsllightness_barycentric_hsl_step_ish8`** — see item 3; this is the
  single hottest render source line in the whole profile.

## Item 2 — opaque shade-blended texture span (do only if Gate 0 justifies it)

`raster_linear_opaque_blend_lerp8_v3`, standalone, measured: **~110
instructions per 8 pixels.** Irreducible cost of the same work is ~40 (8 scalar
loads, 6 `punpck` to assemble two vectors, 8 `movd` extracts, 14 for the two
`shade_blend4_sse` groups, 2 stores). Three concrete wastes, all fixable in C:

1. **The index vector is rebuilt from scratch every block.** GCC vectorises the
   `for(i=0..8) idx[i]` loop by computing `{0,1,2,3}·step_u` with *two* emulated
   `mullo_epi32` (visible at `0x49`/`0x65` and `0x9a`/`0xa8`) and reloading the
   `{0,1,2,3}` constant from `.rodata` each call. The caller walks blocks
   sequentially — carry two running `__m128i` u/v accumulators across blocks and
   add a constant `step*8` vector. Zero multiplies, zero constant reloads.

2. **`texture_shift` is a runtime value, so `psrad` needs its count in a
   register** — GCC spills it to `0x8(%esp)` and reloads it as a `movq` twice
   (`0xac`–`0xd6`). The function *asserts* `texture_shift == 7 || == 6`. Split
   into two specialisations with an immediate shift; the caller already knows
   which from `texture_width & 0x80`. Kills a stack round-trip on every block.

3. `_mm_set1_epi16(shade)` is rebuilt per call (`movd` + `punpcklwd` +
   `pshufd`); `shade` is constant for the whole 8-pixel block and changes only
   by a fixed step per block. Hoist it to the block loop.

Expected: **~1.4–1.7×** on the span. The gather itself (8 extracts + 8 loads +
6 assembles) is ~55% of the ideal kernel and SSE2 offers nothing better — that
part is a floor, not a target.

## Item 3 — the actual #1 render source line

`graphics/raster/gouraudhsllightness/gouraudhsllightness_barycentric_steps.h:18`
is **0.096 s exclusive — more than any single line inside any raster inner
loop** (the hottest is 0.086 s). It is:

```c
return (numerator << 8) / sarea;   /* one idiv, twice per triangle */
```

The comment above it says float reciprocal was rejected because
`numerator << 8` overruns a 24-bit mantissa. That reasoning holds for `float`
and not for `double`: 53 bits of mantissa covers it exactly, and with
`-mfpmath=sse` this is `cvtsi2sd` + `mulsd` + `cvttsd2si`, ~15 cycles against
20–40 for i686 `idiv`. Both calls share the same `sarea`, so compute
`1.0/sarea` once per triangle and multiply twice.

This is a four-line change and, on the evidence available, worth more than
either kernel rewrite. Do it first.

## Item 4 — the s4 gouraud span is already tight; leave it

`draw_scanline_gouraudhsllightness_screen_opaque_bary_branching_s4_ordered`
runs one HSL16→RGB table lookup and four `mov` stores per four pixels — about
2 instructions per pixel. Replacing the four stores with `movd`+`pshufd`+
`movdqu` saves one instruction per four pixels and introduces an alignment
case. Not worth it. Its 0.79 s is span *volume*, not span inefficiency.

---

## Where hand-written assembly would actually earn its place

After items 1–3 land and are re-measured, two narrow spots remain where a
compiler genuinely cannot follow:

- **The SSE2 gather in the texture span.** Eight `movd` extracts and six
  assembles have a scheduling order GCC picks poorly (it interleaves the two
  4-groups badly, visible around `0x107`–`0x1c6`). A hand-scheduled `__asm__`
  block could plausibly find 10–15%.
- **Register allocation across the fused projection loop.** With eight XMM and
  seven usable GPRs, a hand allocation that pins the four hottest constants and
  spills deliberately could beat GCC's choices — but only *after* the constant
  set has been shrunk by item 1. Doing it before just hand-writes the same
  overloaded loop.

Both are ~10–20% refinements on top of the 1.5–2× available from C. Neither is
worth starting before the algorithmic work lands, and neither is worth starting
before Gate 0 says which kernel is actually hot.

One flag note, cheaper than any of the above: the profiled build carries
`-fno-omit-frame-pointer`, which costs `%ebp` — one of only seven usable GPRs
on i686. That is correct for a profiling image and should be confirmed absent
from the shipping lane before any of these numbers are used as a release
baseline.

## Order of work

| # | Task | Cost | Confidence |
|---|---|---|---|
| 0 | Re-profile `-fno-lto` (or noinline the spans) on a warm texture-heavy scene | 1 run | — |
| 1 | `double` reciprocal for `barycentric_hsl_step_ish8` | ~1 h | high |
| 2 | Float reciprocal for the fused-projection scalar tails and remaining zdiv `idiv`s | ~2 h | high |
| 3 | `pmaddwd` model-yaw stage in the SSE2 fused projection | ~1 d | high |
| 4 | Shrink the live constant set in stages 2–3; re-measure spill | ~1 d | medium |
| 5 | Texture span: incremental UV, shift specialisation, hoisted shade | ~1 d | medium, gated on 0 |
| 6 | Hand `__asm__` for the gather / projection register allocation | ~2–3 d | low, gated on 3–5 |

Verification for every step: `benchmarks/texture_opaque_ordered`,
`benchmarks/benchmark_project` and `benchmarks/texture_scanline` for the
microbenchmarks; `toridraw_scanline_parity_test` and
`toridraw_texture_span_uv_test` for bit-exactness. Items 1 and 2 change results
by design (integer division to float reciprocal) and need a parity-test policy
decision before they land; items 3 and 5 must be bit-exact and the parity tests
are the gate.
