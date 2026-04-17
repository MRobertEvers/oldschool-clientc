# Raster directory migration (plan execution)

Tracks phased moves from [Raster variant catalogue](../.cursor/plans/raster_variant_catalogue_2aabb9e0.plan.md) **Proposed file organization**.

## Phase 1 (done)

- [`src/graphics/archive/`](../src/graphics/archive/) — unused / duplicate TUs:
  - `gouraud_branching.c`, `gouraud_s1_branching.c`
  - `texture_opaque_blend_affine.c`, `texture_transparent_blend_affine.c`
- [`src/graphics/reference/`](../src/graphics/reference/) — research / decomp:
  - `gouraud_deob.c`, `gouraud_deob.h`
- [`android/CMakeLists.txt`](../android/CMakeLists.txt) — `gouraud_deob.c` path updated for the Android target.
- Commented includes in [`gouraud.u.c`](../src/graphics/old/gouraud.u.c) / [`gouraud_s1.u.c`](../src/graphics/old/gouraud_s1.u.c) point at `reference/gouraud_deob.h`.

## Phase 2 (done)

- [`src/graphics/old/`](../src/graphics/old/) — former top-level raster routing TUs (`flat*`, `gouraud*`, `texture*`, `render_*`) now live here; [`dash.c`](../src/graphics/dash.c) includes them via `old/render_*.u.c`. Those TUs use `../` for quoted includes of headers and `raster/` shards still rooted at [`src/graphics/`](../src/graphics/) (e.g. `../alpha.h`, `../raster/texture/...`).

## Later phases (not done here)

- `support/`, `projection/`, `2d/`, `raster/{flat,gouraud,texture}/` with per-variant filenames and `#include` chain updates from [`dash.c`](../src/graphics/dash.c) — high touch count; run a full build after each batch.
