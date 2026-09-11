AS# Staged / batched software raster plan

Target: the win32 (i686, Pentium 4, Windows XP) soft3d lane. Baseline profile
(Very Sleepy, docs/winxp_profiles/):

```
15.2%  toridraw_textri_opaque_lerp8_v3_asm
11.2%  toridraw_gouraud_tri_opaque_s4_asm
 7.7%  ToriDraw_ComputeProjectedFaceOrderSmall
 2.4%  ToriDraw_RasterPainter (dispatch C)
```

## The thesis

Every drawn face today is touched three times, and each touch re-gathers the
same data through the index arrays:

1. `bucket_sort_by_average_depth_small` (toridraw_render.u.c) gathers a/b/c,
   reads vx/vy (winding) and vz (depth), then keeps only a bucket id. The
   winding cross product and the `clip_candidate` predicate are thrown away.
2. `ToriDraw_RasterModelFaceKernel` (toridraw_raster.u.c) re-gathers a/b/c,
   classifies the face, and `toridraw_raster_face_is_near_clipped` re-reads
   vx[a/b/c] to re-derive exactly the predicate step 1 discarded. It fills the
   ~15-field `prepared` struct and makes an indirect call.
3. The kernel shim gathers vx/vy/colors a third time to push 13 (gouraud) or
   25 (texture) cdecl args; the asm prologue then recomputes `sarea`, which is
   step 1's winding up to a sign flip.

The fix is a staged pipeline, per model, chunked so scratch stays in L1:

```
sort/cull ──▶ stage (gather once, classify, compact, tag)
                 │  SoA rows: x,y ×3, shades ×3, kind, flags [, texture]
                 ▼
             preamble (shared per-triangle setup, one tight loop)
                 ▼
             kernel walk (batch asm entry: screen constants loaded once
                          per chunk, no per-face cdecl marshalling)
```

Draw order is preserved end-to-end — every pass is an order-preserving stream
transform — so output is bit-identical and the existing parity harnesses pin
it.

What this is NOT: a SIMD play on the P4. The preamble's expensive ops are
32×32→low32 multiplies (no `pmulld` before SSE4.1; the `pmuludq` packing
measured 0.72x vs `imul` on the box — see gouraud_tri_i686.S) and exact
`idivl` edge slopes (divps is not exact; documented in the same file). The win
here is structural: marshalling deleted, work deduped, and the P4 trace cache
no longer thrashing between branchy dispatcher C and the kernel on every face.
SIMD preamble lanes are an optional later stage for x64 only.

## Stage 0 — bound the prize (one evening on the box; GO/NO-GO gate)

Cheap falsifier before any surgery. Two runtime-env ablation arms in the C
dispatcher (NOT the compile-time census machinery — probe CFLAGS withdraw the
asm kernels, and these arms must keep them):

- `TORIRS_ABL_NOKERNEL=1`: `ToriDraw_RasterKernelSDDispatch` returns right
  before invoking the kernel. Everything up to and including marshalling runs.
- `TORIRS_ABL_NOFACES=1`: `toridraw_raster_draw_faces` skips its loop body.

Follow the existing env-probe pattern (`getenv` once into a static, e.g.
`soft3d_abl_nochrome`). Run all three arms on the pinned bench (osrs239
Lumbridge steady state, docs/winxp_profiles/STEADY_STATE_TARGETS.md), same
binary, recording fps + CPU ms/frame like the damage-clear A/B note in
platform_sdl2_renderer_soft3d.c.

- `full − NOKERNEL` = kernel cost (sanity check vs the profile's 26%).
- `NOKERNEL − NOFACES` = dispatcher + gather + marshal share — the ceiling on
  what Stages 1–4 can recover (minus the kernels' own prologue overhead,
  which Stage 4 also attacks).

Prior: 2.5–5% of frame. **Gate: if the recoverable share measures under
~1.5%, stop here** and spend the effort on the sprite-RLE blit idea instead.

While on the box, also run a `TORIDRAW_SPAN_RATIO` capture — it feeds the
separate face-order decision and costs nothing extra.

## Stage 1 — stage pass in C, no asm changes

Restructure the sorted-SD path of `toridraw_raster_draw_faces`:

- Loop A (stage): walk `scene->tmp_face_order`, run everything
  `ToriDraw_RasterModelFaceKernel` does today up to the dispatch — type
  decode, hidden/alpha skips, texture resolve (keeping the one-entry texture
  cache and the `toridraw_raster_note_texture_miss` side effect, fired
  exactly once per face as today), near-clip tagging — and write surviving
  faces compacted into SoA staging arrays: screen x/y ×3, shades ×3,
  opacity, face_class, flags, and for textured faces the texel
  pointer/size/gate/frame. This is where the third gather disappears: bake
  the vertex data here so nothing downstream touches the index arrays.
- Loop B (dispatch): walk the staged rows in order and call the existing
  kernels through a thin shim that reads the row instead of re-gathering.
  Near-clip-tagged rows route to the existing C clipping path, in order.

Mechanics:

- Chunked: a fixed ~256-row staging buffer on the scene (calloc'd at scene
  create beside the other `sm_` arrays), pipeline in chunks so stage output
  is still L1-resident when Loop B reads it. Do NOT size it max_faces.
- Runtime toggle `TORIDRAW_STAGED=0` keeps the old loop selectable in the
  same binary for A/B on the box. Census/ablate/debug-stats builds pin the
  old loop unconditionally (same pattern as the makefile withdrawing the asm
  kernels under `TORIDRAW_PROBE_CFLAGS`) so every debug counter stays where
  it is.
- The unsorted model-order walk and the RasterZ/z-buffer path are untouched.

Correctness gates (all must pass before measuring):

- `scanline_parity_test`, `gouraud_tri_asm_test`, `textri_asm_test` targets.
- Headless EXIT_BMP screenshot A/B: `TORIDRAW_STAGED=0` vs `=1`, byte-equal.
- Debug assert: staged drawn count == old-path drawn count on the same frame.

Expectation on the box: small win or a wash (one gather layer and the
duplicate near-clip test die; most work has only moved). This stage is the
foundation, not the payoff — do not stop on a flat number here.

## Stage 2 — carry the sort pass's discarded results

`bucket_sort_by_average_depth_small` already writes `sm_face_depth[f]`; have
it also record per accepted face:

- the `clip_candidate` bit (stage pass consumes it; delete the raster-time
  `toridraw_raster_face_is_near_clipped` re-derivation on the sorted path),
- the low 32 bits of the winding cross product (consumed in Stage 4).

Wrap semantics note for the winding carry: `toridraw_winding_2d` is 64-bit;
the kernels' `sarea` is a wrapping 32-bit `imull` chain and their degenerate
test is on the 32-bit value. Store `(int32_t)winding`; the consumer applies
the y-sort permutation's sign flip and tests the 32-bit result, or a
pathological sliver diverges from the C twin.

## Stage 3 — split the C reference kernels into setup + walk

Factor the C twins (start with
`raster_gouraudhsllightness_screen_opaque_bary_branching_s4`, 82.5% of drawn
faces per the census) into:

- `..._setup(row) -> setup_row`: stable y-sort (the `<=` tie-break chain is
  load-bearing — triangles that tie differently stop tiling; transcribe it
  exactly and record the permutation), early rejects, edge deltas, sarea
  (via int_wrap.h), edge-step and colour-gradient divides — arithmetic
  identical to today, including the shared-reciprocal `divsd` pair.
- `..._walk(setup_row)`: the trapezoid segments and span fill, unchanged.

Preamble loop runs setup over a staged chunk; dispatch loop runs walk. The
asm kernels are still the shipping fill at this point — this stage is
validated on the C lane (parity vs old path, all existing tests) and gives
the walk entry point Stage 4's asm mirrors. Measuring the C-vs-C structural
effect on the box is informative but not decisive; the decisive numbers come
with Stage 4.

## Stage 4 — batch asm entry, gouraud first

New entry in gouraud_tri_i686.S (or a sibling .S including the shared body,
the tex_span_body.inc pattern — the fill must stay single-copy for the trace
cache):

```
toridraw_gouraud_batch_opaque_s4_asm(
    pixel_buffer, stride, screen_width, screen_height,
    staged_rows /* SoA base pointers */, count)
```

Step (a) — marshalling kill only: the batch loop wraps the EXISTING prologue
+ walk. Frame layout keeps every slot; what changes is that `A(n)` argument
reads become loads from a staged-row pointer that increments per face, and
the entry-time constants (V_PIX/V_STRIDE/V_SCRW/V_SCRH and derived values)
are loaded into their slots once per chunk instead of once per triangle. The
push/call/ret + register save/restore + 13-arg marshal per face is gone; the
arithmetic is untouched, so bit-exactness is inherited, not re-proven.

Step (b) — only if Stage 0's numbers say prologue share still matters after
(a): consume Stage 3's setup rows and enter at the walk, and take the Stage 2
sarea carry (saving the two sarea `imull`s and the degenerate re-test per
face). Bigger asm restructure; gated on the (a) measurement.

Harness: extend `toridraw_gouraud_tri_asm_test` with a batch mode — build a
randomized staged chunk, run batch-asm vs per-face C reference,
framebuffer-vs-framebuffer, fail on any byte. i686 only, no win64 twin (same
precedent and reasoning as the existing kernels: x64's ABI hides most of the
cost being deleted).

Measure on the box: this is the stage the whole plan is priced on. Compare
against the Stage 0 ceiling.

## Stage 5 — textured batch entry

Same treatment for tex_tri_i686.S. Extra wins specific to the texture path:

- Texel pointer, texture width dispatch (the 64/128 WALK specialization), and
  `ToriDraw_TexturePlanePrepare32`'s invariant inputs hoist per
  homogeneous-texture run of staged rows, not per triangle. The stage pass
  already resolved texture ids, so runs are visible for free.
- The 25-arg marshal was the worst offender; staged rows replace it.

Textured is 15.2% of the profile but only ~17.5% of faces — per-face overhead
share is proportionally larger here.

## Stage 6 — optional, benchmark-gated extensions

- Scatter-bake: have the counting sort's scatter pass write staged rows
  directly into depth-order position (flip the prefix sum descending so
  consumption is forward-sequential), deleting the stage pass's gather for
  the no-priority path. Only worth it if Stage 1's stage loop shows up in a
  profile.
- SIMD preamble on the x64/modern lanes only (pmulld exists there; keep the
  per-ISA variant pattern the span kernels already use). Expect the P4 to
  keep the scalar preamble permanently.

## XP box measurement recipe (every measured stage)

1. `make -C src winxp OPT=1` (or build_winxp.ps1) → src/torirs.exe; copy to
   the box with the cache.
2. Pinned bench: osrs239 Lumbridge steady state per
   docs/winxp_profiles/STEADY_STATE_TARGETS.md.
3. Very Sleepy capture + the perf CSV; compare with
   docs/winxp_profiles/compare_hotspots.py / compare_windows.py against
   baseline-winxp-soft3d-torirs-perf.csv.
4. A/B inside one binary via env toggles wherever possible
   (TORIRS_DAMAGE_CLEAR_PLAIN precedent); record fps + CPU ms/frame per arm.
5. One unresolved item from the profile to settle during Stage 0's session:
   identify the 13% ntdll single-syscall entry (NtGdiBitBlt vs
   NtDelayExecution vs NtQueryPerformanceCounter — if the capture ran with
   perf scopes enabled, the whole baseline is polluted and needs a re-take
   with `g_torirs_perf_enabled` off).

## Standing risks

- Bit-exactness is the contract: scanline_parity_test, both asm tests, and
  EXIT_BMP A/B gate every stage. Nothing lands on a "looks the same".
- Order preservation is by construction (stream transforms only); assert
  counts in debug builds.
- Census/ablation/debug builds keep the old per-face loop so every counter
  and stat stays in the C where it lives today.
- Side effects (`toridraw_raster_note_texture_miss`, debug histograms) fire
  at stage time, once per face, exactly as before.
- All preamble C uses int_wrap.h for the wrapping adds/multiplies; the
  reference client depends on the overflow.
- Work on a branch off v3; no `git stash` in this repo.
