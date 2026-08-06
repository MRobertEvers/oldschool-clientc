#!/usr/bin/env bash
# Drive the instrumented osrs239 client through every clientscript in the cache
# and collect per-opcode operand-stack telemetry (instr/src/CS2Trace.java).
#
#   tools/perf/cs2_sweep.sh [last-script-id]
#
# The client does the sweeping (CS2Sweep, posted back to the event queue in
# small batches); this only starts it, polls, and dumps. That split is the whole
# point: an earlier version ran a range inside one blocking control-channel
# command, so a client that died mid-range looked exactly like one still
# working — the socket sat there until its timeout. Now every request returns in
# milliseconds and a dead client is obvious on the next poll.
#
# Invoking scripts blind eventually runs one that takes the client down. That is
# expected: the telemetry is dumped after every window, and the client is
# relaunched and logged in again rather than losing the run. Chunk CSVs are
# merged by tools/perf/cs2_merge.py, which knows a relaunched client's counters
# start from zero again.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

LAST=${1:-9725}
WINDOW=${CS2_SWEEP_WINDOW:-250}
OUTDIR=${CS2_SWEEP_OUT:-/tmp/cs2_sweep}
PORT=${JCTL_PORT:-43601}

mkdir -p "$OUTDIR"
rm -f "$OUTDIR"/chunk_*.csv "$OUTDIR/live.csv"

jctl() { printf '%s\n' "$1" | nc -w "${2:-10}" 127.0.0.1 "$PORT" 2>/dev/null; }
alive() { [ -n "$(jctl stat 5)" ]; }

launch() {
    pkill -f 'net.runelite.client.RuneLite' 2>/dev/null
    sleep 2
    JAVA_EXTRA_OPTS="-Dcs2.stacktrace=true -Dcs2.stacktrace.out=$OUTDIR/live.csv" \
        nohup tools/perf/run_java_client.sh > /tmp/java_client.log 2>&1 &
    for _ in $(seq 1 40); do
        sleep 2
        alive && break
    done
    alive || { echo "client did not come up; see /tmp/java_client.log" >&2; return 1; }
    { printf 'click 461 291\nwait 1500\n'
      printf 'type testc\nwait 400\nkey 10\nwait 400\n'
      printf 'type test\nwait 400\nkey 10\nwait 22000\n'; } | nc -w 60 127.0.0.1 "$PORT" >/dev/null
    return 0
}

launch || exit 1

lo=0
while [ "$lo" -le "$LAST" ]; do
    hi=$((lo + WINDOW - 1))
    [ "$hi" -gt "$LAST" ] && hi=$LAST
    if ! alive; then
        echo "  client gone, relaunching before $lo"
        launch || break
    fi
    jctl "cs2sweep $lo $hi" >/dev/null
    # Poll rather than wait: the client answers throughout, so "stopped
    # answering" and "still working" are different observations again.
    for _ in $(seq 1 120); do
        sleep 1
        st=$(jctl cs2status 5)
        [ -z "$st" ] && break                 # died
        case "$st" in *idle*) break ;; esac
    done
    echo "  $lo..$hi ${st:-<client died>}"
    jctl cs2dump 10 >/dev/null
    [ -f "$OUTDIR/live.csv" ] && cp "$OUTDIR/live.csv" "$OUTDIR/chunk_$(printf '%05d' "$lo").csv"
    lo=$((hi + 1))
done

jctl cs2dump 10 >/dev/null
[ -f "$OUTDIR/live.csv" ] && cp "$OUTDIR/live.csv" "$OUTDIR/chunk_zfinal.csv"
python3 tools/perf/cs2_merge.py "$OUTDIR" "$OUTDIR/merged.csv"
