# QBD face render priorities — before and after

Evidence for [`RS2012_BACKPORT.md` §1](../../RS2012_BACKPORT.md). All images are
the Queen Black Dragon's default form (`rs2012_model_70260` + `_69766`, the
npc's model1 and model2 merged as the client merges them), rendered by
`rs2012_model_view` through the real `RenderModel1Project` / `2SortFaces` /
`3Raster` path at three yaws inside the client's own camera pitch clamp.

Regenerate everything below with:

```sh
tools/rs2012_qbd_prio.sh          # writes run/, never the lane
```

## The pictures

| | |
|---|---|
| [00_head_before_after.png](images/00_head_before_after.png) | the pair stacked, if you only look at one |
| [01_head_before.png](images/01_head_before.png) | lane as shipped: RS727 priorities honoured |
| [02_head_after.png](images/02_head_after.png) | authored bands |
| [03_head_zbuffer_reference.png](images/03_head_zbuffer_reference.png) | what a z-buffer would show, flat shaded |
| [04_error_mask_before.png](images/04_error_mask_before.png) | red = a surface painted in front of one that is nearer |
| [05_error_mask_after.png](images/05_error_mask_after.png) | same views, same scale |
| [06_body_before.png](images/06_body_before.png) / [07_body_after.png](images/07_body_after.png) | whole head and neck, four yaws |

## What to look at

**In the renders.** Left view: the pink frill sits behind a dark plate and the
jaw is a single dark mass; after, the frill and the tooth row read. Centre: a
black neck plate cuts across the crest and swallows half the horns; after, the
crest and horns are whole. The after frames and the z-buffer reference agree
almost everywhere — that reference is the target, not something the client can
render.

**In the masks.** The difference is not that there is less red, it is *what
kind*. Before, the red is large contiguous slabs: whole neck plates painting
through the head, tens of units deep. After, what is left is thin filigree
along seams where two surfaces interpenetrate at nearly equal depth. That
residue is the irreducible part — much of it is one feature sorting wrongly
against itself, which no band can reach, because a feature has to live in
exactly one band or its own halves stop interleaving.

The score behind the captions counts pixels where the painter's sort leaves a
surface behind the one a z-buffer would have shown, over 24 camera angles:

| | wrong pixels | mean depth of an error |
|---|---:|---:|
| shipped (RS727 priorities) | 11.84% | 119 units |
| priorities stripped | 4.28% | 9 units |
| authored bands | 4.21% | 12 units |

Most of the win is removing priorities that were never a draw order; the
measured bands are what is left on the table after that.

## Seeing it in the client

The authored models are also in the cache as an npc of their own, so the two
can be compared in the engine rather than only in this folder:

```sh
tools/rs2012_qbd_prio.sh                          # solve -> docs/rs2012_qbd_priorities/run/
python3 tools/rs2012_qbd_register_authored.py     # register, idempotent
make -C src mock230-cache-rs2012                  # re-pack
```

| | symbol | npc id | model1 / model2 |
|---|---|---:|---|
| original | `rs2012_qbd_default` | 25000 | 110000 / 110001 |
| authored | `rs2012_qbd_prioritized_authored` | 25010 | 110660 / 110661 |

`QBD_Prioritized_Authored` is a field-for-field clone of the default form —
same geometry, animations, size, combat level — differing only in the models it
names, and those differ from the originals only in their face render
priorities. Anything that looks different between the two is the sort and
nothing else.

The lane's own records are untouched: this adds files and lines, it replaces
nothing. Spawning it needs a server-side spawn like any other npc; nothing in
the encounter code references it.
