#!/usr/bin/env bash
#
# Two ports of the same dragon, side by side.
#
# The lane has been through two independent pieces of porting work, and they fix
# different things:
#
#   priorities  docs/rs2012_qbd_priorities — re-authored FACE RENDER PRIORITIES.
#               Changes the ORDER faces are painted in. Nothing about colour.
#
#   materials   docs/HD_KERNELS.md — the HD/SD material route: per-texel alpha,
#               modulate by the face colour, detail maps, and OB_TORI per-face
#               kernel routing. Changes WHAT each face is filled with. Nothing
#               about order.
#
# They are orthogonal, they were built separately, and neither README shows the
# other's result — so it is easy to assume one of them subsumes the other. This
# renders the same geometry, from the same textures, at the same camera, through
# each port and both together.
#
# Deliberately its own harness: it reads the priorities run/ directory and the
# lane side by side and writes only under docs/qbd_port_compare/. It never
# writes the lane, never packs a cache and never touches the client.
#
#   tools/qbd_port_compare.sh                  # every form both ports have
#   FORMS=default tools/qbd_port_compare.sh
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"

TREE="${TREE:-OSRS-Content/osrs239-content}"
LANE="${LANE:-$TREE/models/ported/rs2012_qbd_td}"
PRIO="${PRIO:-docs/rs2012_qbd_priorities/run}"
OBTORI="${OBTORI:-build/qbd_obtori}"
OUT="${OUT:-docs/qbd_port_compare}"
VIEW="${VIEW:-src/build_win64_opt/rs2012_model_view.exe}"
FORMS="${FORMS:-default worm soul}"

[ -x "$VIEW" ] || VIEW="src/build_win64_opt/rs2012_model_view"
[ -x "$VIEW" ] || { echo "no rs2012_model_view; make -C src rs2012-model-view" >&2; exit 1; }
mkdir -p "$OUT/images" "$OBTORI"

# model1 (+model2) per form, as the client merges them.
models_for() {
    case "$1" in
        default) echo "70260 69766" ;;
        worm)    echo "69765" ;;
        soul)    echo "70761" ;;
        *) echo "qbd_port_compare: unknown form $1" >&2; exit 2 ;;
    esac
}

# Framed on the head for `default` (where both ports do their work) and on the
# whole model otherwise. Same numbers the priorities sheets use, so the two sets
# of evidence line up.
view_args_for() {
    case "$1" in
        default) echo "--angles 3 --yaw0 1300 --pitch 300 --tile 420 --focus -31,-243,261 --radius 270" ;;
        *)       echo "--angles 3 --yaw0 512 --pitch 300 --tile 380" ;;
    esac
}

# render <form> <label> <dir-holding-the-models> <extension>
render() {
    local form="$1" label="$2" dir="$3" ext="$4"
    local args=() m
    for m in $(models_for "$form"); do
        local path="$dir/rs2012_model_$m.$ext"
        [ -f "$path" ] || { echo "  - $label: no $path, skipped"; return 1; }
        args+=(--model "$path")
    done
    # shellcheck disable=SC2046
    "$VIEW" "${args[@]}" --lane-textures "$TREE" $(view_args_for "$form") \
        --bg 202430 --out "$OUT/images/${form}_${label}.bmp" >/dev/null 2>&1 ||
        { echo "  ! $label failed"; return 1; }
    echo "  $label"
}

for form in $FORMS; do
    echo "== $form"

    # The materials port needs its OB_TORI containers; build them here rather
    # than depending on a previous run having left them somewhere.
    for m in $(models_for "$form"); do
        src/build_win64_opt/rs2012_qbd_obtori.exe --model "$m" --out "$OBTORI" >/dev/null 2>&1 || true
    done

    render "$form" lane        "$LANE"   ob3    || true
    render "$form" priorities  "$PRIO"   ob3    || true
    render "$form" materials   "$OBTORI" obtori || true
done

python3 tools/qbd_port_compare_sheets.py --dir "$OUT/images" --forms "$FORMS"
echo "sheets in $OUT/images"
