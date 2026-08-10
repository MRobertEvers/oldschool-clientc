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
| Long stretched / corrupted polygons (“streaks”) on QBD | Soft3d arena, close camera; isolation viewer looks fine | **Still open** |
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
| J | Texture span UV scan signed overflow | **Confirmed, not yet fixed** | UBSan abort: `tex.span.scalar.u.c:493` — `u_scan/v_scan += step_*` with values like `-2111117872 + -301868496`. Stack: transparent perspective blend → QBD textured faces. Strongest current lead for streaks. |

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

6. **Build knobs** (`src/makefile`):
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

## Next steps (suggested)

1. **Instrument** the transparent lerp8_v3 scanline for `|cur_v|`, `|w|`,
   `w==0` skip counts, and shift/add overflow-would-happen — confirm whether
   extreme V quotients correlate with streak frames / platform blanking.
2. **Fix** the confirmed span overflow with defined modular UV scan arithmetic
   (unsigned accumulators matching `u_mask`/`v_mask`), and/or reject or
   saturate blocks when `|B/C|` is pathological near `C≈0`.
3. Apply the same discipline to sibling NEON/SSE/AVX span twins.
4. Re-check platform strip separately if it remains after textured-path fix
   (large flat models ~8509 faces draw with `drawn_textured: 0` in logs —
   may be a different subsystem than QBD streaks).
5. Only after interactive visual confirmation: remove `#region agent log`
   instrumentation; decide whether safe-near stays permanent or is replaced by
   a screen-space guard-band clipper.

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
