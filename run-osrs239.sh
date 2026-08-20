#!/usr/bin/env bash
# Bring up everything a real OldSchool client needs to play against ToriRSServer.
#
#   ./run-osrs239.sh                 server + jav_config + RuneLite
#   ./run-osrs239.sh --no-client     just the services (launch RuneLite yourself)
#   ./run-osrs239.sh --stop          stop whatever this script started
#   ./run-osrs239.sh --status        what is up, what is not
#   ./run-osrs239.sh --build         rebuild server, scripts and deob jar first
#
# Three pieces have to be up, and a missing one always presents the same way —
# the client sits on "Connecting to server..." forever:
#
#   ToriRSServer on 43594   the game server, with JS5 on the same socket (the client
#                      picks with its first byte: 14 game, 15 JS5)
#   RuneLite           1.12.33 + the recompiled deob gamepack
#
# and one thing you should never have to think about:
#
#   a jav_config server   RuneLite takes no server address. It takes
#                         `--jav_config=<URL>` and reads `codebase` out of it to
#                         learn which host to dial, and it fetches that over
#                         HTTP (OkHttp's HttpUrl.get refuses file://), so a
#                         local file will not do. This script runs one on 8080
#                         (what the runelite checkout hardcodes) and kills
#                         whatever is already there. Override with JAV_PORT=.
#
# Everything below exists because of a specific way this went wrong:
#
# - Health checks before launching the client, because "the server is down" and
#   "the config server is down" are indistinguishable from the login screen.
# - It TAKES the ports. Anything already on the game port, the config port or
#   any running RuneLite is killed first, so a run always starts from a clean
#   stack. A half-dead server from a previous run is indistinguishable from a
#   working one until the client hangs on it, and reclaiming is cheaper than
#   diagnosing. Pass --keep to leave existing processes alone.
# - A warning when the client cache is unseeded: the client fetches reference
#   tables over JS5 and then reads groups from its own on-disk cache, so without
#   it the first group stalls.
# - One account per session. There is no duplicate-login guard in the server, so
#   two clients on one username both "log in", load the same save, and fight;
#   the second one hangs. TORIRS_ACCOUNT names the one this script expects to be
#   in use so --status can say so.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

RUNDIR=${RUNDIR:-$ROOT/build/run239}
GAME_PORT=${GAME_PORT:-43594}
WORLD_ID=${WORLD_ID:-$((GAME_PORT - 40000))}
ENVIRONMENT=${ENVIRONMENT:-$([ "$GAME_PORT" = 43594 ] && printf 0 || printf 1)}
# 8080 by convention, because that is what the runelite checkout's
# run-osrs239-deob.sh and instr/RUNNING.md hardcode — moving it would mean this
# script and that one disagree about where the config lives. Anything already on
# it is killed (see reclaim), so "in use" is not a reason to pick another.
JAV_PORT=${JAV_PORT:-8080}
CACHE=${TORIRSSERVER_JS5_CACHE:-cache.osrs239}
CACHEDIR=${CACHEDIR:-torirs239}
DEOB=${DEOB_REPO:-$HOME/Documents/git_repos/Deobfuscator}
INSTR=$DEOB/instr/build/out/injected-client-1.12.33-instr.jar
PATCHED=$ROOT/build/rl239/client-1.12.33.jar
SEEDED=$HOME/jagexcache/$CACHEDIR/LIVE

mkdir -p "$RUNDIR"

say()  { printf '%s\n' "$*"; }
warn() { printf 'run-osrs239: %s\n' "$*" >&2; }
die()  { warn "$*"; exit 1; }

# `lsof` is the only portable-enough way to ask "is something listening" without
# connecting; a connect-probe would be answered by the JS5 handshake and look
# like success even when the game half is wedged.
listening() { lsof -nP -iTCP:"$1" -sTCP:LISTEN >/dev/null 2>&1; }

pidfile() { printf '%s/%s.pid' "$RUNDIR" "$1"; }
logfile() { printf '%s/%s.log' "$RUNDIR" "$1"; }

alive() {
    local f; f=$(pidfile "$1")
    [ -f "$f" ] || return 1
    kill -0 "$(cat "$f")" 2>/dev/null
}

# By PID, and only PIDs this script wrote. Never a pattern: the pattern matches
# a server someone else started and taking it out reads as a client hang.
stop_one() {
    local f; f=$(pidfile "$1")
    [ -f "$f" ] || return 0
    local pid; pid=$(cat "$f")
    if kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
        for _ in 1 2 3 4 5 6 7 8 9 10; do
            kill -0 "$pid" 2>/dev/null || break
            sleep 0.3
        done
        kill -9 "$pid" 2>/dev/null || true
        say "stopped $1 (pid $pid)"
    fi
    rm -f "$f"
}

# Everything holding a port, and every RuneLite, regardless of who started it.
#
# The polite version of this refused to touch a port it did not open, which is
# correct for a shared machine and wrong for a dev loop: the usual case is a
# leftover from the last run, and refusing just moves the failure to the client
# as a hang.
reclaim() {
    local port pids
    for port in "$GAME_PORT" "${JAV_PORT:-}"; do
        [ -n "$port" ] || continue
        pids=$(lsof -nP -iTCP:"$port" -sTCP:LISTEN -t 2>/dev/null || true)
        if [ -n "$pids" ]; then
            say "reclaiming port $port (killing $(echo "$pids" | tr '\n' ' '))"
            # shellcheck disable=SC2086
            kill $pids 2>/dev/null || true
            sleep 1
            pids=$(lsof -nP -iTCP:"$port" -sTCP:LISTEN -t 2>/dev/null || true)
            # shellcheck disable=SC2086
            [ -n "$pids" ] && { kill -9 $pids 2>/dev/null || true; sleep 1; }
        fi
    done
    pids=$(pgrep -f 'net.runelite.client.RuneLite' || true)
    if [ -n "$pids" ]; then
        say "killing running RuneLite ($(echo "$pids" | tr '\n' ' '))"
        # shellcheck disable=SC2086
        kill $pids 2>/dev/null || true
        sleep 2
        pids=$(pgrep -f 'net.runelite.client.RuneLite' || true)
        # shellcheck disable=SC2086
        [ -n "$pids" ] && kill -9 $pids 2>/dev/null || true
    fi
    rm -f "$(pidfile server)" "$(pidfile javconfig)" "$(pidfile client)"
}

cmd_stop() {
    stop_one client
    stop_one javconfig
    stop_one server
}

cmd_status() {
    for s in server javconfig client; do
        if alive "$s"; then
            say "$s: running (pid $(cat "$(pidfile "$s")"), log $(logfile "$s"))"
        else
            say "$s: not running"
        fi
    done
    listening "$GAME_PORT" && say "port $GAME_PORT: listening" || say "port $GAME_PORT: FREE"
    if [ -n "$JAV_PORT" ]; then
        listening "$JAV_PORT" && say "jav_config port $JAV_PORT: listening" \
            || say "jav_config port $JAV_PORT: FREE"
    fi
    local n
    n=$(lsof -nP -iTCP:"$GAME_PORT" 2>/dev/null | grep -c ESTABLISHED || true)
    say "connected clients: $n"
    [ "$n" -gt 0 ] && say "  (one account per session — a second client on the" \
        "same username hangs at 'Connecting to server')"
    return 0
}

cmd_build() {
    say "building server + scripts…"
    make -C src ToriRSServer -j8 >/dev/null
    make -C src torirsserver-scripts >/dev/null
    if [ -x "$DEOB/instr/build.sh" ]; then
        say "building instrumented client…"
        (cd "$DEOB" && ./instr/build.sh >/dev/null)
    else
        warn "no $DEOB/instr/build.sh — skipping the deob jar"
    fi
}

preflight() {
    [ -x src/build/torirsserver ] || die "no src/build/torirsserver — run with --build"
    [ -d "$CACHE" ] || die "no cache at $CACHE (JS5 serves it; see docs/RSPROT_OSRS239_PORT.md §4)"
    [ -f OSRS-Content/osrs239-content/server/scripts/build/script.dat ] \
        || die "no compiled scripts — run with --build (a fresh checkout has none)"

    if [ "$WANT_CLIENT" = yes ]; then
        [ -f "$INSTR" ]   || die "no instrumented jar: $INSTR (Deobfuscator/instr/build.sh)"
        [ -f "$PATCHED" ] || die "no patched client jar: $PATCHED (tools/runelite_patch.py)"
        [ -d "$SEEDED" ]  || warn "client cache not seeded at $SEEDED
    The client reads groups from its own on-disk cache after fetching the
    reference tables, so without this it stalls on the first group:
        mkdir -p $SEEDED && cp $CACHE/main_file_cache.* $SEEDED/"
    fi
}

start_server() {
    if listening "$GAME_PORT"; then
        say "server: already running on $GAME_PORT (--keep)"
        return 0
    fi
    say "starting ToriRSServer on $GAME_PORT (world + JS5 cache $CACHE)…"
    # One cache: the world reads the same directory JS5 serves. The two used to
    # differ (world on the bake, RuneLite served pristine), which meant the
    # world could act on config values no connected client had — point BOTH at
    # a baked cache when client-visible content matters, never just one.
    # Detach from the invoking terminal.  CI/agent command runners close their
    # pseudo-terminal as soon as this orchestration script exits; without
    # nohup all three children receive SIGHUP together and a healthy stack
    # looks like a protocol crash a few lines into boot.
    nohup env TORIRSSERVER_CACHE="$CACHE" \
        TORIRSSERVER_JS5_REV=239 TORIRSSERVER_JS5_CACHE="$CACHE" TORIRSSERVER_VERBOSE=1 \
        "$ROOT/src/build/torirsserver" "$GAME_PORT" --rev osrs239 \
        > "$(logfile server)" 2>&1 < /dev/null &
    echo $! > "$(pidfile server)"
    # The boot reads the cache, the content tree and the script pack; it is tens
    # of seconds on a cold page cache, and connecting before it listens is the
    # other way to get a client that hangs.
    for _ in $(seq 1 60); do
        listening "$GAME_PORT" && { say "server: up"; return 0; }
        alive server || { tail -5 "$(logfile server)" >&2; die "server exited during boot"; }
        sleep 1
    done
    die "server never listened on $GAME_PORT — see $(logfile server)"
}

start_javconfig() {
    if listening "$JAV_PORT"; then
        say "jav_config: already running on $JAV_PORT (--keep)"
        export JAV_CONFIG="http://127.0.0.1:$JAV_PORT/jav_config.ws"
        return 0
    fi
    say "starting jav_config on ${JAV_PORT} (internal)…"
    nohup python3 "$ROOT/tools/torirs_javconfig.py" --host 127.0.0.1 --port "$JAV_PORT" \
        --revision 239 --world-id "$WORLD_ID" --environment "$ENVIRONMENT" \
        --cachedir "$CACHEDIR" > "$(logfile javconfig)" 2>&1 < /dev/null &
    echo $! > "$(pidfile javconfig)"
    for _ in $(seq 1 20); do
        curl -sf -m 2 "http://127.0.0.1:$JAV_PORT/jav_config.ws" >/dev/null 2>&1 \
            && { say "jav_config: up"; return 0; }
        sleep 0.5
    done
    die "jav_config never answered — see $(logfile javconfig)"
}

start_client() {
    # The client learns the game host from this and nowhere else.
    export JAV_CONFIG="http://127.0.0.1:$JAV_PORT/jav_config.ws"
    say "starting RuneLite (jav_config $JAV_CONFIG)…"
    nohup env JAVA_EXTRA_OPTS="${JAVA_EXTRA_OPTS:-}" \
        "$ROOT/tools/perf/run_java_client.sh" > "$(logfile client)" 2>&1 < /dev/null &
    echo $! > "$(pidfile client)"
    say "client: launching (log $(logfile client))"
}

WANT_CLIENT=yes
DO_BUILD=no
DO_RECLAIM=yes
for arg in "$@"; do
    case "$arg" in
        --stop)      cmd_stop; exit 0 ;;
        --status)    cmd_status; exit 0 ;;
        --no-client) WANT_CLIENT=no ;;
        --build)     DO_BUILD=yes ;;
        --keep)      DO_RECLAIM=no ;;
        --help|-h)   sed -n '2,40p' "$0"; exit 0 ;;
        *)           die "unknown option: $arg (try --help)" ;;
    esac
done

[ "$DO_BUILD" = yes ] && cmd_build

preflight
[ "$DO_RECLAIM" = yes ] && reclaim
start_server
start_javconfig
[ "$WANT_CLIENT" = yes ] && start_client

say ""
say "ready. logs in $RUNDIR"
say "  server     $(logfile server)"
say "  jav_config $(logfile javconfig)  (port $JAV_PORT)"
[ "$WANT_CLIENT" = yes ] && say "  client     $(logfile client)"
say ""
say "stop with: $0 --stop"
say "check with: $0 --status"
