#!/usr/bin/env bash
# Run a boot manifest's world under the REAL OldSchool client: RuneLite, driving
# the recompiled deob gamepack, against this repo's mock server.
#
#   ./run-runelite.sh                            manifests/manifest_osrs239.ini
#   ./run-runelite.sh manifests/manifest_osrs239_rs2012.ini
#   ./run-runelite.sh --stop | --status
#   ./run-runelite.sh -- --developer-mode        args after -- go to RuneLite
#
# This is run-live.sh's counterpart: the same manifest selects the same cache,
# the same lane bake and the same server script pack, and the only thing that
# changes is which client draws the pixels. run-live.sh builds src/torirs and
# (for osrs230/osrs239) links the server into it; here the client is a JVM
# process that cannot be linked into anything, so the server runs beside it and
# the two talk over a socket.
#
# Four things have to line up, and a missing one always presents the same way --
# the client sits on "Connecting to server..." or drops back to the login
# screen with nothing in its own log:
#
#   the gamepack     Deobfuscator/instr/src recompiled into an injected-client
#                    jar. Checked for changes on every run and rebuilt when its
#                    sources moved; then gated on instr/tools/verify_api.py and
#                    instr/tools/verify_static_init.py. Both guard the same
#                    thing: something the decompiler changed that javac accepts.
#                    A dropped accessor is an AbstractMethodError inside a
#                    plugin hours away from this build; a forward-referenced
#                    static initialiser is a zero-length array that crashes
#                    whenever the field is finally used.
#   the RSA modulus  compiled into that jar. It has to be the manifest's
#                    rsa_mod, which is the mock server's public key -- otherwise
#                    the login block is encrypted to a key nothing here holds
#                    and login fails with no useful message on either side.
#   ToriRSServer          the world AND the JS5 cache server, on one socket (the
#                    client picks with its first byte: 14 game, 15 JS5).
#   a jav_config     RuneLite takes no server address. It takes
#                    `--jav_config=<URL>` and reads `codebase` out of it to
#                    learn which host to dial, over HTTP (OkHttp's HttpUrl.get
#                    refuses file://), so a local file will not do.
#
# The manifest's [net:boot] port is NOT the game port for an embed manifest --
# there it is a placeholder for a transport that never binds (see
# manifests/manifest_osrs239.ini). An OldSchool client cannot be handed an arbitrary port
# either: it derives one from the jav_config world id, as 40000 + world_id when
# the environment is nonzero, and the fixed 43594 when it is zero. So an embed
# manifest runs on 43594 (GAME_PORT= overrides) and only a transport=tcp
# manifest brings its own.
#
# A run TAKES the ports: anything listening on the game or config port, and any
# running RuneLite, is killed first — including one you started by hand a minute
# ago. A half-dead server from a previous run is indistinguishable from a working
# one until the client hangs on it, and reclaiming is cheaper than diagnosing.
# Pass --keep to leave existing processes alone.
#
# A machine with no JDK and no RuneLite install is one unzip from working:
# toolchains/java-toolchain-osrs239.zip is unpacked automatically when either is
# missing, and ignored when they are not. See toolchains/README.md.
#
# Knobs: GAME_PORT (43594), JAV_PORT (8080), CACHEDIR (~/jagexcache subdir),
# DEOB_REPO, RL_REPO, JAVA_HOME, TOOLCHAIN_DIR, RL_VERSION (1.12.33), TORIRS_MOCK_BIN,
# JAVA_EXTRA_OPTS, JPROF=1 (stage timers), JCTL_PORT (43601),
# TORIRS_RL_SET_MODULUS=1 (repoint the gamepack's key at this manifest),
# TORIRS_FORCE_DEOB_BUILD=1, TORIRS_NO_PREPARE=1 (skip the bake/script check).
#
# The client is driveable while it runs: JCTL is a line-oriented TCP channel on
# 127.0.0.1:43601 (`printf 'stat\n' | nc 127.0.0.1 43601`, plus click/type/key/
# shot/quit). Deobfuscator/instr/RUNNING.md §6 has the login sequence.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

# ------------------------------------------------------------------ arguments

MANIFEST=''
DO_BUILD_DEOB=auto
DO_RECLAIM=yes
SKIP_PREPARE="${TORIRS_NO_PREPARE:-0}"
WANT_CLIENT=yes
RL_ARGS=()

# The header block, however long it has become — a line range goes stale the
# first time anyone adds a paragraph to it, silently truncating the help.
usage() { awk 'NR>1 && /^#/ {print; next} NR>1 {exit}' "$0"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --) shift; RL_ARGS=("$@"); break ;;
        --stop|--status) ACTION=${1#--}; shift ;;
        --no-client)  WANT_CLIENT=no; shift ;;
        --build)      DO_BUILD_DEOB=force; shift ;;
        --no-build)   DO_BUILD_DEOB=never; shift ;;
        --keep)       DO_RECLAIM=no; shift ;;
        --help|-h)    usage; exit 0 ;;
        -*)           echo "run-runelite: unknown option: $1 (try --help)" >&2; exit 1 ;;
        *)
            [ -z "$MANIFEST" ] || { echo "run-runelite: one manifest, not two ($MANIFEST, $1)" >&2; exit 1; }
            MANIFEST=$1; shift ;;
    esac
done
ACTION=${ACTION:-run}

# The default the header advertises, and the one everything below is verified
# against. Any manifest whose [net:boot] rev is osrs239 works the same way.
MANIFEST=${MANIFEST:-manifests/manifest_osrs239.ini}

say()  { printf '%s\n' "$*"; }
warn() { printf 'run-runelite: %s\n' "$*" >&2; }
die()  { warn "$*"; exit 1; }

# ------------------------------------------------------------------- manifest

[ -f "$MANIFEST" ] || die "manifest '$MANIFEST' not found"

# Same keys, same one-line sed as run-live.sh, so the two launchers cannot read
# one manifest differently.
mkey() { sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*//p" "$MANIFEST" | head -1; }

REV=$(mkey rev)
TRANSPORT=$(mkey transport)
HOST=$(mkey host)
MANIFEST_PORT=$(mkey port)
RAW_CACHE_DIR=$(mkey dir)
RAW_SERVER_SCRIPTS=$(mkey scripts)
RSA_MOD=$(mkey rsa_mod)
CLIENT_VERSION=$(mkey client_version)
[ -n "$CLIENT_VERSION" ] || CLIENT_VERSION=$(mkey revision)

# BootManifest_LoadFile resolves cache and script paths against the manifest's
# own directory; the server has to receive the same resolved paths.
MANIFEST_DIR=${MANIFEST%/*}
[ "$MANIFEST_DIR" = "$MANIFEST" ] && MANIFEST_DIR=''
manifest_path() {
    case "$1" in
        '' | /*) printf '%s\n' "$1" ;;
        *) if [ -n "$MANIFEST_DIR" ]; then printf '%s/%s\n' "$MANIFEST_DIR" "$1"
           else printf '%s\n' "$1"; fi ;;
    esac
}
CACHE_DIR=$(manifest_path "$RAW_CACHE_DIR")
SERVER_SCRIPTS=$(manifest_path "$RAW_SERVER_SCRIPTS")

# The gamepack is one revision of one client. A manifest for any other era does
# not become playable by pointing this launcher at it -- the cache format, the
# packet numbering and the interface layouts all move together -- so say so here
# rather than letting it fail as a login timeout.
[ "$REV" = "osrs239" ] || die "this launcher is the revision-239 deob gamepack;
    '$MANIFEST' is rev '$REV'. Use ./run-live.sh for it."
[ -d "$CACHE_DIR" ] || die "no cache at '$CACHE_DIR' (the manifest's [cache:boot] dir=)"

# ------------------------------------------------------------------- endpoint

# See the header: an embed manifest's port is a placeholder for a transport that
# never binds, and the client cannot be told an arbitrary port anyway.
if [ "$TRANSPORT" = tcp ] && [ -n "$MANIFEST_PORT" ]; then
    GAME_PORT=${GAME_PORT:-$MANIFEST_PORT}
else
    GAME_PORT=${GAME_PORT:-43594}
fi
GAME_HOST=${GAME_HOST:-${HOST:-127.0.0.1}}
# `localhost` resolves to ::1 first on this machine and ToriRSServer binds IPv4
# loopback only, so the manifest's spelling would cost a connection refused.
if [ "$GAME_HOST" = localhost ]; then GAME_HOST=127.0.0.1; fi

JAV_PORT=${JAV_PORT:-8080}
# The client's port arithmetic, in reverse. 43594 is the live fixed port and
# needs environment 0; anything else has to come out of 40000 + world id.
if [ "$GAME_PORT" = 43594 ]; then
    WORLD_ID=${WORLD_ID:-1}
    ENVIRONMENT=${ENVIRONMENT:-0}
elif [ "$GAME_PORT" -gt 40000 ] 2>/dev/null && [ "$GAME_PORT" -le 65535 ]; then
    WORLD_ID=${WORLD_ID:-$((GAME_PORT - 40000))}
    ENVIRONMENT=${ENVIRONMENT:-1}
else
    die "game port $GAME_PORT is not one this client can be told to dial:
    it uses 43594, or 40000 + the jav_config world id. Pass GAME_PORT=43594."
fi

# Own only a loopback endpoint. A manifest naming a remote host is somebody
# else's server: point the config at it and start nothing.
OWN_SERVER=yes
case "$GAME_HOST" in
    127.0.0.1|0.0.0.0) ;;
    *) OWN_SERVER=no ;;
esac

# The client keeps its own on-disk cache under ~/jagexcache/<cachedir>, and a
# client of one revision reading a cache another wrote fails on the first
# archive whose format moved -- so never 'oldschool' (the live client's), and
# never one lane's directory for another lane's bake. The pristine cache keeps
# the name RUNNING.md and run-osrs239.sh already seeded, so the common case
# reuses a copy that is already on disk.
case "$(basename "$CACHE_DIR")" in
    cache.osrs239) CACHEDIR=${CACHEDIR:-torirs239} ;;
    *) CACHEDIR=${CACHEDIR:-torirs239_$(basename "$CACHE_DIR" | sed 's/^cache\.osrs239\.//; s/^cache\.//; s/[^A-Za-z0-9_.-]/_/g')} ;;
esac
SEEDED="$HOME/jagexcache/$CACHEDIR/LIVE"

RUNDIR=${RUNDIR:-$ROOT/build/run-runelite}
DEOB=${DEOB_REPO:-$HOME/Documents/git_repos/Deobfuscator}
RL_REPO=${RL_REPO:-$HOME/.runelite/repository2}
RL_VERSION=${RL_VERSION:-1.12.33}
INSTR_JAR=${INJECTED_CLIENT_JAR:-$DEOB/instr/build/out/injected-client-$RL_VERSION-instr.jar}
PATCHED_DIR=${PATCHED_DIR:-$ROOT/build/rl239}
PATCHED_CLIENT=$PATCHED_DIR/client-$RL_VERSION.jar
JAVA_HOME=${JAVA_HOME:-/Library/Java/JavaVirtualMachines/temurin-21.jdk/Contents/Home}

mkdir -p "$RUNDIR"

pidfile() { printf '%s/%s.pid' "$RUNDIR" "$1"; }
logfile() { printf '%s/%s.log' "$RUNDIR" "$1"; }

# lsof rather than a connect probe: the JS5 half answers a connect even when the
# game half is wedged, so a probe reports success on a server that cannot log
# anyone in.
listening() { lsof -nP -iTCP:"$1" -sTCP:LISTEN >/dev/null 2>&1; }

alive() {
    local f; f=$(pidfile "$1")
    [ -f "$f" ] || return 1
    kill -0 "$(cat "$f")" 2>/dev/null
}

# By PID, and only PIDs this script wrote. Never a pattern: the pattern matches
# a server someone else started, and taking it out reads as a client hang.
stop_one() {
    local f pid; f=$(pidfile "$1")
    [ -f "$f" ] || return 0
    pid=$(cat "$f")
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

cmd_stop() { stop_one client; stop_one javconfig; stop_one server; }

cmd_status() {
    local s n
    for s in server javconfig client; do
        if alive "$s"; then say "$s: running (pid $(cat "$(pidfile "$s")"), log $(logfile "$s"))"
        else say "$s: not running"; fi
    done
    listening "$GAME_PORT" && say "game port $GAME_PORT: listening" || say "game port $GAME_PORT: FREE"
    listening "$JAV_PORT"  && say "jav_config port $JAV_PORT: listening" || say "jav_config port $JAV_PORT: FREE"
    n=$(lsof -nP -iTCP:"$GAME_PORT" 2>/dev/null | grep -c ESTABLISHED || true)
    say "connected clients: $n"
    return 0
}

case "$ACTION" in
    stop)   cmd_stop; exit 0 ;;
    status) cmd_status; exit 0 ;;
esac

# ------------------------------------------------- content tree, bake, scripts

# Kept in step with run-live.sh, which explains the ordering: the submodule
# first (what everyone commits to), then checkouts under build/. This is the one
# thing duplicated from that file, because the bake below is a child process and
# cannot export a variable back to this one.
content_tree_has_lanes() {
    [ -d "$1/ported/scape2009_summoning" ] && [ -d "$1/ported/rs2012_qbd_td" ]
}
if [ -z "${TORIRSSERVER_CONTENT_DIR:-}" ]; then
    for candidate in OSRS-Content/osrs239-content build/*/osrs239-content; do
        if content_tree_has_lanes "$candidate"; then
            TORIRSSERVER_CONTENT_DIR=$(cd "$candidate" && pwd)
            break
        fi
    done
fi
if [ -n "${TORIRSSERVER_CONTENT_DIR:-}" ]; then
    # TORIRSSERVER_CONTENT_DIR is the BUILD side, TORIRSSERVER_CONTENT the RUNTIME side.
    # Setting only the first compiles script.dat into one tree and boots
    # another's -- see run-live.sh.
    export TORIRSSERVER_CONTENT_DIR TORIRSSERVER_CONTENT="$TORIRSSERVER_CONTENT_DIR"
fi

# The lane cache bake and the server script pack are run-live.sh's, invoked
# rather than reimplemented: a live server loads whatever script.dat was last
# compiled, and a lane cache is written by nothing else, so a launcher that
# skips this runs today's client against last week's content and reports
# nothing. TORIRS_FORCE_SCRIPT_BUILD / TORIRS_FORCE_CACHE_BAKE / TORIRS_NO_CACHE_BAKE
# all still apply -- they are read on that side.
if [ "$SKIP_PREPARE" != 1 ]; then
    TORIRS_PREPARE_ONLY=1 ./run-live.sh "$MANIFEST"
fi

# ------------------------------------------------------ the vendored toolchain

# toolchains/java-toolchain-osrs239.zip carries a JDK, RuneLite's jar repository
# and the deob's reference jars, so a machine with none of them installed is one
# unzip from a working launcher (toolchains/README.md, toolchains/vendor_java.sh).
#
# It is a FALLBACK and nothing else. An installed JDK and a real
# ~/.runelite/repository2 win, so having unpacked it once cannot start shadowing
# the installs on a machine that has them -- which is the failure mode a vendored
# toolchain usually introduces, and it presents as a build that ignores an
# updated jar for reasons nothing reports.
TOOLCHAIN_ZIP=${TOOLCHAIN_ZIP:-$ROOT/toolchains/java-toolchain-osrs239.zip}
TOOLCHAIN_DIR=${TOOLCHAIN_DIR:-$ROOT/toolchains/unpacked}

have_java()    { [ -x "$JAVA_HOME/bin/java" ] && [ -x "$JAVA_HOME/bin/javac" ]; }
have_rl_repo() { [ -f "$RL_REPO/client-$RL_VERSION.jar" ]; }

if ! have_java || ! have_rl_repo; then
    if [ ! -d "$TOOLCHAIN_DIR" ] && [ -f "$TOOLCHAIN_ZIP" ]; then
        say "unpacking the vendored java toolchain -> $TOOLCHAIN_DIR (once)…"
        # A partial unzip is worse than none: it satisfies the -d test above on
        # every later run while missing whatever the interruption cut off. Stage
        # and rename, so the directory either exists complete or not at all.
        rm -rf "$TOOLCHAIN_DIR.part"
        unzip -q "$TOOLCHAIN_ZIP" -d "$TOOLCHAIN_DIR.part" || die "unzip $TOOLCHAIN_ZIP failed"
        mv "$TOOLCHAIN_DIR.part" "$TOOLCHAIN_DIR"
    fi
    if ! have_java && [ -x "$TOOLCHAIN_DIR/jdk/temurin-21.jdk/Contents/Home/bin/javac" ]; then
        JAVA_HOME=$TOOLCHAIN_DIR/jdk/temurin-21.jdk/Contents/Home
        say "java: using the vendored JDK ($JAVA_HOME)"
    fi
    if ! have_rl_repo && [ -f "$TOOLCHAIN_DIR/runelite/repository2/client-$RL_VERSION.jar" ]; then
        RL_REPO=$TOOLCHAIN_DIR/runelite/repository2
        say "runelite: using the vendored jar repository ($RL_REPO)"
    fi
fi
export JAVA_HOME

# ------------------------------------------------------------- the deob client

have_java || die "no JDK at $JAVA_HOME (set JAVA_HOME, or unzip $TOOLCHAIN_ZIP)"
[ -d "$DEOB" ] || die "no Deobfuscator checkout at $DEOB (set DEOB_REPO)"
[ -x "$DEOB/instr/build.sh" ] || die "no $DEOB/instr/build.sh"

# instr/build.sh compiles against runelite-api-<ver>-runtime.jar, and
# verify_api.py reads gamepack-deob.jar as its baseline, both at paths fixed
# inside the Deobfuscator checkout -- outputs of a decompile run, not of the
# repository. Restore them from the vendored copies rather than failing several
# hundred javac errors deep in a build whose classpath is simply short one jar.
DEOB_TMP=$DEOB/tmp/deob_run_rl1_12_33
for _jar in "runelite-api-$RL_VERSION-runtime.jar" gamepack-deob.jar; do
    if [ ! -f "$DEOB_TMP/$_jar" ] && [ -f "$TOOLCHAIN_DIR/deob/$_jar" ]; then
        say "staging $_jar into $DEOB_TMP"
        mkdir -p "$DEOB_TMP"
        cp "$TOOLCHAIN_DIR/deob/$_jar" "$DEOB_TMP/$_jar"
    fi
done

# The modulus is compiled into the gamepack, so it is a property of the BUILD,
# not of the launch: it has to be checked before the staleness test, because
# changing it is a source edit that then has to be compiled.
if [ -n "$RSA_MOD" ]; then
    HAVE_MOD=$(python3 "$DEOB/instr/tools/set_modulus.py" --show 2>/dev/null | tail -1 | tr -d '[:space:]' || true)
    if [ "$HAVE_MOD" != "$RSA_MOD" ]; then
        if [ "${TORIRS_RL_SET_MODULUS:-0}" = 1 ]; then
            say "gamepack key != manifest rsa_mod — repointing it at this manifest"
            python3 "$DEOB/instr/tools/set_modulus.py" "$RSA_MOD"
            DO_BUILD_DEOB=force
        else
            die "the gamepack encrypts its login block with a key this server cannot read.
    gamepack: ${HAVE_MOD:-<none found>}
    manifest: $RSA_MOD
    Fix it (a source edit in $DEOB, idempotent) with either:
        TORIRS_RL_SET_MODULUS=1 $0 $MANIFEST
        python3 $DEOB/instr/tools/set_modulus.py $RSA_MOD"
        fi
    fi
fi

# "Checks the deob for changes": the jar against everything build.sh reads. A
# needless rebuild costs about a minute; a wrongly skipped one costs a session
# spent debugging a client that does not contain the change under test.
deob_stale() {
    if [ "$DO_BUILD_DEOB" = force ] || [ "${TORIRS_FORCE_DEOB_BUILD:-0}" = 1 ]; then
        return 0
    fi
    [ -f "$INSTR_JAR" ] || return 0
    local newer
    newer=$(find "$DEOB/instr/src" "$DEOB/instr/prebuilt" "$DEOB/instr/build.sh" \
        -newer "$INSTR_JAR" -print -quit 2>/dev/null || true)
    [ -n "$newer" ]
}

if [ "$DO_BUILD_DEOB" = never ]; then
    [ -f "$INSTR_JAR" ] || die "no gamepack jar: $INSTR_JAR (drop --no-build)"
elif deob_stale; then
    say "deob sources changed — rebuilding the gamepack…"
    ( cd "$DEOB" && VERSION="$RL_VERSION" RL_REPO="$RL_REPO" JAVA_HOME="$JAVA_HOME" \
        ./instr/build.sh ) || die "gamepack build failed"
    # The gate. RuneLite's injector adds ACC_BRIDGE|ACC_SYNTHETIC forwarders
    # that carry the whole plugin-facing API; every decompiler hides them, and a
    # jar missing one starts, renders, and then kills a plugin with an
    # AbstractMethodError nowhere near this build.
    if [ -x "$DEOB/instr/tools/verify_api.py" ]; then
        ( cd "$DEOB" && python3 instr/tools/verify_api.py ) \
            || die "API surface incomplete — see the census above"
    fi
    # The second gate, and it is the same shape of problem: something the
    # decompiler changed that javac accepts. CFR orders fields its own way and
    # folds <clinit> into the declarations, so a field initialised from one
    # declared below it gets qualified as `Self.later` -- legal Java that reads
    # the DEFAULT value. class70's nine draw arrays came out four int[0] and
    # five int[50] that way, and the first npc to say anything overhead killed
    # the client in Statics.method6709 with an index-0-of-length-0.
    if [ -x "$DEOB/instr/tools/verify_static_init.py" ]; then
        ( cd "$DEOB" && python3 instr/tools/verify_static_init.py ) \
            || die "forward-referenced static initialiser — see the list above"
    fi
else
    say "gamepack is up to date ($INSTR_JAR)"
fi
[ -f "$INSTR_JAR" ] || die "no gamepack jar after the build: $INSTR_JAR"

# RuneLite's own client jar needs two edits, and neither is a setting: it
# refuses a jav_config host that is not .jagex.com/.runescape.com, and it is
# signed, so the edit means unsigning it. Produced once into build/rl239 and
# left alone afterwards -- it does not depend on the deob sources.
if [ ! -f "$PATCHED_CLIENT" ]; then
    say "patching RuneLite's client jar (host allowlist) into $PATCHED_DIR…"
    STOCK_INJECTED=$DEOB/tmp/deob_run_rl1_12_33/injected-client-$RL_VERSION.jar
    [ -f "$STOCK_INJECTED" ] || STOCK_INJECTED=$RL_REPO/injected-client-$RL_VERSION.jar
    [ -f "$STOCK_INJECTED" ] || STOCK_INJECTED=$TOOLCHAIN_DIR/deob/injected-client-$RL_VERSION.jar
    [ -f "$STOCK_INJECTED" ] || die "no stock injected-client-$RL_VERSION.jar to patch beside
    (looked in $DEOB/tmp/deob_run_rl1_12_33, $RL_REPO and $TOOLCHAIN_DIR/deob)"
    [ -f "$RL_REPO/client-$RL_VERSION.jar" ] \
        || die "no $RL_REPO/client-$RL_VERSION.jar — RuneLite $RL_VERSION is the
    revision-239 release; 1.12.34 and later ship a rev-240 client."
    python3 tools/runelite_patch.py \
        --jar "$STOCK_INJECTED" \
        --client-jar "$RL_REPO/client-$RL_VERSION.jar" \
        --out "$PATCHED_DIR" \
        --modulus "${RSA_MOD:?manifest has no rsa_mod to patch with}" \
        || die "runelite_patch.py failed"
    [ -f "$PATCHED_CLIENT" ] || die "runelite_patch.py wrote no $PATCHED_CLIENT"
fi

# Classpath. The version pin is the whole point: two client-*.jar or two
# runelite-api-*.jar on one path silently runs whichever sorts first (both carry
# logback.xml and net.runelite.client.RuneLite), and this machine's repository2
# currently holds three of each. So take exactly the pinned pair by name and
# sweep in only the libraries.
[ -d "$RL_REPO" ] || die "no RuneLite jar repository at $RL_REPO (set RL_REPO)"
CP="$PATCHED_CLIENT:$INSTR_JAR"
API_JAR=$RL_REPO/runelite-api-$RL_VERSION-runtime.jar
[ -f "$API_JAR" ] || die "no $API_JAR"
CP="$CP:$API_JAR"
for j in "$RL_REPO"/*.jar; do
    case "$(basename "$j")" in
        client-*|injected-client-*|runelite-api-*) continue ;;
    esac
    CP="$CP:$j"
done

# ------------------------------------------------------------ the client cache

# Required, not an optimisation: the client fetches reference tables over JS5
# and then reads GROUPS from its own on-disk cache, so an unseeded one stalls on
# the first group with no error. Re-seeded when the served cache is newer --
# a lane rebake otherwise leaves the client reading the previous bake's groups.
seed_cache() {
    local src=$CACHE_DIR/main_file_cache.dat2
    [ -f "$src" ] || die "no $src to seed the client cache from"
    if [ -d "$SEEDED" ] && [ ! "$src" -nt "$SEEDED/main_file_cache.dat2" ] \
       && [ ! "$CACHE_DIR/main_file_cache.idx255" -nt "$SEEDED/main_file_cache.dat2" ]; then
        return 0
    fi
    say "seeding the client cache: $CACHE_DIR -> $SEEDED"
    mkdir -p "$SEEDED"
    cp "$CACHE_DIR"/main_file_cache.* "$SEEDED"/
}
if [ "$WANT_CLIENT" = yes ]; then seed_cache; fi

# --------------------------------------------------------------------- launch

# Take the ports. A half-dead server from the last run is indistinguishable from
# a working one until the client hangs on it, and reclaiming is cheaper than
# diagnosing. --keep leaves everything alone.
# shellcheck disable=SC2086  # $pids is a deliberately word-split PID list
reclaim() {
    local port pids
    for port in "$GAME_PORT" "$JAV_PORT"; do
        # A remote game port is somebody else's server; never reclaim it.
        if [ "$port" = "$GAME_PORT" ] && [ "$OWN_SERVER" = no ]; then continue; fi
        pids=$(lsof -nP -iTCP:"$port" -sTCP:LISTEN -t 2>/dev/null || true)
        if [ -n "$pids" ]; then
            say "reclaiming port $port (killing $(echo "$pids" | tr '\n' ' '))"
            kill $pids 2>/dev/null || true
            sleep 1
            pids=$(lsof -nP -iTCP:"$port" -sTCP:LISTEN -t 2>/dev/null || true)
            if [ -n "$pids" ]; then kill -9 $pids 2>/dev/null || true; sleep 1; fi
        fi
    done
    pids=$(pgrep -f 'net.runelite.client.RuneLite' || true)
    if [ -n "$pids" ]; then
        say "killing running RuneLite ($(echo "$pids" | tr '\n' ' '))"
        kill $pids 2>/dev/null || true
        sleep 2
        pids=$(pgrep -f 'net.runelite.client.RuneLite' || true)
        if [ -n "$pids" ]; then kill -9 $pids 2>/dev/null || true; fi
    fi
    rm -f "$(pidfile server)" "$(pidfile javconfig)" "$(pidfile client)"
    return 0
}

start_server() {
    [ "$OWN_SERVER" = yes ] || { say "server: $GAME_HOST:$GAME_PORT is not ours — starting nothing"; return 0; }
    if listening "$GAME_PORT"; then
        say "server: already listening on $GAME_PORT (--keep)"
        return 0
    fi
    # A deterministic, non-embedded native lane, the same one run-live.sh's web
    # path builds. The outer environment may carry OPT/MEMTRACE/sanitizer
    # variables for some other experiment; it must not decide which binary this
    # launcher then looks for.
    local bin=${TORIRS_MOCK_BIN:-src/build_opt/torirsserver}
    if [ -z "${TORIRS_MOCK_BIN:-}" ]; then
        say "building the native mock server…"
        make -C src PLATFORM=native OPT=1 MEMTRACE=0 ENABLE_ASAN=0 ENABLE_UBSAN=0 \
            TORIDRAW_NO_SIMD=0 TORIDRAW_OPT=0 EMBED_SERVER=0 ToriRSServer >/dev/null \
            || die "ToriRSServer build failed"
    fi
    [ -x "$bin" ] || die "mock binary '$bin' is not executable"

    say "starting ToriRSServer on $GAME_PORT (world + JS5, cache $CACHE_DIR)…"
    # ONE cache: the world reads the same directory JS5 serves. The two used to
    # differ (world on the bake, the client served pristine), which meant the
    # world acted on config values no connected client had.
    #
    # nohup: an agent/CI command runner closes its pseudo-terminal as soon as
    # this script's foreground job ends, and without it every child takes the
    # same SIGHUP -- a healthy stack that looks like a crash a few lines in.
    nohup env TORIRSSERVER_CACHE="$CACHE_DIR" \
        ${SERVER_SCRIPTS:+TORIRSSERVER_SCRIPTS="$SERVER_SCRIPTS"} \
        TORIRSSERVER_REV=osrs239 TORIRSSERVER_VERBOSE=1 \
        TORIRSSERVER_JS5_REV="${CLIENT_VERSION:-239}" TORIRSSERVER_JS5_CACHE="$CACHE_DIR" \
        "$bin" "$GAME_PORT" --rev osrs239 \
        > "$(logfile server)" 2>&1 < /dev/null &
    echo $! > "$(pidfile server)"
    # The boot reads the cache, the content tree and the script pack: tens of
    # seconds on a cold page cache, and connecting before it listens is the
    # other way to get a client that hangs.
    for _ in $(seq 1 90); do
        listening "$GAME_PORT" && { say "server: up"; return 0; }
        alive server || { tail -20 "$(logfile server)" >&2; die "server exited during boot"; }
        sleep 1
    done
    die "server never listened on $GAME_PORT — see $(logfile server)"
}

start_javconfig() {
    if listening "$JAV_PORT"; then
        say "jav_config: already listening on $JAV_PORT (--keep)"
        return 0
    fi
    say "starting jav_config on $JAV_PORT (codebase $GAME_HOST, world $WORLD_ID, env $ENVIRONMENT)…"
    nohup python3 "$ROOT/tools/torirs_javconfig.py" \
        --host "$GAME_HOST" --port "$JAV_PORT" \
        --revision "${CLIENT_VERSION:-239}" --world-id "$WORLD_ID" \
        --environment "$ENVIRONMENT" --cachedir "$CACHEDIR" \
        > "$(logfile javconfig)" 2>&1 < /dev/null &
    echo $! > "$(pidfile javconfig)"
    for _ in $(seq 1 20); do
        curl -sf -m 2 "http://127.0.0.1:$JAV_PORT/jav_config.ws" >/dev/null 2>&1 \
            && { say "jav_config: up"; return 0; }
        sleep 0.5
    done
    die "jav_config never answered — see $(logfile javconfig)"
}

# The services are this script's run. A Ctrl-C, or RuneLite's own exit, releases
# both -- otherwise the next run finds the ports held and fails to bind.
cleanup() { cmd_stop; }
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

if [ "$DO_RECLAIM" = yes ]; then reclaim; fi
start_server
start_javconfig

if [ "$WANT_CLIENT" != yes ]; then
    say ""
    say "services up. logs in $RUNDIR — Ctrl-C to stop them."
    say "  jav_config  http://127.0.0.1:$JAV_PORT/jav_config.ws"
    while alive server || alive javconfig; do sleep 1; done
    exit 0
fi

say ""
say "run-runelite: manifest   $MANIFEST"
say "run-runelite: cache      $CACHE_DIR  (client copy $SEEDED)"
say "run-runelite: gamepack   $INSTR_JAR"
say "run-runelite: client     $PATCHED_CLIENT"
say "run-runelite: server     $GAME_HOST:$GAME_PORT   logs $RUNDIR"
say ""

# RuneLite runs in the FOREGROUND, and deliberately not through exec: exec would
# replace this shell and with it the EXIT trap, leaving the server and the config
# holding their ports after the client window closed. The next run would then
# reclaim them, which works, but the state in between is a world still ticking
# with nobody in it.
#
# --add-exports is not optional on macOS: without it the JVM dies in
# OSXFullScreenAdapter with an IllegalAccessError nowhere near the game. -ea is
# required for --developer-mode, which is a fatal dialog without it. JCTL is the
# control channel (see the header); JPROF's stage timers stay off unless asked.
RC=0
env \
    JCTL="${JCTL:-1}" JCTL_PORT="${JCTL_PORT:-43601}" \
    ${JPROF:+JPROF="$JPROF"} ${JPROF_CSV:+JPROF_CSV="$JPROF_CSV"} \
    ${JPROF_WINDOW:+JPROF_WINDOW="$JPROF_WINDOW"} \
    "$JAVA_HOME/bin/java" -ea \
    ${JAVA_EXTRA_OPTS:-} \
    --add-exports java.desktop/com.apple.eawt=ALL-UNNAMED \
    --add-exports java.desktop/sun.awt=ALL-UNNAMED \
    --add-exports java.desktop/sun.java2d=ALL-UNNAMED \
    -cp "$CP" \
    net.runelite.client.RuneLite \
    --jav_config="http://127.0.0.1:$JAV_PORT/jav_config.ws" \
    --safe-mode \
    ${RL_ARGS[@]+"${RL_ARGS[@]}"} 2>&1 | tee "$(logfile client)" || RC=$?

say ""
say "run-runelite: client exited — stopping the server and jav_config."
exit "$RC"
