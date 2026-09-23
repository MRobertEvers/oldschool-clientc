#!/bin/bash
#
# Start the game world and io_server, keep them up, and stop them in a way that
# does not throw away player progress.
#
# This is supervise.ps1's job, done as PID 1 of a container. The one thing it
# does that a plain `CMD` could not is the stop: `docker stop` sends SIGTERM to
# PID 1 and then SIGKILLs the whole container after its grace period, and
# torirsserver only writes a player's save when that player logs out. So the
# stop path here forwards SIGTERM and then WAITS for the game server to finish
# logging everyone out, and compose gives it a grace period long enough to do
# it (stop_grace_period in compose.yaml). Getting that wrong does not fail
# loudly — it just quietly loses whatever everyone online had done.
set -uo pipefail

ROOT=${TORIRS_ROOT:-/opt/torirs}
BIN=${TORIRS_BIN:-$ROOT/bin-linux}
LOGS=${TORIRS_LOGS:-$ROOT/logs}
GAME_PORT=${TORIRS_GAME_PORT:-43594}
WEB_PORT=${TORIRS_WEB_PORT:-8088}
REV=${TORIRS_REV:-osrs239}
MANIFEST=${TORIRS_MANIFEST:-manifests/manifest_osrs239.ini}
WEB_ROOT=${TORIRS_WEB_ROOT:-build-web}
# How long to let the game server log everyone out before giving up on it. Must
# be comfortably under compose's stop_grace_period or SIGKILL arrives first.
STOP_TIMEOUT=${TORIRS_STOP_TIMEOUT:-20}
# Seconds to wait before restarting a server that died on its own. Stops a
# binary that cannot start (a bad build, a missing cache) from spinning.
RESTART_DELAY=${TORIRS_RESTART_DELAY:-3}

# The server reads all of these; they are set here rather than baked into the
# image so that the same image serves a differently-laid-out deployment.
export TORIRSSERVER_BIND=${TORIRSSERVER_BIND:-0.0.0.0}
export TORIRSSERVER_CACHE=${TORIRSSERVER_CACHE:-$ROOT/cache.osrs239}
export TORIRSSERVER_CONTENT=${TORIRSSERVER_CONTENT:-$ROOT/content/osrs239-content}
export TORIRSSERVER_SAVES=${TORIRSSERVER_SAVES:-$ROOT/saves}

say() { printf 'torirs-docker: %s\n' "$*"; }
die() { printf 'torirs-docker: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- preflight
#
# Checked here rather than left to fail later, because each of these presents
# downstream as something that looks like a different bug: a missing binary as
# a crash loop, a missing cache as a world that boots empty, an unwritable
# saves directory as characters that reset on every logout.
[ -d "$ROOT" ]                 || die "deployment root $ROOT is not mounted"
[ -x "$BIN/torirsserver" ]     || die "no executable $BIN/torirsserver (built for linux?)"
[ -x "$BIN/io_server" ]        || die "no executable $BIN/io_server (built for linux?)"
[ -d "$TORIRSSERVER_CACHE" ]   || die "cache missing: $TORIRSSERVER_CACHE"
[ -d "$TORIRSSERVER_CONTENT" ] || die "content missing: $TORIRSSERVER_CONTENT"
[ -d "$TORIRSSERVER_SAVES" ]   || die "saves directory is not mounted: $TORIRSSERVER_SAVES"
[ -w "$TORIRSSERVER_SAVES" ]   || die "saves directory is not writable by uid $(id -u): $TORIRSSERVER_SAVES"
[ -d "$LOGS" ] && [ -w "$LOGS" ] || die "logs directory is not mounted writable: $LOGS"

cd "$ROOT" || die "cannot enter $ROOT"

say "root=$ROOT bin=$BIN uid=$(id -u) game=$GAME_PORT web=$WEB_PORT rev=$REV"
say "saves=$TORIRSSERVER_SAVES ($(ls -1 "$TORIRSSERVER_SAVES" | wc -l) file(s))"

# ---------------------------------------------------------------- processes
#
# Each server's output goes to BOTH its log file under the bind-mounted logs/
# (which is where the existing tooling and the Windows side look for it) and
# this process's stdout (which is where `docker logs` looks). The process
# substitution matters for more than tidiness: with a pipeline, `$!` would be
# the pid of `tee` and every signal below would be delivered to the wrong
# process — the game server would never be asked to stop, and would be
# SIGKILLed with everyone still logged in.
GAME_PID=0
WEB_PID=0

start_game() {
    "$BIN/torirsserver" "$GAME_PORT" --rev "$REV" \
        > >(tee -a "$LOGS/torirsserver.log") 2>&1 &
    GAME_PID=$!
    say "game server started (pid $GAME_PID)"
}

start_web() {
    "$BIN/io_server" --manifest "$MANIFEST" --root "$WEB_ROOT" \
        --boot-root . --port "$WEB_PORT" \
        > >(tee -a "$LOGS/io_server.log") 2>&1 &
    WEB_PID=$!
    say "io_server started (pid $WEB_PID)"
}

alive() { [ "$1" -gt 0 ] && kill -0 "$1" 2>/dev/null; }

# ---------------------------------------------------------------- shutdown
stopping=0

on_stop() {
    [ "$stopping" -eq 1 ] && return          # a second docker stop is not a new deadline
    stopping=1
    say "stop requested — asking the world to log everyone out"
    # The game server first and on its own: its SIGTERM handler logs every
    # player out, and each logout writes that player's save. io_server holds no
    # state, so it can go at the same time without anything being lost.
    alive "$GAME_PID" && kill -TERM "$GAME_PID" 2>/dev/null
    alive "$WEB_PID"  && kill -TERM "$WEB_PID"  2>/dev/null
}

trap on_stop TERM INT

start_game
start_web

while :; do
    # Returns when any child exits, and also when a trapped signal arrives —
    # which is what makes the stop prompt rather than waiting out a tick.
    wait -n 2>/dev/null

    if [ "$stopping" -eq 1 ]; then
        waited=0
        while alive "$GAME_PID" && [ "$waited" -lt "$STOP_TIMEOUT" ]; do
            sleep 1
            waited=$((waited + 1))
        done
        if alive "$GAME_PID"; then
            say "game server still up after ${STOP_TIMEOUT}s — killing it; SAVES MAY BE INCOMPLETE"
            kill -KILL "$GAME_PID" 2>/dev/null
        else
            say "game server exited cleanly after ${waited}s; saves written"
        fi
        alive "$WEB_PID" && kill -KILL "$WEB_PID" 2>/dev/null
        wait 2>/dev/null
        say "stopped"
        exit 0
    fi

    # Nobody asked us to stop, so something died on its own. Restart it, the
    # way supervise.ps1 does.
    if ! alive "$GAME_PID"; then
        say "game server exited unexpectedly — restarting in ${RESTART_DELAY}s"
        sleep "$RESTART_DELAY"
        [ "$stopping" -eq 1 ] || start_game
    fi
    if ! alive "$WEB_PID"; then
        say "io_server exited unexpectedly — restarting in ${RESTART_DELAY}s"
        sleep "$RESTART_DELAY"
        [ "$stopping" -eq 1 ] || start_web
    fi
done
