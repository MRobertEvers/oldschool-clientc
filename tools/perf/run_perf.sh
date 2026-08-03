#!/usr/bin/env bash
# Build and run a deterministic headless perf scenario with the embedded server.
#
# Usage:
#   ./tools/perf/run_perf.sh [idle|ui|world] [frames]
#
# Env:
#   PLATFORM_OBJ_BASE   private objdir (default: build_perf)
#   TORIDRAW_OPT        Soft3D -O2 knob (default: 1)
#   EMBED_SERVER        must be 1 for embed manifest (default: 1)
#   TORIRS_PERF_OUT     CSV output path override
#   USER/PASS           login credentials (default: testc/test)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

SCENARIO="${1:-idle}"
FRAMES="${2:-1200}"
OBJ_BASE="${PLATFORM_OBJ_BASE:-build_perf}"
TD_OPT="${TORIDRAW_OPT:-1}"
USER_NAME="${TORIRS_PROFILE_USER:-testc}"
PASS_NAME="${TORIRS_PROFILE_PASS:-test}"
REV="$(git rev-parse --short HEAD 2>/dev/null || echo nogit)"
OUT_DIR="$ROOT/tools/perf/results"
mkdir -p "$OUT_DIR"
CSV="${TORIRS_PERF_OUT:-$OUT_DIR/${REV}-${SCENARIO}.csv}"
MANIFEST="$ROOT/manifest_osrs230_embed.ini"

case "$SCENARIO" in
  idle|ui|world) ;;
  *)
    echo "usage: $0 [idle|ui|world] [frames]" >&2
    exit 2
    ;;
esac

echo "run_perf: building EMBED_SERVER=1 TORIDRAW_OPT=$TD_OPT PLATFORM_OBJ_BASE=$OBJ_BASE"
mkdir -p "src/${OBJ_BASE}$([ "$TD_OPT" = 1 ] && echo _tdo || true)"
make -C src PLATFORM_OBJ_BASE="$OBJ_BASE" EMBED_SERVER=1 TORIDRAW_OPT="$TD_OPT" -j8 torirs

# Scenario-specific input injection. Frames are wall-clock iterations; with
# --uncapped they burn CPU as fast as possible so the harness measures work,
# not the 50 fps sleep. Boot takes a few hundred frames; SIM_CLICK_AT times
# are absolute frame indices into the loop.
EXTRA_ENV=()
case "$SCENARIO" in
  idle)
    # Logged in, world visible, no input.
    ;;
  ui)
    # Open the bank via a cheat once boot has settled (~frame 400).
    EXTRA_ENV+=(TORIRS_NET_CHEAT="::bank")
    ;;
  world)
    # Spawn NPCs / projectiles for model-instance cache pressure.
    EXTRA_ENV+=(TORIRS_NET_CHEAT="::npc")
    EXTRA_ENV+=(TORIRS_SPAWN_NPC=1)
    ;;
esac

echo "run_perf: scenario=$SCENARIO frames=$FRAMES csv=$CSV"
# shellcheck disable=SC2086
env SDL_VIDEODRIVER=dummy \
  TORIRS_PERF=1 \
  TORIRS_PERF_CSV="$CSV" \
  TORIRS_MAX_FRAMES="$FRAMES" \
  TORIRS_BOOT_STATS=1 \
  "${EXTRA_ENV[@]}" \
  "$ROOT/src/torirs" \
  --manifest "$MANIFEST" \
  --user "$USER_NAME" \
  --pass "$PASS_NAME" \
  --uncapped \
  --soft3d \
  >"$OUT_DIR/${REV}-${SCENARIO}.log" 2>&1 || {
    echo "run_perf: client exited non-zero — see $OUT_DIR/${REV}-${SCENARIO}.log" >&2
    tail -40 "$OUT_DIR/${REV}-${SCENARIO}.log" >&2
    exit 1
  }

echo "run_perf: wrote $CSV"
# Print the report summary from the log for interactive use.
grep -A80 '=== torirs_perf report ===' "$OUT_DIR/${REV}-${SCENARIO}.log" || true
