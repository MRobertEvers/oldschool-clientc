#!/usr/bin/env bash
# Build and run a deterministic headless perf scenario with the embedded server.
#
# Usage:
#   ./tools/perf/run_perf.sh [idle|ui|world|drift|drift-capped|drift-ui|soak-ui] [frames]
#
# Env:
#   PLATFORM_OBJ_BASE   private objdir (default: build_perf)
#   TORIDRAW_OPT        Soft3D -O2 knob (default: 1)
#   EMBED_SERVER        must be 1 for embed manifest (default: 1)
#   TORIRS_PERF_OUT     CSV output path override
#   TORIRS_PERF_WINDOW  window size for drift CSV (default: 1000 idle/ui/world, 500 drift*)
#   USER/PASS           login credentials (default: testc/test)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

SCENARIO="${1:-idle}"
FRAMES="${2:-}"
OBJ_BASE="${PLATFORM_OBJ_BASE:-build_perf}"
TD_OPT="${TORIDRAW_OPT:-1}"
USER_NAME="${TORIRS_PROFILE_USER:-testc}"
PASS_NAME="${TORIRS_PROFILE_PASS:-test}"
REV="$(git rev-parse --short HEAD 2>/dev/null || echo nogit)"
OUT_DIR="$ROOT/tools/perf/results"
mkdir -p "$OUT_DIR"
MANIFEST="$ROOT/manifest_osrs230_embed.ini"

case "$SCENARIO" in
  idle|ui|world|drift|drift-capped|drift-ui|soak-ui) ;;
  *)
    echo "usage: $0 [idle|ui|world|drift|drift-capped|drift-ui|soak-ui] [frames]" >&2
    exit 2
    ;;
esac

# Default frame counts: short scenarios stay at 1200; drift needs many windows.
if [ -z "$FRAMES" ]; then
  case "$SCENARIO" in
    drift|drift-ui) FRAMES=30000 ;;
    soak-ui) FRAMES=60000 ;;
    drift-capped) FRAMES=30000 ;; # ~10 min at 50 fps
    *) FRAMES=1200 ;;
  esac
fi

CSV="${TORIRS_PERF_OUT:-$OUT_DIR/${REV}-${SCENARIO}.csv}"
WINDOW="${TORIRS_PERF_WINDOW:-}"
if [ -z "$WINDOW" ]; then
  case "$SCENARIO" in
    drift|drift-capped|drift-ui|soak-ui) WINDOW=500 ;;
    *) WINDOW=1000 ;;
  esac
fi

echo "run_perf: building EMBED_SERVER=1 TORIDRAW_OPT=$TD_OPT PLATFORM_OBJ_BASE=$OBJ_BASE"
mkdir -p "src/${OBJ_BASE}$([ "$TD_OPT" = 1 ] && echo _tdo || true)"
make -C src PLATFORM_OBJ_BASE="$OBJ_BASE" EMBED_SERVER=1 TORIDRAW_OPT="$TD_OPT" -j8 torirs

# Scenario-specific input injection. Frames are wall-clock iterations; with
# --uncapped they burn CPU as fast as possible so the harness measures work,
# not the 50 fps sleep. Boot takes a few hundred frames; SIM_CLICK_AT times
# are absolute frame indices into the loop.
EXTRA_ENV=()
PACE=(--uncapped)
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
  drift)
    # Long idle uncapped — many client frames between 600 ms server ticks.
    ;;
  drift-capped)
    # Long idle at 50 fps so wall clock ≈ frame count / 50; server ticks
    # accumulate over real minutes (the user's "sitting idle" case).
    PACE=()
    ;;
  drift-ui)
    # Menu open/close churn: re-open bank every 40 logic ticks, Escape mid-
    # cycle to close it, and cycle sidebar tabs (f1–f12) on main-loop frames.
    # Escape at frame 350 also clears default chat focus so F-keys bind.
    EXTRA_ENV+=(TORIRS_NET_CHEAT="::bank")
    EXTRA_ENV+=(TORIRS_NET_CHEAT_EVERY=40)
    {
      parts=("350,escape")
      f=400
      while [ "$f" -lt "$FRAMES" ]; do
        for key in f1 f2 f3 f4 f5 f6 f7 f8 f9 f10 f11 f12 escape; do
          parts+=("${f},${key}")
          f=$((f + 25))
          [ "$f" -ge "$FRAMES" ] && break
        done
      done
      IFS=';'
      EXTRA_ENV+=(TORIRS_SIM_HOTKEY="${parts[*]}")
    }
    ;;
  soak-ui)
    # Multi-panel residency soak: rotate distinct IF_OPENSUB targets so
    # component_count grows with each new pack (drift-ui remounts one pack).
    # Escape closes the current modal between opens; f-keys cycle sidebars.
    EXTRA_ENV+=(TORIRS_NET_CHEAT="bank;equipstats;xptracker;loottools;hiscores;farmkit")
    EXTRA_ENV+=(TORIRS_NET_CHEAT_EVERY=50)
    EXTRA_ENV+=(TORIRS_NET_CHEAT_ROTATE=1)
    EXTRA_ENV+=(TORIRS_IFACE_STATS=1)
    EXTRA_ENV+=(TORIRS_STATS=1)
    {
      parts=("350,escape")
      f=400
      while [ "$f" -lt "$FRAMES" ]; do
        for key in escape f1 f2 f3 f4 f5 f6 f7 f8 f9 f10 f11 f12 escape; do
          parts+=("${f},${key}")
          f=$((f + 20))
          [ "$f" -ge "$FRAMES" ] && break
        done
      done
      IFS=';'
      EXTRA_ENV+=(TORIRS_SIM_HOTKEY="${parts[*]}")
    }
    ;;
esac

echo "run_perf: scenario=$SCENARIO frames=$FRAMES window=$WINDOW csv=$CSV"
# shellcheck disable=SC2086
env SDL_VIDEODRIVER=dummy \
  TORIRS_PERF=1 \
  TORIRS_PERF_CSV="$CSV" \
  TORIRS_PERF_WINDOW="$WINDOW" \
  TORIRS_MAX_FRAMES="$FRAMES" \
  TORIRS_BOOT_STATS=1 \
  "${EXTRA_ENV[@]}" \
  "$ROOT/src/torirs" \
  --manifest "$MANIFEST" \
  --user "$USER_NAME" \
  --pass "$PASS_NAME" \
  "${PACE[@]}" \
  --soft3d \
  >"$OUT_DIR/${REV}-${SCENARIO}.log" 2>&1 || {
    echo "run_perf: client exited non-zero — see $OUT_DIR/${REV}-${SCENARIO}.log" >&2
    tail -40 "$OUT_DIR/${REV}-${SCENARIO}.log" >&2
    exit 1
  }

echo "run_perf: wrote $CSV"
if [ -f "${CSV}.windows.csv" ]; then
  echo "run_perf: wrote ${CSV}.windows.csv"
fi
# Print the report summary from the log for interactive use.
grep -A80 '=== torirs_perf report ===' "$OUT_DIR/${REV}-${SCENARIO}.log" || true
# Drift: print window frame_p95 lines so the slope is visible without opening CSV.
if [[ "$SCENARIO" == drift* || "$SCENARIO" == soak-ui ]]; then
  echo "--- window frame_p95 ---"
  grep 'torirs_perf: window=' "$OUT_DIR/${REV}-${SCENARIO}.log" || true
  if [ -f "${CSV}.windows.csv" ]; then
    python3 "$ROOT/tools/perf/compare.py" --drift "${CSV}.windows.csv" || true
  fi
fi
