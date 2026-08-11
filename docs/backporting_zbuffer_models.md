# Backporting z-buffered models to the OSRS painter's sort

How to take a model authored against a depth-buffered client — RS727, RS3, or
anything else that resolved visibility per pixel — and make it look right under
ToriDraw, which resolves it per *face*, in one global order, with no depth
buffer.

Written from the RS2012 Queen Black Dragon port, where the numbers below come
from. It is a procedure, not a recipe: the first half is diagnosis, and the
most valuable thing it can tell you is that the model does not need the work.

Related: [`RS2012_BACKPORT.md`](../RS2012_BACKPORT.md) for the other three
conversions a pre-EoC import gets wrong (materials, alpha, scale), and
[`rs2012_qbd_priorities/`](rs2012_qbd_priorities/README.md) for the before and
after pictures.

---

## 1. What actually breaks, and why

A depth-buffered renderer needs no draw order. Its content is therefore allowed
to have properties that a painter's algorithm cannot express, and an artist
working against one will produce them without noticing:

- **Interpenetrating parts.** A jaw passing through a skull, a wing through a
  body, a claw ring clasped around a neck. Two triangles that each occlude the
  other in different places have no correct order at all.
- **Coplanar decals.** A decorative plate lying flat on a surface. Both have
  nearly the same centroid depth, so the sort's answer is a coin flip that
  flickers as the model turns.
- **Priority bytes that never ordered anything.** The format has a per-face
  render-priority field. A z-buffered client ignores it, so whatever is in
  those bytes is unexamined garbage — and ToriDraw *honours* it, overriding the
  depth sort that would have been correct.

That last one is the big one and the easy one. On the QBD, honouring the
inherited priorities put **11.84%** of pixels behind the surface that should
have been visible. Dropping them entirely: **4.28%**. Everything else in this
document is fighting over the remaining four points.

### The sorter you are authoring for

`sort_face_draw_order` in
[`3rd/toridraw/toridraw_render.u.c`](../3rd/toridraw/toridraw_render.u.c).
Read it before you touch a priority byte. Emitted back to front:

```
flexible faces deeper than avg(prio 1,2)
prio 0, 1, 2                 <- fixed band, each internally depth sorted
flexible faces deeper than avg(prio 3,4)
prio 3, 4
flexible faces deeper than avg(prio 6,8)
prio 5, 6, 7, 8, 9
remaining flexible faces
```

Two mechanisms, not one:

- **Priorities 0–9 are ten hard bands.** Every face in band N paints over every
  face in band N−1, whatever their depths. Inside a band the depth sort still
  runs.
- **Priorities 10 and 11 are the flexible band.** Those faces are purely depth
  sorted and *spliced* into the fixed run at three depth averages.

If every face is flexible, or every face is band 0, you get a plain depth sort.
The two are only distinguishable when you mix them — which is the escape hatch
in §7.

---

## 2. Build the loop before you change anything

You cannot author a global sort order by eye. You will fix the jaw, wreck a
wing, and see an improvement. Two tools make it a measured loop:

```sh
make -C src rs2012-model-view rs2012-face-priorities
```

**[`rs2012_model_view`](../src/engine/proctex/test/rs2012_model_view.c)** renders
an `.ob3` through the *real* `RenderModel1Project` / `2SortFaces` /
`3Raster` path, so the priorities under test are honoured exactly as the client
honours them. Several `--model` arguments are merged the way an npc's `model1`
and `model2` are.

Its `--score` is the whole point. It rasterizes one projection twice — once in
the order the sorter produced, once through a true z-buffer — and counts pixels
where the painter's winner is genuinely *behind* the z-buffer's:

```sh
src/build/rs2012_model_view --model head.ob3 --model body.ob3 \
    --out sheet.bmp --angles 8 --pitch 190,300,383 --tile 300 \
    --score --score-out mask.bmp --reference-out truth.bmp
```

- `--score-out` paints the mask: red where a farther surface is showing.
- `--reference-out` paints what a z-buffer would have shown — the target.
- `--compare` renders the same model both ways through the engine's own
  depth-tested kernels and diffs them (§7).

Report both numbers it prints. The percentage says how much is wrong; the
**mean depth error** says what kind. Nine units on a model 1,700 deep is
filigree along seams. A hundred and nineteen is whole plates punching through a
head, and that is what a viewer notices.

**[`rs2012_face_priorities`](../src/engine/proctex/test/rs2012_face_priorities.c)**
segments, measures, solves and writes. It never writes its input — `--in` and
`--out` are separate paths.

**[`tools/rs2012_qbd_prio.sh`](../tools/rs2012_qbd_prio.sh)** wires both into
one command per lane. Copy it and change the model table for a new one.

---

## 3. Diagnose before you author

Run the analyser with `--report` and no `--out`. It costs a second and prints
the ceiling:

```
baseline: 36138/678610 wrong pixels under the pure depth sort (5.33%)
          2515 within one feature (no band can reach these), 33623 between features
```

**`within one feature` is unreachable.** A feature has to live in exactly one
band (§4, rule 1), so a surface sorting wrongly against *itself* cannot be
addressed by any priority at all. If most of the error is intra-feature, stop:
this is not a priority problem, and the answer is §7 or a geometry edit.

Then look at the mask. Ask which of these you have:

| what the mask shows | what it is | fix |
|---|---|---|
| large contiguous slabs, deep | inherited priorities, or a real feature ordering | §5 |
| thin filigree along seams | interpenetration at near-equal depth | irreducible; §7 if it matters |
| flicker between adjacent angles | coplanar decal | §5, usually a clean win |
| one part inside another | genuinely interpenetrating parts | §7 |

---

## 4. The three rules

Each of these is a way to make the model measurably **worse**. All three were
learned the expensive way.

### Rule 1 — a feature lives in exactly one band

Features are connected components: faces sharing a vertex index are one surface,
because that is what "one part" meant to whoever built it. Weld coincident
vertex *positions* first — imports routinely duplicate a seam's vertices, and
the halves must not be able to land in different bands.

Splitting one surface across two bands does not refine its order. It forbids
its own halves from interleaving, so the far half paints over the near half.
Bands separate features; depth orders faces *within* one.

### Rule 2 — a band is not a pairwise promise

This is the trap that looks like success. Score each ordered pair of features
over a sphere of viewpoints:

```
fixable[A][B]    A should win this pixel, B is what the sort paints
breakable[A][B]  B should win it and does; putting A over B destroys it
net(A over B) = fixable - breakable
```

Then promote every pair with a positive net, and you get a *regression*.
Measured on the QBD: **4.5% → 7.4%**, mean depth error **9 → 80**.

Because putting A one band above B puts it above everything else in B's band
too — including the 2,258-face body it was never compared against. Each pair
reads as a win; the sum lands a small spike in front of the whole dragon.

So solve it globally. Choose a band per feature maximising

```
sum over pairs with band(A) > band(B) of net(A over B)
```

and hill climb it. Start from a state that *is* the plain depth sort, so the
result provably cannot score below shipping no priorities at all — the one
guarantee worth having.

Start from the **middle** band, not from zero. The climb moves one feature at a
time, and from band 0 the only available move is "put this one over
everything". The relation that usually wants expressing is the opposite: one
feature needs to go behind the rest, which from zero would take a coordinated
move of every other feature at once. From the middle both directions are one
move away. Compact the bands down to a contiguous run from 0 afterwards — which
numbers are occupied matters, because the flexible splice points are averages
over bands 1/2, 3/4 and 6/8.

### Rule 2a — the bind pose is not where the model is seen

Bands are a property of the model, but every measurement that chooses them is
taken in some pose, and a boss spends almost none of its screen time in the
bind pose. Features that never overlap in the bind pose can spend a whole
animation on top of each other, and a bind-pose solve has no opinion about
them. So pose is a sampling axis of its own, orthogonal to camera angle:

```sh
src/build/rs2012_face_priorities --in a.ob3 --in b.ob3 \
    --cache cache.osrs239.rs2012 \
    --frames <the sequence config's packed frame ids> --frame-stride 6
```

Load the frames **through the cache**, not from the lane's staged `.anim` and
`.base` files. Those are pass-through assets — the importer copies the bytes —
so what is on disk is encoded in the SOURCE revision, while the codec that
decodes them is a property of the DESTINATION cache. Going through
`RSCache_Dat2Disk` with the destination profile set makes the poses measured
here the poses the client draws, by construction.

And check that the poses are poses. A frame whose rig does not match the model
does not fail loudly — it produces geometry, just not the right geometry, and
the usual wreckage is every vertex collapsed onto the pivot. Measured, that
contributes a solid blob of feature-vs-feature "errors" that are pure decode
artefact and would move real bands. The tool rejects a pose whose reach is
under a tenth of the bind pose's and refuses to author at all if they all go,
because authoring from the bind pose while reporting an animated solve is the
one failure worth being loud about.

### Rule 3 — only sample the angles the client can produce

`app.c` clamps world camera pitch to **128..383 of 2048** — 22.5° to 67.3°
looking down. Nothing unclamps it. Extend the range above the horizon only by
as much as perspective gives on a model taller than the camera (the Queen's
head is twenty units up, and players do see its underside).

Sweeping the whole sphere asks every pair to agree from *underneath* the model
as well. On a closed creature almost no pair does — a spike standing out of a
neck is in front of it from one side and behind it from the other — so every
real ordering gets scored away as a conflict and the solver finds nothing.

The pleasant consequence of scoring pairs rather than asking "which is in
front" is that rule 2's conflicts handle themselves: a pair that swaps as the
model turns accumulates `breakable` until its net goes negative, and is never
separated. You do not have to detect conflicts, only to count honestly.

---

## 5. Author, verify, ship

```sh
# 1. analyse and write, alongside the source and never over it
src/build/rs2012_face_priorities \
    --in  lane/model_A.ob3 --in  lane/model_B.ob3 \
    --out out/model_A.ob3  --out out/model_B.ob3 \
    --report --views 24 --pitches 7 --res 256

# 2. verify with the renderer, not with the solver's own prediction
src/build/rs2012_model_view --model lane/model_A.ob3 --model lane/model_B.ob3 \
    --out before.bmp --angles 8 --pitch 190,300,383 --tile 300 --score
src/build/rs2012_model_view --model out/model_A.ob3  --model out/model_B.ob3 \
    --out after.bmp  --angles 8 --pitch 190,300,383 --tile 300 --score

# 3. only then copy into the lane and re-pack
cp out/*.ob3 OSRS-Content/osrs239-content/models/ported/<lane>/
make -C src mock230-cache-rs2012            # the QBD lane's re-pack target
```

Step 2 is not optional. The solver optimises a first-order model of the sort;
the renderer runs the real one. They agree on the QBD, but the check is cheap
and the failure mode is silent.

**Analyse an npc's models together.** The client merges `model1` and `model2`
before it draws, so their bands have to be chosen in one coordinate system.
Pass every model of one form as multiple `--in`; each `--out` receives its own
slice. If a model is shared between forms (the QBD's `model2` is in four),
check that every run agrees on it — on the QBD it stayed entirely in band 0, so
the question was moot, but do not assume it.

**The bake overwrites model edits.** `rs2012_material_bake` rewrites OB3s from
the source cache. Priority authoring must come *after* it.

---

## 6. Traps in the code, not the content

Each of these cost real time.

**The encoder prefers the provenance header.** `RSCache_ModelEncodeFormat`
takes the recorded header over anything derived from the model, so writing
`face_priorities` is not enough — the header's priority byte has to say "per
face" (255) or the array is silently not written. The strip tool has the mirror
problem: it must write 0 there, or 504 of 660 models fail to encode.

```c
model->face_priorities = /* one byte per face */;
model->model_priority = 255;
if( provenance->header_flag_count > 1 )
    provenance->header_flags[1] = 255;   /* the priority byte */
```

Do not "fix" it by passing `provenance = NULL` — that discards the OB3 tail,
which carries particle and billboard sections the library preserves verbatim
without interpreting. Round-trip and re-decode every file you write; both tools
do, and it is the only thing standing between a bad byte and a broken lane.

**Screen vertices are centre-relative.** `scene->screen_vertices_x/y` come out
of the projection *without* the viewport offset; the raster adds
`x_center`/`y_center` as it draws. Any scoring pass that reads that scratch has
to apply the same offset, or the mask lands half a tile from the picture it is
grading — which looks exactly like a sort bug.

**The AABB cull assumes the viewport starts at the origin.** It compares the
projected box against `0..width`. A contact sheet that points several tiles at
one big buffer by moving `x_center` gets every tile but the first culled
outright. Render each tile into its own origin-anchored buffer and blit.

**`flags` bit 0 is now the z-buffer opt-in.** The ToriRS adaptor sets bit 0 on
every model it converts, so the flag cannot be read as intent — drive it
explicitly wherever it matters.

**Version-13+ ob3s store vertices at 4×.** The engine adaptor shifts them down
(`format_version >= 13`). Any tool doing its own geometry work has to agree, or
two inputs at different versions land at different scales.

---

## 7. When priorities are not the answer

They often are not, and pretending otherwise is how a day disappears. Three
escapes, in increasing order of cost:

**`--bulk-flex`.** Put the unpromoted bulk at priority 10 instead of band 0. A
feature left in a hard band is then no longer pinned in front of everything:
the sorter splices the flexible run around it at the averaged depth of the
occupied hard bands, so the feature sorts *as a unit* at its own depth — over
the far half of the surface it sits on and under the near half. That is what a
claw ring round a neck actually wants, and no arrangement of hard bands can
express it. On the QBD it measured slightly worse than hard bands (4.87% vs
4.16%) so it did not ship, but for a model whose problem is one part clasped
around another it is the right shape of answer. Measure it; it is one flag.

**`TORIDRAW_MODEL_FLAG_ZBUFFER`.** The renderer can depth-test a model
per pixel. It is opt-in per *model*, not per scene, and resetting the buffer
before each such model bounds the effect to that model — nothing changes about
how it layers against the rest of the scene. This is the honest answer for a
model whose parts genuinely interpenetrate, which is exactly the model a
z-buffered client was free to produce. `rs2012_model_view --compare` renders
both ways and diffs them, so you can see what the depth test buys before
spending it. It costs a screen-sized buffer and the depth-tested kernels; do
not reach for it to avoid measuring.

**Edit the geometry.** Pull a decal off the surface it z-fights with. Separate
a part that interpenetrates. This is what the original artist would have done
had they been authoring for a painter's algorithm, and for a small local
problem it is cheaper than everything above.

---

## 8. Worked example: the Queen Black Dragon

Five npc forms, 216 connected features over 9,012 faces in the two merged
models of the default form. Scored over 24 camera angles inside the client's
pitch clamp:

| form | shipped (inherited priorities) | stripped | authored bands |
|---|---:|---:|---:|
| default | 11.84% | 4.28% | **4.21%** |
| crystal | 10.92% | — | **4.35%** |
| hardened | 11.84% | — | **4.51%** |
| tortured soul | 5.51% | — | **1.40%** |
| giant worm | 0.90% | — | **0.06%** |

Mean depth of an error on the default form: **119 units → 12**. In the masks,
the large contiguous slabs — whole neck plates painting through the head —
disappear; what remains is thin filigree along interpenetration seams.

The honest reading: most of the win is *removing* priorities that were never a
draw order, which the existing `rs2012-strip-priorities` also achieves. The
measured bands add 0.1 points on the big forms and 4 points on the soul. The
solver promoted between zero and six features per form, 10 to 26 faces in total — small
protruding detail, exactly what an artist would have reached for, and nothing
like the whole-part layering the inherited bytes claimed.

Pictures: [`rs2012_qbd_priorities/`](rs2012_qbd_priorities/README.md).

---

## 9. Checklist

- [ ] Does any imported model report priorities? (`--stats` prints the
      histogram.) If they came from a z-buffered client they are noise.
- [ ] Baseline scored with the renderer, at the client's camera pitch range.
- [ ] Intra-feature share of the error known before authoring starts.
- [ ] Features segmented with coincident vertices welded.
- [ ] Bands solved globally, from a start state equal to the plain depth sort.
- [ ] Every model of one npc form analysed together; shared models agree.
- [ ] Output written alongside the input, never over it.
- [ ] Every written file re-decoded and compared before it lands.
- [ ] After beats before *in the renderer*, not just in the solver.
- [ ] Priority authoring runs after the material bake, and the cache is
      re-packed afterwards.
