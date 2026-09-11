#!/bin/sh
#
# The multi-PROCESS half of ToriRSMapEd, which no in-process test can reach:
# a real daemon, several client processes, and the Client (session group)
# scoping that decides who hears what.
#
# What it proves, and why each one matters:
#
#   1. Separate processes attach to one daemon and are told their Client id.
#   2. A connection publishing state reaches the OTHER connections of its
#      Client — the viewer-click-to-controller path, across process
#      boundaries, with no shared memory anywhere.
#   3. A different Client hears none of it. This is the isolation the design
#      turns on: two people editing one world must not fight over a
#      selection.
#   4. A late joiner's STATE_SYNC replays its own Client's store and nobody
#      else's.
#   5. The authoritative document answers a square open to a client that
#      holds no document of its own.
#
# Document facts (FACT_CMD/FACT_SAVED) are proven in test-torirsmaped-embed,
# which has real mirror documents to apply them to; this script covers what
# that one structurally cannot — that the same protocol works between
# processes.
#
#   maped_daemon_test.sh <daemon-binary> <ctl-binary> <scratch-dir> <sample-jm2> <sample-jl2>

set -eu

DAEMON="$1"
CTL="$2"
SCRATCH="$3"
SAMPLE_JM2="$4"
SAMPLE_JL2="$5"
PORT="${TORIRSMAPED_TEST_PORT:-43611}"

FAILURES=0
CHECKS=0

check() {
    CHECKS=$((CHECKS + 1))
    if [ "$1" = "1" ]; then
        :
    else
        echo "FAIL: $2"
        FAILURES=$((FAILURES + 1))
    fi
}

contains() {
    # contains <file> <needle> <description>
    if grep -qF -- "$2" "$1"; then check 1 "$3"; else check 0 "$3 (looked for '$2' in $1)"; fi
}

lacks() {
    if grep -qF -- "$2" "$1"; then check 0 "$3 (found '$2' in $1)"; else check 1 "$3"; fi
}

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH/maps"
cp "$SAMPLE_JM2" "$SCRATCH/maps/"
cp "$SAMPLE_JL2" "$SCRATCH/maps/"

# --repo-root '' disables baking: a test must never shell out to a cache build.
"$DAEMON" "$PORT" --content-dir "$SCRATCH" --repo-root '' > "$SCRATCH/daemon.log" 2>&1 &
DAEMON_PID=$!
cleanup() { kill "$DAEMON_PID" 2>/dev/null || true; }
trap cleanup EXIT INT TERM

# The daemon binds before it loads, so a short settle is enough.
sleep 1

# 1. Two processes attach and are granted distinct Clients.
"$CTL" --port "$PORT" status > "$SCRATCH/status_a.log" 2>&1
"$CTL" --port "$PORT" status > "$SCRATCH/status_b.log" 2>&1
contains "$SCRATCH/status_a.log" "as client 1" "the first connection is granted Client 1"
contains "$SCRATCH/status_b.log" "as client 2" "the second connection is granted Client 2"
contains "$SCRATCH/status_a.log" "1 square(s) in the tree" "the daemon serves its content tree"

# 2 + 3. State reaches one Client's connections and no other's.
# Explicit pids, never job specs: this runs under a non-interactive shell
# with no job control, where `wait %1` is an error and `wait` alone would
# block on the daemon, which never exits.
"$CTL" --port "$PORT" --client 1 watch 4 > "$SCRATCH/watch_c1.log" 2>&1 &
WATCH_C1=$!
"$CTL" --port "$PORT" --client 2 watch 4 > "$SCRATCH/watch_c2.log" 2>&1 &
WATCH_C2=$!
sleep 1
"$CTL" --port "$PORT" --client 1 select 3200 3200 0 > "$SCRATCH/select.log" 2>&1
"$CTL" --port "$PORT" --client 1 tool 2 > "$SCRATCH/tool.log" 2>&1
wait "$WATCH_C1" || true
wait "$WATCH_C2" || true

contains "$SCRATCH/watch_c1.log" "sel  terrain at 3200,3200 level 0" \
    "a Client's own connections receive its selection, across processes"
contains "$SCRATCH/watch_c1.log" "tool 2" "and its tool change"
lacks "$SCRATCH/watch_c2.log" "sel  terrain" \
    "another Client never hears that selection"
lacks "$SCRATCH/watch_c2.log" "tool 2" "nor the tool change"

# 4. STATE_SYNC replays this Client's store only.
"$CTL" --port "$PORT" --client 1 sync > "$SCRATCH/sync_c1.log" 2>&1
"$CTL" --port "$PORT" --client 2 sync > "$SCRATCH/sync_c2.log" 2>&1
contains "$SCRATCH/sync_c1.log" "2 state key(s)" "a late joiner replays its Client's two keys"
contains "$SCRATCH/sync_c1.log" "sel  terrain at 3200,3200" "with the stored values"
contains "$SCRATCH/sync_c2.log" "0 state key(s)" "the other Client's store is empty"

# 5. The authoritative document answers a client that holds none.
"$CTL" --port "$PORT" open 50 50 > "$SCRATCH/open.log" 2>&1
contains "$SCRATCH/open.log" "m50_50 open:" "the document answers a square open"
contains "$SCRATCH/open.log" "dirty map=0 loc=0" "a freshly opened square is clean"

# 6. The browser's route in: the same daemon, upgraded, framed. Skipped
# rather than failed when python3 is absent — the probe needs a WebSocket
# client and hand-rolling one in sh is not a test, it is a liability.
if command -v python3 > /dev/null 2>&1; then
    if python3 "$(dirname "$0")/maped_ws_probe.py" "$PORT" > "$SCRATCH/ws.log" 2>&1; then
        check 1 "a WebSocket client completes the handshake and the protocol"
    else
        check 0 "a WebSocket client completes the handshake and the protocol"
        cat "$SCRATCH/ws.log"
    fi
    contains "$SCRATCH/ws.log" "0 failures" "every WebSocket probe check passed"
else
    echo "(python3 absent — skipping the WebSocket probe)"
fi

echo "maped_daemon_test: $CHECKS checks, $FAILURES failures"
[ "$FAILURES" -eq 0 ]
