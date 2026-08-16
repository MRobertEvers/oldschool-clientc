#!/bin/sh
# Reproduce "the Queen Black Dragon's animation bugs out" -- her geometry
# inflating until she is a screenful of stretched shards.
#
# The failure is CUMULATIVE, and that is the whole diagnostic. A frame is meant
# to be applied to the model's BIND pose: the renderer restores the captured
# original vertices, then applies the keyframe. If anything re-captures the
# originals while the model is mid-pose, the posed geometry becomes the new
# "bind" and every later frame stacks on top of it. The signature in the log is
# one element replaying ONE frame with a growing extent:
#
#   anim_blowup: element=4581 seq=22009 frame=0 radius  3076 -> 11952
#   anim_blowup: element=4581 seq=22009 frame=0 radius  6508 -> 32839
#
# Same element, same seq, same frame 0, radius climbing ~3.4x each time. Her
# bind radius is ~2600 and no legitimate pose in any of her 81 sequences goes
# past ~2820 (measured with rs2012_model_view --pose-stats-only), so anything
# beyond BOUND is geometry that compounded rather than geometry that animated.
#
# What triggers it in the encounter is a spotanim landing on her while she is
# animating: attaching one merges her CURRENT vertices with the effect model and
# captures the merge as bind, and detaching re-captures her posed body. The
# fight throws breath and artefact effects at her constantly, which is why this
# reads as intermittent -- it needs an effect to coincide with a pose.
#
# Usage:  tools/qbd_anim_compound_harness.sh [frames]
#   QBD_OUT=<dir>   where the log and the frame BMPs land
# Exit 0 = no pose compounded. Non-zero = reproduced, see the sheet.
set -eu

cd "$(dirname "$0")/.."

FRAMES="${1:-9000}"
OUT="${QBD_OUT:-/tmp/qbd_anim_compound}"
BOUND="${BOUND:-8000}"      # bind ~2600, worst legitimate pose ~2820

rm -rf "$OUT"
mkdir -p "$OUT/saves" "$OUT/frames"

# SANITIZE=1 runs the ASan+UBSan build instead, with the same flags run-live.sh
# uses -- including TORIDRAW_NO_SIMD=1, because UBSan cannot see inside the
# vector kernels and without it the checked build is not checking the code that
# actually runs. halt_on_error=0 so one finding does not hide the rest.
CLIENT_BIN=src/torirs
if [ "${SANITIZE:-0}" != 0 ]; then
    CLIENT_BIN=src/torirs_asan
    make -C src EMBED_SERVER=1 ENABLE_ASAN=1 ENABLE_UBSAN=1 TORIDRAW_NO_SIMD=1 \
        PLATFORM_OBJ_BASE=build_asan PLATFORM_TARGET=torirs_asan torirs_asan >/dev/null || exit 1
    export ASAN_OPTIONS="detect_leaks=0:halt_on_error=0:abort_on_error=0:print_stacktrace=1:log_path=stderr"
    export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0"
else
    [ -x src/torirs ] || make -C src EMBED_SERVER=1 torirs
fi

# `qbd` puts the player in the arena with the encounter armed, `god` makes the
# player unkillable so the run survives long enough to reach the later phases,
# and `jas` maxes the stats. The rs2012qbd* cheats then fire her set pieces
# directly rather than waiting for the fight to earn them -- the breath, the
# fire wall and the platform phase are what put effects on screen while she is
# mid-animation, which is the coincidence this bug needs. One cheat fires per
# interval, in order.
#
# The camera matters: the Queen is NORTH of the arena entrance, so the view has
# to face north (yaw 0) at a steep downward pitch to have her in frame at all.
# Yaw 1024 is the opposite direction -- half of 2048 -- and shows an empty
# chamber, which reads as a clean pass for the wrong reason. Yaw 256/1792 are
# the north-west and north-east three-quarter views. ORBIT=yaw,pitch,zoom.
# TORIRS_NET_CHEAT REPLACES the manifest's own `cheat=` line rather than adding
# to it, so that line is read back out of the manifest and kept as the prefix.
# It is what provisions the account (summoning unlock, pouches, curses); drop it
# and the run is a differently-equipped player, which is not the flow being
# reproduced. Reading it instead of copying it means the two cannot drift.
MANIFEST="${MANIFEST:-manifest_osrs239_torirs.ini}"
BASE_CHEATS=$(sed -n 's/^cheat=//p' "$MANIFEST" | head -1)
USER_NAME=$(sed -n 's/^user=//p' "$MANIFEST" | head -1); USER_NAME="${USER_NAME:-asdf}"
PASS=$(sed -n 's/^pass=//p' "$MANIFEST" | head -1); PASS="${PASS:-a}"
QBD_CHEATS="${QBD_CHEATS:-qbd;god;jas;jas;jas;rs2012qbddrain;rs2012qbdrestore;rs2012qbddrain;rs2012qbdrestore;rs2012qbddrain;rs2012qbdrestore}"
CHEATS="${BASE_CHEATS:+$BASE_CHEATS;}$QBD_CHEATS"

# Everything below mirrors `./run-live.sh manifest_osrs239_torirs.ini --soft3d`:
# same manifest, same embed transport that run-live exports for a native osrs239
# run, same user/pass the manifest declares, same --soft3d. The additions are
# the dummy video driver, a scratch save dir (a shared one makes runs depend on
# each other), the detectors, and a frame budget.
SDL_VIDEODRIVER=dummy \
TORIRS_TRANSPORT=embed \
MOCK230_SAVES="$OUT/saves" \
TORIRS_ANIM_BLOWUP=1 \
TORIRS_ANIM_RECAPTURE=1 \
TORIRS_ANIM_STACK=1 \
TORIRS_ORBIT_CAM="${ORBIT:-0,340,240}" \
TORIRS_NET_CHEAT="$CHEATS" \
TORIRS_NET_CHEAT_ROTATE=1 \
TORIRS_NET_CHEAT_EVERY="${EVERY:-200}" \
TORIRS_BMP_SERIES="$OUT/frames,${SHOT_START:-3300},${SHOT_STEP:-70},${SHOT_N:-30}" \
TORIRS_MAX_FRAMES="$FRAMES" \
  "./$CLIENT_BIN" --manifest "$MANIFEST" --user "$USER_NAME" --pass "$PASS" --soft3d \
  > "$OUT/run.log" 2>&1 || true

echo "log:    $OUT/run.log"
echo "frames: $OUT/frames"

# The cause, reported where it happens rather than inferred from the size. Any
# line here is a capture that overwrote a live bind pose; the blowups below are
# what that turns into a few frames later.
echo "--- sanitizer findings ---"
sanitizer=$(grep -c "runtime error\|ERROR: AddressSanitizer" "$OUT/run.log" || true)
grep -E "runtime error|ERROR: AddressSanitizer" "$OUT/run.log" | head -6 || true
echo "count: $sanitizer"

echo "--- anim_stack (keyframe applied to a pose, not the bind) ---"
stacked=$(grep -c "anim_stack" "$OUT/run.log" || true)
grep "anim_stack" "$OUT/run.log" | head -6 || true
echo "count: $stacked"

echo "--- anim_recapture (posed vertices captured as bind) ---"
recapture=$(grep -c "anim_recapture" "$OUT/run.log" || true)
grep "anim_recapture" "$OUT/run.log" | head -6 || true
echo "count: $recapture"

echo "--- anim_blowup events ---"
grep "anim_blowup" "$OUT/run.log" | sed 's/ -- this keyframe.*//' | head -20 || true

# The gate. Take every posed radius the detector reported and compare the
# largest against BOUND. A radius is only reported on a >2x edge, which is
# exactly the compounding step, so a run that never compounds prints nothing
# here and passes.
worst=$(grep -o "radius [0-9]* -> [0-9]*" "$OUT/run.log" | awk '{print $4}' | sort -n | tail -1)
worst=${worst:-0}

# Only OVERSIZED blowups are interesting. A walk cycle legitimately swings a
# player model's radius past the 2x edge every stride, so an unfiltered list is
# almost entirely seq 808/813 noise with the one line that matters buried in it.
echo "--- blowups past the bound ---"
awk -v b="$BOUND" '/anim_blowup/ { for (i=1;i<=NF;i++) if ($i=="->") if ($(i+1)+0 > b) { print; next } }' \
    "$OUT/run.log" | head -12 || true

echo "worst posed radius: $worst (bound $BOUND)"
fail=0
[ "$worst" -gt "$BOUND" ] && {
    echo "FAIL: a pose compounded -- geometry grew past any legitimate pose"; fail=1; }
[ "$recapture" -gt 0 ] && {
    echo "FAIL: $recapture capture(s) took a posed model as its bind pose"; fail=1; }
[ "$stacked" -gt 0 ] && {
    echo "FAIL: $stacked keyframe(s) applied to a pose instead of the bind"; fail=1; }
[ "$sanitizer" -gt 0 ] && {
    echo "FAIL: $sanitizer sanitizer finding(s)"; fail=1; }
[ "$fail" = 0 ] && echo "PASS: bind poses stayed pristine and no pose compounded"
exit "$fail"
