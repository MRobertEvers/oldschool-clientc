# Remove int64 from gouraud barycentric; no `g_reciprocal` for triangle area

## Scope clarification

- **`g_reciprocal15` / deob lines 49–54** apply to **scanline span** colour interpolation (`len = (xB - xA) >> 2`, divide-by-span-length). That remains the deob pattern where it already exists; **do not** repurpose this table for **barycentric** `(numerator << 8) / sarea`.

- **Triangle area division** (`sarea` = edge cross product): **do not** use `g_reciprocal` here. Index scale is wrong (area can exceed table size), and the user explicitly avoids reciprocal tables for this divisor.

## Barycentric HSL steps (`step_x_hsl_ish8` / `step_y_hsl_ish8`)

Replace `int64_t` used for `(numerator << 8) / sarea` with a **non–64-bit-integer** approach, e.g.:

- **`double` once per triangle** (two steps per triangle):  
  `(int)((double)numerator * 256.0 / (double)sarea)`  
  matching truncating division toward zero closely enough for setup math, **or**
- Another **integer-only** formulation that avoids `(numerator << 8)` UB and avoids `int64_t` (only if you prefer no float—document tradeoffs).

No `g_reciprocal15[sarea]` / `g_reciprocal16[sarea]`.

Optional small helper, e.g. `gouraud_barycentric_hsl_step_ish8(int numerator, int sarea)`, implemented with **double** (or chosen integer strategy)—**not** reciprocal table.

## Why the earlier reciprocal idea was dropped for `sarea`

See previous note: deob’s index is **span length in quarter-pixels**; **`abs(sarea)`** is **twice pixel triangle area** and is often **far above 4095**. Reciprocal tables are sized for edge deltas / span lengths, not arbitrary areas—using them for `sarea` was incorrect for this project direction.

## Current state

- `int64_t` in: `gouraud.screen.*.bary.sort.*.u.c` (4 files), `gouraud.screen.opaque.bary.branching.s4.c`.
- Plain `(… << 8) / sarea` in: `gouraud.screen.alpha.bary.branching.s4.c`, `gouraud.screen.alpha.bary.branching.s1.c`.

## Implementation

1. Add a tiny helper (header or local `static inline`) that computes `(numerator << 8) / sarea` **without** `int64_t` and **without** `g_reciprocal` for `sarea`—**double** divide is the default recommendation for clarity and one-time cost.

2. Replace `step_x_hsl_ish8` / `step_y_hsl_ish8` at all seven barycentric sites with two calls.

3. Grep: no `int64`/`uint64`/`long long` under `src/graphics/raster/gouraud/`; build benchmark.

## Todos

- [ ] Implement `gouraud_barycentric_hsl_step_ish8` (or equivalent) using **double** or agreed integer-only method—**no** reciprocal table for `sarea`.
- [ ] Wire all seven barycentric rasterizer files.
- [ ] Grep + build.
