#!/usr/bin/env bash
#
# Face-priority authoring loop for the RS2012 Queen Black Dragon lane.
#
# Builds the two tools, re-authors face render priorities for every QBD npc
# form into a SEPARATE output tree (the lane's .ob3 files are never written),
# renders before/after contact sheets, and prints the sort score for each form
# so an edit is judged by a number instead of by eye.
#
#   tools/rs2012_qbd_prio.sh                    # every form, default sampling
#   FORMS=default tools/rs2012_qbd_prio.sh      # just the one
#   OUT=/tmp/qbd tools/rs2012_qbd_prio.sh --views 32 --pitches 8
#
# Extra arguments are passed to rs2012_face_priorities, so the sampling
# density, the elevation range and --bulk-flex are all reachable from here.
#
# Outputs land in $OUT (default docs/rs2012_qbd_priorities/run), under docs
# rather than build/ because they are the record of what was authored and build/
# is not tracked -- a result nobody else can see is not a result:
#   rs2012_model_*.ob3      the re-authored models, alongside not over
#   <form>.report.txt       what the solve did, per form
#   <form>_before.png       the lane model as it ships today
#   <form>_after.png        the same geometry with authored bands
#   <form>_after_err.png    sort-error mask: red = a surface behind the truth
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"

LANE="${LANE:-OSRS-Content/osrs239-content/models/ported/rs2012_qbd_td}"
OUT="${OUT:-docs/rs2012_qbd_priorities/run}"
FORMS="${FORMS:-default crystal hardened soul worm}"

# Each form is the npc's model1 (+ model2 where it has one), because the client
# merges them before it draws and priorities have to be chosen across the merge.
models_for() {
    case "$1" in
        default)  echo "70260 69766" ;;   # rs2012_qbd_default / _sleeping
        crystal)  echo "70267 69766" ;;   # rs2012_qbd_crystal
        hardened) echo "70268 69766" ;;   # rs2012_qbd_hardened
        soul)     echo "70761" ;;         # rs2012_qbd_tortured_soul
        worm)     echo "69765" ;;         # rs2012_qbd_giant_worm
        *) echo "rs2012_qbd_prio: unknown form $1" >&2; exit 2 ;;
    esac
}

# The client's own camera pitch clamp (app.c: 128..383 of 2048) is what makes
# the sheet representative; scoring from underneath would report breakage no
# player can see.
# Framed on the head at 420px a tile, on a lit background. The whole model at
# 300px is a dark sliver on black — technically a render, unreadable as
# evidence, and the differences the bands make are all in the head anyway.
VIEW_ARGS="${VIEW_ARGS:---angles 3 --pitch 300 --tile 420 --yaw0 1300 --bg 202430}"
HEAD_FOCUS="${HEAD_FOCUS:---focus -31,-243,261 --radius 270}"

toolchain="$repo/toolchain/mingw64/bin"
if [ -d "$toolchain" ]; then
    PATH="$toolchain:$PATH"
    MAKE="${MAKE:-mingw32-make}"
else
    MAKE="${MAKE:-make}"
fi
export PATH

"$MAKE" -C src rs2012-model-view rs2012-face-priorities >/dev/null

for dir in src/build_win64 src/build; do
    [ -x "$dir/rs2012_model_view" ] && bin="$dir" && ext="" && break
    [ -x "$dir/rs2012_model_view.exe" ] && bin="$dir" && ext=".exe" && break
done
view="$bin/rs2012_model_view$ext"
prio="$bin/rs2012_face_priorities$ext"

mkdir -p "$OUT"

score() { "$@" 2>&1 | sed -n 's/^sort score: //p'; }

printf '%-10s %-34s %s\n' form before after
for form in $FORMS; do
    models="$(models_for "$form")"
    in_args=() out_args=() before=() after=()
    for m in $models; do
        in_args+=(--in "$LANE/rs2012_model_$m.ob3")
        out_args+=(--out "$OUT/rs2012_model_$m.ob3")
        before+=(--model "$LANE/rs2012_model_$m.ob3")
        after+=(--model "$OUT/rs2012_model_$m.ob3")
    done

    # --report always: the per-form file is the only record of what the solve
    # actually did, and a run whose reports are empty cannot be reviewed later.
    "$prio" "${in_args[@]}" "${out_args[@]}" --report "$@" >"$OUT/$form.report.txt"

    # Head framing for the three big forms only: the soul and the worm are small
    # models with no neck, and the head focus would frame empty space.
    focus=""
    case "$form" in default|crystal|hardened) focus="$HEAD_FOCUS" ;; esac

    b=$(score "$view" "${before[@]}" --out "$OUT/${form}_before.bmp" \
        --score-out "$OUT/${form}_before_err.bmp" $VIEW_ARGS $focus --score)
    a=$(score "$view" "${after[@]}" --out "$OUT/${form}_after.bmp" \
        --score-out "$OUT/${form}_after_err.bmp" $VIEW_ARGS $focus)
    printf '%-10s %-34s %s\n' "$form" "${b%%,*}" "${a%%,*}"
done

# BMP is what the raster writes; PNG is what a viewer (or an agent) can read.
python3 - "$OUT" <<'PY'
import sys, pathlib
try:
    from PIL import Image
except ImportError:
    sys.exit(0)
out = pathlib.Path(sys.argv[1])
for bmp in out.glob("*.bmp"):
    Image.open(bmp).save(bmp.with_suffix(".png"))
    bmp.unlink()   # the BMP is the renderer's native output; PNG is what ships
PY

echo "models and sheets in $OUT; per-form reports in $OUT/<form>.report.txt"
