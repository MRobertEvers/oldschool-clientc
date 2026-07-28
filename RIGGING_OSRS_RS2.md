# Porting animations between the OSRS and RS2 player rigs

Ported equipment goes stiff and ported animations tear the model apart, and both
have the same cause: **a vertex label is a joint index local to the rig that
authored it, not a stable id.** Nothing in either cache says so, and nothing
errors when it is wrong.

This is the method for porting an animation across the two eras, the measured
correspondence between the two player rigs, and how to apply it with
`port_lostcity`. Worked example: the dragon claws "Slice and Dice" special
(OSRS obj 13652, seq 7514) into LostCity rev 254.

## How animation actually binds to a model

A frame does not move vertices. It moves **label groups**, in pairs:

| transform type | what it does |
| --- | --- |
| 0 ORIGIN | names labels whose vertex centroid becomes the pivot |
| 1 TRANSLATE | shifts its labels' vertices |
| 2 ROTATE | turns its labels' vertices **about the last ORIGIN** |
| 3 SCALE | scales its labels' vertices about the last ORIGIN |
| 5 ALPHA | per-face transparency; no pivot |

Three separate things have to agree on what a label number means:

1. the **model** — every vertex carries one label (`hasVertexLabels` in `.ob2`)
2. the **framemap** — every transform names a set of labels
3. the **frame** — indexes transforms positionally, and carries their deltas

Only 1 and 2 travel between eras. A ported model keeps the source rig's
numbering; a ported framemap does too. The destination model does not.

## The two failure modes

Both are silent. `Model.animate2` (Client-TS `src/dash3d/Model.ts:1237`, and
xrsps `src/rs/model/Model.ts:1078` — **the two clients are byte-for-byte the same
algorithm here**) guards every lookup with `label < labelVertices.length` and
skips misses.

**Equipment that never moves.** A label the destination rig never addresses is
skipped, so those vertices sit still while the limb they belong to swings. The
dragon claws wield model came across tagged `{50, 161}`; 161 is not a joint in
rev 254 at all, so half the geometry was inert.

**A model that stretches.** For ORIGIN, matching nothing is not a skip — it
falls through to using the raw frame value as an **absolute** pivot:

```js
if (count > 0) { oX = (oX / count | 0) + x; ... } else { oX = x; oY = y; oZ = z; }
```

The rotate that follows then swings its limb around a point near the model's
base instead of around a joint. On the claws animation exactly three transforms
did this, and one was the root — 67 live labels, the whole body, hinged on
nothing.

## What the two rigs actually are

Dump either side with the tools in `3rd/rscache/tools`:

```sh
find_named --rev osrs239 cache.osrs239 --framemap 0        # source rig
find_named --dat1-anim <content>/models/anim_80.anim        # destination rig
```

|  | RS2 rev 254 (`anim_80.anim`) | OSRS (framemap 0) |
| --- | --- | --- |
| transforms | **71** | **245** |
| highest label | **72** | **217** |
| `[0] origin` | 0 | 0 |
| `[3] origin` | **1, 36** | **3, 2** |
| `[5] origin` | **19** | **29** |
| `[8] origin` | **23, 45, 47** | **40, 101, 102** |

They agree on transform 0 and diverge from transform 3. This is a **re-rig**,
not an extension — OSRS subdivides the same body about 3.5× more finely. Note
that rev 643 (2010) is already on the finer rig (205 transforms, labels to 254),
so there is no era holding both a post-2007 animation and the rev-254 skeleton.
Retargeting is the only route.

## Deriving the correspondence

Both rigs label the same human body, so **match joints by where they physically
are**. Identikit (`idk`) models are the right sample: they are the player body
itself, in the rest pose, and between them they cover every joint an animation
can address.

```sh
# OSRS side — walks idk configs, loads their models, prints label centroids
find_named --rev osrs239 cache.osrs239 --idk-centroids
```

For the RS2 side, read the `.ob2` vertex labels and positions out of
`<content>/models/human/`. Two rules matter:

- **Exclude `chathead/` and `jaw/`.** They are a *separate head-only rig* reusing
  labels 0–23 with different meanings, and averaging them in destroys the match.
- **Reject cross-body matches.** A hand must not pair with the opposite hand:
  require `sign(x)` to agree whenever both labels are meaningfully lateral
  (`|x| > 6`). Without this the mirror-symmetric limbs pair at random.

Then nearest-centroid — but nearest alone is not enough, and both of the extra
rules below were learned from artefacts that only showed up in a **side
profile**. Check profiles as well as front views: an error in depth is invisible
head-on.

- **Break near-ties by destination vertex count, not by distance.** Joints that
  sit in the same place are a tie the distance metric cannot settle sensibly.
  OSRS joint 3 is 2.2 units from rev-254 joint 41 and 2.4 from joint 36 — and 41
  carries 2 vertices where 36 carries 70. Letting 0.2 units decide handed the
  back of the head to a stub and left its 70 vertices with nothing to move them.
- **Every destination joint with real geometry needs a driver.** A joint nothing
  maps to is a patch of the body that stays put while everything around it
  moves. After the greedy pass, walk the undriven joints and give each the
  nearest source that can spare one — a source whose current target already has
  another driver. That recovered rev-254 joint 40 (head) and joint 18 (pelvis).

Read the result as anatomy — an arm label landing among the legs means the match
is wrong, not that the rig is strange.

## The measured correspondence

OSRS → rev 254, derived as above. 56 of OSRS's labels have a counterpart; most
match within 1–5 units. Several OSRS labels collapse onto one rev-254 joint,
which is the 245 → 71 difference showing up.

| rev 254 | joint | OSRS labels | centroid (x, y, z) |
| --- | --- | --- | --- |
| 0 | ground anchor | 0 | (0, 6, 0) |
| 1 | neck / upper head | 2, 49, 69, 94 | (0, −163, −2) |
| 2 | torso | 8 | (0, −140, −6) |
| 10 | left forearm | 19 | (−28, −112, 1) |
| 11 | left upper arm | 17 | (−26, −129, 1) |
| 12 | right shoulder | 24, 25, 26, 59 | (24, −148, 0) |
| 13 | right upper arm | 23 | (26, −129, 1) |
| 14 | right forearm | 22 | (28, −112, 1) |
| 15 | left shoulder | 18, 20, 21 | (−23, −148, 0) |
| **16** | **right hand** | **28** | (29, −94, 1) |
| **17** | **left hand** | **27** | (−29, −94, 1) |
| 18 | pelvis | 29 | (0, −109, −2) |
| 20 | chest | 5 | (0, −114, 8) |
| 21 | rear | 41 | (0, −94, 10) |
| 22 | pelvis centre | 39, 57, 79, 168 | (0, −90, 0) |
| 23 | right hip | 40 | (18, −92, 1) |
| 24 | left hip | 42 | (−18, −92, 1) |
| 25 | right thigh | 37 | (9, −46, 0) |
| 26 | right shin | 38, 76 | (9, −23, 1) |
| 27 | right knee | 35, 81 | (9, −59, −2) |
| 28 | left thigh | 31 | (−9, −46, 0) |
| 29 | left shin | 32, 77 | (−9, −24, 0) |
| 30 | left knee | 34, 80 | (−9, −59, −2) |
| 31 | right foot | 47 | (10, 0, 1) |
| 32 | right toe | 46 | (10, 2, −13) |
| 33 | left foot | 48 | (−10, 0, 1) |
| 34 | left toe | 45 | (−10, 2, −13) |
| 35 | head | 1 | (0, −181, −9) |
| 37 | waist | 4, 30, 86 | (0, −118, −3) |
| 41 | head top | 3 | (0, −171, 0) |
| 42 | right lower leg | 36 | (13, −56, −2) |
| 43 | left lower leg | 33 | (−13, −56, −2) |
| 44 | right calf | 43, 167 | (15, −76, 0) |
| 46 | left calf | 44, 166 | (−15, −75, 0) |
| 60 | leg centre | 82, 83 | (0, −53, 1) |
| 62 | head side | 60 | (−7, −155, 3) |

## Applying it

Two different maps, for two different things.

**Models** — `--label-map FROM=TO`, or `[export:label_map]`. Retags the vertex
labels of models merged into the player, so native animations move them:

```ini
[export:label_map]
161 = 16
50 = 17
```

Scoped to wield models only. A spotanim's model carries labels too, but they are
rigged to the spotanim's *own* framemap, which ports alongside it and stays
self-consistent — retagging that pair breaks an animation that already works.

**Animations** — `[export:rig_map]`. Renumbers every exported framemap's label
sets. The frame data is untouched: frames address transforms by index, and the
transform list keeps its shape, so only the labels need to move.

```ini
[export:rig_map]
1 = 35
8 = 2
28 = 16
27 = 17
...
```

**Collapsed joints must be deduplicated.** This is the step that decides whether
a retarget looks right or merely close. A finer source rig maps several joints
onto one destination joint, so a transform that named three distinct shoulder
joints ends up naming the same one three times — and the client walks the label
list applying the transform *once per entry*. The joint is rotated three times
and the limb lands at three times the intended angle. The result is a coherent,
plausible, wrong pose, with nothing visibly broken to trace back from:

```
xform 22 rotate  before dedup: 12 16 12 14 13 12     <- joint 12 rotated 3x
xform 30 rotate  before dedup: 17 11 15 15 15 10     <- joint 15 rotated 3x
```

`port_lostcity` collapses duplicates within each transform automatically once a
`[export:rig_map]` is present. `anim_compare --report` is what surfaces them:
it prints the live destination labels per transform, and a repeat is obvious.

**Scope the map to the framemaps it describes.** A rig map states one skeleton's
joints in terms of another's, so it is meaningful only for framemaps built on
that skeleton — and an export usually carries others. A spotanim is rigged to
its *own* framemap, which travels with its own model and is already
self-consistent; running a player correspondence over it renumbers its joints
into player joints its model does not have:

```
source framemap 369  [0] origin labels: 1 2 3 4 5 6 7 8 9 10   (spotanim's joints)
unscoped export      [0] origin labels: 35 1 41 37 20 254 2    (player joints)
```

The claws' spec graphic is a 120-vertex model carrying labels 1..18, so after
that it was being posed by transforms addressing 35, 41, 37 and 20 — joints it
does not have — and the streaks flew off on their own. Declare
`rig_framemaps = 0` and only the player rig is touched.

**Every source label needs an entry, including the ones with no counterpart.**
Park those on a number no destination model uses (254 works; rev 254 tops out at
72 with 255 as the no-group sentinel). Leaving them alone is worse than dropping
them — OSRS label 50 is not rev-254 joint 50, so identity quietly bends a thigh
with a cape transform. For the claws that was 56 mapped and 199 parked.

## anim_compare: watching both rigs play the same frames

Joints are invisible in every other view, so the loop for refining a
correspondence is a side-by-side player: source on the left, port on the right,
paired frame by frame.

```sh
make -C 3rd/rscache/tools anim_compare

./3rd/rscache/tools/anim_compare/anim_compare \
  --a-rev osrs239 --a-cache cache.osrs239 --a-seq 7514 \
  --b-models <content>/models/human/man \
  --b-anim   <content>/models/dclaws/dclaws_animset_0.anim \
  --out /tmp/cmp --sheet --by-label
```

Left is a dat2 cache and a sequence id; right is the `.ob2` body models and the
`.anim` animset the exporter wrote. Frames pair by index, which holds because an
exported animset carries its sequence's frames in order. `--sheet` writes one
contact sheet of the whole run so only the frames that diverge need opening.

`--a-model ID` with `--b-model FILE.ob2` compares a single model instead of the
player body — needed for anything that is not a player animation, such as a
spotanim rigged to its own framemap. When both sides share a model and a rig the
panels should be pixel-identical, which makes that pairing a hard pass/fail
rather than a judgement call.

`--report` prints which transforms the animation actually drives and their live
destination labels. That is what surfaces duplicate collapsed joints.

`--motion` is the numeric check, and the one to reach for when two different
meshes make eyeballing unreliable. It reports how far each joint's vertices
travel from the rest pose, averaged over the animation and expressed as a
percentage of body height so the two meshes' scales cancel. Read it against the
map: a joint that travels in the source but barely moves in the port is
underdriven; one that travels much further is overdriven. On the finished claws
retarget seven of the eight substantial joint pairs agree within 1.6 points:

```
OSRS  28 -> rev254  16   source  26.1%   port  26.3%     (right hand)
OSRS  27 -> rev254  17   source  24.6%   port  24.5%     (left hand)
OSRS   1 -> rev254  35   source  15.7%   port  16.0%     (head)
OSRS  38 -> rev254  26   source  20.6%   port  24.6%     (shin, +4.0)
```

The shin is the 245 → 71 collapse showing: two source joints drive it through
separate transforms, so it travels further than the original. That is the
residual cost of retargeting a finer rig onto a coarser one.

**`--by-label` is the mode that finds rig bugs.** It colours faces by vertex
label instead of by material, so a joint that has been mapped to the wrong
counterpart shows up as a limb in the wrong colour or in the wrong place, rather
than as a vaguely-off pose you have to squint at.

Two things this tool taught the hard way, both worth knowing before writing
another one:

- **Use one model per body part on both sides.** `models/human/man` holds every
  *variant* of each part — a dozen heads, a dozen torsos — and a player wears
  one of each. Merging the lot stacks overlapping meshes and renders an exploded
  figure whether or not the rig is right, which looks exactly like a rig bug.
- **Rotation order is roll (z), then pitch (x), then yaw (y).** Rotations do not
  commute. Applying them in index order gives a coherent but subtly wrong pose —
  again indistinguishable from a bad correspondence. The kernel in `anim_compare`
  is a line-for-line port of `Model.animate2` for exactly this reason; an
  approximation there produces confident, wrong conclusions about the map.

## Verifying

Four things had to be true together before the claws animation played correctly,
and each alone left it visibly wrong:

1. `[export:rig_map]` renumbering source joints into destination joints
2. `rig_framemaps` scoping that map to the player rig, so other framemaps that
   travel with their own models are left alone
3. duplicate collapsed joints removed within each transform
4. dead ORIGIN pivots re-pointed at what their dependents move

Missing (1) bends the wrong parts. Missing (2) scrambles every *other* rig in
the export — for the claws, the spec graphic. Missing (3) over-rotates by the
collapse factor. Missing (4) flings a limb from an absolute pivot. All four are
applied by `port_lostcity` when a rig map is declared.

The spotanim is the check that the whole chain is right: source and port use the
same model and, once scoped, the same rig, so `anim_compare --a-model 29207
--b-model <...>.ob2` should come out **pixel-identical**. It does — 0.0%
silhouette mismatch across all 32 frames.

Structural check first — the retargeted animset should read as anatomy:

```sh
find_named --dat1-anim <content>/models/dclaws_animset_0.anim
```

`[5] origin` on the retargeted claws animation comes out as rev-254 joint **18**
(pelvis) where rev 254's own rig uses **19** — the same joint. That is the shape
of a correct retarget.

Then in-game. Headless captures are timing-sensitive: the cheat fires on the
first game tick but world-build latency varies per run, so **check the viewport
is actually loaded before trusting a diff** — a blank frame differs from a live
one by a lot and reads as motion. Re-fire the animation on a queue so a capture
cannot land after it ends, then diff the player region across frames; a cycling
animation shows alternating poses with a repeat at the loop period.

## Limits

- **245 → 71 is lossy.** Where several OSRS joints collapse onto one rev-254
  joint, only one rotation of the several survives deduplication, so finely
  subdivided motion is coarser than the original. That is the correct trade:
  keeping them all multiplies the angle instead of refining it.
- **The map covers the body only.** OSRS labels above the identikit set are
  equipment, hair and cape joints with no rev-254 equivalent; they are parked
  inert, so any motion authored purely for them is lost.
- **The pose assumption.** Centroid matching assumes both rigs' body models are
  authored in the same rest pose. It holds between these two; check it before
  reusing the method on a rig that is not humanoid.
