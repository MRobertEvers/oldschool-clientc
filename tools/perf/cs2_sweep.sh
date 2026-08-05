#!/usr/bin/env bash
# Drive the instrumented osrs239 client through every clientscript in the cache
# and collect per-opcode operand-stack telemetry.
#
#   tools/perf/cs2_sweep.sh [last-script-id] [chunk]
#
# Why a driver rather than one command: invoking scripts blind — with no
# arguments beyond the id, which is the point, because that is what reaches the
# opcodes no screen reaches — eventually runs one that takes the client down.
# The client is therefore treated as expendable: each chunk dumps before the
# next one starts, and a dead client is relaunched and logged in again rather
# than losing the run. Chunk CSVs are merged at the end by
# tools/perf/cs2_merge.py.
#
# Everything the telemetry needs is in instr/src/CS2Trace.java; the client only
# records when -Dcs2.stacktrace=true, which run_java_client.sh passes through
# JAVA_EXTRA_OPTS.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

LAST=${1:-9725}
CHUNK=${2:-250}
OUTDIR=${CS2_SWEEP_OUT:-/tmp/cs2_sweep}
PORT=${JCTL_PORT:-43601}

mkdir -p "$OUTDIR"
rm -f "$OUTDIR"/chunk_*.csv

jctl() { printf '%s\n' "$1" | nc -w "${2:-120}" 127.0.0.1 "$PORT" 2>/dev/null; }
alive() { [ -n "$(jctl stat 10)" ]; }

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
    # Log in. The scene has to exist before scripts that touch it will run.
    { printf 'click 461 291\nwait 1500\n'
      printf 'type testc\nwait 400\nkey 10\nwait 400\n'
      printf 'type test\nwait 400\nkey 10\nwait 22000\n'; } | nc -w 60 127.0.0.1 "$PORT" >/dev/null
    return 0
}

launch || exit 1

lo=0
while [ "$lo" -le "$LAST" ]; do
    hi=$((lo + CHUNK - 1))
    [ "$hi" -gt "$LAST" ] && hi=$LAST
    if ! alive; then
        echo "  client gone, relaunching before $lo"
        launch || break
    fi
    out=$(jctl "cs2run $lo $hi" 600)
    echo "  $lo..$hi ${out:-<client died>}"
    # Dump after every chunk: the next one may be the one that kills it.
    jctl cs2dump 60 >/dev/null
    if [ -f "$OUTDIR/live.csv" ]; then
        cp "$OUTDIR/live.csv" "$OUTDIR/chunk_$(printf '%05d' "$lo").csv"
    fi
    lo=$((hi + 1))
done

jctl cs2dump 60 >/dev/null
[ -f "$OUTDIR/live.csv" ] && cp "$OUTDIR/live.csv" "$OUTDIR/chunk_final.csv"
echo "chunks in $OUTDIR"
python3 tools/perf/cs2_merge.py "$OUTDIR" "$OUTDIR/merged.csv"
