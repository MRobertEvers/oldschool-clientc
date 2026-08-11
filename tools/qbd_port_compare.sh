#!/usr/bin/env bash
#
# Two ways to draw the same dragon, in one image.
#
#   1. priorities   BASELINE. The model whose face render priorities were
#                   re-authored for a painter's-algorithm renderer
#                   (docs/rs2012_qbd_priorities). Ordinary depth sort, the
#                   authored priorities honoured.
#
#   2. zbuffer      The SAME model with its priorities thrown away and the model
#                   opting into TORIDRAW_MODEL_FLAG_ZBUFFER instead, so it
#                   resolves itself per pixel. Ignoring the priorities is the
#                   point: honouring them would leave the depth test nothing to
#                   do and the picture would prove nothing. If the z-buffer
#                   works, this lands close to panel 1 without any of the
#                   authoring that produced it.
#
# The third panel this harness used to draw - a port through per-face HD/SD
# material kernels - is gone with the kernels themselves. See the README.
#
# Nothing here writes the lane, packs a cache or touches the client.
#
#   tools/qbd_port_compare.sh                  # every form
#   FORMS=default tools/qbd_port_compare.sh
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"

TREE="${TREE:-OSRS-Content/osrs239-content}"
LANE="${LANE:-$TREE/models/ported/rs2012_qbd_td}"
PRIO="${PRIO:-docs/rs2012_qbd_priorities/run}"
OUT="${OUT:-docs/qbd_port_compare}"
BIN="${BIN:-src/build_win64_opt}"
VIEW="${VIEW:-$BIN/rs2012_model_view.exe}"
FORMS="${FORMS:-default worm soul}"

[ -x "$VIEW" ] || VIEW="$BIN/rs2012_model_view"
[ -x "$VIEW" ] || { echo "no rs2012_model_view; make -C src rs2012-model-view" >&2; exit 1; }
mkdir -p "$OUT/images"

# model1 (+model2) per form, as the client merges them.
models_for() {
    case "$1" in
        default) echo "70260 69766" ;;
        worm)    echo "69765" ;;
        soul)    echo "70761" ;;
        *) echo "qbd_port_compare: unknown form $1" >&2; exit 2 ;;
    esac
}

# Framed on the head for `default` (where the sort does its work) and on the
# whole model otherwise. Same numbers the priorities sheets use, so the two sets
# of evidence line up.
view_args_for() {
    case "$1" in
        default) echo "--angles 3 --yaw0 1300 --pitch 300 --tile 420 --focus -31,-243,261 --radius 270" ;;
        *)       echo "--angles 3 --yaw0 512 --pitch 300 --tile 380" ;;
    esac
}

# render <form> <label> <dir> [extra viewer args...]
render() {
    local form="$1" label="$2" dir="$3"; shift 3
    local args=() m
    for m in $(models_for "$form"); do
        local path="$dir/rs2012_model_$m.ob3"
        [ -f "$path" ] || { echo "  - $label: no $path, skipped"; return 1; }
        args+=(--model "$path")
    done
    # shellcheck disable=SC2046
    "$VIEW" "${args[@]}" --lane-textures "$TREE" $(view_args_for "$form") "$@" \
        --bg 202430 --out "$OUT/images/${form}_${label}.bmp" >/dev/null 2>&1 ||
        { echo "  ! $label failed"; return 1; }
    echo "  $label"
}

for form in $FORMS; do
    echo "== $form"
    render "$form" priorities "$PRIO"                                   || true
    render "$form" zbuffer    "$PRIO" --ignore-priorities --zbuffer     || true

    # Kept out of the sheet but rendered anyway: the lane as the client draws
    # it, which is the white-and-black dragon the README explains. Evidence,
    # not a third port.
    render "$form" lane "$LANE" >/dev/null 2>&1 || true
done

python3 tools/qbd_port_compare_sheets.py --dir "$OUT/images" --forms "$FORMS"
echo "sheets in $OUT/images"
