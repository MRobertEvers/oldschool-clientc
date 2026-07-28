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

Then nearest-centroid, and read the result as anatomy — an arm label landing
among the legs means the match is wrong, not that the rig is strange.

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

**Every source label needs an entry, including the ones with no counterpart.**
Park those on a number no destination model uses (254 works; rev 254 tops out at
72 with 255 as the no-group sentinel). Leaving them alone is worse than dropping
them — OSRS label 50 is not rev-254 joint 50, so identity quietly bends a thigh
with a cape transform. For the claws that was 56 mapped and 199 parked.

## Verifying

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
  joint their rotations compound on the same vertex group, so finely subdivided
  motion is coarser than the original.
- **The map covers the body only.** OSRS labels above the identikit set are
  equipment, hair and cape joints with no rev-254 equivalent; they are parked
  inert, so any motion authored purely for them is lost.
- **The pose assumption.** Centroid matching assumes both rigs' body models are
  authored in the same rest pose. It holds between these two; check it before
  reusing the method on a rig that is not humanoid.
