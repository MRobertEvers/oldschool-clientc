# The scythe of vitur's red sweep

The dark red arc that trails the blade — what it is, what was wrong with it, and
the harness that says so in numbers rather than in screenshots.

The reference is **Near-Reality**'s `ScytheOfViturCombat.java`
(`RSPS-NEAR-REALITY/near-reality-server-main/core/src/main/java/com/zenyte/game/
world/entity/player/action/combat/melee/`). Everything below was checked against
it rather than reasoned from first principles, and where this tree differs it
says so and says by how much.

## What was wrong: the wrong mechanism, not the wrong tuning

`scythe_of_vitur.rs2` played `spotanim_pl` — a graphic **attached to the
player**. NR plays `World.sendGraphics(gfx, tile)`, a graphic **on a tile**.
Those are not two settings of one thing.

An attached graphic is not drawn beside the player. `app_world_sync_one_entity_
spotanim` (`src/app.c`) poses it, strips its labels, lifts it by the height and
`ToriDraw_ModelMerge`s it **into the player's own model**; the scene then draws
that merged mesh at the player's yaw. So the copy it plays gets turned a second
time, by the player. It played `dragon_halberd_special_west_red`, whose long
axis runs **front-to-back through the player** — and turning that by the yaw
leaves it front-to-back through the player at every facing.

| | before | after |
|---|---|---|
| mechanism | `spotanim_pl` — merged into the player's model | `spotanim_map` — a tile graphic, NR's |
| copies used | one, `_west_red` | four, `_darkred`, picked by direction |
| graphic's axis | **RADIAL** — through the player | **TANGENT** — across the swing |
| where it sits | straddling the player's own tile | centred 133 units ahead, on the next tile |
| colour | red (luminance 64) | dark red (luminance 28) |
| height / delay | 100 / 16 | 96 / **30** — NR's height, and NR's one-tick lag |

`before/south_top.png` and `fixed/south_top.png` are the same swing, one cell
per client cycle, straight down. The before sheet has a bright red crescent
standing on end through the player; the after sheet has a dark red one lying
across the ground in front of them.

## What Near-Reality does

One MAP_ANIM on the tile **one step toward the target**, with the compass copy
that matches that step:

| facing | copy sent (cache name) | NR's constant | tile |
|---|---|---|---|
| west | `..._west_red` (1231) | `SWEEP_DRAGON_WEST_GFX` | (x−1, z) |
| north | `..._north_red` (506) | `SWEEP_DRAGON_SOUTH_GFX` | (x, z+1) |
| east | `..._east_red` (1172) | `SWEEP_DRAGON_EAST_GFX` | (x+1, z) |
| south | `..._south_red` (478) | `SWEEP_DRAGON_NORTH_GFX` | (x, z−1) |

NR's own constant *names* have north and south swapped against the cache's. Read
by cache name it is a clean rule — **facing D takes the copy named D, on the
tile one step D** — and that is how `scythe_of_vitur.rs2` states it.

### The direction rule, and how closely it is matched

NR picks the direction with
`(int)(atan2(-dx, -dy) * 325.949) + (size even ? 128 : 0) & 2047`, bucketed at
256 / 768 / 1280 / 1792. This tree has no `atan2` and does not need one: the same
answer falls out of comparing `|dx|` against `|dz|` on **doubled** offsets (so an
even footprint's half-tile centre stays integral), plus a 22.5° rotation for even
sizes to reproduce the `+128`.

That equivalence is measured, not asserted. Against a transcription of NR's own
expression:

| positions | cases | disagreements |
|---|---|---|
| every tile **adjacent to a footprint**, sizes 1–12 | 312 | **0** |
| every tile **within 2** of a footprint, sizes 1–12 | 1080 | **0** |
| every offset within ±14 tiles, sizes 1–8 | 6524 | 6 |

The six are all at 13–14 tiles' distance, where NR's angle truncates exactly onto
a bucket edge and its `d > 256` test is strict. No melee swing is made from
there.

The `+128` even-size bias is real and load-bearing: it moves 2 of the 8 tiles
adjacent to a 2×2 and 4 of the 16 around a 4×4. `size 2, d=(0,+1)` is the one to
watch — the target is twice as far north as east and NR still draws the **east**
arc. `::gearrun` asserts that case, and disabling the bias in the implementation
makes it fail (checked, see below).

## Dark red rather than red

NR plays the **red** family. The cache also ships
`dragon_halberd_special_*_darkred` — spotanims **1891–1894**, the *same four
models* (4003–4006), differing only by the record's own `recol1s=960
recol1d=924`. Both are hue 0, saturation 7; luminance goes **64 → 28**. Literally
the same red, much darker.

This is the one deliberate departure from NR, and it was asked for. Swapping the
four names in `[proc,scythe_of_vitur_sweep_direction]` to `_red` is the whole of
going back; `tools/scythe_animation.sh <name> 96 0 red` draws NR's colour for
comparison.

## The asset edit that was removed

`models/spot/dragon_halberd_special_west_red.model` carried a hand-applied
**−64 on X**, added to shove the attached arc off the player's centre line. With
a tile graphic that is a defect, not a fix: the four copies must be identical up
to rotation or the west facing sits half a tile off its three siblings. It has
been reverted to the cache's own geometry, and
`3rd/rscache/tools/port_lostcity/scythe_of_vitur.ini` — which used to instruct
re-applying it — now records why there is nothing to re-apply.

All four now read a centre within 2 units of the origin:

```
python3 tools/shift_halberd_arc.py --model spot/dragon_halberd_special_west_red --show
```

`specs/pvm_dragon_halberd.rs2` still plays `_west_red` the old attached way and
inherits the revert. Its arc has the same quarter-turn defect the scythe's had —
**measured here, not fixed**; it is a different weapon's file.

## The harness

```sh
tools/scythe_animation.sh fixed 96 30 darkred     # what ships
```

The fourth argument takes `red` instead of `darkred` to draw NR's own colour
through the same mechanism, if a side-by-side is ever wanted.

One run per player facing — the four cardinals **and the four diagonals**,
because a player fighting anything bigger than one tile faces its centre and the
rule has to snap. Each writes, into `docs/scythe_animation/<name>/`:

| file | what it is |
|---|---|
| `<facing>_top.png` | straight down, **one cell per client cycle**, no sampling. Cyan cross = the blade, green = the lit part of the graphic, white = the player's origin |
| `<facing>_side.png` | the same cells from the game camera, for height |
| `<facing>_plot.png` | one overhead trace on a one-tile grid: the blade's path, the lit graphic's path, and a tie line per cycle whose length *is* the gap |
| `frames_<facing>.csv` | one row per cycle — both the player-local and the world-rotated coordinates of both |
| `frames.csv` | all eight facings stacked, one table |
| `report_<facing>.txt` | the measurement: the graphic's principal axis, a delay sweep, the residual offset |

It ends with a check rather than a claim:

```
snap check: PASS — every diagonal puts the lit arc on exactly the world
  track its cardinal does, cycle for cycle, while the blade's track
  differs because the player really is turned
```

That is the "regardless of the angle the player is facing" property, stated as
something falsifiable: after the snap a diagonal plays the same copy on the same
tile as its cardinal, so the arc's **world** track must coincide cycle-for-cycle
while the blade's must not.

### Reproducing the old arrangement

The attached case needs no `--tile`; `before/` was made with:

```sh
M=OSRS-Content/osrs239-content/models/spot
for row in south:0 west:512 north:1024 east:1536; do
  f=${row%%:*}; y=${row##*:}
  tools/entity_viewer/ev_swing --rev osrs239 cache.osrs239 \
    --spotanim 1231 --arc-model $M/dragon_halberd_special_west_red.model \
    --height 100 --delay 16 --yaw $y --facing $f \
    --rows 0 --columns 11 --side 160 --pitch 200 \
    --csv before/frames_$f.csv --out before/$f > before/report_$f.txt
done
```

### What the harness had to grow

- **`--tile <dx> <dz>`** — a `spotanim_map` is world-fixed and never enters the
  player's model, but the harness has only one way to draw and measure two
  things at once, which is the merge. So the tile case is reproduced inside it:
  the graphic is pre-turned by the *inverse* of the player's yaw and offset by
  the tile, and the renderer's own turn by that yaw puts it back where the world
  says it stands. The inverse turn has to land **before** the lighting bake — RS
  lighting is baked per face from the geometry's orientation — so it is a
  parameter of `ev_build_spotanim_model` rather than something applied after.
- **`--csv` / `--facing`** — the per-cycle sheet, carrying both spaces.
- **markers at any facing** — they used to be drawn only at yaw 0 and dropped
  everywhere else, which left the three sheets that most needed checking with
  nothing on them to check against.

The one error the harness introduces and the game does not: the inverse turn
runs through toridraw's 16.16 integer sin/cos and truncates per vertex, costing
about 1.6 units at 45° and nothing at 0° and 90°. The snap check's tolerance is 3
units out of the 128 in a tile for that reason, and for no other.

## Timing: the sweep is one tick behind the swing

`spotanim_map(gfx, tile, 96, ^scythe_of_vitur_sweep_delay)`, and that constant is
**30 client cycles — one server tick**, not zero.

NR's literal is `new Graphics(506, 0, 96)`, i.e. delay 0, and shipping that fired
the sweep a tick early. The delay argument is not where NR's timing lives. In
`ScytheOfViturCombat.processAfterMovement`, `animate()` is called **inline**
while the graphic is wrapped in `WorldTasksManager.schedule(...)` — and
`WorldThread`'s tick runs `WorldTasksManager.processTasks()` (line 111) **before**
`player.processEntity()` (line 147). A task queued during player processing sits
in `pending` until the *next* tick's drain. NR's graphic is one tick behind its
animation by construction.

NR's own code corroborates it rather than leaving it to a reading of the tick
order: the hits scheduled inside that same task go out as
`delayHit(t, -1, hit)`, and that `-1` exists purely to cancel the task's tick and
put them back where `delayHit(0, hit)` would have landed. The graphic gets no
such compensation. Our hits already land on the swing tick — matching NR's
compensated `-1` — so only the graphic moved.

Measured, from `report_south.txt`, over the 77-cycle swing:

| | arc lit over | closest approach to the blade |
|---|---|---|
| delay 0 (NR's literal) | cycles 10–58 | 20 units |
| **delay 30 (one tick)** | cycles **40–76** | **4 units** |

The strike — the largest pose change in the swing — is at cycle 31.

## Checks

`::gearrun` (`gear/gear_selftest.rs2`) asserts the direction rule directly, on
fourteen cases taken from **NR's** expression rather than from this
implementation's output: the four cardinals, the four 45° lines of a 1×1, and
the tiles the even-size bias moves. Both the graphic **and** the tile step are
asserted, so a rule that draws the west arc on the east tile cannot pass.

```sh
make -C src torirsserver-scripts
TORIRSSERVER_GEARRUN=1 ./src/build_opt/torirsserver --selftest 2>&1 | grep -i gearrun
```

Negative control, run rather than assumed: changing `modulo($size, 2) = 0` to
`= 99` in the implementation — i.e. dropping NR's `+128` — produces

```
gearrun FAIL: scythe sweep size 2 d(-2,0) chose the wrong graphic
FAIL ::gearrun should report no failures
```

so the stanza does execute and the assertions do bite.
