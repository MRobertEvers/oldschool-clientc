#!/usr/bin/env bash
# Watchdog for an autonomous Java-client run.
#
#   tools/perf/watchdog.sh <log> [deadline-sec=30] [jctl-port]
#
# The client has hung twice in this effort and both times it looked identical
# from outside: a live process, no output, no exception. Once it was a spin
# (100% CPU in a byte-overflow loop) and once it was a dead JVM held open by
# non-daemon AWT threads after RuneLite's startup failed. Neither ends, so a
# run without a deadline just stops making progress silently.
#
# So: wait for the client to prove it is alive -- the control port opens only
# after the canvas exists -- and if it does not by the deadline, capture WHY
# (a thread dump, top CPU thread, the log tail) and kill it. The diagnosis is
# the point; a bare timeout would leave the same mystery.
set -uo pipefail

LOG=${1:?usage: watchdog.sh <log> <deadline-sec> [jctl-port]}
DEADLINE=${2:-30}
PORT=${3:-43601}
JAVA_HOME=${JAVA_HOME:-/Library/Java/JavaVirtualMachines/temurin-21.jdk/Contents/Home}
DUMP="${LOG%.log}.hang.txt"

alive() { nc -z 127.0.0.1 "$PORT" >/dev/null 2>&1; }

waited=0
while [ "$waited" -lt "$DEADLINE" ]; do
    if alive; then
        echo "watchdog: client is up on 127.0.0.1:$PORT after ${waited}s"
        exit 0
    fi
    if ! pgrep -f "net.runelite.client.RuneLite" >/dev/null 2>&1; then
        echo "watchdog: process exited before the control port opened"
        tail -30 "$LOG" 2>/dev/null
        exit 2
    fi
    # A failed startup does not exit: RuneLite logs the failure and the JVM
    # stays up at 0% CPU on AWT's non-daemon threads, which is indistinguishable
    # from a hang until you read the log. Don't burn the deadline on it.
    if grep -qE "Failure during startup|Client error|Unable to start|CreationException" "$LOG" 2>/dev/null; then
        echo "watchdog: startup already failed -- not waiting out the deadline"
        grep -m1 -A6 -E "Failure during startup|Client error|CreationException" "$LOG"
        pkill -f "net.runelite.client.RuneLite" 2>/dev/null
        exit 3
    fi
    sleep 2
    waited=$((waited + 2))
done

PID=$(pgrep -f "net.runelite.client.RuneLite" | head -1)
echo "watchdog: no control port after ${DEADLINE}s (pid $PID) -- capturing $DUMP"
{
    echo "=== ps ==="
    ps -o pid,%cpu,etime,rss -p "$PID"
    echo
    echo "=== busiest threads (a spin shows up here; a deadlock does not) ==="
    top -l 2 -pid "$PID" -stats pid,cpu -n 1 2>/dev/null | tail -5
    echo
    echo "=== jstack ==="
    "$JAVA_HOME/bin/jstack" -l "$PID" 2>&1
    echo
    echo "=== log tail ==="
    tail -60 "$LOG" 2>/dev/null
} > "$DUMP" 2>&1

echo "watchdog: RUNNABLE threads with client frames (the usual suspect):"
grep -B2 -A6 'java.lang.Thread.State: RUNNABLE' "$DUMP" \
    | grep -A6 -E '"(Client|Preloader|main|AWT-EventQueue-0)"' | head -30

kill -9 "$PID" 2>/dev/null
echo "watchdog: killed $PID; full dump in $DUMP"
exit 1
