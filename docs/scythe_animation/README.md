# The scythe of vitur's red sweep

The red arc that trails the blade — what it is, what was wrong with it, and
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
| copies used | one, `_west_red` | four, picked by direction, coloured by ornament |
| graphic's axis | **RADIAL** — through the player | **TANGENT** — across the swing |
| where it sits | straddling the player's own tile | centred 133 units ahead, on the next tile |
| colour | red (luminance 64) | red; sanguine dark red, holy blue |
| height / delay | 100 / 16 | 96 / **20** — NR's height; the delay measured from footage |

`before/south_top.png` and `fixed/south_top.png` are the same swing, one cell
per client cycle, straight down. The before sheet has a bright red crescent
standing on end through the player; the after sheet has a red one lying
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

### Checked against footage (2026-10-17)

Twenty-odd real swings in [YouTube DwfCPT6aiAM](https://www.youtube.com/watch?v=DwfCPT6aiAM)
and [IcQRgbVybW8](https://www.youtube.com/watch?v=IcQRgbVybW8), read with
the in-game compass for the world heading, compared with this harness's sheets
at the same screen-relative facing:

- **Copy, side, curve and growth match** in every swing where the player was
  already fighting the target: the arc stands on the target's side with its
  hollow toward the player, and grows from the same end the harness's does
  (screen-right targets: bottom first, upward; screen-left: top first,
  downward; away from the camera: right to left over the head). World
  headings covered: north (Kalphite Queen, compass N to the right; Giant
  Mole, N down), east (General Graardor, N up), south (Dagannoth Supreme,
  N down). No clip with a readable compass shows a clean west swing.
- **The real game follows the player's facing, not the target.** The two
  swings where the arc stood on the side *away* from the boss — the first
  swing on a respawned King Black Dragon (140 s) and the first swing after
  arriving at Dagannoth Supreme (544 s) — are both the opening swing of an
  engagement, before the player had turned. Every later swing points at the
  target. This rule, like NR's, aims at the target every time, so it differs
  on that opening swing only. Matching it needs the engine to expose the
  player's server-side facing; content has setters (`facesquare`) and no
  getter.
- **Not testable from footage:** which cardinal a diagonal snaps to and the
  even-size `+128` bias. They need the player's exact tile against the
  target's footprint, which no clip shows reliably — an RSProx capture would.

## Colour: red, and the ornaments' recolours

The plain scythe plays the **red** family, as NR does. The cache ships two more
recolours of the *same four models* (4003–4006), differing only by the record's
own `recol1s=960 recol1d=…`:

| family | spotanims | `recol1d` | scythe |
|---|---|---|---|
| red | 478 / 506 / 1172 / 1231 | (model's own) | Scythe of Vitur |
| dark red | 1891–1894 | 924 | Sanguine (`scythe_of_vitur_bl`) |
| blue | 1895–1898 | 39896 | Holy (`scythe_of_vitur_or`) |

No source names the owners — the cache and RuneLite call all of them
`dragon_halberd_special_*`, NR predates the ornaments, Blert records no
graphics. The ids are the evidence: dark red and blue were added together,
directly before the holy sanguinesti staff's `sanguinesti_staff_*_justiciar`
(1899–1902), in the Theatre of Blood: New Modes update of 3 June 2021 that also
released both ornament kits. Dark red for blood, blue for Saradomin.

Until 2026-10-17 every scythe played dark red, a stated choice rather than a
decode. `[proc,scythe_of_vitur_sweep_direction]` now answers in red and
`[proc,scythe_of_vitur_sweep_recolour]` swaps the copy for an ornamented scythe;
`::gearrun` asserts both.

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
tools/scythe_animation.sh fixed 96 20 red         # the plain scythe
```

The fourth argument picks the family: `darkred` draws the sanguine scythe's,
`blue` the holy scythe's.

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

## Timing: 20 cycles after the swing, measured from the real game

`spotanim_map(gfx, tile, 96, ^scythe_of_vitur_sweep_delay)`, and that constant is
**20 client cycles**. In the real game the arc lights as the blade crosses the
front — about 2 cycles after the strike (seq 8056's largest pose change, cycle
31) and one frame before the hit's splat — and grows while the blade sweeps.
The arc's first lit face is glow frame 4, cycle 10 of seq 1204, so the delay is
the strike's 31, plus ~2, minus 10 ≈ 20.

| source | anchor | delay |
|---|---|---|
| wiki [`Scythe_of_Vitur_attack.gif`](https://oldschool.runescape.wiki/images/Scythe_of_Vitur_attack.gif), plain scythe | arc's rise (20 / 40 / 70 / 100 % of peak area) against the swing's start, GIF clock scaled by its 2.83 s swing period | 20–22 |
| [YouTube IcQRgbVybW8](https://www.youtube.com/watch?v=IcQRgbVybW8) at 30 fps, holy swing at 63 s | first arc sliver 1–2 frames after the strike | 23–24 |
| same video, sanguine swing at 25 s | first arc sliver one frame before the hitsplat, which lands a tick into the swing | ~18 |
| [YouTube DwfCPT6aiAM](https://www.youtube.com/watch?v=DwfCPT6aiAM) (60 fps, captured at ~50 fps), Kalphite Queen swings at 585.4 s and 591.0 s | first arc frame against the swing's first pose change, static camera | 20.0, 19.2 |
| same video, KQ swing at 593.0 s | arc's jump to its 3→9-face stage against the swing's start | ~20.5 |

The harness's own blade-tracking sweep (`report_<facing>.txt`, "delay sweep")
independently puts its best fit at 16–20.

Anchoring on a hitsplat is weaker than anchoring on the swing: the hitsplat
mask carries its own delay field, so a splat need not land exactly a tick in.
One Dagannoth Supreme swing in the same video, read that way, gives ~28; read
against the pose it is consistent with the rest.

What footage cannot settle is the exact integer — every measurement here is
±2 cycles — nor the height (96 is NR's; no clip has a camera angle that pins
it). Both are values the server sends in the MAP_ANIM packet, so an
[RSProx](https://github.com/blurite/rsprox) recording of one real swing would
read them off directly. The beta-world dump on Rune-Server
([thread 697435](https://rune-server.org/threads/osrs-weapon-anims-gfx-from-osrs-beta-world.697435/))
confirms the mechanism — "gfx on entity tile: one of these (it varies): 506,
1231, 478, 1172" — but records no delay or height.

Before 2026-10-17 this was 30, one server tick, from reading Near-Reality:
`ScytheOfViturCombat` wraps its graphic in `WorldTasksManager.schedule(...)`,
which drains before player processing, so NR's graphic lands a tick after its
animation. That is NR's behaviour, not the game's: it lights the arc ~9 cycles
after the strike, with the blade already past. NR's literal delay 0 lights it
during the wind-up.

### Colour, measured

The GIF's arc, with the grass behind it solved out at the glow's alpha, is
≈ (247, 129, 117); ours at the client's default brightness 0.8 is (248, 97, 89),
which at brightness 0.6 (the brightest setting) is (250, 124, 116). The GIF was
recorded at maximum brightness; the red family is the exact colour. The same
video's holy arc measures (203, 223, 253) — the saturated `_blue` family, not
the saturation-0 `_white` one — and its sanguine arc (134, 24, 8), `_darkred`.

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
