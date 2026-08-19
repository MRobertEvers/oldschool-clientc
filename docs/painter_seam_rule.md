# The bucket painter's seam rule, by example

`painter_paint_bucket` (`src/painters/painters_bucket.u.c`) orders the world
back-to-front with the reference's tile-adjacency gates, plus one extra rule of
its own — the **seam exception** — that lets a tile's *ground* go down early in
one precise situation the reference gets wrong. This page draws that rule out,
case by case. The code is `bucket_gate_blocks`,
`bucket_neighbour_holds_only_nearer_scenery`, `bucket_far_neighbours_pending`
and the `seam_relaxed` / `seam_scan` fields of `TilePaint`.

Measured outcome (2026-08-19): with this rule the bucket painter is
pixel-identical to `painter_paint_world3d` in 64 ToB views and 16 QBD views
([painter_sweeps/](painter_sweeps/README.md)), and it still orders the one
topology the reference gate loses (two abutting large locs on the camera
column). Unit tests: `make -C src test-painters-terrain-levels`.

## Notation

```
        z (north, away from the eye)
        ^
        |   . . . . .      .   empty tile (ground only)
        |   . A A A .      A   tile covered by loc A (a multi-tile loc)
        |   . . T . .      T   the tile being popped ("this tile")
        |   . . . . .      N/W/E/S  its neighbours
        |   . . E . .      E   the eye's tile (camera_sx, camera_sz)
        +----------------> x (east)
```

- **ring** (distance) of a tile = `|x - Ex| + |z - Ez|`, Manhattan. The bucket
  drains farthest ring first, so a tile at ring 9 is popped before ring 8.
- **far neighbour** = the neighbour one step *away* from the eye on each axis.
  For a tile north-east of the eye that is its N and E neighbour; for a tile
  exactly on the eye's column (`x == Ex`) *both* W and E count (the reference's
  `x <= Ex` and `x >= Ex` are both true there); same for the eye's row.
- A tile goes `READY → GROUND → DONE`. Its terrain is emitted when it passes
  the ground gate (READY→GROUND); its locs are released once every footprint
  tile is at least GROUND; its near walls are emitted at DONE.
- A multi-tile loc is **released at the last footprint tile to get ground**,
  which is always the one nearest the eye — so a loc lands in the draw stream
  at its *nearest corner's* ring, however far its far corner reaches.

## The reference gate (what the bucket does by default)

A tile may not paint its ground until each far neighbour is **DONE**, with one
escape: if the two tiles **share a loc** (this tile's `spans` bit points at the
neighbour), the neighbour's GROUND is enough — otherwise the shared loc, which
waits on this tile's ground, would deadlock against it.

### Example 1 — ordinary wait

```
   z
   5   .  .  .  .  .
   4   .  .  N  .  .      N: ring 5, has a 1x1 loc n on it
   3   .  .  T  .  .      T: ring 4
   2   .  .  .  .  .
   1   .  .  .  .  .
   0   .  .  E  .  .
       0  1  2  3  4  x
```

Pop N (ring 5): its far neighbours are DONE, ground goes down, loc `n` is
released in the same pop (its footprint is N itself), N completes → DONE.
Pop T (ring 4): N is DONE → T's ground goes down. Order: `N floor, n, T floor`.
Correct — `n` sits behind T's floor on screen.

If T had been popped while N was still GROUND (loc `n` pending), T would have
**waited** (`continue`, dropped from the queue) and been re-queued when N
completed, because N's completion pushes its inward neighbour T.

### Example 2 — the span exception (shared loc)

```
   5   .  .  .  .  .
   4   .  A  A  A  .      A is 3x2: x[1,3] z[3,4]
   3   .  A  A  A  .
   2   .  .  .  .  .
   0   .  .  E  .  .
       0  1  2  3  4
```

Pop (2,3), ring 3: its N neighbour (2,4) is under A and only GROUND (A is not
released until (2,3) itself has ground). (2,3)'s `spans` has the NORTH bit set
(A covers both) → the exception passes → (2,3)'s ground goes down → A's last
footprint tile is now GROUND → **A is released at (2,3)**, ring 3. Without the
span exception every multi-tile loc would deadlock against its own footprint.

## The seam exception

The span exception is keyed on *this* tile's spans, so it cannot fire when the
neighbour is held by a loc that does **not** cover this tile.

### Example 3 — two abutting large locs on the eye's column (the QBD seam)

```
   8   W  W  W  W  |  B  B  B  B       W: x[0,3], B: x[4,7], both z[2,8]
   7   W  W  W  W  |  B  B  B  B       The seam is between x=3 and x=4.
   6   W  W  W  W  |  B  B  B  B
   5   W  W  W  W  |  B  B  B  B       E is at x=3 — ON the seam column.
   4   W  W  W  W  |  B  B  B  B
   3   W  W  W  W  |  B  B  B  B
   2   W  W  W  W  |  B  B  B  B
   1   .  .  .  .  |  .  .  .  .
   0   .  .  .  E  |  .  .  .  .
       0  1  2  3     4  5  6  7
```

Take T = (3,7), ring 7, under W. Because `x == Ex`, T is gated on **both** its
W neighbour (2,7) and its E neighbour (4,7). (2,7) is under W too → span
exception, fine. (4,7) is under **B**, which does not cover T — no span bit —
so the reference gate demands (4,7) be DONE. (4,7) cannot be DONE until B is
released, and B is released at its nearest tile (4,2), **ring 3**.

Reference result: the whole column x=3 (rings 7 down to 3) waits, B is drawn at
ring 3, and *then* the column's floor — rings 7..3, i.e. farther than B — is
emitted **on top of B**. On screen, a one-tile-wide strip of ground running up
over the platform. `painter_paint_world3d` has exactly this defect.

The seam exception: when T pops and the reference gate blocks on a neighbour
that already has its ground down, and **everything still keeping that
neighbour from DONE is pending scenery whose nearest corner is nearer the eye
than T** (B's nearest corner is ring 3 < T's ring 7), the neighbour has nothing
left that belongs *behind* T — B is going to be drawn nearer than T no matter
what — so T's ground may go down now. The column sweeps in order and B lands
after it. This is the `test_seam_between_two_large_locs_keeps_the_sweep` test.

That was the whole rule until 2026-08-19. It is wrong in general, and the next
examples are why it now has two more conditions.

## Condition 1 — only the LATERAL gate may relax

A large loc straddles rings in depth as well as sideways. "Its nearest corner is
nearer than T" does not mean "it is in front of T".

### Example 4 — loc directly BEHIND the tile (Xarpus ledge)

```
  11   .  L  L  L  L  L  L  .  .        L: 6x5 at x[1,6] z[7,11]
  10   .  L  L  L  L  L  L  .  .
   9   .  L  L  L  L  L  L  .  .        E at (8,-17) (off the bottom of the
   8   .  L  L  L  L  L  L  .  .        picture: the real numbers are Xarpus
   7   .  L  L  L  L  L  L  .  .        E=(50,43), T=(44,66), L at z[67,71],
   6   .  .  T  .  .  .  .  .  .        shifted by (-42,-60)).
   5   .  .  .  .  .  .  .  .  .
       0  1  2  3  4  5  6  7  8
```

L's nearest corner (6,7) is at ring `|6-8| + |7-(-17)|` = 2 + 24 = **26**.
T = (2,6) is at ring 6 + 23 = **29**. T's N neighbour (2,7) is under L, GROUND,
L pending, 26 < 29 — the old rule relaxed T. T's floor went down at ring 29, L
was released at ring 26, and L's tall z=7 row — *directly behind T on screen* —
painted **over** T's floor. That is the Xarpus "grey slab over the mossy floor",
and the Maiden "landing over the steps" (`32804`, 4x1 at z=52, behind the 1x1
steps at z=53..55) is the same shape.

The fix asks which axis the gate is on, relative to the eye→T ray:

```
   eye→T = (dx, dz) = (2-8, 6-(-17)) = (-6, +23)   → |dz| > |dx|
   → the z axis is DEPTH; T's N gate is a depth gate → it may NOT relax.
```

T waits for (2,7) to be DONE, which means L is drawn first and T's floor lands
on top of it — the reference order, and what world3d draws.

Rule: for a tile at `(dx, dz)` from the eye, the axis with the larger `|delta|`
is the depth axis; the gate across the *other* axis is lateral. `|dx| == |dz|`
is treated as depth (strict) on both axes.

```
        |dz| > |dx|            |dx| > |dz|            |dx| == |dz|
   (T ahead of the eye)   (T beside the eye)          (diagonal)

        N  depth                N  lateral              N  depth
   W lateral  E lateral    W depth  E depth         W depth  E depth
        S  depth                S  lateral              S  depth
```

### Example 5 — the QBD seam is lateral, so it still relaxes

Back to Example 3: T = (3,7), E = (3,0) → `(dx,dz) = (0, 7)`, `|dz| > |dx|`,
the W/E gates are **lateral**. The blocking neighbour (4,7) is across the E
gate → the exception may fire → the seam column still sweeps in order.

### Example 6 — loc BESIDE the tile, camera rotated

```
   6   .  .  .  .  .  .  .
   5   .  .  .  .  .  .  .
   4   .  T  .  M  M  M  M      M: 4x3 at x[3,6] z[2,4]; E at (0,3)
   3   .  .  .  M  M  M  M      T = (1,4): (dx,dz) = (1, 1)... tie.
   2   .  .  .  M  M  M  M      Take T = (1,5) instead: (1,2) → |dz|>|dx|,
   1   .  .  .  .  .  .  .      z is depth, W/E lateral.
   0   E  .  .  .  .  .  .
       0  1  2  3  4  5  6
```

(The eye is at the west edge here: the camera is yawed 90°. The rule is in
tile space and does not know the yaw — only `(dx, dz)`.) T = (1,5)'s far
neighbours: W is out of the box; E = (2,5) is empty; N = (1,6) empty. Nothing
to relax; T just draws. Now T = (2,3): `(2,0)` → x is depth, N/S lateral. Its
E neighbour (3,3) is under M (a *depth* gate, x): strict — M, which reaches the
eye's row, is drawn first, then T. Its N neighbour (2,4) is empty. Fine.

The point: "lateral" is not "the x axis". It flips with where T is relative to
the eye, so a yawed camera gets the same treatment as a square one.

### Example 7 — lateral neighbour held by a wall → no relaxation

```
   4   .  .  .  .  .
   3   .  T  N| .  .     N = (2,3) has a wall on its near side and a pending
   2   .  .  .  .  .     loc reaching nearer than T.
   0   .  .  .  E  .     E = (3,0): T = (1,3) → (-2, 3): z depth, E lateral.
       0  1  2  3  4
```

A far tile's *near* wall must still precede a nearer tile's ground (it is
emitted at N's DONE, and T is nearer). Any wall or wall-decor on the neighbour
disqualifies it: T waits. (`bucket_neighbour_holds_only_nearer_scenery` returns
0; the memo `seam_scan` records `SEAM_SCAN_DISQUALIFIED` so the chain is never
re-walked this paint.)

### Example 8 — pending loc does NOT reach nearer than T → ordinary wait

```
   5   .  .  .  .  .  .
   4   .  .  .  P  P  .     P: 2x2 at x[3,4] z[3,4]. Nearest corner (3,3):
   3   .  T  .  P  P  .     ring |3-2|+3 = 4. T = (1,3): ring 1+3 = 4.
   2   .  .  .  .  .  .     E = (2,0) → T at (-1, 3): z depth, W/E lateral.
   0   .  .  E  .  .  .
       0  1  2  3  4  5
```

T's E neighbour (2,3) is empty — but suppose P were 3 wide, x[2,4]: then (2,3)
is under P, P's nearest corner is (2,3), ring 3 < 4 → relax. With P at x[3,4]
nothing on (2,3) is pending. And if P's nearest corner were at ring **≥** T's
ring (e.g. P at z[4,5], nearest (3,4) ring 5), the neighbour's pending loc does
*not* reach past T's ring → `seam_scan - 1 >= dist` → the gate stands, T waits.
That is the ordinary "wait for the tile behind me", untouched.

### Example 9 — neighbour has no ground yet → no relaxation

If the lateral neighbour is still READY (its own gate has not passed), there
is nothing to relax against: the neighbour's ground must go down before T's
regardless. T waits and is re-queued when the neighbour's ground pass pushes
its span/footprint neighbours or when the neighbour completes.

## Condition 2 — a relaxed tile gets only its GROUND

Relaxing the gate lets T's *terrain* go down. It must not let T's own walls,
decor or scenery go down too: those sit *on* T, and a tall loc on the lateral
neighbour whose far part abuts T is still behind them.

### Example 10 — barrier beside a ledge (Xarpus (49,51))

```
  51   .  L  L  L  L  L  L  b  .      L: 6x5 at x[43,48] z[47,51], nearest
  50   .  L  L  L  L  L  L  .  .      corner (48,47) ring 2+4 = 6.
  49   .  L  L  L  L  L  L  .  .      b: 1x1 barrier piece on T = (49,51),
  48   .  L  L  L  L  L  L  .  .      ring 1+8 = 9. E = (50,43).
  47   .  L  L  L  L  L  L  .  .      T: (dx,dz) = (-1, 8) → z depth, W lateral.
  46   .  .  .  .  .  .  .  .  .
  43   .  .  .  .  .  .  .  E  .
      42 43 44 45 46 47 48 49 50
```

T's W neighbour (48,51) is under L, GROUND, L pending with nearest ring 6 < 9
→ the lateral gate relaxes → T's **terrain** goes down. Good: that is what a
QBD-style seam needs. But T also carries `b`, whose footprint is T alone, so
the old code drew `b` in the same pop — *before* L. L's row z=51 abuts T; its
tall far part then painted over `b`'s west edge (the "barrier edge" shot in
[painter_sweeps/defects](painter_sweeps/README.md)).

So a relaxed tile is flagged `seam_relaxed` and:

- its **3D features** (bridge underpass wall/scenery, far walls, ground decor,
  ground objects, wall decor — `bucket_emit_tile_features`) are **not**
  emitted in the ground pass;
- its **scenery pass** and **completion** are skipped (`continue`) until the
  plain reference gate passes (`bucket_far_neighbours_pending` — every far
  neighbour DONE or span-shared-with-ground);
- when that gate finally passes, the deferred features are emitted first,
  then scenery, then near walls / DONE — the exact order world3d's front pass
  produces.

Who re-queues T? The neighbour's completion pushes its inward neighbours (T is
one), and a loc drawn on T's footprint pushes T too. T is re-popped at its own
ring; the READY block is skipped (T is GROUND), the re-check runs, and it
proceeds.

In the example: T's terrain at ring 9 (before L — fine, terrain is flat and
beside L), then L at ring 6, then (48,51) completes and pushes T, then `b`.
Identical to world3d.

### Example 11 — QBD seam with Condition 2

Back to Example 3: the seam column tiles are under W. Each relaxed (3,z) puts
its ground down at ring z, is flagged, and defers its scenery — which is W
itself. W is released at its nearest footprint tile, (3,2), ring 2. By the time
(3,3) and (3,2) pop, B's nearest ring (3) is no longer *less than* theirs, so
the exception does not fire for them: they wait for (4,3)/(4,2) to be DONE,
i.e. for B. Then their ground goes down, W is released, W draws after B. Order:
`column floor rings 7..4 (relaxed), B at ring 3, (3,3) and (3,2) floor, W`.
The floor is under both locs; B and W are side by side. No strip — and the two
nearest seam tiles following B is the correct order (they are nearer than B's
release ring), which is what the unit test allows.

## Why both conditions are needed (mutation evidence)

| variant | QBD seam test | loc-behind test (Example 4 as a flat scene) |
|---|---|---|
| any axis, no hold (original) | pass | **fail** (10/144 floor tiles before the ledge) |
| lateral only, no hold | pass | pass — but Xarpus barrier edge wrong (Example 10), 757 px |
| any axis + hold | pass | **fail** by 1 tile (Xarpus happened to render right — scene luck) |
| lateral + hold (current) | pass | pass; 0 px vs world3d in every view tried |

## The rule in one place

```
ground gate for T, per far neighbour N on direction d:
    if N is DONE                                     → pass
    if N is GROUND and T.spans has d                 → pass   (reference span exception)
    if d is LATERAL for T  (W/E when |dz|>|dx|, N/S when |dx|>|dz|; never on a tie)
       and N is GROUND (not READY)
       and N has no wall / wall decor
       and N has pending scenery
       and max over N's pending scenery of nearest-corner ring  <  ring(T)
                                                     → pass, mark T seam_relaxed
    else                                             → T waits (re-queued by N's completion)

ground pass for T:
    emit terrain (and bridge underpass terrain)
    if not seam_relaxed: emit 3D features (far walls, decor, objects, underpass wall/scenery)
    step = GROUND; push span neighbours
    if seam_relaxed: stop here (next pop)

scenery pass / completion for T:
    if seam_relaxed:
        if any far neighbour fails the REFERENCE gate → wait
        clear seam_relaxed; emit the deferred 3D features
    ... scenery, raised items, near walls, DONE as before
```

## Cost

The lateral test is two compares on values the pop already has. The pending
scan of a neighbour is memoized per paint in `TilePaint.seam_scan` (elements
only leave the pending set as they draw, so the cached max is a sound upper
bound — a stale value can only delay a relaxation, never grant one wrongly).
A relaxed tile costs one extra pop. Measured: `scripts/painter/c/fuzz_real …
bench` ratio bucket/world3d 0.77–0.85 (old any-axis rule 0.75–0.82, same
machine, same session); live ToB Xarpus paint stage p50 ≈ 221 µs vs world3d
≈ 260 µs (old rule ≈ 211 µs). See `painter_bucket_vs_world3d.md`.
