#!/usr/bin/env bash
#
# Evidence sheets for the two imported-material kernels (docs/HD_KERNELS.md).
#
# Renders the same geometry, from the same assets, three ways:
#
#   legacy    TORIDRAW_TEX_LEGACY=1 - both kernels off. Every texture takes the
#             stock opaque / colour-key path: what this renderer did before.
#   alpha     coverage on, tint off (TORIDRAW_NO_MODULATE=1).
#   both      shipping behaviour.
#
# Three renders of one cache is the point: nothing is re-baked between them, so
# a difference is the kernel and cannot be an asset change. A per-pixel diff of
# each pair goes beside them, because "these two pictures differ" is a claim a
# reader should not have to take on trust.
#
#   tools/hd_kernels_shots.sh                 # every subject
#   SUBJECTS=qbd tools/hd_kernels_shots.sh    # just the dragon
#   OUT=/tmp/hd tools/hd_kernels_shots.sh
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"

TREE="${TREE:-OSRS-Content/osrs239-content}"
LANE="${LANE:-$TREE/models/ported/rs2012_qbd_td}"
OUT="${OUT:-docs/hd_kernels/images}"
VIEW="${VIEW:-src/build_win64_opt/rs2012_model_view.exe}"
SUBJECTS="${SUBJECTS:-qbd_head qbd_body worm soul}"

[ -x "$VIEW" ] || VIEW="src/build_win64/rs2012_model_view"
[ -x "$VIEW" ] || { echo "no rs2012_model_view; make -C src rs2012-model-view" >&2; exit 1; }
mkdir -p "$OUT"

# Each subject is a framing of one npc form. The head framings use the same
# focus/radius the priority sheets use, so the two sets of evidence line up.
args_for() {
    case "$1" in
        qbd_head)
            echo "--model $LANE/rs2012_model_70260.ob3 --model $LANE/rs2012_model_69766.ob3 \
                  --angles 3 --yaw0 1300 --pitch 300 --tile 420 --focus -31,-243,261 --radius 270" ;;
        qbd_body)
            echo "--model $LANE/rs2012_model_70260.ob3 --model $LANE/rs2012_model_69766.ob3 \
                  --angles 4 --yaw0 0 --pitch 320 --tile 360" ;;
        worm)
            echo "--model $LANE/rs2012_model_69765.ob3 --angles 3 --yaw0 512 --pitch 300 --tile 380" ;;
        soul)
            echo "--model $LANE/rs2012_model_70761.ob3 --angles 3 --yaw0 512 --pitch 300 --tile 380" ;;
        *) echo "hd_kernels_shots: unknown subject $1" >&2; exit 2 ;;
    esac
}

render() { # render <subject> <variant> <env...>
    local subject="$1" variant="$2"; shift 2
    local bmp="$OUT/${subject}_${variant}.bmp"
    # shellcheck disable=SC2046
    env -u TORIDRAW_TEX_LEGACY -u TORIDRAW_NO_MODULATE "$@" "$VIEW" $(args_for "$subject") \
        --lane-textures "$TREE" --bg 202430 --out "$bmp" >/dev/null 2>&1 || {
            echo "  ! $subject/$variant failed" >&2; return 1; }
    echo "$bmp"
}

for subject in $SUBJECTS; do
    echo "== $subject"
    render "$subject" legacy TORIDRAW_TEX_LEGACY=1        >/dev/null
    render "$subject" alpha  TORIDRAW_NO_MODULATE=1       >/dev/null
    render "$subject" both   TORIDRAW_KERNELS=on            >/dev/null
    echo "  legacy / alpha / both written"
done

python3 tools/hd_kernels_sheets.py --dir "$OUT" --subjects "$SUBJECTS"
echo "sheets in $OUT"
