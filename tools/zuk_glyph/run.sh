#!/bin/sh
# The Zuk-glyph draw-order lane: drive the TzKal-Zuk encounter headlessly
# against the in-process server and prove the floor stays under the fight.
#
#   tools/zuk_glyph/run.sh check          the gate: 0 ground-over-entity rows
#   tools/zuk_glyph/run.sh prove          the same run with the fix OFF -- must FAIL
#   tools/zuk_glyph/run.sh shots          a frame strip through the glyph's walk
#   tools/zuk_glyph/run.sh ab             both arms, one binary, and a pixel diff
#   tools/zuk_glyph/run.sh spans          every mover's span beside its model reach
#
# WHY A LANE AND NOT A UNIT TEST
# ------------------------------
# The defect is a draw ORDER defect, and draw order is a property of a whole
# painted frame of a real scene: which tiles the wave front reached, in which
# order, with which entity standing on which of them. The Inferno produces it
# because its floor is shape-22 lava plates (ground decor, not terrain) with a
# 3x3 npc walking across them while a dozen more crowd the same squares. No
# fixture reproduces that; the encounter does.
#
# DETERMINISM
# -----------
# Two runs of this script paint the same pixels. That is not free and it is
# what makes the A/B below mean anything:
#
#   TORIRS_MAX_FRAMES   already frame-locks the CLIENT's logic tick -- one 20 ms
#                       cycle per frame, never the wall clock (src/app/app_frame.c).
#   TORIRS_EMBED_CLOCK_MS=20   frame-locks the SERVER's 600 ms tick the same way
#                       (src/platform/net_transport_embed.c). Without it the world
#                       ticks whenever this machine gets there, the glyph lands
#                       somewhere else every run, and no comparison survives.
#   TORIRS_WEDGE_CAM    pins the eye. The follow camera is driven by the player,
#                       who is being shot at.
#   TORIRSSERVER_SAVES  a scratch save, because a run that keeps its own state
#                       poisons the next one.
#
# The one thing that still differs between two runs is the debug overlay's
# frame-time readout, which is why `ab` ignores the left 180 columns.
#
# REQUIREMENTS
#   - cache.osrs239.baked and a compiled server script pack. Neither is built
#     here: `TORIRS_PREPARE_ONLY=1 ./run-live.sh manifests/manifest_osrs239.ini`
#     builds both, and this script says so rather than baking behind your back.
#   - a private PAINTERS_DEBUG=1 EMBED_SERVER=1 binary, which it does build,
#     into its own object directory. Never src/torirs: several sessions share
#     this checkout and the last writer of that path wins.
set -eu

cd "$(dirname "$0")/../.."
ROOT=$(pwd)

MODE=${1:-check}
OUT=${ZUK_GLYPH_OUT:-${SCRATCH:-/tmp}/zuk_glyph}
BIN=src/torirs_zukglyph
OBJ=build_zukglyph
MANIFEST=tools/zuk_glyph/world.ini
CACHE=cache.osrs239.baked
PACK=OSRS-Content/osrs239-content/server/scripts/build/script.dat

# The camera, and it is load-bearing. Whether the seam exception fires at all is
# a function of where the eye sits relative to a tile, so the gate is only as
# sensitive as the eye it is given: over one run of this encounter,
# 7104,-2500,7000,450,0 catches 8 violations and this one catches 431. Chosen by
# sweeping; if you move it, run `prove` afterwards or you have quietly turned
# the gate off. x,y,z are SCENE fine units (tile*128); pitch/yaw 2048 per turn.
CAM=${ZUK_GLYPH_CAM:-7552,-3000,7100,520,0}
# Frame 300 sends `::zuktest`, which allocates the Inferno instance, teleports
# in and starts the Zuk phase. The glyph reaches an end of its row about 35
# server ticks later and walks from there; 2600-4100 is the window that holds.
SIM=${ZUK_GLYPH_SIM:-300,zuktest}
FRAMES=${ZUK_GLYPH_FRAMES:-4200}
LOG_AT=${ZUK_GLYPH_LOG_AT:-2600}
LOG_FRAMES=${ZUK_GLYPH_LOG_FRAMES:-1500}

say() { printf '%s\n' "$*" >&2; }

require_content() {
    [ -d "$CACHE" ] || {
        say "zuk_glyph: $CACHE is missing -- the Inferno is content, and the"
        say "  pristine cache boots without it. Build it with:"
        say "    TORIRS_PREPARE_ONLY=1 ./run-live.sh manifests/manifest_osrs239.ini"
        exit 2
    }
    [ -f "$PACK" ] || {
        say "zuk_glyph: no server script pack at $PACK. ::zuktest is a"
        say "  [debugproc] in it, so without one this lane starts nothing."
        say "    TORIRS_PREPARE_ONLY=1 ./run-live.sh manifests/manifest_osrs239.ini"
        exit 2
    }
}

build() {
    mkdir -p "src/${OBJ}_opt_es"
    make -C src OPT=1 EMBED_SERVER=1 PAINTERS_DEBUG=1 \
        PLATFORM_OBJ_BASE="$OBJ" PLATFORM_TARGET="$(basename "$BIN")" \
        "$(basename "$BIN")" >/dev/null
}

# run <run-dir> <relaxed-ready 0|1> [extra env assignments...]
run() {
    _dir=$1; shift
    _relaxed=$1; shift
    mkdir -p "$_dir/saves"
    # A clean save every run: the previous one wrote its own back.
    cp saves/testc.ini "$_dir/saves/testc.ini"
    env \
        SDL_VIDEODRIVER=dummy \
        TORIRSSERVER_SAVES="$_dir/saves" \
        TORIRSSERVER_ALLOW_STALE_SCRIPTS=1 \
        TORIRS_TRANSPORT=embed \
        TORIRSSERVER_REV=osrs239 \
        TORIRS_EMBED_CLOCK_MS=20 \
        TORIRS_PAINTER_RELAXED_READY="$_relaxed" \
        TORIRS_MAX_FRAMES="$FRAMES" \
        TORIRS_SIM_CMD="$SIM" \
        TORIRS_WEDGE_CAM="$CAM" \
        "$@" \
        "$BIN" --manifest "$MANIFEST" --user testc --pass test --soft3d \
        >"$_dir/stdout.txt" 2>"$_dir/stderr.txt"
}

wedge_run() {
    _dir=$1; _relaxed=$2
    run "$_dir" "$_relaxed" \
        TORIRS_WEDGELOG="$_dir/wedge.txt" \
        TORIRS_WEDGELOG_AT="$LOG_AT" \
        TORIRS_WEDGELOG_FRAMES="$LOG_FRAMES"
}

require_content
build

case "$MODE" in
check)
    say "zuk_glyph: painting the Zuk phase at cam $CAM ..."
    wedge_run "$OUT/check" 0
    python3 tools/zuk_glyph/check_draw_order.py "$OUT/check/wedge.txt"
    ;;

prove)
    # The mutation lives in the shipped binary as an env knob rather than in a
    # patched source file ON PURPOSE: this checkout is shared, and an edit made
    # to prove a test can fail has been compiled into someone else's build
    # before now (CLAUDE.md, 2026-09-16).
    say "zuk_glyph: same run with TORIRS_PAINTER_RELAXED_READY=1 (fix off) ..."
    wedge_run "$OUT/prove" 1
    if python3 tools/zuk_glyph/check_draw_order.py "$OUT/prove/wedge.txt"; then
        say "zuk_glyph: FAIL -- the check passed with the fix switched off, so it"
        say "  is not testing the fix. Widen the window (ZUK_GLYPH_LOG_FRAMES) or"
        say "  move the camera (ZUK_GLYPH_CAM) until it fails again."
        exit 1
    fi
    say "zuk_glyph: ok -- the check fails without the fix, so it can fail."
    ;;

shots)
    _start=${ZUK_GLYPH_SHOT_START:-3300}
    _step=${ZUK_GLYPH_SHOT_STEP:-5}
    _count=${ZUK_GLYPH_SHOT_COUNT:-60}
    rm -rf "$OUT/shots"; mkdir -p "$OUT/shots"
    run "$OUT/shots" 0 TORIRS_BMP_SERIES="$OUT/shots,$_start,$_step,$_count"
    say "zuk_glyph: $(ls "$OUT/shots"/frame_*.bmp 2>/dev/null | wc -l) frames in $OUT/shots"
    ;;

ab)
    _start=${ZUK_GLYPH_SHOT_START:-3455}
    _step=${ZUK_GLYPH_SHOT_STEP:-1}
    _count=${ZUK_GLYPH_SHOT_COUNT:-50}
    for _arm in off on; do
        [ "$_arm" = off ] && _relaxed=1 || _relaxed=0
        rm -rf "$OUT/ab_$_arm"; mkdir -p "$OUT/ab_$_arm"
        run "$OUT/ab_$_arm" "$_relaxed" \
            TORIRS_BMP_SERIES="$OUT/ab_$_arm,$_start,$_step,$_count"
    done
    python3 tools/zuk_glyph/diff_frames.py "$OUT/ab_off" "$OUT/ab_on"
    ;;

spans)
    # Not a gate -- the first question a "the floor is on top of X" report gets
    # asked. A span shorter than its model's reach is a second, independent way
    # to land the floor on an entity, and the two read identically on screen.
    run "$OUT/spans" 0 TORIRS_MOVER_FOOTPRINT_DEBUG=1
    python3 tools/zuk_glyph/summarise_spans.py "$OUT/spans/stderr.txt"
    ;;

*)
    say "zuk_glyph: unknown mode '$MODE' (check | prove | shots | ab | spans)"
    exit 2
    ;;
esac
