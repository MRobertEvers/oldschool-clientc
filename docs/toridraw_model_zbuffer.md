# Per-model depth testing ("model zbuffer")

ToriDraw orders faces, not pixels. `sort_face_draw_order` buckets a model's
faces by depth and priority and paints them back to front, which is what the
reference client does and what the content was authored against.

That cannot represent a model whose parts interpenetrate. When a wing passes
through a body, each of the two surfaces is in front of the other somewhere, and
no ordering of whole faces produces the right picture — one of them will be
painted over where it should have won. On the imported RS2012 QBD this is about
10% of the pixels the model covers.

`TORIDRAW_MODEL_FLAG_ZBUFFER` routes such a model through a depth-tested raster
family instead, so it resolves **itself** per pixel.

This document is the engine side: what the flag does, what it costs, and how to
measure it. The other half of the decision — whether the model needs it at all,
versus fixing the priorities it inherited from the client it was authored for —
is [Backporting z-buffered models](backporting_zbuffer_models.md). Most imported
models want that one; on the QBD, simply dropping inherited priority bytes took
the error from 11.84% to 4.28% for free. Reach for the depth buffer for the part
that survives, not instead of measuring.

## What it does and does not change

The scene owns **one** z-buffer. A model that opts in **resets** it before
drawing and then tests every pixel against it. That reset is the whole scoping
mechanism:

- one model's depths can never reject another model's pixels, because the next
  model that opts in starts from a cleared buffer;
- models that do not opt in never read it at all.

So layering **between** models is untouched — it is still the scene's painter
order over the element list, and the reference's layering rules still hold. Only
the model's own faces are resolved differently.

Coverage is also untouched. The depth-tested kernels copy each stock family's
span entry and exit rules verbatim, so a face touches exactly the pixels it
touched before. The flag reorders; it does not reshape.

## Turning it on

Two things are required, and either one alone is inert.

```c
/* 1. the scene must carry the buffer */
scene = ToriDraw_SceneNew(
    TORIDRAW_SCENE_FULL | TORIDRAW_SCENE_MODEL_ZBUFFER,
    TORIDRAW_SCRATCH_BUFFER_HIGH_8K);

/* 2. the model must ask for it */
model->flags |= TORIDRAW_MODEL_FLAG_ZBUFFER;
```

The buffer is screen sized, so it cannot be allocated at `SceneNew` time — the
viewport is not known until a model is drawn. The scene flag is permission plus
intent: the first raster of a model that opts in sizes the buffer to that
viewport and regrows it if a later one is larger. To pay the allocation up front
instead, call `ToriDraw_SceneZBufferResize(scene, stride, rows)`; the scene flag
is not required for that, and `ToriDraw_SceneZBufferFree` turns the feature off
again without touching any model.

`ToriDraw_ModelNewMerge` ORs the flag from its parts, so merging an npc's
model1/model2 keeps the opt-in — which matters, because merging is what puts the
interpenetrating parts into one face order in the first place.

Cost is `stride * rows * sizeof(torizdepth_t)`: **2 bytes per pixel** where the
toolchain has a real `_Float16`, 4 otherwise. The per-model reset covers the
model's projected bounding box, not the viewport, so it scales with the model's
size on screen.

## How depth is represented

`graphics/zdepth.h`. The stored value is a **depth key**, not a depth: larger is
nearer, and a cleared buffer holds the farthest key there is.

Under perspective the key is `4096 / z`. The reciprocal is not an optimisation —
it is what makes the key exactly linear in screen space, so a span interpolates
it with one add per pixel and is still perspective-correct. Interpolating `z`
itself is not linear in screen space; it bows away from the true surface between
vertices and mis-resolves the middle of long triangles, which on a model the
size of the QBD is most of them.

Under parallel projection there is no divide, depth is already linear, and the
key is a negated scaled `z` — negated so "larger is nearer" holds in both modes
and the kernels need no knowledge of which they are drawing.

16-bit half gives ~0.05% relative resolution, which at the QBD's viewing
distance is about 1.5 world units. Precision is relative, so the scale factors
exist only to keep the key inside half's exponent range.

## Translucency

Opaque faces test and write. Translucent ones — face alpha below 255, or a
per-texel-alpha material — test but do **not** write, so whatever the depth sort
scheduled to blend after them still composites in order. A colour-keyed texel
that is skipped writes nothing either, because the depth write sits behind the
same test that decides to draw the pixel.

The consequence is that translucent surfaces do not occlude each other, and
their result still depends on draw order. That is deliberate and is the expected
residual in the order check below.

## Limitations

- 32-bit pixel targets only. Under `TORIDRAW_PIXEL16` the family is not compiled
  and the model flag is inert.
- The affine texture kernels (`RASTER_FLAG_TEXTURE_AFFINE`, not currently
  enabled) have no depth-tested twin; a z-buffered model takes the perspective
  path.
- The depth-tested texture spans divide per pixel rather than fitting a line
  through eight, because a depth test cannot be amortised over a block. This
  path is opt-in and rare; it is not the one to reach for on scenery.

## Iterating

Two harnesses, answering different questions.

### `make -C src test-zbuffer`

[`toridraw_zbuffer_test.c`](../3rd/toridraw/toridraw_zbuffer_test.c) drives the
real pipeline over hand-built meshes and asserts the contract: interpenetrating
quads resolve to the nearer surface, the result does not depend on face order,
coverage matches the stock kernels, the crossing lands where the geometry says
(which is what grades the reciprocal), translucent faces do not write depth, and
the buffer is reset per model. Both textured dispatches are covered. The file
header carries the mutation table.

### `make -C src rs2012-model-view`

[`rs2012_model_view.c`](../src/engine/proctex/test/rs2012_model_view.c) renders a
real `.ob3` to a contact sheet with no cache, window or game:

```sh
src/build/rs2012_model_view \
    --model .../rs2012_model_70260.ob3 --model .../rs2012_model_69766.ob3 \
    --out painter.bmp --out-zbuffer zbuf.bmp --diff-out diff.bmp \
    --compare --order-check --angles 4 --pitch 0,300
```

- `--zbuffer` draws through the depth-tested kernels.
- `--compare` renders both ways and splits the differences by the sort-error
  mask, so "fixed something the sort had wrong" is separated from "changed
  something that was already right". `--diff-out` paints that split: green
  fixed, red changed-but-was-right, blue still wrong.
- `--order-check` draws the model depth-tested twice, the second time with the
  face order reversed. Opaque output must not depend on order, so this grades
  the kernels without needing a reference picture at all.
- `--score` / `--score-out` / `--reference-out` are the pre-existing sort grader:
  a per-pixel z-buffer reconstruction used only as a yardstick.

Measured on the QBD (models 70260 + 69766 merged, 4 yaws x 2 pitches, textures
stripped):

```
sort score: 7101/68990 pixels behind the true surface (10.293%)
depth test: 5032 of the 7101 wrong pixels changed (70.9%); 2069 left untouched
order check: 957/67114 pixels differ when the face order is reversed (1.426%)
```

Read those together. The order check bounds what the depth test itself can still
be getting wrong, and 1.4% is close to the share of the model's faces that are
translucent (175 of 9,012) — which are order-dependent by design. The 2,069
untouched "wrong" pixels are mostly the same population: the sort grader counts a
translucent face's depth, the raster does not write it.
