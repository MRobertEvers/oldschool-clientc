---
name: VA terrain tile seams (raster)
overview: "VA terrain hairline seams: propose scoped raster inclusive-x or boundary-only geometry; optional logging to validate ±1 half-open gap. Camera-dependent seams support projection + scanline theory."
todos:
  - id: confirm-seam-shape
    content: "Optional: log shared-edge screen coords ± scanline x_start/x_end at a seam (VA vs VA=0) to confirm half-open gap."
    status: pending
  - id: implement-va-scanline
    content: "Recommended fix: RASTER_FLAG scoped to GROUND_VA + inclusive right pixel in draw_scanline_gouraud_ordered_bary_bs4 (and alpha variant if used for terrain)."
    status: pending
  - id: validate-visual
    content: "Visual pass on level 1 + pan camera; check internal diagonals / textured terrain for double-draw artifacts."
    status: pending
isProject: false
---

# VA terrain: slight gaps between tiles (raster hypothesis)

## User correction (authoritative)

- **All tiles are drawn** — rules out whole-tile frustum/AABB cull as the primary explanation.
- Symptom is a **small, slight gap between some tiles**, absent when `WORLD_BUILD_TERRAIN_VA` is off.
- User suspects **rasterization**, not the earlier AABB / `vertexint_t` overflow theory.
- **Seams move / flicker with camera** (yaw/pitch), not locked as a static world-space crack. That fits **projection-dependent rounding** on shared edges and/or **depth ordering** between adjacent tile draws more than a single fixed quantization bug.

The overflow hypothesis is **deprioritized** unless logging shows truncation or assert fires.

## Why this still points at the VA path, not “different gouraud math”

With `VA=0`, each tile is a small [`decode_tile`](src/osrs/terrain_decode_tile.u.c) model: local XZ in `[0,128]`, [`DashPosition`](src/osrs/world_terrain.u.c) carries tile origin.

With `VA=1`, [`build_scene_terrain_va`](src/osrs/world_terrain.u.c) uses **world-space** vertices in a shared [`DashVertexArray`](src/graphics/dash_model.c), [`DashPosition`](src/osrs/world_terrain.u.c) is `(0,0,0)`, and draw uses **sparse** face indices ([`dash3d_raster`](src/graphics/dash.c) + [`projection_sparse.u.c`](src/graphics/projection_sparse.u.c)).

The **same** [`raster_face_gouraud`](src/graphics/render_gouraud.u.c) / [`raster_gouraud_bary_branching_s4`](src/graphics/gouraud_branching_barycentric.c) runs for both; the difference is **which integers reach the scanline** (projection layout + deduped corners), not a separate “level 1” raster variant.

## Leading raster-side mechanisms (sub-pixel / 1-pixel seams)

### 1. Half-open horizontal coverage `[x_start, x_end)`

[`draw_scanline_gouraud_ordered_bary_bs4`](src/graphics/gouraud_branching_barycentric.c) uses:

- `x_start = x_start_ish16 >> 16`, `x_end = x_end_ish16 >> 16`
- early exit when `x_start >= x_end`
- **`stride = x_end - x_start`** and writes that many pixels starting at `x_start`

So the last filled pixel index is **`x_end - 1`**: coverage is **half-open on the right** (`[x_start, x_end)` in pixel index space).

Two abutting triangles that share a mathematical edge in the plane are only crack-free if their **fixed-point edge endpoints** line up so one triangle’s effective `x_end` matches the other’s `x_start` at the shared column. If either side is off by **one sub-16.16 unit** after projection, you can get a **one-pixel column** that neither scanline claims → a **hairline gap** (often visible on level 1 where slopes and camera pitch make shared edges nearly vertical/horizontal in screen space).

### 2. VA dedupes corners; per-tile duplicates corners

[`corner_grid`](src/osrs/world_terrain.u.c) forces **one** world-space corner for `(gx,gz)` across tiles. Neighbors still raster **different triangles** with different winding / scan setup, but they **must** agree on the **exact** projected coordinates for that shared corner.

Per-tile mode **re-projects the same logical corner twice** (two models, two passes) into **different** `screen_vertices_*` slots; tiny differences can **overlap** seams (hiding cracks) as often as they expose them—user observation is that **VA shows more** gaps, consistent with **eliminating duplicate rounding** so the seam is “tighter” and the half-open rule can bite.

### 3. Vertex order into the scan converter

[`raster_face_gouraud`](src/graphics/render_gouraud.u.c) calls `raster_gouraud` with **`xa, xc, xb`** (note **C before B**). Different triangle corners feed different edges in [`gouraud_branching_barycentric.c`](src/graphics/gouraud_branching_barycentric.c). That is intentional for splitting the triangle, but it means **edge parity** at tile boundaries depends on consistent ordering of inputs across tiles. Any mismatch in which corner is “A” vs “C” between neighbors does not change geometry if coordinates are identical—but **fixed-point edge walking** can still differ slightly if intermediate steps are not symmetric.

### 4. What to log first (concrete)

At one seam `(tile_x, tile_z)` and neighbor `(tile_x+1, tile_z)` on level 1, for the **two triangles** that share the vertical edge at `x = (tile_x+1)*128`:

1. After projection, print **screen** `x,y` (pre-`offset_x`/`offset_y` in [`raster_face_gouraud`](src/graphics/render_gouraud.u.c)) for the two endpoints on that edge from **both** tiles, `VA=1` vs `VA=0`.
2. For the same scanline `y`, print computed `x_start_ish16 >> 16` and `x_end_ish16 >> 16` from both triangles (may require a temporary hook in [`draw_scanline_gouraud_ordered_bary_bs4`](src/graphics/gouraud_branching_barycentric.c) or logging one level higher).

If the gap column is `x = K` and one triangle ends at `K-1` while the other starts at `K+1`, you have a **classic half-open + rounding** seam.

## Mitigation directions (after logging confirms)

- **Raster-only**: adjust seam rule for terrain (e.g. expand right edge by 1 pixel when `x_end > x_start` and drawing opaque gouraud ground), or use a **tie-break** for shared world edges (heavier).
- **Projection**: ensure VA and non-VA paths use **identical** integer pipeline for boundary vertices (already intended; logging validates).
- **Avoid dedupe for corners only** (revert to per-tile positions for boundary vertices only)—geometry cost, last resort.

## Proposed solution (ranked)

### A — Recommended: VA-scoped inclusive horizontal span (1 pixel)

**Idea:** Keep global gouraud math unchanged for characters and per-tile terrain. Only for **`DASHMODEL_TYPE_GROUND_VA`**, widen each scanline by **one pixel on the “right” edge** of the half-open interval so coverage becomes **`[x_start, x_end]`** (inclusive of the last column) instead of **`[x_start, x_end)`**, after existing clamping to the viewport.

**Why it targets the bug:** Hairlines appear when two neighbors’ intervals barely fail to meet (`…, K-1` vs `K+1, …`). Adding one inclusive column on terrain VA triangles tends to **heal inter-tile seams**; coplanar splits **inside** the same tile often share the same gouraud color along the extra pixel, so the cost is usually invisible. Risk: **visible 1px on rare underlay/overlay diagonal splits** — validate visually.

**Where to implement:**

1. Add a new raster context flag (e.g. `RASTER_FLAG_GROUND_VA_SCANLINE_OVERDRAW` or reuse an existing spare bit in [`dash.c`](src/graphics/dash.c) `enum DashModelRasterFlags` / `ctx.flags`) set **only** when `dashmodel__is_ground_va(model)` in [`dash3d_raster_with_face_indices`](src/graphics/dash.c) (same place `RASTER_FLAG_TEXTURE_AFFINE` is already OR’d for ground).
2. In [`draw_scanline_gouraud_ordered_bary_bs4`](src/graphics/gouraud_branching_barycentric.c) (and the **alpha** sibling [`draw_scanline_gouraud_ordered_bary_alpha_bs4`](src/graphics/gouraud_branching_barycentric.c) if textured VA terrain can hit it), if the flag is set and `stride > 0`, use `stride_inclusive = x_end - x_start + 1` (with `x_end` already clamped to `screen_width - 1`), then run the existing 4-wide loop over that length—or special-case `+1` without duplicating the whole loop.

**Guardrails:** Only enable for `GROUND_VA` + opaque (or only when `alpha == 0xFF`) first; expand to alpha later if needed.

### B — Geometry: stop deduping **boundary** corners only

In [`build_scene_terrain_va`](src/osrs/world_terrain.u.c), when `gx == 1 || gz == 1 || gx == scene_size-1 || gz == scene_size-1` (or the inner loop’s equivalent), **do not** reuse `corner_grid` for edges on the **outer ring** of active tiles—emit a fresh vertex so each tile owns boundary positions like `decode_tile`. Interior dedupe stays.

**Pros:** Brings back the same “double projection / slight overlap” behavior as per-tile on **tile–tile** seams only. **Cons:** More vertices, more faces unchanged, slightly more work; must index `corner_grid` carefully so **interior** shared corners still dedupe.

### C — Depth / order (only if A leaves shimmer)

If seams still **flicker** after A, log whether **adjacent VA tile draws** swap order by frame. Mitigation would be a **stable terrain sort key** (e.g. sort by `(tile_z, tile_x)` before depth) for `GROUND_VA` only—narrower and riskier than A; try after A.

### D — Last resort: disable VA default

Ship `WORLD_BUILD_TERRAIN_VA 0` until A or B is proven—performance tradeoff only.

## References (code)

- Sparse VA draw: [`dash3d_raster`](src/graphics/dash.c) → `dash3d_raster_with_face_indices` with [`sparse_a/b/c`](src/graphics/dash.c).
- Scanline coverage: [`draw_scanline_gouraud_ordered_bary_bs4`](src/graphics/gouraud_branching_barycentric.c) lines 32–47.
- Face entry: [`raster_face_gouraud`](src/graphics/render_gouraud.u.c) (vertex order into `raster_gouraud`).
