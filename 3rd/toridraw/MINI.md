# ToriDraw for small clients

Drawing a handful of models into a 16-bit buffer, on a target with a
quarter-megabyte of RAM and often no heap worth the name.

Everything here is additive: the world client's API is untouched, and the two
paths share every kernel. What is new is a way to say what you need, be told
what it costs, and hand over the memory.

```c
#include "toridraw.h"          /* toridraw_mini.h comes with it */

static _Alignas(TORIDRAW_ARENA_ALIGN) uint8_t g_arena[32 * 1024];
static toripixel_t g_framebuffer[64 * 64];

ToriDraw_Init();

struct ToriDraw_MiniLimits limits;
ToriDraw_MiniLimitsForModel(model, &limits);
assert(ToriDraw_MiniViewBytes(&limits) <= sizeof(g_arena));

struct ToriDraw_MiniView* view =
    ToriDraw_MiniViewInit(g_arena, sizeof(g_arena), &limits);

struct ToriDraw_MiniTarget target = { g_framebuffer, 64, 64, 64 };
struct ToriDraw_MiniPose pose = TORIDRAW_MINI_POSE_DEFAULT;
pose.yaw = 512;

ToriDraw_MiniClear(&target, 0);
ToriDraw_MiniDrawModel(view, model, &target, &pose);
```

That is the whole interface. No malloc runs anywhere in it —
`toridraw_mini_test.c` counts the calls through `--wrap` and holds the draw
path to zero.

## What it costs

Measured with `gcc -O1`, RGB565, x86-64. "Library RAM" is the `.bss` of
`toridraw_unity.o` — the process-wide lookup tables, present whether or not a
model is ever drawn.

| build | library RAM |
|---|---|
| as it was | 918 KB |
| vector reciprocal tables made opt-in | 278 KB |
| texture-animation scratch handed to the caller | 214 KB |
| `-DTORIDRAW_TABLES_PRECOMPUTED` | **21.9 KB** |

and per client, from the arena:

| what | arena bytes |
|---|---|
| a 4-face fixture | 4,176 |
| 256 vertices, 512 faces, no textures | 30,568 |
| the same with a texture map | 46,960 |
| 1,024 vertices, 2,048 faces | 123,464 |

For comparison, the smallest scene `ToriDraw_SceneNew` can build — `LOW_2K`
plus `SMALL` plus `LAZY_TEXTURES` — is 388 KB across thirty allocations, and it
is that size whether the model has four faces or four thousand.

### Where the savings came from

Four things, in the order they matter:

1. **`g_reciprocal16_simd` and `g_reciprocal_norm30`: 640 KB, unreferenced.**
   They sat behind `TORIDRAW_DISABLE_SIMD_TABLES`, which no build lane in this
   tree ever set, so every build carried them and `ToriDraw_Init` spent 288K
   divisions filling them. The kernels that were to read them exist only as
   commented-out lines. They are now opt-in behind
   `TORIDRAW_SIMD_RECIPROCAL_TABLES`; the old macro is a hard error rather than
   a silent no-op.
2. **The tables that ARE used: 193 KB, and immutable after init.** The HSL16
   palette is 128 KB of that at 16bpp. `TORIDRAW_TABLES_PRECOMPUTED` turns them
   into `const` arrays in a generated translation unit, which a linker script
   puts in flash. See below.
3. **`ToriDraw_TextureMapAnimate`'s 64 KB rotate buffer**, a function-local
   `static`, reserved by every client that linked ToriDraw whether or not it
   had a single scrolling texture. It is now the caller's argument.
4. **`struct ToriDraw_Scene` was 25,392 bytes, 24,576 of which were
   `sparse_a/b/c[4096]`** — three arrays nothing in the tree reads. The struct
   is now 816 bytes, which is what makes putting it in an arena reasonable.

## Precomputed tables

```
cc -DTORIDRAW_PIXEL_FORMAT=TORIDRAW_PF_RGB565 -I3rd/toridraw \
   3rd/toridraw/tools/toridraw_tables_gen.c -lm -o tables_gen
./tables_gen > toridraw_tables_precomputed.c

cc -DTORIDRAW_PIXEL_FORMAT=TORIDRAW_PF_RGB565 -DTORIDRAW_TABLES_PRECOMPUTED \
   -I3rd/toridraw -c toridraw_unity.c toridraw_tables_precomputed.c
```

Build the generator with the same `-DTORIDRAW_PIXEL_FORMAT` as the target: the
palette's element is `toripixel_t`, so a unit generated for one format is the
wrong width for another. The emitted file carries a `_Static_assert` on the
format, so getting it wrong is a compile error and not a screen of wrong
colours.

The generator `#include`s `graphics/shared_tables.c` and calls its own builders
rather than reimplementing the palette's HSL-to-RGB or the atan table's
rounding. A second copy of that math would be wrong within a month, silently —
a palette that is subtly off does not fail, it just looks slightly wrong on the
target and nowhere else.

`g_projection_model_yaw_table` (16 KB) stays writable in either mode: it is
derived from whichever sine and cosine tables are selected, and
`ToriDraw_SetSinTable` exists so a host may select its own.

## Two more knobs worth knowing

- **`-DTORIDRAW_TEXTURE_ID_CAPACITY=64`** shrinks the texture map from 16 KB of
  pointers to 512 bytes. Registering an id past the capacity is dropped;
  *looking* one up aborts, which is the right way round — a model naming a
  texture the build cannot hold is a configuration error, and the alternative
  is a face that silently does not draw.
- **`-DTORIDRAW_PIXEL_FORMAT=TORIDRAW_PF_RGB565`** (or `..._ARGB1555`) selects a
  two-byte framebuffer word. Every raster family draws on every one of the
  seven formats; see `graphics/pixel_format.h`.

## The arena, without the mini facade

`toridraw_arena.h` is usable on its own, for a client that wants the full API
against caller-owned memory:

```c
struct ToriDraw_SceneLimits limits = {
    .max_vertices = 256, .max_faces = 512, .depth_levels = 400,
};
struct ToriDraw_Scene* scene =
    ToriDraw_SceneArenaInit(buffer, sizeof(buffer), &limits);
```

Such a scene has no asset registry, no element pool, no event queue and no
shared-model store — the world client's machinery, and where its other 50 KB
and five hash maps go. Every render entry point takes a model handle directly,
so none of it is in the way.

`ToriDraw_SceneFree` **aborts** on an arena scene. A stack buffer passed to
`free()` is not a leak, it is a heap corruption several frames later in an
allocation that has nothing to do with this one.

`ToriDraw_SceneEnsureScratch` aborts on one too, rather than growing it: an
arena was sized once, by the caller, from limits that said what it would hold.
The one case that reaches it is driving an arena scene with
`ToriDraw_KernelGetSoftwarePainter` without setting `limits.batched_raster` —
that table's whole-model door reads a stash costing 32 bytes a face, and the
sprite-baker table the mini view takes does not.

### `depth_levels`

The face sort buckets by depth, and a face past the end of the table is
dropped. It must reach twice the model's `min_z_depth_any_rotation`, because
the sort buckets a face at `avg(camera z) + that radius` and the average ranges
over ±it whatever the yaw. `ToriDraw_SceneLimitsForModel` computes it; a table
sized to the radius alone draws a model with its back missing rather than
failing.

## Gotchas the API now catches

- **A model with no bounds cylinder is culled** (`TORIDRAW_CULL_ERROR`) and
  draws nothing, with no other symptom. `ToriDraw_MiniDrawModel` asserts and
  names `ToriDraw_ModelSetBoundsCylinder`.
- **A model that has not been lit draws black**: `face_colors_a/b/c` are what
  the raster interpolates, and they are separate from the flat `face_colors`
  the cache carries. `toridraw_rscache` allocates them and
  `ToriDraw_RSCacheModelLight` fills them.

## Self-contained

`toridraw_unity.c` now compiles and links with `-I3rd/toridraw` and nothing
else, which is what its own header always claimed. Two things were in the way:
`toridraw_font.c` was not in the unity while `toridraw_scene.c`'s font registry
called into it, and `toridraw_sprite.c` reached `3rd/bmp` for a BMP export
nothing in the tree calls. The font is in; the export is behind
`-DTORIDRAW_SPRITE_BMP_EXPORT`.
