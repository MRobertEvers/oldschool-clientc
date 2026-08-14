# Scythe of vitur — the charged swing and its red slash

The scythe's attack draws a red crescent alongside the swing. The asset is the
dragon halberd special's own graphic, reused by this weapon in the source game
and exported under its source name (see
`3rd/rscache/tools/port_lostcity/scythe_of_vitur.ini`). It exists as **four
copies** — `dragon_halberd_special_{north,south,east,west}_red`, spotanims 506 /
478 / 1172 / 1231 — which are the same 24-vertex mesh at four rotations, all
centred on the origin.

`scythe_of_vitur.rs2` plays `_west_red`. These sheets are why that is the wrong
one.

## The sheets

Every sheet is the full `scythe_of_vitur_attack` swing (sequence 8056): **77
cells, one per client cycle**, 11 across, no sampling. The player wears the
charged scythe (obj 22325) and nothing else, and the graphic is merged in the
way the client merges one — posed, labels stripped, height-translated, combined
into the player's own model.

| | what it shows |
|---|---|
| `*_top.png` | straight down. The view where the graphic's placement in the player's local plane reads as a distance instead of being guessed from foreshortening. **Down the image is the direction the player faces.** |
| `*_side.png` | the game's own camera pitch, which is where height reads. |
| `*_plot.png` | not a spritesheet: an overhead trace on a one-tile grid — cyan is the blade, green is the lit part of the graphic, the olive ties are the gap at each cycle. |

| set | spotanim | what it is |
|---|---|---|
| `west_*` | 1231 | **what ships today**, including the hand −64 x shift on its model |
| `south_*` | 478 | the copy that is correct |
| `north_*` | 506 | |
| `east_*` | 1172 | |
| `corrected_*` | 478 | 478 plus the placement the measurement converges on |

The `west` set is drawn from the content tree's copy of the model, which carries
a hand-applied −64 x shift ([[spotanim-pl-has-no-lateral-offset]] in the session
notes; `tools/shift_halberd_arc.py --show`). The other three are unmodified
exports, so for them the content and the cache agree.

## What the four copies measure

Two properties decide which copy is right, and neither is a matter of taste. A
slash lies **tangent** — its long axis across the player's facing, its body out
in front, the way a blade passes. And its lit segment must travel the way the
blade travels; a copy a half turn out lies in exactly the same place and covers
exactly the same ground while playing the streak **backwards**.

| copy | axis | centre | streak direction | |
|---|---|---|---|---|
| north 506 | tangent | 5 behind | **opposed** | ✗ |
| **south 478** | **tangent** | **5 in front** | **same way** | **✓** |
| east 1172 | **radial** | 5 to the left | **opposed** | ✗ |
| west 1231 *(shipped)* | **radial** | 69 to the right | same way | ✗ |

Only `south` passes both. Compare `south_top.png` against `west_top.png`: the
same crescent, a quarter turn apart, one lying across the swing and one lying
front-to-back through the player.

The naming is consistent with this rather than accidental. A player at yaw 0
faces **south**, and a player-attached graphic inherits the player's yaw — the
client applies no rotation of its own at merge time — so the south copy is
correct at *every* facing. The compass name records the facing the copy was
authored for, and the other three exist for `spotanim_map`, where a tile has no
facing to inherit.

`scythe_of_vitur.rs2` predicted this exact symptom and this exact remedy in its
own comment: *"If the arc ever renders rotated a quarter turn from the swing,
the fix is one of the other three names here — not a direction test."* It does,
and it is.

## The corrected placement

`corrected_*` is spotanim 478 with a −90 z shift on its model and height 117.
Every residual converges: across-axis −0.2 units, height stable at 117, and the
delay sweep picks 16 **independently** — the shipped delay was already right.

```
spotanim_pl(dragon_halberd_special_south_red, 117, 16)
python3 tools/shift_halberd_arc.py --model spot/dragon_halberd_special_south_red --dz -90
```

**Not applied.** It edits an exported asset, and spotanim 1231 is also played by
`pvm_dragon_halberd.rs2`, which has the same defect — whether to move both is a
call for whoever owns that file.

## Regenerating

`tools/entity_viewer/ev_swing` (see `tools/entity_viewer/README.md`). Drop
`--no-markers` to get the measured points drawn on the top-down sheet — a cyan
cross on the blade, a green one on the lit graphic, white on the player's
origin.

```sh
M=OSRS-Content/osrs239-content/models/spot
for n in north south east west; do
  case $n in north) id=506;; south) id=478;; east) id=1172;; west) id=1231;; esac
  tools/entity_viewer/ev_swing --rev osrs239 cache.osrs239 \
    --spotanim $id --arc-model $M/dragon_halberd_special_${n}_red.model \
    --rows 0 --columns 11 --side 160 --no-markers \
    --out docs/scythe_of_vitur_charged/$n
done

tools/entity_viewer/ev_swing --rev osrs239 cache.osrs239 \
  --spotanim 478 --arc-model $M/dragon_halberd_special_south_red.model \
  --shift-z -90 --height 117 --delay 16 \
  --rows 0 --columns 11 --side 160 --no-markers \
  --out docs/scythe_of_vitur_charged/corrected
```

It writes BMP; these were converted with `sips -s format png`. `--rows 0` is
what gives one cell per cycle — the default caps the sheet at four rows and
samples, which is right for a quick look and wrong for a record of the
animation.
