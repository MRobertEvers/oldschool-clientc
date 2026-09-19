# toridraw_rscache

The adaptor between [RSCache](../rscache) and [ToriDraw](../toridraw). It
depends on both and nothing depends on it, which is the point: neither library
has to know the other exists.

```c
#include <toridraw_rscache.h>

struct ToriDraw_Model* model = ToriDraw_RSCacheModelFromBlob(blob, blob_size);
ToriDraw_RSCacheModelLight(model, NULL);      /* NULL = the reference rig */

/* ... draw it with toridraw_mini.h, or place it in a scene. */
```

## Building

```
cc -I3rd/toridraw_rscache/include \
   -I3rd/toridraw \
   -I3rd/rscache/include -I3rd/rscache/src \
   -DTORIDRAW_PIXEL_FORMAT=<the target's> \
   -c 3rd/toridraw_rscache/toridraw_rscache_unity.c
```

One translation unit, no build system. `-I3rd/rscache/src` is needed because
`rscache.h` includes its own headers by bare name; that is RSCache's packaging,
not this library's.

`-DTORIDRAW_PIXEL_FORMAT` must match the ToriDraw build it links against.

## Why this library exists at all

RSCache decodes a blob into a struct that mirrors the **wire format**. ToriDraw
draws a struct built for the **raster**. Four things differ, and each one is a
bug if it is not handled — usually a silent one:

1. **Width.** A cache vertex is `int32` and a cache face index is `int32`;
   ToriDraw stores both in 16 bits, because the projection reads six arrays of
   them per model and the halving is what keeps a model's working set in cache.
2. **Packing.** A cache face priority is a byte holding 0–12; ToriDraw packs two
   per byte, low nibble first.
3. **Scale.** Format version 13 and up stores vertices at 4× precision and the
   *reference* decode shifts them down. RSCache deliberately does not — the
   shift drops two bits and its bar is byte-exact round-trip — so this is the
   `scaleDown` site. Skipping it renders every 643-era model four times too
   large, which blanketed whole map squares in giant gravel when it was missed.
4. **Meaning.** `textured_p/m/n` are vertex indices for render type 0 and a raw
   axis triple for types 1–3. Range-checking them all as indices silently strips
   the mapping from every cylinder, cube and sphere face — which does not crash,
   it demotes them to untextured.

## Copy or move

`ToriDraw_RSCacheModelNew` copies and leaves the decoded model intact.
`ToriDraw_RSCacheModelSteal` moves what it can — the seven arrays whose element
type is identical on both sides cross by pointer — and leaves `src` hollow, so
a small client never holds two copies of the same geometry.
`RSCache_ModelFree` on a hollow model is correct and is what releases the shell.

`ToriDraw_RSCacheModelFromBlob` is `Steal` with the decode and the free folded
in, and is the one a small client should reach for: it never holds both
representations at once.

## Lighting is a separate call

Because lighting is not a property of the cache data, it is a property of the
scene, and it has to run against the geometry in the pose it will be drawn in.
A model that is resized, mirrored or merged before it is drawn must be lit
after all of that — the reference client merges a player's dozen body parts and
lights the result once. Folding lighting into the conversion would light every
part separately and then throw the work away.

It is also the expensive half. An icon baker lights once and caches; a client
that recolours a model at runtime relights only then.

Until it runs, `face_colors_a/b/c` are zero and the model draws **black**. They
are allocated by the conversion rather than by the lighting, so a caller who
forgets gets a black model rather than a NULL dereference in the raster.

## Scope

**In:** model geometry, bones, animaya skin, bounds; lighting; single-sprite
textures; framemaps and frames.

**Out:** multi-sprite texture blending and procedural materials. Those need a
sprite loader, a material table and a Perlin generator, none of which belong in
an adaptor — they are the world client's business
(`src/engine/dat2/task_dat2_texture_load.c`). `ToriDraw_RSCacheTextureFromSprite`
handles the single-sprite case, which is every v2 texture definition by
construction and the large majority of v1 ones.
