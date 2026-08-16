#!/usr/bin/env bash
#
# Render the QBD encounter start — seq rs2012_seq_16714, the wake the session
# script queues at rs2012_qbd_wake — one folder per animation frame, each with
# the lane model as it ships and the priority-authored model side by side.
#
#   tools/rs2012_qbd_wake_frames.sh            # all 108 frames
#   STRIDE=4 tools/rs2012_qbd_wake_frames.sh   # every 4th
#
# Output: docs/rs2012_qbd_wake/frame_000/{before,after}.png + frame.txt
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo"

LANE="${LANE:-OSRS-Content/osrs239-content/models/ported/rs2012_qbd_td}"
AUTHORED="${AUTHORED:-docs/rs2012_qbd_priorities/run}"
CACHE="${CACHE:-cache.osrs239.rs2012}"
OUT="${OUT:-docs/rs2012_qbd_wake}"
SEQ="${SEQ:-rs2012_seq_16714}"
STRIDE="${STRIDE:-1}"
VIEW_ARGS="${VIEW_ARGS:---angles 3 --pitch 300 --tile 300 --yaw0 1300}"

toolchain="$repo/toolchain/mingw64/bin"
[ -d "$toolchain" ] && PATH="$toolchain:$PATH" && MAKE="${MAKE:-mingw32-make}" || MAKE="${MAKE:-make}"
export PATH
"$MAKE" -C src rs2012-model-view >/dev/null
view="src/build_win64/rs2012_model_view.exe"
[ -x "$view" ] || view="src/build/rs2012_model_view"

# The sequence config is the source of truth for which frames the encounter
# plays and in what order; nothing else knows the order. Written through a file
# with explicit LF newlines — python on Windows emits CRLF, and a stray CR
# reaches the arithmetic below as a parse error rather than as a bad number.
mkdir -p build
python3 - "$SEQ" <<'PY'
import re, sys, pathlib
cfg = pathlib.Path(
    "OSRS-Content/osrs239-content/ported/rs2012_qbd_td/configs/rs2012.seq").read_text()
block = re.search(rf"\[{sys.argv[1]}\]\n(.*?)(?=\n\[|\Z)", cfg, re.S).group(1)
ids = [m.group(1) for m in re.finditer(r"frame=(\d+),(\d+)", block)]
with open("build/wake.frames", "w", newline="\n") as f:
    f.write("\n".join(ids) + "\n")
PY
mapfile -t FRAMES < build/wake.frames

echo "${SEQ}: ${#FRAMES[@]} frames, stride $STRIDE -> $OUT"
mkdir -p "$OUT"

i=0
for packed in "${FRAMES[@]}"; do
    if [ $((i % STRIDE)) -ne 0 ]; then i=$((i + 1)); continue; fi
    dir="$OUT/$(printf 'frame_%03d' "$i")"
    mkdir -p "$dir"
    printf 'sequence   %s\nposition   %d of %d\npacked id  %s\nanimset    %d\nframe      %d\n' \
        "$SEQ" "$i" "${#FRAMES[@]}" "$packed" "$((packed >> 16))" "$((packed & 0xFFFF))" \
        > "$dir/frame.txt"

    "$view" --model "$LANE/rs2012_model_70260.ob3" --model "$LANE/rs2012_model_69766.ob3" \
        --out "$dir/before.bmp" --cache "$CACHE" --frames "$packed" $VIEW_ARGS \
        >/dev/null 2>>"$dir/frame.txt" || true
    "$view" --model "$AUTHORED/rs2012_model_70260.ob3" --model "$AUTHORED/rs2012_model_69766.ob3" \
        --out "$dir/after.bmp" --cache "$CACHE" --frames "$packed" $VIEW_ARGS \
        >/dev/null 2>>"$dir/frame.txt" || true
    i=$((i + 1))
done

python3 - "$OUT" <<'PY'
import sys, pathlib
try:
    from PIL import Image
except ImportError:
    sys.exit(0)
for bmp in pathlib.Path(sys.argv[1]).rglob("*.bmp"):
    Image.open(bmp).save(bmp.with_suffix(".png"))
    bmp.unlink()
PY

echo "wrote $(find "$OUT" -name 'after.png' | wc -l) frame folders in $OUT"
