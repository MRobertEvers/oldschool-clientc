#!/usr/bin/env bash
#
# Face-priority iteration loop for the imported RS2012 lane.
#
# Builds the two tools, re-authors priorities into a SEPARATE output model
# (the lane's .ob3 files are never written), renders before/after contact
# sheets plus sort-error masks, and prints both scores so a priority edit is
# judged by a number rather than by eye.
#
#   tools/rs2012_qbd_prio.sh                       # QBD head+neck, default view
#   tools/rs2012_qbd_prio.sh --strategy shell
#   OUT=/tmp/qbd tools/rs2012_qbd_prio.sh --focus 0,-800,400 --radius 700
#
# Outputs land in $OUT (default build/qbd_prio):
#   before.bmp / before_err.bmp   the lane model as it ships today
#   after.bmp  / after_err.bmp    the same model with authored priorities
#   *.ob3                         the re-authored models, alongside not over
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"

LANE="${LANE:-OSRS-Content/osrs239-content/models/ported/rs2012_qbd_td}"
OUT="${OUT:-build/qbd_prio}"
# QBD "default" npc: model1 is the head/neck, model2 the wings/body plate.
MODELS="${MODELS:-rs2012_model_70260 rs2012_model_69766}"
VIEW_ARGS="${VIEW_ARGS:---angles 6 --pitch 0,220,1848 --tile 360}"

toolchain="$repo/toolchain/mingw64/bin"
if [ -d "$toolchain" ]; then
    PATH="$toolchain:$PATH"
    MAKE="${MAKE:-mingw32-make}"
else
    MAKE="${MAKE:-make}"
fi
export PATH

"$MAKE" -C src rs2012-model-view rs2012-face-priorities >/dev/null

bin="src/build_win64"
[ -x "$bin/rs2012_model_view.exe" ] || bin="src/build"
view="$bin/rs2012_model_view"
prio="$bin/rs2012_face_priorities"
[ -x "$view" ] || view="$view.exe"
[ -x "$prio" ] || prio="$prio.exe"

mkdir -p "$OUT"

before_args=()
after_args=()
for m in $MODELS; do
    before_args+=(--model "$LANE/$m.ob3")
    after_args+=(--model "$OUT/$m.ob3")
    "$prio" --in "$LANE/$m.ob3" --out "$OUT/$m.ob3" "$@"
done

echo "--- before (lane model, no priorities) ---"
"$view" "${before_args[@]}" --out "$OUT/before.bmp" --score-out "$OUT/before_err.bmp" \
    $VIEW_ARGS
echo "--- after (authored priorities) ---"
"$view" "${after_args[@]}" --out "$OUT/after.bmp" --score-out "$OUT/after_err.bmp" \
    $VIEW_ARGS

# BMP is what the raster writes; PNG is what a viewer or an agent can read.
python3 - "$OUT" <<'PY'
import sys, pathlib
try:
    from PIL import Image
except ImportError:
    sys.exit(0)
out = pathlib.Path(sys.argv[1])
for bmp in out.glob("*.bmp"):
    Image.open(bmp).save(bmp.with_suffix(".png"))
PY

echo "sheets in $OUT"
