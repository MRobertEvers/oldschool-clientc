# The vertex bake was dividing

Round C of the XP work. Round A/B (PR #63) took the frame from 21.5 ms to
18.3 ms on `--d3d9-zbuffer` by removing work that was being repeated —
a layout walk, a pool scan, a command memset. This round is different: the
work here was not repeated, it was *arithmetic that produced a value the
caller already had*.

## 1. Where the time was

The retained-geometry bake — turning a posed model's faces into GPU vertices —
was 2.74 ms of an 18.3 ms frame, spread over four symbols. That is more than
`d3d9_draw_model`, which was the single largest entry at 2.03 ms.

| symbol | share | at 18.3 ms |
|---|---|---|
| `d3d9_bake_pose_vertices` | 4.86% | 0.890 ms |
| `trspk_toridraw_bake_face` | 4.60% | 0.843 ms |
| `trspk_toridraw_face_colors` | 4.26% | 0.780 ms |
| `trspk_toridraw_world_vertex` | 1.22% | 0.223 ms |
| **total** | **14.94%** | **2.736 ms** |

## 2. What it was doing

### The colour round trip

A D3D9 vertex stores colour as a packed `0xAARRGGBB`. `ToriDraw_Hsl16ToRgb` is
a table lookup that returns `0x00RRGGBB`. Between those two facts the bake did:

```
hsl16 -> uint32 RGB -> 4 floats (/255.0f) -> uint32 ARGB (*255 + 0.5)
```

`/ 255.0f` is not folded into a multiply — it cannot be, the reciprocal is not
exact — so it compiles to `DIVSS`. Four per vertex, **twelve per face**. On the
Pentium 4 target `DIVSS` is ~30 cycles and does not pipeline.

Confirmed by disassembly rather than inference: 13 `divss` in the bake path.

The fix is to notice that both ends of that chain are the same packed integer.
A bake now states which colour form it wants — `TRSPK_BAKE_COLOR_ARGB` for
D3D9, `TRSPK_BAKE_COLOR_FLOAT` for the GL lanes and the D3D9 widget clipper —
and the other is never computed.

### UVs computed for faces that have no texture

`uv_pnm_compute` ran for **every** face: two cross products, nine deltas, six
dot products, and two reciprocals. For an untextured face every consumer throws
it away —

* `d3d9_bake_pose_vertices` forces the corners to the tile centre;
* `d3d9_widget_model_face_texture` returns `CONFIG_NONE` on `tex_id < 0`
  without reading `uv`;
* the GL3 fragment shader hits `if (atlas_id < 0) { frag_color = v_color; return; }`
  and never samples.

Untextured faces are the majority of a scene's faces, so this was the second
largest item.

Its two reciprocals were also written `1.0 / (...)` — a **double** literal, so
each promoted the whole expression to double division and converted back.

### Rotation setup resolved per corner

`trspk_toridraw_world_vertex` normalised both angles and read four trig table
entries *on every corner it transformed*. Those values are constant for the
whole model. A face has three corners and a mesh's vertices are shared by about
two faces each, so the setup ran roughly six times for every vertex the model
actually has. It now takes a `TRSPK_WorldPlacement` the caller resolves once.

### Smaller things on the same path

* the atlas tile's row and column were a signed `/` and `%` per corner, on a
  power-of-two grid, for a value that is never negative — now a shift and a
  mask, resolved once per face;
* a 128-byte `memset` per face of a struct that `bake_face` then fully writes;
* the VBO dirty flag set three times per face instead of once per model.

## 3. Measured

### Microbenchmark

`make bench-trspk-bake` drives the real bake functions — never a copy — over a
512-face model, i686 `OPT=1`. It has two switchable axes so the state before
each change is still measurable:

| colour | placement setup | ns/face |
|---|---|---|
| float, via `/255.0f` | per corner | **382.8** |
| float, SIMD reciprocal | per corner | 35.6 |
| float, SIMD reciprocal | hoisted | 34.2 |
| packed ARGB | hoisted | 25.9 |
| + colour kernel family | hoisted | 20.8 |
| + UVs skipped when untextured | hoisted | **17.6** |

The bench is DSE-proof — every face's result is read back — and its timing
scales linearly with iteration count, which is how that was checked.

All colour modes produce identical checksums, so the packed path is bit-for-bit
what the float round trip produced.

The bench machine is not a Pentium 4, so these ratios understate the target:
`DIVSS` and `DIVSD` are far more expensive there than here.

### On the box

Fullscreen 1024x768, `cpu ms/frame`, palindrome-ordered so the box's drift over
a job cannot be read as a difference between arms. Two reps per arm.

Measuring `bcabc475b` — the divide removal and the placement hoist only:

| arm | base | base | opt | opt | mean base | mean opt | delta |
|---|---|---|---|---|---|---|---|
| `--d3d9-zbuffer` | 18.91 | 18.83 | 17.40 | 16.09 | 18.87 | **16.75** | **-11.2%** |
| `--d3d9` | 19.69 | 19.91 | 18.83 | 18.05 | 19.80 | **18.44** | **-6.9%** |

Every baseline rep is worse than every optimised rep inside its own arm, which
is a cleaner separation than the box's ~7.5% bimodality would otherwise allow.

Measuring the whole stack -- everything through `15748a735`, so including the
UV skip, the colour kernel family, the arena index and the swizzle -- with
`--soft3d` added back as a control, since it does not execute the vertex bake
at all and therefore must not move:

| arm | base | base | opt | opt | mean base | mean opt | delta |
|---|---|---|---|---|---|---|---|
| `--d3d9-zbuffer` | 18.90 | 18.61 | 16.61 | 15.38 | 18.76 | **16.00** | **-14.7%** |
| `--d3d9` | 19.92 | 19.93 | 18.50 | 18.24 | 19.92 | **18.37** | **-7.8%** |
| `--soft3d` (control) | 35.24 | 35.07 | 34.72 | 34.65 | 35.16 | 34.68 | -1.4% |

The control lands on parity, which is the result that makes the other two
readable. An earlier pass had soft3d at +4.5%; that run's first arm was a
client that lost its server connection and then rendered forever instead of
exiting, and had to be killed mid-job. Re-run clean, it is flat.

Cumulative against the round A/B baseline in `results.md`, `--d3d9-zbuffer`
has gone 21.5 -> 18.3 -> 16.0 ms.

## 4. What else the survey found

Searched TRSPK for the same shapes. Ranked by what they cost:

| finding | status |
|---|---|
| `uv_pnm_compute` for untextured faces | fixed |
| `trspk_modelarena_find` is a linear scan, called once per model bake — O(N^2) per frame | fixed (bucket index); **latent, never appeared in a profile** |
| `trspk_sprite_argb_to_rgba`, per-pixel byte swizzle over whole sprites | fixed (SSE2, 4 px/pass); upload-time, GL lanes only |
| `trspk_atlas.c` tile UVs, four divides per tile | fixed (one reciprocal per axis) |
| `trspk_color_rgb_to_rgba` / `_argb_to_rgba`, four `DIVSS` each | fixed (kernel family) |

### The sprite blits have no divisions

Checked because it was asked. `ToriDraw2D_BlitSpriteRotatedEx` and
`ToriDraw2D_BlitSpriteRotatedMaskedEx` — the ones the compass and minimap
actually use — are already fixed-point `>>16` throughout.

The one per-pixel `idiv` in the file, `dst_x % stride` and `dst_x / stride` at
`toridraw_sprite.c:659`, is in `ToriDraw2D_BlitSpriteRotated`, which **has no
callers in v3**; only the legacy `v1/` tree calls it. Writing a kernel for it
would be optimising dead code.

## 5. How the kernels are organised

Following the raster and projection families rather than inventing a shape:
one entry point, an ISA lane selected in a family header, a scalar lane that
defines what the others must reproduce, and a `TRSPK_SSE2_DISABLED` escape so
the two can be A/B'd against each other.

```
3rd/trspk/core/trspk_color_simd.h        entry + lane selection
3rd/trspk/core/trspk_color_simd.sse2.h   PUNPCK/CVTDQ2PS/MULPS/SHUFPS
3rd/trspk/core/trspk_color_simd.scalar.h the definition
3rd/trspk/core/trspk_swizzle_simd.h      + .sse2.h / .scalar.h
```

`PUNPCK` rather than `PSHUFB` throughout: `PSHUFB` is SSSE3 and the target is
a Pentium 4.

The two lanes are **not** required to agree bit for bit on colour — a
reciprocal multiply may land an ulp off a divide and nothing downstream can
tell. What is required, and what `test-trspk-color-simd` pins, is that a colour
round trips: unpack a byte, pack it back through `(x * 255 + 0.5)`, get the
same byte. The swizzle *is* held to bit equality, because a byte shuffle has no
tolerance to spend.

## 6. Tests

| target | what it holds |
|---|---|
| `test-trspk-color-simd` | round trip for every byte value and 65,536 distinct-channel colours; channel order stated outright |
| `test-trspk-swizzle-simd` | vector lane matches the scalar rule bit for bit — every byte value, alpha corners, every tail length, and in place |
| `test-trspk-modelarena-index` | indexed find checked against a brute-force scan across growth/rehash, scattered unload, free-list reuse, `unload_element`, and `clear` |
| `bench-trspk-bake` | the number this round is about |

`test-rotate-blit` and the `trspk_leak` arena check pass unchanged.
