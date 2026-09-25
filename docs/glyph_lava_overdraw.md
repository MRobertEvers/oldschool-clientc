# Lava plates over the Ancestral Glyph

*The Inferno's floor drawing on top of the npc standing on it, for a few frames
at a time, while the glyph walks its row.*

## The report

> When the glyph is strafing in the Zuk encounter, there is a short period where
> the lava tiles are rendering ABOVE the glyph.

A tile-shaped slab of lava, in the glyph's own colours, laid across the middle
of a 3x3 boss and then gone again.

## What was actually happening

The painter draws a scene far tile to near tile. Each tile puts down its
**ground pass** — terrain meshes first, then its walls, wall decor, ground
objects and **ground decor** — and only then the scenery standing on it. A
multi-tile element (an npc, a loc) is held back until *every* tile of its
footprint has had that pass, which is what keeps a floor from ever landing on
what is standing on it.

Two facts turn that into the reported defect.

**The Inferno's floor is ground decor, not terrain.** Its lava is shape-22 locs
— floor plates — and those live in a tile's exclusive ground-decor slot, emitted
in the tile's ground pass. (`painter_ground_decor_enabled` in
`src/painters/painters.h` names them.) So "the lava tile" is an element the
ground pass emits, not part of the terrain mesh.

**The seam exception lets a tile put down its terrain and defer the rest.** When
the bucket painter's adjacency gate blocks a tile laterally and the blocking
neighbour holds only nearer scenery, `bucket_gate_blocks` relaxes the gate and
sets `TilePaint::seam_relaxed`. The tile then emits its terrain, reaches
`PAINT_STEP_GROUND`, and **skips `bucket_emit_tile_features`** — walls, decor,
ground objects, ground decor — until the plain reference gate finally passes.
That is deliberate: the waiting loc needed the terrain, not the features.

The readiness test for scenery read only the step:

```c
if( paints[ti].step < PAINT_STEP_GROUND )
    all_base = 0;
```

A relaxed tile is at `PAINT_STEP_GROUND`, so it read as "the ground under this
element is down". It was not: the lava plate was still to come. The element was
emitted, the tile was released some hundreds of commands later, and its
deferred ground decor went down on top.

## The evidence

`TORIRS_WEDGELOG` from a `PAINTERS_DEBUG=1` build, one frame of the Zuk phase.
Tile `(51,56)`:

```
3365  0 51 56  floor       p=796  ...            <- terrain only: relaxed
3442  0 51 55  entity  ent=1610638193 fp=50,55+2x2   <- footprint covers (51,56)
3535  0 51 56  grounddecor p=864 elem=1267      <- the deferred lava plate
```

The entity is drawn at sequence 3442; the lava plate it is standing on is drawn
at 3535. Note the missing `grounddecor` beside the `floor` at 3365 — that is the
deferral, visible directly in the log.

It lasts as long as the exception holds the tile open, which in captures is six
to a dozen consecutive frames: the "short period" of the report.

It is not rare, and it is not only the glyph. Over one run of the Zuk phase
(1,500 painted frames) the check reports 431 violations at the camera the lane
now ships, and something at twelve of the fifteen camera positions swept.
Everything the ground pass puts down can land this way -- terrain, ground decor,
ground objects, far walls and far wall decor are deferred together.

Lumbridge, swept the same way at five cameras, reports nothing in either arm.
The exception needs a tile whose lateral neighbour holds only nearer scenery,
which the Inferno's arena produces constantly and an ordinary town does not.

## The fix

`src/painters/painters_bucket.u.c` — a relaxed tile is not ready for anything
standing on it:

```c
if( paints[ti].step < PAINT_STEP_GROUND ||
    (paints[ti].seam_relaxed & relaxed_not_ready) )
    all_base = 0;
```

The blast radius is exactly the relaxed tiles. Every other tile answers the test
as before, and `painter_paint_world3d` — which has no seam exception — is
unaffected, so the two painters agree again. 431 → 0, 296 → 0, 220 → 0 at the
three worst cameras; `test-painters-*`, `test-world`, `test-world-builder` and
`test-wev` pass, and the painter stall census is 0 in both arms.

`TORIRS_PAINTER_RELAXED_READY=1` restores the old behaviour. It exists so the
defect can be reproduced from a shipped binary: a draw-order defect can only be
demonstrated by an A/B of two paints of the same scene, and this checkout must
never be mutated to produce one (CLAUDE.md).

## What this is NOT

**A span-versus-model mismatch.** An entity registers over the tile span its
*declared size* claims — `(size - 1) * 64 + 60` fine units of padding around its
draw position, reference `World.addDynamic` — and a model is under no obligation
to fit in it. The glyph's model reaches 211 fine units on x against a 188-unit
pad, so roughly a fifth of a tile of rock does stand on a tile the gate never
waited for. That is real, it is the reference's own behaviour, and it is *not*
this defect: it is worth a sliver of overdraw at a tile edge, not a slab across
the middle.

Widening the span to cover the model was tried and reverted. It fixes the sliver
and breaks something worse: the span is also what orders an entity against other
entities, so a glyph grown to 5x5 starts claiming tiles nearer the camera than
the projectile flying at it, and Zuk's fireball disappears behind the thing it
is hitting. The span is the entity's extent for ordering; it cannot be quietly
inflated for the benefit of the ground gate.

`TORIRS_MOVER_FOOTPRINT_DEBUG=1` prints both numbers per mover per cycle —
the declared span, the cylinder radius (the *corner* diagonal, which overstates
a square model by 41% and is the wrong number to reason from) and the
axis-aligned reach — so the next report of this shape can be told apart from
this one in one run. `tools/zuk_glyph/run.sh spans` reduces it to one row per
element. Measured over a whole Zuk phase, every overhang is 1 to 52 fine units,
a fifth of a tile at worst:

| element | pad | reach x | reach z | overhang |
|---|---|---|---|---|
| local player | 60 | 49 | 79 | 19 |
| Ancestral Glyph | 188 | 211 | 184 | 23 |
| an add (size 3) | 188 | 211 | 221 | 33 |
| TzKal-Zuk | 444 | 346 | 400 | 0 |

**One caveat on reading that table for yourself.** An element's bounds are the
POSED model's, refreshed when a pose is applied — so on the cycle a model is
first assigned, before its first pose lands, they still describe the BIND pose.
TzKal-Zuk reads 674 on two such cycles out of 3,525 and the glyph reads 494 on
one out of 3,701. Taken as maxima, both look like models two tiles too big for
their span. They are not; `summarise_spans.py` counts how many cycles exceeded
a tile so the swap can be told from a real overhang.

## Reproducing it

`tools/zuk_glyph/` — see its README. In short:

```sh
tools/zuk_glyph/run.sh prove     # fails: 431 violations with the fix off
tools/zuk_glyph/run.sh check     # passes: 0 violations with it on
```

The check reads the wedge log rather than pixels, and a tile there is
`(plane, x, z)`. Reading it as `(x, z)` makes every upper-storey floor a
violation against the loc on the ground floor beneath it — 30,000 of them in a
thousand Lumbridge frames, every one a correct draw.
