#!/bin/bash
#
# scythe_animation.sh — the scythe's swing and its dark red streak, from every
# facing, measured and drawn.
#
# The sweep is a TILE graphic: one `spotanim_map` on the tile a step toward the
# target, with the compass copy that matches that step, which is what
# Near-Reality's `ScytheOfViturCombat.java` does and what
# `scythe_of_vitur.rs2` now does. So the direction is not decoration here —
# each facing plays a DIFFERENT asset on a DIFFERENT tile, and the eight runs
# below are eight different things rather than one thing seen from eight
# angles.
#
#   tools/scythe_animation.sh fixed 96 0 darkred
#   tools/scythe_animation.sh nr    96 0 red        # NR's own colour
#
# Arguments, all positional and all required:
#   <name>     subdirectory under docs/scythe_animation/
#   <height>   spotanim_map's third argument
#   <delay>    spotanim_map's fourth argument, in client cycles
#   <family>   `darkred` (1891-1894, what ships) or `red` (478/506/1172/1231,
#              NR's). Same four models either way; the spotanim record's own
#              recolour is the whole difference
#
# To measure the OLD player-attached arrangement for comparison, call ev_swing
# directly without --tile; see docs/scythe_animation/README.md.
#
# The eight facings are the four cardinals plus the four diagonals. The
# diagonals are the interesting half: a player fighting anything bigger than
# one tile faces its centre, which is usually not a cardinal, and the rule has
# to snap. Which cardinal each diagonal snaps to is NR's rule, transcribed
# below and in the script.
set -e

if [ $# -ne 4 ]; then
    sed -n '2,32p' "$0"
    exit 2
fi

name=$1
height=$2
delay=$3
family=$4

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

swing=tools/entity_viewer/ev_swing
[ -x "$swing" ] || make -C tools/entity_viewer ev_swing

case $family in
    darkred) id_south=1891; id_north=1892; id_east=1893; id_west=1894 ;;
    red)     id_south=478;  id_north=506;  id_east=1172; id_west=1231 ;;
    *) echo "family must be darkred or red" >&2; exit 2 ;;
esac

out=docs/scythe_animation/$name
mkdir -p "$out"
rm -f "$out"/*.bmp "$out"/*.csv "$out"/*.txt

combined=$out/frames.csv
first=1

# facing:yaw:snapped-cardinal
#
# The snap is `scythe_of_vitur_sweep`'s own rule — nearest cardinal to the
# target, with the exact 45-degree lines going north-east -> EAST,
# north-west -> WEST and both southern diagonals -> SOUTH, which is where NR's
# float truncation lands. A player faces the target, so the player's yaw IS the
# direction being snapped.
for row in south:0:south southwest:256:south west:512:west northwest:768:west \
           north:1024:north northeast:1280:east east:1536:east southeast:1792:south
do
    facing=$(echo "$row" | cut -d: -f1)
    yaw=$(echo "$row" | cut -d: -f2)
    snap=$(echo "$row" | cut -d: -f3)

    case $snap in
        south) spot=$id_south; model=south; tx=0;  tz=-1 ;;
        north) spot=$id_north; model=north; tx=0;  tz=1  ;;
        east)  spot=$id_east;  model=east;  tx=1;  tz=0  ;;
        west)  spot=$id_west;  model=west;  tx=-1; tz=0  ;;
    esac
    arc=OSRS-Content/osrs239-content/models/spot/dragon_halberd_special_${model}_red.model
    [ -f "$arc" ] || { echo "no such model: $arc" >&2; exit 1; }

    "$swing" --rev osrs239 cache.osrs239 \
        --spotanim "$spot" --arc-model "$arc" \
        --height "$height" --delay "$delay" \
        --tile "$tx" "$tz" \
        --yaw "$yaw" --facing "$facing" \
        --rows 0 --columns 11 --side 160 --pitch 200 \
        --csv "$out/frames_$facing.csv" \
        --out "$out/$facing" > "$out/report_$facing.txt"

    # One sheet with every facing in it. The header comes from the first file
    # only — `tail -n +2` on the rest — so the result loads as one table rather
    # than as eight tables glued together.
    if [ $first -eq 1 ]; then
        cat "$out/frames_$facing.csv" > "$combined"
        first=0
    else
        tail -n +2 "$out/frames_$facing.csv" >> "$combined"
    fi
done

# ---- BMPs are the harness's output; PNGs are what a repo should carry ------
#
# `bmp_write_file` is what ev_swing has, and a 77-cell uncompressed sheet is
# about 6 MB. Eight facings of three sheets each came to 137 MB, which is not
# something to commit. PNG is lossless, so nothing is given up but the bytes.
# Where `sips` is missing (anything but macOS) the BMPs simply stay, and the
# doc says which it is looking at rather than assuming.
if command -v sips > /dev/null 2>&1; then
    for b in "$out"/*.bmp; do
        sips -s format png "$b" --out "${b%.bmp}.png" > /dev/null 2>&1 && rm -f "$b"
    done
fi

# ---- the check ------------------------------------------------------------
#
# Unlike the old attached arrangement, the eight runs are NOT expected to
# produce identical reports: the report is in the player's LOCAL frame, and a
# player turned 45 degrees sees the same tile at different local coordinates.
# Comparing those was the first check written here and it failed on all four
# diagonals for that reason alone.
#
# The claim worth checking is in WORLD space, and the CSV carries it: a
# diagonal facing snaps to a cardinal, so it plays the SAME copy on the SAME
# tile, and the lit graphic's world track must therefore be identical to that
# cardinal's, cycle for cycle. The blade's must NOT be — the player really is
# turned — which is why only the arc columns are compared.
#
# Tolerance is 3 units out of the 128 in a tile, and it is quantisation this
# harness introduces rather than anything the game does. To hold a world-fixed
# graphic inside a player-attached merge, the mesh is pre-turned by the inverse
# of the yaw through toridraw's 16.16 integer sin/cos and truncated per vertex,
# then translated by a rounded offset. At 45 degrees that costs about 1.6 units;
# at 0 and 90 it costs nothing. The real client never pays it — a `spotanim_map`
# is not turned at all — so this is the measuring instrument's error, not the
# thing being measured. 1 unit was the first tolerance tried and it failed only
# the south-east diagonal, on exactly that.
arc_track() { awk -F, 'NR>1 && $19!="" {printf "%d %d %.0f %.0f\n", $3, $12, $19, $20}' \
                  "$out/frames_$1.csv"; }
fail=
for row in southwest:south northwest:west northeast:east southeast:south; do
    diag=${row%%:*}
    card=${row##*:}
    if ! paste -d' ' <(arc_track "$diag") <(arc_track "$card") | awk '
        { if ($1 != $5 || $2 != $6 ||
              ($3-$7) > 3 || ($7-$3) > 3 || ($4-$8) > 3 || ($8-$4) > 3) bad++ }
        END { exit (bad > 0) }'
    then
        fail="$fail $diag/$card"
    fi
done
if [ -n "$fail" ]; then
    echo "FAIL: a diagonal's arc does not stand where its cardinal's does:$fail" >&2
    echo "      The snap should make them the same asset on the same tile." >&2
else
    echo "snap check: PASS — every diagonal puts the lit arc on exactly the world"
    echo "  track its cardinal does, cycle for cycle, while the blade's track"
    echo "  differs because the player really is turned"
fi

echo "wrote $out/"
echo "  frames.csv            every facing, one row per client cycle"
echo "  frames_<facing>.csv   one facing each"
echo "  <facing>_top.png      straight down, one cell per cycle, markers on"
echo "  <facing>_side.png     the game camera"
echo "  <facing>_plot.png     overhead trace on a one-tile grid, world-aligned"
echo "  report_<facing>.txt   the measurement, per facing"
