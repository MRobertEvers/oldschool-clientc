---
name: Fix VA terrain 1px offset
overview: The 1-pixel offset in VA terrain is caused by a sub-pixel precision mismatch between the sparse projection path (used by VA terrain) and the dense NEON projection path (used by locs and non-VA terrain). The sparse path loses 9 bits of precision during FOV scaling, and uses integer division for perspective divide vs NEON float reciprocal. Fixing the sparse FOV scaling to match the dense path should eliminate the misalignment.
todos:
  - id: fix-sparse-fov-16
    content: Fix projection16_sparse_slot_notex_dense_match and projection16_sparse_slot_tex_dense_match (VERTEXINT_BITS==16) in projection_sparse.u.c to use >> 6 instead of >> 15 then << 9
    status: completed
  - id: fix-sparse-fov-32
    content: Fix projection_sparse_slot_notex and projection_sparse_slot_tex (VERTEXINT_BITS!=16) in projection_sparse.u.c with the same >> 6 change
    status: completed
  - id: build-and-test
    content: "Build and visually verify: check level 1+ tile gaps and terrain-loc alignment with VA=1"
    status: completed
  - id: audit-scalar-tails
    content: Audit dense NEON scalar tails in projection16_simd.u.c for the same >> 15 << 9 vs >> 6 inconsistency (secondary)
    status: completed
isProject: false
---

# Fix VA Terrain 1-Pixel Offset

## Root Cause Analysis

There are **two projection pipelines** in this renderer that produce subtly different screen coordinates for the same world-space point:

### Pipeline A: Dense NEON (locs, non-VA terrain)

Used by `DASHMODEL_TYPE_GROUND` / `DASHMODEL_TYPE_FULL` via [`project_vertices_array_noyaw_neon_notex`](src/graphics/projection16_simd.u.c) (lines 628-726).

FOV scaling (NEON fast path, lines 687-695):

```c
int32x4_t xfov = vmulq_s32(x_scene, cot_v);   // x_scene * cot_fov_half_ish15
int32x4_t x_scaled = vshrq_n_s32(xfov, 6);     // >> 6 (preserves all bits)
```

Z-division (float reciprocal, via `ARM_NEON_FLOAT_RECIP_DIV`):

```c
float32x4_t recip = vrecpeq_f32(z_f);  // + 2 Newton-Raphson refinements
// x * recip_q31 >> 30
```

### Pipeline B: Sparse scalar (VA terrain)

Used by `DASHMODEL_TYPE_GROUND_VA` via [`projection16_sparse_slot_notex_dense_match`](src/graphics/projection_sparse.u.c) (lines 54-86).

FOV scaling (scalar, loses 9 bits of precision):

```c
x *= cot_fov_half_ish15;
x >>= 15;                      // truncates 15 bits
screen_vertices_x[idx] = SCALE_UNIT(x);  // << 9  (re-expands, but 9 bits are gone)
```

Z-division (exact integer):

```c
screen_vertices_x[zi] = screen_vertices_x[zi] / z;  // integer truncation
```

### The math difference

- Dense NEON pre-divide value: `(x_scene * cot) >> 6`
- Sparse pre-divide value: `((x_scene * cot) >> 15) << 9`

The sparse result equals the dense result **with the low 9 bits zeroed**. The maximum pre-divide difference is 511 units. After perspective divide by z:

| Depth (z) | Max pixel error |
| --------- | --------------- |
| 200       | 2.5 px          |
| 400       | 1.3 px          |
| 600       | 0.8 px          |

On **level 0**, terrain heights are near zero, so `x_scene` / `y_scene` values (orthographic output) tend to be moderate, keeping the error sub-pixel. On **level 1+**, the elevated height (y ~ -240 per level) contributes larger `y_scene` values after camera pitch rotation, pushing the error over the 1-pixel threshold -- explaining why it is more visible on higher levels.

Additionally, the z-division method itself differs (NEON float reciprocal vs integer division), which can add another +/-1 rounding discrepancy.

## Proposed Fix

### Fix the sparse FOV scaling to match the dense NEON path

In [`projection_sparse.u.c`](src/graphics/projection_sparse.u.c), change the two `projection16_sparse_slot_*_dense_match` functions to use `>> 6` directly instead of `>> 15` then `<< 9`:

**Before** (lines 75-85):

```c
x *= cot_fov_half_ish15;
y *= cot_fov_half_ish15;
x >>= 15;
y >>= 15;

screen_vertices_z[idx] = z;
screen_vertices_x[idx] = SCALE_UNIT(x);
screen_vertices_y[idx] = SCALE_UNIT(y);
```

**After:**

```c
x *= cot_fov_half_ish15;
y *= cot_fov_half_ish15;
x >>= 6;
y >>= 6;

screen_vertices_z[idx] = z;
screen_vertices_x[idx] = x;
screen_vertices_y[idx] = y;
```

Apply the same change to `projection16_sparse_slot_tex_dense_match` (lines 39-51).

**This must also be done for the 32-bit variants** (`VERTEXINT_BITS != 16`): `projection_sparse_slot_notex` (line 344-349) and `projection_sparse_slot_tex` (line 297-302).

### Verify the dense scalar tail matches

The dense NEON functions have a **scalar tail** for the last 1-3 vertices (e.g. lines 698-725 in `projection16_simd.u.c`). These tails currently use the `>> 15` + `SCALE_UNIT` pattern (matching sparse, not NEON). If we fix sparse, we should also audit whether the scalar tails should use `>> 6` for consistency -- though these only affect non-VA terrain and are less critical.

## Secondary: Tile-Tile Seams (Half-Open Rasterization)

If tile-to-tile gaps persist after the above fix (they may, since this is a separate mechanism), the half-open scanline `[x_start, x_end)` rule in [`gouraud_branching_barycentric.c`](src/graphics/gouraud_branching_barycentric.c) can leave 1-pixel gaps between VA tiles whose shared edge aligns vertically on screen. The existing plan in `va_terrain_tile_seams_raster.plan.md` covers this -- a VA-scoped inclusive right pixel would close those seams. That should be treated as a follow-up if gaps remain after fixing the projection precision.

## Files to Change

- **[`src/graphics/projection_sparse.u.c`](src/graphics/projection_sparse.u.c)** -- 4 functions: fix FOV scaling from `>> 15 << 9` to `>> 6`
- **Possibly [`src/graphics/projection16_simd.u.c`](src/graphics/projection16_simd.u.c)** -- scalar tail consistency audit (low priority)
