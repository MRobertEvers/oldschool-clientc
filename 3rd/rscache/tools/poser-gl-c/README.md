# poser-gl-c

A C port of [fglass/poser-gl](https://github.com/fglass/poser-gl), the OpenGL
animation editor for RuneScape caches.

Load a cache, pick an entity, pick a sequence, watch it play, drag its joints,
and write the result back out — as poser-gl's own `.pgl` interchange file, as a
317-format `.dat` pair, or into a copy of the cache itself.

```sh
make -C 3rd/rscache/tools poser-gl

./poser-gl-c/poser-gl --rev osrs230 cache.osrs230
```

## What changed, and why

Three of poser-gl's dependencies have no counterpart here, so each became a
decision rather than a translation.

**The cache layer is rscache, not a plugin.** poser-gl reaches a cache through a
JAR implementing `ICacheLoader`, one per revision, which the user builds and
drops in a `plugin/` directory. rscache already is that layer — a revision
profile in place of a plugin — so `pg_cache.c` is the adaptor between its structs
and the shapes the editor's model and animation code want. `--rev` names the
profile; the default is `osrs230`. Anything rscache reads as dat2 works; dat1
caches are refused at open, because the frame and framemap codecs the editor
drives are dat2's.

**The UI is immediate mode, not LEGUI.** poser-gl builds a retained component
tree with a flexbox solver, a theme system and NanoVG text. What the editor
actually asks of its UI is narrow — panels, three scrolling lists, numeric
sliders, a search box and a timeline — and every one of those is a rectangle plus
a hit test. `pg_gui.c` is about six hundred lines and does the lot. Two visible
consequences: layout is explicit pixel arithmetic at each call site, and the
toolbar carries text labels where poser-gl has PNG icons, because there is no
image decoder in this tool.

**The font is bitmap, authored in the source.** There is no rasteriser here, and
the caches' own fonts are unreachable before a cache has been chosen. `pg_font.c`
holds a 5x7 ASCII set written as legible art rather than packed hex, so a wrong
pixel shows up in a diff; `--font-specimen OUT.bmp` renders the whole set for
checking by eye.

**The gizmo models are generated, not loaded.** poser-gl ships three `.obj` files
(the rotation ring alone is nine thousand lines). An arrow along +X of unit
length and a unit ring in the XZ plane are simple enough to state exactly, and
generating them keeps this a single binary with no asset path to resolve. Both
dimensions are load-bearing — the pick volumes and the rotation gizmo's circle
projection are expressed in multiples of the gizmo scale and assume a unit model.

## Two deliberate departures from the reference's behaviour

**The projection's y is negated.** Cache models are authored with -y up — feet at
zero, head at about -200 — and nothing between poser-gl's decoder and its screen
puts that right: the entity shader flips only x, and the camera's own up axis
works out pointing at model-down. A faithful port draws every character standing
on its head, which it did until this was added. Flipping the projection rather
than the geometry keeps it a display concern: the frame deltas, the joint pivots,
the pick rays and the gizmo axes all stay in the model's own coordinate system,
and the unprojection inverts this matrix so picking follows the flip for free.
The one knock-on is winding, which is why the entity draw culls back faces where
the reference culls front.

**Vertex normals are grouped once, not rebuilt per frame.** The reference
recomputes smoothing groups inside the per-frame normal pass, comparing *animated*
positions — an O(vertices x faces) scan every frame whose result can change
mid-animation when two coincident vertices separate. Here the grouping is fixed
at the rest pose and only the accumulation runs per frame, which makes the cost
linear in faces and the smoothing stable while an animation plays. Same normals
for every model whose coincident vertices stay coincident, which is all of them
in practice.

Everything else is a translation, including the parts that look like bugs and are
not: the projection matrix's inverted aspect term, the `ORIGIN` fallback that
makes a mis-labelled model stretch rather than sit still, and the rotation
gizmo's one-step-per-frame clamp are all reproduced as written, because the
editor's framing, zoom limits and node scale were tuned against them.

## What it does

- **Entities** — every npc with models, plus the composed default player built
  from identikits (`-1 Player`). Models merge with vertex deduplication by
  position, exactly as the OSRS deob does, which is what makes a shared seam bend
  as one piece.
- **Items** — click one to put it in the entity's hand. A sequence's
  `leftHandItem` / `rightHandItem` equip and unequip with it.
- **Animations** — every sequence, parsed into keyframes of reference nodes. A
  skeleton is inferred by asking which joint's rotation label set contains which
  other's; the root is the joint whose rotation moves the most labels.
- **Editing** — drag a joint's gizmo or type into the X/Y/Z sliders, change a
  keyframe's length, and add / copy / paste / interpolate / delete keyframes. All
  of it goes through one undoable command path, so a drag and a typed value are
  indistinguishable in the history.
- **Copy-on-write** — the first edit to a cache animation copies it under a new
  id above the cache's highest sequence. Nothing overwrites a cache animation in
  place.
- **Export** — `.pgl`, byte compatible with poser-gl's own, so a file written by
  either editor opens in the other; and the 317 framemap/frame `.dat` pair.
- **Pack** — writes the modified keyframes into a fresh animation archive of a
  *copy* of the cache, and the sequence record beside it. Unmodified keyframes
  keep pointing at the frames they came from.

## Controls

| | |
|---|---|
| left drag | pan, or drag a gizmo axis |
| left click | select a joint (click again to deselect) |
| middle / right drag | orbit |
| wheel | zoom |
| space | play / pause |
| left / right | step one keyframe |
| cmd+z, cmd+shift+z | undo / redo |
| cmd+e | repeat the last export |

Click a keyframe marker or anywhere on the timeline to seek. `Skeleton` in the
timeline bar shows the joints; nothing can be selected until it is on.

## Harness flags

None of the editing is reachable without a mouse, so there are flags that drive
it from the command line. They are how this port was checked.

```sh
--npc ID / --seq ID / --nodes     select at startup
--shot PATH [--shot-frames N]     render N frames, write a BMP, exit
--sim-click X,Y                   click once at a window point
--self-test                       drive every editing command, then undo and redo
--export-pgl PATH                 write the selected animation and exit
--import-pgl PATH                 load a .pgl at startup
--pack DIR                        pack into a copy of the cache and exit
--font-specimen OUT.bmp           the glyph set, no GL context needed
```

Checks run against `cache.osrs230`:

- `--self-test` passes on sequences 91, 808, 1205, 2100, 5061 and 7514, under
  `MallocScribble=1 MallocPreScribble=1`. It asserts each command's effect on the
  keyframe count, that the first edit marks the animation modified, that a
  transform lands and undoes, and that undoing everything then redoing it returns
  the same count both ways.
- `--export-pgl` then `--import-pgl` then `--export-pgl` again is byte identical
  for sequence 808 (54,215 bytes).
- `--pack` of an imported animation produces a cache the tool then reads back:
  11,617 sequences where the source had 11,616, and the packed sequence plays.
  (`rscache` prints a "Failed to read dat2 index entry" line during the commit,
  which is it probing for the not-yet-existing archive; the read-back is the
  evidence the write is right.)
- Opens `osrs184`, `osrs230`, `osrs239` and `rs643`. On `rs643` the entity
  downscales by four, which is poser-gl's `isHigherRev` handling of RS2's larger
  vertex scale.

What is **not** verified by harness, only by reading: dragging a rotation or
scale gizmo. `--sim-click` selects a joint and brings the gizmo up, and the
translation gizmo's arithmetic is shared with the other two, but no scripted
drag exercises the angle projection.

## Files

| | |
|---|---|
| `pg_cache.c` | rscache adaptor — npcs, items, identikits, sequences, models, frame archives, and the pack |
| `pg_model.c` | poser-gl's `ModelDefinition`: merge, bone groups, and the `animate` kernel |
| `pg_anim.c` | sequence to keyframes, reference nodes, skeleton inference |
| `pg_render.c` | shaders, buffers, camera, entity / grid / bone / node draws |
| `pg_gizmo.c` | the three manipulators and their pick volumes |
| `pg_gui.c` | the immediate-mode widget layer |
| `pg_ui.c` | the panels, in poser-gl's arrangement |
| `pg_app.c` | editor state, playback, picking, undo history |
| `pg_transfer.c` | `.pgl` and `.dat` codecs, and the pack front end |
| `pg_gl.c` | the GL 3.3 core entry points, loaded through SDL |

## Requirements

SDL2 and an OpenGL 3.3 core context. `make -C 3rd/rscache/tools poser-gl` builds
it; it is deliberately not part of that makefile's `all`, so a machine without
SDL2 still builds the other tools.
