> **SUPERSEDED — read `docs/scythe_animation/README.md` instead.**
>
> This page diagnosed the scythe's arc while it was played as a **player-
> attached** graphic (`spotanim_pl`), and its recommendation — attach
> `dragon_halberd_special_south_red` instead of `_west_red`, and shift the
> asset — was the best fix *within that mechanism*.
>
> The mechanism itself was the defect. Near-Reality's `ScytheOfViturCombat`
> draws the sweep as a **tile** graphic (`spotanim_map`) on the tile a step
> toward the target, choosing the compass copy that matches, and
> `scythe_of_vitur.rs2` does that now. The `-64` shift this page's sibling notes
> describe has been reverted; the four copies must be identical up to rotation
> for a tile graphic, and an asset edit is no longer a lever anyone should pull.
>
> What stays true here, and is why the page is kept: the measurements of the
> **attached** case. `specs/pvm_dragon_halberd.rs2` still plays `_west_red` that
> way and still has every defect described below.

# Scythe of vitur (charged) — the swing, from each direction

Spritesheets of `scythe_of_vitur_attack` (sequence 8056) on a player wearing the
charged scythe (obj 22325), with the red slash attached the way the client
attaches one.

Every sheet is **77 cells, one per client cycle**, 11 across, no sampling — the
whole swing, nothing dropped.

## The four facings

| file | facing | yaw |
|---|---|---|
| `facing_south.png` | south | 0 |
| `facing_west.png` | west | 512 |
| `facing_north.png` | north | 1024 |
| `facing_east.png` | east | 1536 |

Those are `world_cycle.c`'s own numbers: a step with z decreasing gets yaw 0,
z increasing 1024, x decreasing 512, x increasing 1536.

`corrected_facing_*.png` are the same four with the fix described below.

**The graphic's placement is identical in all four.** That is not a shortcut in
how these were made — it is the mechanism. `spotanim_pl` does not put a graphic
in the world; `app_world_sync_one_entity_spotanim` (`src/app.c`) poses it,
strips its labels, lifts it by the spotanim height and **merges it into the
player's own model**. The merged mesh is what the scene draws, at the player's
yaw, so the graphic turns with the player and its position in the player's local
space never changes. The four sheets differ only in the camera's relationship to
that fixed arrangement.

Two things follow, and they are the whole diagnosis:

- Choosing a different graphic per facing would rotate it **twice**. Exactly one
  copy of the asset can be correct, and it is correct at every facing.
- The body's sequence cannot move a single one of the graphic's vertices,
  because its labels are gone. A player-attached slash physically cannot track
  the blade; where it sits is a property of the model's vertices alone.

## What is wrong today

The asset exists as four copies — `dragon_halberd_special_{north,south,east,
west}_red`, spotanims 506 / 478 / 1172 / 1231 — the same 24-vertex mesh at four
rotations. `scythe_of_vitur.rs2` plays `_west_red`.

`topdown_*.png` are the four copies seen straight down, where placement reads as
a distance rather than being guessed from foreshortening. **Down the image is
the direction the player faces.**

A slash lies **tangent**: long axis across the facing, body out in front, the
way a blade passes. And its lit segment must travel the way the blade travels —
a copy a half turn out lies in the same place and covers the same ground while
playing the streak **backwards**.

| copy | long axis | centre | streak direction | |
|---|---|---|---|---|
| north 506 | tangent | 5 behind | **opposed** | ✗ |
| **south 478** | **tangent** | **5 in front** | **same way** | **✓** |
| east 1172 | **radial** | 5 left | **opposed** | ✗ |
| west 1231 *(shipped)* | **radial** | 69 right | same way | ✗ |

Measured, not eyeballed — the axis is the principal direction of the graphic's
own vertices, and it is corroborated by the raw meshes:
`tools/shift_halberd_arc.py --model spot/dragon_halberd_special_<n>_red --show`
gives north/south a long **X** span and east/west a long **Z** span.

Compare `topdown_south.png` with `topdown_west.png`: the same crescent a quarter
turn apart, one lying across the swing and one running front-to-back through the
player. `plot_shipped.png` and `plot_corrected.png` are overhead traces on a
one-tile grid — cyan the blade, green the lit graphic, olive ties the per-cycle
gap.

The naming is consistent with this rather than accidental: yaw 0 faces **south**,
so the south copy is the one authored for the orientation a merged graphic
inherits. The other three exist for `spotanim_map`, where a tile has no facing.

`scythe_of_vitur.rs2` predicted exactly this in its own comment — *"If the arc
ever renders rotated a quarter turn from the swing, the fix is one of the other
three names here — not a direction test."*

## The fix

```
spotanim_pl(dragon_halberd_special_south_red, 117, 16)
python3 tools/shift_halberd_arc.py --model spot/dragon_halberd_special_south_red --dz -90
```

Every residual converges: across-axis −0.2 units, height stable at 117, and the
delay sweep picks **16 independently** — the shipped delay was already right.
The −64 x shift on `..._west_red.model` becomes unused.

**Not applied.** It edits an exported asset, and spotanim 1231 is also played by
`pvm_dragon_halberd.rs2`, which has the same defect — whether to move both is a
call for whoever owns that file.

## Regenerating

`tools/entity_viewer/ev_swing`; see `tools/entity_viewer/README.md`.

```sh
M=OSRS-Content/osrs239-content/models/spot
for d in south:0 west:512 north:1024 east:1536; do
  n=${d%%:*}; y=${d##*:}
  tools/entity_viewer/ev_swing --rev osrs239 cache.osrs239 \
    --spotanim 1231 --arc-model $M/dragon_halberd_special_west_red.model \
    --height 100 --delay 16 --yaw $y --pitch 200 \
    --rows 0 --columns 11 --side 160 --no-markers --out /tmp/ship_$n
done
```

`--rows 0` is what gives one cell per cycle; the default caps the sheet at four
rows and samples, which is right for a quick look and wrong for a record of the
animation. The `_side` output is the game-camera sheet, `_top` is straight down.
Drop `--no-markers` (at yaw 0 only) to get the measured points drawn on — a cyan
cross on the blade, green on the lit graphic, white on the player's origin.
