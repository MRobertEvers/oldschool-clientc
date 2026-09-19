#!/usr/bin/env bash
# Run one C-client scenario from a PRE-BUILT binary, for the C-vs-Java study.
#
#   tools/perf/run_cvj.sh <binary> <scenario> [frames]
#
# Differs from run_perf.sh in exactly one way and it is the point: run_perf.sh
# builds the binary it runs, always at OPT=0 with TORIDRAW_OPT. This study needs
# the same scenario run against two different builds (-O0 and -O3), so the
# binary is an argument and nothing is rebuilt. Binaries live in
# tools/perf/bin/ and are built by:
#
#   -O0  make -C src PLATFORM_OBJ_BASE=build_cvj_o0 EMBED_SERVER=1 TORIDRAW_OPT=1 torirs
#   -O3  make -C src PLATFORM_OBJ_BASE=build_cvj_o3 OPT=1 EMBED_SERVER=1 torirs
#
# Both write src/torirs, so each is copied aside immediately after its link
# step -- running them back to back without copying measures one build twice.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

BIN="${1:?usage: run_cvj.sh <binary> <scenario> [frames]}"
SCENARIO="${2:-idle}"
FRAMES="${3:-1200}"
TAG="$(basename "$BIN")"
OUT_DIR="$ROOT/tools/perf/results/cvj"
mkdir -p "$OUT_DIR"
CSV="$OUT_DIR/${TAG}-${SCENARIO}.csv"
LOG="$OUT_DIR/${TAG}-${SCENARIO}.log"
MANIFEST="$ROOT/manifests/manifest_osrs230_embed.ini"

EXTRA_ENV=()
case "$SCENARIO" in
  idle) ;;
  ui)    EXTRA_ENV+=(TORIRS_NET_CHEAT="${TORIRS_NET_CHEAT:-bank}") ;;
  world) EXTRA_ENV+=(TORIRS_NET_CHEAT="::npc" TORIRS_SPAWN_NPC=1) ;;
  *) echo "usage: $0 <binary> [idle|ui|world] [frames]" >&2; exit 2 ;;
esac

echo "run_cvj: $TAG scenario=$SCENARIO frames=$FRAMES"
env SDL_VIDEODRIVER=dummy \
  TORIRS_PERF=1 \
  TORIRS_PERF_CSV="$CSV" \
  TORIRS_PERF_WINDOW=500 \
  TORIRS_MAX_FRAMES="$FRAMES" \
  TORIRS_BOOT_STATS=1 \
  "${EXTRA_ENV[@]}" \
  "$BIN" \
  --manifest "$MANIFEST" \
  --user "${TORIRS_PROFILE_USER:-testc}" \
  --pass "${TORIRS_PROFILE_PASS:-test}" \
  --uncapped \
  --soft3d \
  >"$LOG" 2>&1 || {
    echo "run_cvj: client exited non-zero — see $LOG" >&2
    tail -40 "$LOG" >&2
    exit 1
  }

echo "run_cvj: wrote $CSV"
grep -A40 '=== torirs_perf report ===' "$LOG" || true
