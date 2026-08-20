#!/bin/sh
# Profile torirs on macOS and write a flamegraph — no sudo (unlike profile.d,
# which needs dtrace). Uses /usr/bin/sample, which works on your own processes.
#
#   ./profile-mac.sh                              # headless, manifests/manifest_osrs230.ini, 25s
#   ./profile-mac.sh manifests/manifest_rs254.ini           # another manifest
#   ./profile-mac.sh manifests/manifest_osrs230_embed.ini 40 # embedded server, Soft3D (harness)
#   ./profile-mac.sh manifests/manifest_osrs230.ini 40      # sample for 40 seconds
#   TORIRS_PROFILE_WINDOWED=1 ./profile-mac.sh    # real SDL window instead of dummy
#   TORIRS_PROFILE_ATTACH=<pid> ./profile-mac.sh  # sample a client you already started
#
# Env:
#   FLAMEGRAPH_DIR   FlameGraph checkout (default: ~/Documents/git_repos/FlameGraph,
#                    then ~/git_repos/FlameGraph, then $PWD/../FlameGraph)
#   OUT              output basename (default: flamegraph_<manifest stem>)
#   TORIRS_PROFILE_WARMUP  seconds to let the client boot + log in before sampling (default 8)
#   TORIRS_PROFILE_SOFT3D=1  force Soft3D (default for embed manifests)
#   TORIDRAW_OPT=1   rebuild Soft3D at -O2 (perf harness default for embed)
#
# Writes <OUT>.svg plus the raw <OUT>.sample / <OUT>.folded next to it, and prints
# the main-thread breakdown. Everything else in the process (AppKit's event thread
# parked in mach_msg) is filtered out of the summary — include it and half the
# samples are a thread that is doing nothing.
set -eu

cd "$(dirname "$0")"

MANIFEST="${1:-manifests/manifest_osrs230.ini}"
DURATION="${2:-25}"
WARMUP="${TORIRS_PROFILE_WARMUP:-8}"
OUT="${OUT:-flamegraph_$(basename "$MANIFEST" .ini)}"

# --- locate FlameGraph -------------------------------------------------------
if [ -z "${FLAMEGRAPH_DIR:-}" ]; then
    for d in "$HOME/Documents/git_repos/FlameGraph" "$HOME/git_repos/FlameGraph" \
             "$(pwd)/../FlameGraph" "$HOME/FlameGraph"; do
        [ -f "$d/flamegraph.pl" ] && FLAMEGRAPH_DIR="$d" && break
    done
fi
if [ -z "${FLAMEGRAPH_DIR:-}" ] || [ ! -f "$FLAMEGRAPH_DIR/flamegraph.pl" ]; then
    echo "profile-mac.sh: FlameGraph not found. git clone https://github.com/brendangregg/FlameGraph" >&2
    echo "                then set FLAMEGRAPH_DIR=/path/to/FlameGraph" >&2
    exit 1
fi

COLLAPSE="$FLAMEGRAPH_DIR/stackcollapse-sample.awk"
[ -f "$COLLAPSE" ] || { echo "profile-mac.sh: missing $COLLAPSE" >&2; exit 1; }

# --- get a running client ----------------------------------------------------
STARTED_PID=""
if [ -n "${TORIRS_PROFILE_ATTACH:-}" ]; then
    PID="$TORIRS_PROFILE_ATTACH"
    kill -0 "$PID" 2>/dev/null || { echo "profile-mac.sh: no process $PID" >&2; exit 1; }
    echo "profile-mac.sh: attaching to pid $PID"
else
    [ -f "$MANIFEST" ] || { echo "profile-mac.sh: manifest '$MANIFEST' not found" >&2; exit 1; }

    TRANSPORT=$(sed -n 's/^[[:space:]]*transport[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)
    PORT=$(sed -n 's/^[[:space:]]*port[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)
    HOST=$(sed -n 's/^[[:space:]]*host[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)
    REV=$(sed -n 's/^[[:space:]]*rev[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)

    # Embed manifests need EMBED_SERVER=1 (and typically TORIDRAW_OPT=1 for the
    # harness). TCP osrs230 still auto-starts ToriRSServer.
    if [ "$TRANSPORT" = "embed" ]; then
        echo "profile-mac.sh: embed transport — building EMBED_SERVER=1 TORIDRAW_OPT=${TORIDRAW_OPT:-1}"
        make -C src EMBED_SERVER=1 TORIDRAW_OPT="${TORIDRAW_OPT:-1}" torirs
        RENDERER="--soft3d"
        OFFLINE=""
    else
        [ -x src/torirs ] || make -C src torirs
        if [ "$REV" = "osrs230" ] && [ "${HOST:-localhost}" = "localhost" ] &&
           ! lsof -i ":${PORT:-43594}" >/dev/null 2>&1; then
            echo "profile-mac.sh: starting src/build/torirsserver on ${PORT:-43594}"
            make -C src ToriRSServer >/dev/null
            src/build/torirsserver "${PORT:-43594}" > "$OUT.mock.log" 2>&1 &
            MOCK_PID=$!
            trap 'kill $MOCK_PID 2>/dev/null || true' EXIT
            sleep 1
        fi
        if [ -n "${TORIRS_PROFILE_SOFT3D:-}" ]; then
            RENDERER="--soft3d"
            OFFLINE="--offline"
        else
            RENDERER="--opengl3"
            OFFLINE="--offline"
        fi
    fi

    # Frames are capped at 50 fps, so the run must outlast warmup + sampling;
    # dummy video keeps it off screen (TORIRS_PROFILE_WINDOWED=1 to see it).
    FRAMES=$(( (WARMUP + DURATION + 10) * 50 ))
    if [ -n "${TORIRS_PROFILE_WINDOWED:-}" ]; then
        # shellcheck disable=SC2086
        TORIRS_MAX_FRAMES=$FRAMES TORIRS_PERF=1 src/torirs --manifest "$MANIFEST" \
            $OFFLINE $RENDERER \
            --user "${TORIRS_PROFILE_USER:-testc}" --pass "${TORIRS_PROFILE_PASS:-test}" \
            > "$OUT.run.log" 2>&1 &
    else
        # shellcheck disable=SC2086
        SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=$FRAMES TORIRS_PERF=1 \
            src/torirs --manifest "$MANIFEST" $OFFLINE $RENDERER \
            --user "${TORIRS_PROFILE_USER:-testc}" --pass "${TORIRS_PROFILE_PASS:-test}" \
            > "$OUT.run.log" 2>&1 &
    fi
    STARTED_PID=$!
    PID=$STARTED_PID
    echo "profile-mac.sh: torirs pid $PID (log $OUT.run.log), warming up ${WARMUP}s"
    sleep "$WARMUP"
    kill -0 "$PID" 2>/dev/null || {
        echo "profile-mac.sh: torirs exited during warmup — see $OUT.run.log" >&2
        tail -5 "$OUT.run.log" >&2
        exit 1
    }
fi

# --- sample, collapse, render ------------------------------------------------
echo "profile-mac.sh: sampling ${DURATION}s at 1ms"
sample "$PID" "$DURATION" 1 -f "$OUT.sample" >/dev/null
awk -f "$COLLAPSE" "$OUT.sample" > "$OUT.folded"
"$FLAMEGRAPH_DIR/flamegraph.pl" \
    --title "torirs $(basename "$MANIFEST" .ini) $(date '+%Y-%m-%d %H:%M')" \
    "$OUT.folded" > "$OUT.svg"

[ -n "$STARTED_PID" ] && kill "$STARTED_PID" 2>/dev/null || true

# --- summary (main thread only) ---------------------------------------------
echo
echo "=== main thread: where the time went ==="
grep DispatchQueue_1 "$OUT.folded" | awk '
{
    n = $NF; sub(/ [0-9]+$/, ""); split($0, frames, ";")
    for( i = 1; i <= length(frames); i++ )
        if( frames[i] ~ /`main$/ ) { top = frames[i+1]; break }
    total[top] += n; sum += n
}
END {
    printf "%8s %6s  %s\n", "samples", "share", "callee of main"
    for( k in total ) printf "%8d %5.1f%%  %s\n", total[k], total[k] * 100 / sum, k
}' | sort -k1 -rn | head -8

echo
echo "=== main thread: top leaf functions ==="
grep DispatchQueue_1 "$OUT.folded" | awk '
{
    n = $NF; sub(/ [0-9]+$/, ""); split($0, frames, ";")
    leaf = frames[length(frames)]; sub(/^torirs`/, "", leaf)
    total[leaf] += n; sum += n
}
END { for( k in total ) printf "%8d %5.1f%%  %s\n", total[k], total[k] * 100 / sum, k }' \
    | sort -rn | head -15

echo
echo "wrote $OUT.svg  (open $OUT.svg)"
