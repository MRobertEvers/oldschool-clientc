# QBD / ToriDraw streaks and platform blanking — debug log

Running investigation notes from the 2026-08-10 soft3d debug session
(Cursor agent transcript `ef81cb6f-4060-461f-9c36-78bcca267502`,
debug session id `ef81cb`).

This is separate from the arena/content repair log in
[`rs2012_qbd_arena/README.md`](rs2012_qbd_arena/README.md). That doc covers
underlays, NPC wiring, and material porting. This one covers **live soft3d
corruption** of the Queen Black Dragon and nearby geometry.

## Symptoms (still open)

| Symptom | Where seen | Status |
|---|---|---|
| Long stretched / corrupted polygons (“streaks”) on QBD | Soft3d arena, close camera; isolation viewer looks fine | **Fixed** — near-clip vertices were projected at a hardcoded scale 512; see [below](#root-cause-near-clip-vertices-were-projected-at-a-hardcoded-scale) |
| Missing body parts / dark blob at some angles | Soft3d arena | Partially improved after safe-near; not fully cleared |
| Floor tiles vanish at some camera angles | Soft3d arena | Reported; not root-caused |
| Center strip of platform blanks out | Soft3d arena | **Still open** |
| Client crash on world load / interactive run | Soft3d + UBSan builds | Several real bugs found and fixed; latest crash is texture-span UBSan |

Isolation (entity viewer) frames the model from far away. The live arena puts
the camera **inside** the QBD’s animated bounding sphere (~4791 units), which
activates near-plane / large-projection paths the isolated view never hits.

## Scene / capacity context

- Live client uses one scene only:
  `ToriDraw_SceneNew(TORIDRAW_SCENE_DEPTH_16K, TORIDRAW_SCRATCH_BUFFER_HIGH_8K)`
  in `src/app.c` (8192 verts / 16384 faces, 16K depth sort).
- QBD in scene is on the order of ~6223 vertices / ~9012 faces with textured
  faces present (so near-clip is allowed).
- Capacity rejection and face-sort bucket overflow were **ruled out** with
  runtime counters and later assertions.

## Hypotheses and outcomes

| ID | Hypothesis | Result | Evidence |
|---|---|---|---|
| A | Projected screen coords exceed raster 16.16 / area math (`x << 16`, `dx*dy`) | **Confirmed**, mitigated | QBD `screen_x` up to ~92k, `screen_y` to ~−91k; grew as camera closed in. After safe-near: coords bounded (e.g. hundreds–low thousands). |
| B | Near-clip drops faces because `allow_near_clip` false (no textures) | **Rejected** | QBD keeps textured faces; `skip_near_clip: 0`, `allow_near_clip: 1`. |
| C | Face-sort depth / bucket overflow / bad permutation | **Rejected** | Clean sorts; max bucket much less than stride; face-order integrity checks clean. Assertions added for depth/priority/flex capacity. |
| D | Scratch / scene capacity rejection | **Rejected** | Already on HIGH_8K + DEPTH_16K; no capacity rejects on QBD. |
| E | Texture not resident → faces skipped | Real but unrelated | Only tiny 2–6 face models; not QBD. App pending-texture race also touched earlier. |
| F | Near-clip reciprocal table OOB (`g_reciprocal16[4096]`) | Latent, fixed prophylactically | Slope uses 32-bit divide for large depth spans (`toridraw_triangle_clip.u.c`). |
| G | Raised safe-near cuts floor / platform | Inconclusive | `raised_cut` mostly 0 in logged frames; user still sees floor/platform blanking. |
| H | Face-order integrity / inversions | **Rejected** | All clean in NDJSON. |
| I | SIMD projection wrap (`x_scene * cot15` etc.) | **Not supported as sole cause** | Headless differential: 0 mismatches. Streaks **persist** on `TORIDRAW_NO_SIMD=1` scalar build. Note: UBSan does **not** instrument NEON/SSE. |
| α | `alpha_blend` signed overflow | **Confirmed + fixed** | UBSan: `15466510 * 175` in `graphics/alpha.h` via gouraud path. Fixed with unsigned arithmetic. |
| SL | Sharelight adjacency stack overrun | **Confirmed + fixed** | `gather_adjacent_tiles` needed 97 slots for 7×7 footprint; buffer was 96. Capacity now derived from `SHARELIGHT_MAX_ELEMENT_TILES`. |
| J | Texture span UV scan signed overflow | **Confirmed + fixed** | UBSan abort: `tex.span.scalar.u.c:493` — `u_scan/v_scan += step_*` with values like `-2111117872 + -301868496`. Stack: transparent perspective blend → QBD textured faces. The overflow turned out to be one of **three** faults in the same V path — see [Root cause: the span V path](#root-cause-the-span-v-path). |
| J2 | NEON non-v3 spans still `continue` on `w == 0` | **Confirmed + fixed** | `tex.span.neon.u.c` kept the pre-repair block loop the other four ISAs had already fixed: the `continue` skipped the `au/bv/cw` advance **and** `offset += 8`, so one degenerate block leaves every later block writing 8 px left of where it belongs, and a trip on the first block drops the whole span. Live on Apple Silicon; invisible on the `TORIDRAW_NO_SIMD=1` builds used for UBSan. |
| J3 | v3 **opaque** spans never clamped `u` | **Confirmed + fixed** | All five ISAs' `..._opaque_blend_branching_lerp8_v3_ordered` used a raw `(int)(au * inv_w)` and relied on `u_mask`, so u **wrapped** where the reference clamps, and an out-of-range quotient hit the same undefined float→int conversion as V. The transparent twins clamped correctly — a silent divergence between the two. |

## Root cause: near-clip vertices were projected at a hardcoded scale

**Fixed.** `ToriDraw_TriangleLerpPlaneProjecti` (`triangles/toridraw_triangle_clip.u.c`)
ended in `SCALE_UNIT(p) / near_plane_z` — the reference projection frozen at
scale 512. The projection kernels project a vertex they *keep* as
`x * (camera_cot16 >> 1) >> 6`, then `/ z` (`projection16_simd.u.c:101`), i.e.
at the camera's own scale.

The two only agree when the camera projects at exactly 512. `9d4b97a9`
("make the projection scale a real parameter") made the scale real everywhere
else and named four hardcoded 512s it removed; this was a fifth it missed. The
live world camera derives its scale from the viewport height
(`app.c` — `scale = vp_h * zoom / 334`), so it is essentially never 512.

Consequence: on any face crossing the near plane, the vertices the clipper
*creates* land `512/scale` times too far from the screen centre while the
vertices it *keeps* land correctly. The polygon is torn between two scales —
long stretched triangles radiating from the model. It only bites on geometry
close enough to clip, which is why the isolation viewer (framed far away, and
at the default 512) was clean while the arena camera inside the QBD's
~4791-unit sphere was not.

Fix: `camera_cot16` is now a parameter of `ToriDraw_TriangleLerpPlaneProjecti`
/ `...Projectf` and is threaded through all 12 near-clip builders (the 8
textured ones already carried it for their uv basis; flat/gouraud gained it).
144 call sites.

Regression test: `test_clip_vertices_follow_camera_scale` in
`toridraw_near_clip_test.c` renders one clipped triangle at scale 512 and 256
and requires the drawn width to halve. Every other case in that file pins
scale 512 — the one value at which this bug is invisible, which is why nothing
caught it. Negative control (helper reverted to `SCALE_UNIT`): reports
`512=270, 256=204` against an expected ~135.

```sh
make -C src test-near-clip
```

## Root cause: the span V path

The perspective span kernels draw 8 pixels at a time and fit a straight line
between the exact uv at the block's two endpoints. The reference rasterizer
(`docs/raster_scanlines_thedaneeffect.txt`) instead divides **per pixel**, and
clamps `u` to `[7, 16256]` while letting `v` wrap through `v1 & 0x3f80`.

ToriDraw inherited the clamp on u and the wrap on v, but the 8-pixel fit broke
the wrap in three separate ways. All three only bite when `w` goes small —
which is exactly what a texture plane passing near the eye does, and what the
camera sitting inside the QBD's ~4791-unit bounding sphere guarantees.

1. **The float reciprocal loses the bits that matter.** `cur_v = (int)(bv * inv_w)`
   carries ~24 bits. Only the low `log2(texture_width)` bits of the row index
   survive `v_mask`, so once `|bv/w|` passes ~2^21 the ulp exceeds one texel row
   and the sampled row is arbitrary. The fix2 capture's quotients were ~1e7.
2. **The out-of-range float→int conversion is undefined**, and on x86 yields
   `INT_MIN` — a whole block pinned to one wrong row.
3. **`cur_v << texture_shift` and `v_scan += step_v` overflow int.** This is the
   reported UBSan abort. It is the *symptom* that made the other two findable.

And a fourth, structural: once a block's V delta exceeds a texture tile, the
straight line is not an approximation of the hyperbola at all. It sweeps the
texture smoothly where the true mapping jumps — a **smooth ramp instead of
noise, which is what reads as a streak**. Making the arithmetic merely *defined*
would have silenced UBSan and left the streaks on screen.

Measured against an exact per-pixel divide, `3rd/toridraw/toridraw_texture_span_uv_test.c`:

| case | HEAD max row error | after | notes |
|---|---|---|---|
| `benign-floor` / `benign-wall` | 1 | 1 | ordinary lerp8 error; unchanged |
| `w-small-bigv` | **64** (max possible) | 0 | ~400/512 px wrong |
| `w-crosses-zero` (±) | **64** | 0 | ~420/512 px wrong |
| `w-one`, `huge-bv-w-one` | **64** | 0 | |
| UBSan `signed-integer-overflow` | **traps** | clean | |

64 is the maximum distance on a 128-row wrapping axis, i.e. the row was not
merely off — it was uncorrelated with the truth.

> The left-shift-of-negative that `-fsanitize=shift` reports in these kernels is
> **not** part of this bug. It fires on completely benign geometry, GCC/Clang
> define it, and `UBSAN_CHECKS` excludes it deliberately. Chasing it is a
> detour.

## Fixes landed (keep until visual verification)

1. **Safe near plane** — `toridraw_safe_near_plane_z` in `toridraw_render.u.c`.
   Raises effective near plane so projected coords stay within
   `TORIDRAW_PROJECTED_COORD_LIMIT` (8192). Stored as
   `scene->projection_near_plane_z`. Toggle: `TORIDRAW_SAFE_NEAR=0` disables.
   Raster uses this plane, not the camera’s raw near. Prefer this over
   widening hot paths to 64-bit (user constraint).

2. **`ToriDraw_TriangleSlopei`** — reciprocal table only 4096; fall back to
   32-bit divide for larger depth spans.

3. **Sort capacity assertions** — depth / priority / flex bounds;
   `scene->flex_prio_capacity` plumbing.

4. **`alpha_blend`** — unsigned arithmetic in `3rd/toridraw/graphics/alpha.h`.

5. **Sharelight adjacency buffer** — `world_sharelight.u.c` sized from
   `SHARELIGHT_MAX_ELEMENT_TILES` (32); up-front assert; debug log when
   footprint &gt; 6×6.

6. **Perspective span uv** — new
   `3rd/toridraw/graphics/raster/texture/span/tex.span_uv.h` owns the rules:
   - `tex_span_v_quotient` keeps V exact, using the float reciprocal only
     inside the range where it still carries the row bits and falling back to
     an integer divide outside it (cold — the fast path is unchanged).
   - `tex_span_u_quotient` clamps **in float**, so an out-of-range quotient is
     never converted.
   - `tex_span_v_scan_start` folds the row before shifting; `(cur_v << shift) &
     v_mask` and `(cur_v & (width-1)) << shift` are equal, and the second
     cannot overflow.
   - `tex_span_lerp8_v_fits` gates the linear fit on the block moving less than
     a tile; blocks that fail (and blocks with `w_n == 0`, which have no far
     endpoint) go through `tex_span_exact_block`, a per-pixel divide matching
     the reference.

   Applied to all four perspective kernels (`{opaque,transparent}` ×
   `{lerp8, lerp8_v3}`). Those four functions contain **no intrinsics in any
   ISA file** — they are scalar control flow around the peer-declared 8-pixel
   kernels — so they are now byte-identical across `scalar / neon / sse2 /
   sse41 / avx`. That sync is what closed J2 and J3, which were pure drift.

7. **Build knobs** (`src/makefile`):
   - `TORIDRAW_NO_SIMD=1` → scalar kernels (`_nosimd` objdir).
   - `ENABLE_UBSAN=1` → signed-integer-overflow on ToriDraw (`_ubsan` objdir).
   - Lighting’s shift-of-negative is noisy; UBSan scope was narrowed so it
     does not mask real raster overflows.

## Instrumentation still in tree

Behind `TORIDRAW_DEBUG_NDJSON=1` (and related env), `#region agent log` blocks in:

- `3rd/toridraw/toridraw_render.u.c` — project range, safe-near, face sort,
  face-order integrity, SIMD/exact differential on yaw-only path.
- `3rd/toridraw/toridraw_raster.u.c` — per-model draw/skip accounting,
  texture ids, tex-plane shift/reject counters.
- `3rd/toridraw/graphics/projection.u.c` — `g_toridraw_tex_plane_max_shift` /
  `g_toridraw_tex_plane_rejected`.
- `3rd/toridraw/triangles/toridraw_triangle_clip.u.c` — clip reciprocal OOB.

NDJSON path used in this session:
`/Users/matthewevers/Documents/git_repos/3draster/.cursor/debug-ef81cb.log`

Useful env:

```text
TORIDRAW_DEBUG_NDJSON=1
TORIDRAW_DEBUG_RUN=<label>
TORIDRAW_SAFE_NEAR=0          # optional A/B
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
```

## How to reproduce (interactive UBSan / no-SIMD)

Prefer the preserved binary ( `run-live` rebuilds a plain `torirs` ):

```sh
# rebuild when needed
make -C src EMBED_SERVER=1 TORIDRAW_NO_SIMD=1 ENABLE_UBSAN=1 -j"$(sysctl -n hw.ncpu)"
cp -f src/torirs src/torirs_ubsan

env TORIDRAW_DEBUG_NDJSON=1 TORIDRAW_DEBUG_RUN=fixN \
  TORIRS_TRANSPORT=embed MOCK230_REV=osrs239 \
  UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  ./src/torirs_ubsan --manifest manifest_osrs239_rs2012.ini \
  --user qbdrepro --pass test --soft3d \
  2>/tmp/toridraw_ubsan.log
```

Enter the QBD arena, move the camera to the angle that shows streaks / the
platform strip, note whether the client aborts.

Headless 850-frame captures completed cleanly after the alpha fix but **do not**
reach the bad interactive camera angles — they are not sufficient to clear
streaks.

## Latest runtime smoking gun (not fixed)

From interactive `fix2` with `torirs_ubsan`:

```text
tex.span.scalar.u.c:493: signed integer overflow:
  -2111117872 + -301868496
#0 raster_linear_transparent_blend_lerp8_v3
#1 draw_texture_scanline_transparent_blend_branching_lerp8_v3_ordered
#2 raster_texshadeblend_persp_textrans_branching_lerp8_v3_ordered
…
#  ToriDraw_TriangleFaceTextureBlendTransparent → soft3d
```

Relevant mechanics in `tex.span.scalar.u.c`:

- Perspective lerp8_v3 path computes `cur_u`/`cur_v` as `au/w`, `bv/w`
  (float inverse in the v3 ordered path).
- **U is clamped** to `[0, texture_width)`; **V is not**.
- Scan uses `v_scan & v_mask` (wrap intended), but `cur_v << texture_shift`
  and `v_scan += step_v` are signed and can overflow when `|cur_v|` is huge
  (homogeneous `C` small relative to `B` after plane prepare).
- `TexturePlanePrepare32` only guarantees `A/B/C` **terms** fit in int across
  the viewport, not that `|B/C|` stays in texture range.

Sharelight fix was confirmed in the same run (7×7 footprint needed 97, buffer
1222). Projection logs for that run showed `screen_mismatch: 0`,
`fixed16_vertices: 0`, `raised_cut: 0` on sampled large models — so the
remaining streaks are **not** explained by the original 16.16 projection
overflow alone.

## What did not work / rejected directions

- Blaming isolation vs scene on scratch size alone (already HIGH_8K).
- Face-sort “neighbor bucket smash” as the QBD streak cause (assertions +
  counters clean).
- Expecting UBSan alone on a SIMD build to catch projection wrap (it does not
  see NEON/SSE math).
- Treating headless clean frames as proof streaks are gone.
- Widening the whole raster hot path to 64-bit (explicitly avoided per
  product preference; safe-near + special cases preferred).

## Root cause: RS2012 face render priorities

**Fixed.** The QBD was the only large arena model reporting `has_priorities: 1`
(the platform and the 8155/2111-face pieces were all pure depth sort).

Face render priorities are a painter's-algorithm crutch: with no depth buffer
the artist pins a face into a draw band so it lands in front of or behind its
neighbours regardless of depth. The RS727 client these models came from
resolved visibility with a **z-buffer**, so their priority bytes never had to
order anything and do not describe a usable painter order. ToriDraw honoured
them, which overrode the depth sort — the neck sorted inside-out, near faces
behind far ones.

`src/engine/proctex/test/rs2012_strip_priorities.c` (`make -C src
rs2012-strip-priorities`) drops them from the lane's OB3 assets: 504 of 660
models carried priorities (161 per-face, 343 whole-model). The property travels
with the content that has it, so OSRS models keep their meaningful priorities.

Note for anyone extending it: the encoder prefers the *provenance's* recorded
header over anything derived from the model, and rejects a header claiming
per-face priorities (255) when the array is gone. `header_flags[1]` is that
byte and has to be cleared too — otherwise 504 of 660 models fail to encode.

Re-pack afterwards; models are assets:

```sh
make -C src rs2012-strip-priorities && src/build/rs2012_strip_priorities --apply
make -C src mock230-cache-rs2012
```

## The grey QBD is not a missing texture

Do not "fix" this by referencing the HD-only materials. It was tried.

The lane bakes 256 materials, but `valid=0` for **204** of them, and
`rs2012_material_bake` erases the texture on every face naming one — 274,715
lane faces — falling back to the face's flat HSL colour. That looks like the
cause of the untextured grey, and it is not: referencing those 204 instead
renders the arena as **blown-out white shards**, because they are HD-only
programs whose baked 128×128 approximation is not a diffuse map. The source
client agrees — `TextureLoader.isSd` selects them out and falls back to the
face colour, which is exactly what the fallback reproduces.

The materials *are* fully authored and present: 256 sprites (8535–8790), the
`texture_0` archive, and `port/rs2012_qbd_td.materials.tsv`. The QBD samples
them (ids inside the baked 211–466 range) with `skip_tex_miss: 0`.

If the encounter should show material detail, the fix is upstream — making
those materials genuinely SD-usable, or an HD-capable renderer — not disabling
the fallback. `--no-ground-mesh-fallback` exists only to re-run the experiment.

## Tests

`3rd/toridraw/toridraw_texture_span_uv_test.c` drives each perspective span
kernel directly and compares the texel every pixel actually sampled against an
exact per-pixel divide. The texture encodes its own coordinates and the shade is
the identity multiplier (256), so a framebuffer word decodes straight back to
the `(u, v)` the kernel sampled.

```sh
cc -std=c11 -O2 -Wall -Wextra -I3rd/toridraw \
   -o /tmp/span_uv 3rd/toridraw/toridraw_texture_span_uv_test.c -lm && /tmp/span_uv

# scalar kernels (what TORIDRAW_NO_SIMD=1 builds)
cc -std=c11 -O2 -Wall -Wextra -I3rd/toridraw \
   -DNEON_DISABLED=1 -DAVX2_DISABLED=1 -DSSE41_DISABLED=1 -DSSE2_DISABLED=1 \
   -o /tmp/span_uv_scalar 3rd/toridraw/toridraw_texture_span_uv_test.c -lm && /tmp/span_uv_scalar

# with the overflow check the live client uses
cc -std=c11 -O1 -g -I3rd/toridraw \
   -DNEON_DISABLED=1 -DAVX2_DISABLED=1 -DSSE41_DISABLED=1 -DSSE2_DISABLED=1 \
   -fsanitize=signed-integer-overflow -fno-sanitize-recover=all \
   -o /tmp/span_uv_ubsan 3rd/toridraw/toridraw_texture_span_uv_test.c -lm && /tmp/span_uv_ubsan
```

`3rd/toridraw/toridraw_scanline_parity_test.c` still passes on scalar / sse2 /
sse41 / avx2.

## The platform strip: paint order, not the rasteriser

**Fixed** (`src/painters/painters_bucket.u.c`). The one-tile-wide strip of teal
arena floor running up over the level-1 platform is `painter_paint_bucket`
emitting plane-0 terrain out of distance order, not a raster or texture fault.

The chain, measured rather than guessed:

- `TORIRS_PIXOWNER=355,375,150,250` named the owner of the teal pixels:
  `TERRAIN tile=49,58 L0` and `tile=49,60 L0` — plane-0 floor on the camera
  column (`camTile=49,43`), drawn over the platform, which is a plane-0 **loc**,
  not level-1 terrain.
- `TORIRS_WEDGELOG` showed why. Column `x=49` emitted normally from distance 31
  down to 23 (paints 265–458), then **stopped for 282 paints** and resumed only
  at paint 740 — immediately after the east platform loc (`x[50,61] z[48,65]`,
  emitted at paint 729, five tiles from the eye). Its floor at distance 15–22
  then landed on top of that loc.
- The floor is two 12×18 plane-0 locs meeting on column 49/50. A tile with
  `sx == camera_sx` satisfies both `x <= cameraX` and `x >= cameraX`, so it is
  gated on both horizontal neighbours; `(50,z)` belongs to the *other* loc, so
  `(49,z)` carries no `SPAN_FLAG_EAST` and the reference span exception cannot
  fire; and `(50,z)` cannot reach `PAINT_STEP_DONE` until its 216-tile loc is
  released, which happens at that loc's nearest footprint tile.

The fix is the seam exception documented in
[painter_bucket_vs_world3d.md](painter_bucket_vs_world3d.md#the-seam-exception-bucket-only):
a neighbour whose only remaining work is scenery that reaches *closer to the eye*
than the tile being gated has nothing left to protect, because such a loc is
drawn nearer than that tile regardless.

Pinned by `test_seam_between_two_large_locs_keeps_the_sweep` in
`src/painters/test/painters_test_terrain_levels.c` (`make -C src
test-painters-terrain-levels`), which reproduces the topology in 32×32 tiles:
before the fix the plane-0 sweep breaks into 3 monotone runs and emits floor at
distance 19 after the loc; after it, one run and nothing farther than the loc's
own ring. In the arena the same measurement goes from 5 runs to **1** over 768
plane-0 floor emissions, and the strip rect has no terrain owner at all.

The full walkthrough, with figures, is [LARGE_LOCS_PAINTER.md](../LARGE_LOCS_PAINTER.md).

`TORIRS_PAINTER_W3D=1` (added while chasing this) runs `painter_paint_world3d`
in the live client, which is how a traversal fault is told from a geometry one.

## Next steps (suggested)

1. **Visual confirmation in the arena is still outstanding** — everything above
   is measured against the reference mapping in a harness, not seen on screen.
   Enter the QBD arena at the angle that showed streaks and check.
2. ~~Re-check the **platform strip** separately if it remains~~ — done, and it
   was a different subsystem: the painter's traversal, not the raster. See
   "The platform strip" above.
3. Re-check **floor tiles vanishing**: the `w_n == 0` block that used to be
   drawn as a fabricated flat gradient is now drawn per pixel, which is the
   horizon band. If floors changed, this is why.
4. Only after visual confirmation: remove `#region agent log` instrumentation;
   decide whether safe-near stays permanent or is replaced by a screen-space
   guard-band clipper.
5. `tex.span.{neon,sse2,sse41,avx}.u.c` had silently drifted from `scalar` on
   these four functions. Worth a lint that asserts they stay identical, since
   nothing in the build catches an ISA-specific regression on a machine that
   does not run that ISA.

## Key files

| File | Role |
|---|---|
| `3rd/toridraw/toridraw_render.u.c` | Project/sort, safe near, debug NDJSON |
| `3rd/toridraw/toridraw_raster.u.c` | Face dispatch + raster debug |
| `3rd/toridraw/graphics/alpha.h` | Alpha blend overflow fix |
| `3rd/toridraw/graphics/projection.u.c` | `TexturePlanePrepare32` |
| `3rd/toridraw/graphics/raster/texture/span/tex.span.scalar.u.c` | **Current UBSan crash / streak lead** |
| `3rd/toridraw/graphics/raster/texture/texshadeblend.persp.textrans.branching.lerp8_v3.u.c` | Caller of crashing span |
| `3rd/toridraw/triangles/toridraw_triangle_clip.u.c` | Slope reciprocal fallback |
| `src/engine/world_builder/world_sharelight.u.c` | Adjacency buffer fix |
| `src/makefile` | `TORIDRAW_NO_SIMD` / `ENABLE_UBSAN` |
| `manifest_osrs239_rs2012.ini` | QBD repro manifest |
| `saves/qbdrepro.ini` | Repro save |
