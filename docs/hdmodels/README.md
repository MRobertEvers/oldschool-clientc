# hdmodels — models and animations for the HD render path

A small checked-in set so the entity viewer's **Select model file…** button has
something to open, and so the `texplane` / `texcylinder` / `texcube` /
`texsphere` kernels can be exercised without a 461 MB cache.

Everything here came out of `cache.rs727_preeoc` via:

```sh
make -C tools/entity_viewer ev_export
tools/entity_viewer/ev_export --rev rs727 cache.rs727_preeoc --out docs/hdmodels
```

Each subject directory has a `manifest.txt` recording exactly what was exported
and what the decoder made of it — face counts, and the render-type split that
decides which kernel each face reaches.

## What is here

| subject | models | animations | why it is here |
|---|---|---|---|
| `qbd/` | 70260, 70267, 70268 (+69766) | 16715 idle, 48 frames | the three Queen Black Dragon phases; **cube** mapping, 166 alpha faces each |
| `tztok_jad/` | 65988 | 16190 idle (150 frames), 16154 walk (16) | the only subject with **all of plane, cylinder and cube** on one model |
| `strykewyrm/` | 51847/51849 ice, 51848/51850 desert, 51852/51853 jungle | 12790 idle, 20 frames | **cylinder**-heavy — 36 of the ice wyrm's 70 texture coords |

Routing measured through the viewer's own path, placeholder texture on:

```
tztok_jad     plane 1231   cylinder  29   cube 480
qbd           plane 2749   cylinder   0   cube 612
strykewyrm    plane  230   cylinder 336   cube 375
```

No subject here uses render type 3 (sphere); that family is covered by
`make -C src test-texmap` instead.

## Two formats, and why they differ

**Models are raw cache archives** (`.ob3`). Not a pre-digested form — the point
is that the viewer runs them through the real `RSCache_ModelNewDecode`, so an
exported file exercises format detection and the complex-texture decode. OB3 is
the only layout that carries the cylinder/cube/sphere mapping parameters the HD
kernels need. Because they are raw, they are **uploaded to the server**, which
decodes and returns an ev_wire blob.

**Animations are ev_wire blobs** (`.eva`). A sequence is not self-contained: it
names frames, which name a framemap, so "the animation file" is three lookups
deep and a raw export would be inert without the cache it came from. The ev_wire
form carries the rig and every frame in playback order, so the browser loads it
**directly** — no server, no cache.

## Using them

```sh
tools/entity_viewer/run.sh
# -> http://127.0.0.1:8099/
```

1. **Select model file…** → pick a `.ob3`. The panel reports the detected
   format, face counts, and where every face routed.
2. **Select animation…** → pick the `.eva` from the same directory.
3. **placeholder texture** → tick it to see the mapped kernels actually run.
   Off is the faithful reading of a bare model file (no textures, so every
   textured face draws flat); on supplies a synthetic checkerboard so the
   cylinder/cube counters are non-zero. It is a lie about the asset and the
   truth about the routing.

**An animation only moves a model that shares its rig.** The manifest records
each sequence's framemap id; pairing across subjects animates nothing, and that
is the format working correctly rather than a bug.

## Where the animations come from

Not the per-npc `standing_animation` / `walking_animation` fields — those are the
OldSchool shape and are `-1` on every subject here. RS727 moved the idle and walk
set into a shared **BasType** the npc record points at with `bas_type_id`, and
all three QBD phases share bas 2502. Reading only the direct fields exports zero
animations and says nothing about why, which is what the first run of the
exporter did.
