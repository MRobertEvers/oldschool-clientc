#!/bin/sh
# Run torirs from a boot manifest, natively or in a browser.
#
#   ./run-live.sh [--skip-checks]       <manifest.ini> [user] [pass] [client args...]
#   ./run-live.sh [--skip-checks] web   <manifest.ini> [user] [pass] [client args...]
#
# The manifest (manifests/manifest_rs254lc.ini, manifests/manifest_osrs230.ini, manifests/manifest_osrs233xrsps.ini, …)
# specifies cache, rev, transport, host/port and RSA keys. user/pass default to
# asdf/a. Anything after them is passed straight through to the client, so
# `./run-live.sh manifests/manifest_rs254lc.ini asdf a --offline` works. Extra behavior via
# env vars: TORIRS_NET_DEBUG=1, TORIRS_NET_CHEAT="tele 0,50,50,21,21",
# TORIRS_MAX_FRAMES/TORIRS_EXIT_BMP.
#
# TORIRS_SANITIZE=1 builds and runs the AddressSanitizer + UndefinedBehavior
# build (src/torirs_asan, its own object dir — src/torirs is left alone) with
# the options already set. Reach for it when the symptom is a visual glitch
# rather than a crash: ASan turns "it corrupts something occasionally" into a
# stack trace at the write that did it. Roughly 2x slower, much larger RSS.
# TORIRS_SANITIZE=leaks additionally turns LeakSanitizer on, which is off by
# default because it reports the whole steady-state heap and buries everything
# else.
#
#   TORIRS_SANITIZE=1 ./run-live.sh manifests/manifest_osrs239_torirs.ini --soft3d
#
# A manifest naming a marked-lane cache (cache.osrs239.rs2012,
# cache.osrs239.summoning) gets that cache baked here when the content tree has
# moved under it, because nothing else bakes those. TORIRS_FORCE_CACHE_BAKE=1
# bakes without asking.
#
# The ordinary content bake (cache.osrs239.baked, `torirsserver-cache`) is checked
# the same way and for the same reason -- it is what puts an edited config,
# interface, CS2 script or sprite in front of a client -- both when a manifest
# boots it directly and as layer 0 under an overlay that is based on it.
#
# One chain is NOT based on it: anything rooted at cache.osrs239.rs2012 is
# composed on the pristine cache.osrs239, so ordinary content cannot enter it at
# any freshness. That case prints what is missing rather than baking, since no
# bake reachable from here would carry it.
#
# The server script pack is compiled here too, when anything sscompile reads is
# newer than the last compile. TORIRS_FORCE_SCRIPT_BUILD=1 recompiles without
# asking.
#
# WHICH content lanes go into that pack is the manifest's `[content:lanes]`
# section — `lane=scape2009_summoning`, one per line — and a lane not named is
# not compiled at all, rather than compiled with its feature flag at 0. The
# lanes a tree declares are its own (`ported/<lane>/lane.ini`); list them with
# `make -C src torirsserver-lanes`.
#
# TORIRS_PREPARE_ONLY=1 does both of those and stops, without building or
# launching a client -- how run-runelite.sh borrows this file's bake policy
# rather than copying it.
#
# --skip-checks (or TORIRS_SKIP_CHECKS=1) skips both content preparation
# steps and runs the cache and script pack exactly as they stand. This is the
# fast client-code iteration path: the incremental client build still runs, so
# edited C is never silently ignored. Do not use it after changing content or
# server scripts unless running those stale artifacts is intentional.
#
# Native osrs230 / osrs239 live runs use the in-process server: they build with
# EMBED_SERVER=1 and set TORIRS_TRANSPORT=embed. Web live runs deliberately do
# not: a browser has no host filesystem for the embedded server's cache, content
# tree, or script pack. They start a native ToriRSServer child instead; the browser
# reaches it over WebSocket while its cache reads still go to io_server.
#
# `web` runs the emscripten build instead. The client is the same program with
# the same command line — it just arrives through the URL rather than argv, and
# its cache reads are answered by the IO server this script starts. Every
# TORIRS_* variable in the environment is forwarded the same way, so a run
# differs from the native one only in where the pixels land. See
# docs/web_build.md.
#
# Web-only knobs: TORIRS_WEB_PORT (default 8088), TORIRS_WEB_DEBUG=1 for the
# unoptimized build, TORIRS_WEB_NO_OPEN=1 to print the URL instead of opening a
# browser.
#
# For lc254 against a live LostCity server, login checks the cache CRCs against
# the server; this script fetches them from http://<host>/crc (port 80) unless
# TORIRS_JAG_CRC is already set. Other revisions (e.g. native osrs230 embed)
# skip it.
set -eu

cd "$(dirname "$0")"

# TORIRSSERVER_CONTENT_DIR selects the tree used by the build rules. The native mock
# reads TORIRSSERVER_CONTENT, so keep the two sides on the same checkout whenever a
# caller selects a non-default content tree.
if [ -n "${TORIRSSERVER_CONTENT_DIR:-}" ]; then
    export TORIRSSERVER_CONTENT="$TORIRSSERVER_CONTENT_DIR"
fi

USAGE='usage: run-live.sh [--skip-checks] [web] <manifest.ini> [user] [pass] [client args...]'

MODE=native
SKIP_CHECKS="${TORIRS_SKIP_CHECKS:-0}"
# Launcher options are leading arguments so they can never be confused with a
# client option. Accept `web` and --skip-checks in either order.
while [ "$#" -gt 0 ]; do
    case "$1" in
        web) MODE=web; shift ;;
        --skip-checks) SKIP_CHECKS=1; shift ;;
        *) break ;;
    esac
done

MANIFEST="${1:?$USAGE}"
shift

# A self-contained manifest may carry development credentials. Preserve the
# historical asdf/a fallback, while letting explicit launcher arguments win.
MANIFEST_USER=$(sed -n 's/^[[:space:]]*user[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)
MANIFEST_PASS=$(sed -n 's/^[[:space:]]*pass[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)
# user/pass are positional, but only when actually present -- a client flag
# (--d3d9-zbuffer, --soft3d, ...) must never be swallowed into either slot.
# Every client arg is long-form (--xxx), so that prefix is the one thing
# user/pass can never legitimately start with: stop taking positionally the
# moment the next argument looks like a flag, and let "$@" (and the
# manifest/asdf-a fallback above) pick it up instead. See run-live.ps1.
USER_NAME="${MANIFEST_USER:-asdf}"
case "${1:-}" in
    ''|--*) ;;
    *) USER_NAME=$1; shift ;;
esac
PASS="${MANIFEST_PASS:-a}"
case "${1:-}" in
    ''|--*) ;;
    *) PASS=$1; shift ;;
esac
# "$@" is now whatever should be handed to the client verbatim.

if [ ! -f "$MANIFEST" ]; then
    echo "run-live.sh: manifest '$MANIFEST' not found" >&2
    exit 1
fi

# rev + host come from the manifest [net:boot] section.
REV=$(sed -n 's/^[[:space:]]*rev[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)
MANIFEST_TRANSPORT=$(sed -n 's/^[[:space:]]*transport[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)
HOST=$(sed -n 's/^[[:space:]]*host[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)
GAME_PORT=$(sed -n 's/^[[:space:]]*port[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)
RAW_SERVER_SCRIPTS=$(sed -n 's/^[[:space:]]*scripts[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)
RAW_CACHE_DIR=$(sed -n 's/^[[:space:]]*dir[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)

# Which content lanes this profile's script pack is built from — `[content:lanes]`,
# one `lane=` per line (src/serverscript/ssc_lane.h).
#
# Section-scoped, unlike every other read above: `lane` is a plausible key
# elsewhere, and a manifest-wide sed would pick up a `[revconfig:…]` record that
# happens to use the word. The keys above are read manifest-wide because each of
# them appears exactly once and always in the section it belongs to; this one is
# a repeated key, so it is worth reading properly.
#
# This replaces guessing the lane set from the OUTPUT DIRECTORY'S NAME, which is
# what the build_summoning/build_curses/build_summoning_curses case below used to
# do. That could only ever recognise lanes the launcher had been taught, so a new
# lane was not launchable until this file learned its suffix — and a manifest that
# named a directory the pattern did not match silently got the pristine pack.
MANIFEST_LANES=$(awk '
    /^[[:space:]]*\[/ { in_section = ($0 ~ /^[[:space:]]*\[content:/); next }
    in_section && /^[[:space:]]*lane[[:space:]]*=/ {
        sub(/^[[:space:]]*lane[[:space:]]*=[[:space:]]*/, "")
        sub(/[[:space:]]*$/, "")
        if( $0 != "" ) printf "%s ", $0
    }
' "$MANIFEST")
MANIFEST_LANES=${MANIFEST_LANES% }

# BootManifest_LoadFile resolves cache and script paths against the manifest's
# directory. The standalone mock must receive the same resolved paths as the
# browser client and io_server, including for `subdir/boot.ini` invocations.
MANIFEST_DIR=${MANIFEST%/*}
[ "$MANIFEST_DIR" = "$MANIFEST" ] && MANIFEST_DIR=''
manifest_path() {
    case "$1" in
        '' | /*) printf '%s\n' "$1" ;;
        *)
            if [ -n "$MANIFEST_DIR" ]; then
                printf '%s/%s\n' "$MANIFEST_DIR" "$1"
            else
                printf '%s\n' "$1"
            fi
            ;;
    esac
}
# ONE ABSOLUTE PATH, resolved once, used by the build and by the server.
#
# The server is handed this value (`TORIRSSERVER_SCRIPTS`, below) and the script pack
# is compiled to it. They must be the same location, and the only reliable way
# to guarantee that is for them to be the same STRING - a relative path means
# "wherever the caller happens to stand", and the two callers do not stand in
# the same place: the server runs from the repo root, the compile runs through
# `make -C src`.
#
# That divergence was live: a manifest `scripts=OSRS-Content/.../build_x` had the
# server loading the real pack from the root while sscompile wrote to
# `src/OSRS-Content/.../build_x`. A phantom content tree grew under src/, every
# compile reported success, the staleness predicate honestly said "stale" on
# every run because the real pack never moved, and the client ran packs that were
# hours old. Absolute here, and there is no caller-relative path left to differ.
SERVER_SCRIPTS=$(manifest_path "$RAW_SERVER_SCRIPTS")
case "$SERVER_SCRIPTS" in
    '' | /*) ;;
    *) SERVER_SCRIPTS="$PWD/$SERVER_SCRIPTS" ;;
esac
CACHE_DIR=$(manifest_path "$RAW_CACHE_DIR")

# Every embedded run compiles the server scripts, and torirsserver-scripts feeds both
# ported/ lanes to sscompile unconditionally (SUMMONING_CLIENT_LANE and
# RS2012_QBD_TD_CLIENT_LANE in src/makefile). A checkout without them dies on a
# missing all.varbit.compack several targets deep, which reads as a broken
# working copy rather than the wrong content root — so find the tree instead of
# demanding the caller name it. TORIRSSERVER_CONTENT_DIR already set wins, and is
# obeyed as given: mid-port, the caller knows better than this check.
#
# run-live.ps1 does the same, in the same order, and the two must stay in step.
content_tree_has_lanes() {
    [ -d "$1/ported/scape2009_summoning" ] && [ -d "$1/ported/rs2012_qbd_td" ]
}

if [ -n "${TORIRSSERVER_CONTENT_DIR:-}" ]; then
    content_tree_has_lanes "$TORIRSSERVER_CONTENT_DIR" || echo \
        "run-live.sh: TORIRSSERVER_CONTENT_DIR=$TORIRSSERVER_CONTENT_DIR has no ported/ lanes -- the bakes will likely fail" >&2
else
    # The submodule first, then checkouts under build/. This used to be
    # reversed -- build/osrs-content-rs2012 (a worktree on
    # rs2012-facebake-v10-m60) carried 596 rebaked .ob3 the submodule's main
    # didn't have, so carrying the ported/ lane DIRECTORIES wasn't proof main
    # had the current bake. main has since gained its own rebake and moved
    # past that worktree's merge point on more than just the rs2012 lane, so
    # a frozen build/ worktree is now the stale tree, and the submodule --
    # what everyone actually commits to -- is preferred again. See
    # run-live.ps1.
    for candidate in OSRS-Content/osrs239-content build/*/osrs239-content; do
        if content_tree_has_lanes "$candidate"; then
            TORIRSSERVER_CONTENT_DIR=$(cd "$candidate" && pwd)
            export TORIRSSERVER_CONTENT_DIR
            break
        fi
    done
fi

# TORIRSSERVER_CONTENT_DIR is the BUILD side (src/makefile); TORIRSSERVER_CONTENT is the
# RUNTIME side (torirs_server_boot.c resolve_content_dir), which otherwise falls back
# to a hardcoded OSRS-Content/osrs239-content. Setting only the first compiles
# script.dat into one tree and boots another's -- the "Unknown command:
# rs2012qbdmanifest" failure. See run-live.ps1's Set-ContentTree.
if [ -n "${TORIRSSERVER_CONTENT_DIR:-}" ]; then
    TORIRSSERVER_CONTENT="$TORIRSSERVER_CONTENT_DIR"
    export TORIRSSERVER_CONTENT
fi

# ws_host/ws_port: where a browser reaches the same server (the web build's
# sockets are WebSockets). For LostCity that is also where /crc lives, which is
# why the CRC fetch below uses it rather than assuming port 80. TORIRS_WS_* wins
# — the same override the client itself honours, so the two cannot disagree.
MANIFEST_WS_HOST=$(sed -n 's/^[[:space:]]*ws_host[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)
MANIFEST_WS_PORT=$(sed -n 's/^[[:space:]]*ws_port[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)
WS_HOST="${TORIRS_WS_HOST:-${MANIFEST_WS_HOST:-${HOST:-localhost}}}"
WS_PORT="${TORIRS_WS_PORT:-${MANIFEST_WS_PORT:-80}}"

valid_port() {
    case "$1" in
        '' | *[!0-9]*) return 1 ;;
    esac
    [ "$1" -ge 1 ] 2>/dev/null && [ "$1" -le 65535 ] 2>/dev/null
}

# This is the endpoint the web client will actually dial. Unlike WS_PORT above,
# it falls back to the game port: 80 is only the historic LostCity CRC default,
# not the endpoint a manifest without ws_port tells the browser to use. Match
# BootManifest_ApplyWebEndpoint's treatment of a zero/unset ws_port.
WEB_GAME_HOST="${TORIRS_WS_HOST:-${MANIFEST_WS_HOST:-${HOST:-localhost}}}"
WEB_GAME_PORT="${GAME_PORT:-}"
if [ -n "${TORIRS_WS_PORT:-}" ]; then
    WEB_GAME_PORT="$TORIRS_WS_PORT"
elif valid_port "$MANIFEST_WS_PORT"; then
    WEB_GAME_PORT="$MANIFEST_WS_PORT"
fi

# The client command line takes precedence over the manifest and web endpoint
# overrides. Track its networking-relevant flags so the mock we own cannot
# boot on a different port or protocol revision than the page will use.
CLI_CONNECT=''
CLI_CONNECT_SET=0
CLI_PORT=''
CLI_PORT_SET=0
CLI_REV=''
CLI_VALUE=''
CLI_OFFLINE=0
for CLI_ARG in "$@"; do
    if [ -n "$CLI_VALUE" ]; then
        case "$CLI_VALUE" in
            connect)
                CLI_CONNECT=$CLI_ARG
                CLI_CONNECT_SET=1
                ;;
            port)
                CLI_PORT=$CLI_ARG
                CLI_PORT_SET=1
                ;;
            rev) CLI_REV=$CLI_ARG ;;
        esac
        CLI_VALUE=''
        continue
    fi
    case "$CLI_ARG" in
        --connect) CLI_VALUE=connect ;;
        --port) CLI_VALUE=port ;;
        --rev) CLI_VALUE=rev ;;
        --offline) CLI_OFFLINE=1 ;;
    esac
done

WEB_GAME_INLINE_PORT=0
if [ "$CLI_CONNECT_SET" = 1 ]; then
    WEB_GAME_HOST=$CLI_CONNECT
    case "$CLI_CONNECT" in
        *:*)
            WEB_GAME_HOST=${CLI_CONNECT%%:*}
            CLI_CONNECT_PORT=${CLI_CONNECT#*:}
            if [ -n "$CLI_CONNECT_PORT" ]; then
                WEB_GAME_PORT=$CLI_CONNECT_PORT
                WEB_GAME_INLINE_PORT=1
            fi
            ;;
    esac
fi
if [ "$CLI_PORT_SET" = 1 ] && [ "$WEB_GAME_INLINE_PORT" = 0 ]; then
    WEB_GAME_PORT=$CLI_PORT
fi

# NetTransport defaults an unset/zero port to 43594. A malformed explicit
# host:port is left invalid below so this launcher does not start a mock the
# client could never reach.
if [ "$WEB_GAME_INLINE_PORT" = 0 ] && { [ -z "$WEB_GAME_PORT" ] || [ "$WEB_GAME_PORT" = 0 ]; }; then
    WEB_GAME_PORT=43594
fi
CLIENT_REV=${CLI_REV:-$REV}

# --offline never logs in, so the CRC handshake below does not apply to it.
OFFLINE=0
if [ "$CLI_OFFLINE" = 1 ] && [ "$CLI_CONNECT_SET" = 0 ]; then
    OFFLINE=1
fi

# Native osrs230 / osrs239 live runs use the in-process server. The browser
# cannot: the wire web build intentionally has no local cache/content files for
# that server to open. It instead talks to a native ToriRSServer child over the
# browser's WebSocket-backed socket API.
USE_EMBED=0
USE_TORIRSSERVER=0
if [ "$MODE" = web ] && { [ "$CLIENT_REV" = "osrs230" ] || [ "$CLIENT_REV" = "osrs239" ] || \
                           [ "$MANIFEST_TRANSPORT" = "embed" ] || [ "${TORIRS_TRANSPORT:-}" = "embed" ]; }; then
    # An Emscripten TCP socket is an RFC 6455 WebSocket. Never leave an embed
    # selection in place for a web run: the browser module is intentionally
    # plain and its world belongs in a native process.
    export TORIRS_TRANSPORT=tcp
fi

if [ "$MODE" = native ]; then
    if { [ "$REV" = "osrs230" ] || [ "$REV" = "osrs239" ]; } && [ "$OFFLINE" = 0 ]; then
        USE_EMBED=1
        export TORIRS_TRANSPORT=embed
        # Embed defaults to osrs230 unless told otherwise; keep server wire = client rev.
        export TORIRSSERVER_REV="${TORIRSSERVER_REV:-$REV}"
    fi
elif [ "$OFFLINE" = 0 ]; then
    # Own only a reachable IPv4 loopback endpoint. ToriRSServer intentionally binds
    # 127.0.0.1 (not IPv6); a remote or explicit IPv6 endpoint belongs to the
    # caller and must not get an unused local process.
    if { [ "$CLIENT_REV" = "osrs230" ] || [ "$CLIENT_REV" = "osrs239" ]; } && valid_port "$WEB_GAME_PORT"; then
        case "$WEB_GAME_HOST" in
            localhost | 127.0.0.1) USE_TORIRSSERVER=1 ;;
        esac
    fi
fi

# ToriRSServer binds IPv4 loopback. Keep the standard manifest's `localhost` URL
# from depending on the browser's IPv6 fallback; an explicit --connect still
# wins later in the client's normal command-line parsing.
if [ "$USE_TORIRSSERVER" = 1 ] && [ "$CLI_CONNECT_SET" = 0 ]; then
    export TORIRS_WS_HOST=127.0.0.1
fi

# lc254 live login checks cache CRCs; fetch the 9 big-endian int32s from the
# server's web endpoint (TORIRS_JAG_CRC env wins over the manifest).
if [ "$REV" = "lc254" ] && [ "$OFFLINE" = 0 ] && [ -z "${TORIRS_JAG_CRC:-}" ]; then
    CRC_URL="http://${WS_HOST}:${WS_PORT}/crc"
    TORIRS_JAG_CRC=$(curl -sf "$CRC_URL" | python3 -c "
import sys, struct
d = sys.stdin.buffer.read()
print(','.join(str(x) for x in struct.unpack('>%di' % (len(d) // 4), d)))
")
    # A failing curl in a pipeline does not fail the pipeline (no pipefail in
    # POSIX sh), so the empty result is the thing to test — otherwise the client
    # is handed a blank TORIRS_JAG_CRC and the server answers "out of date".
    if [ -z "$TORIRS_JAG_CRC" ]; then
        echo "run-live.sh: cannot fetch $CRC_URL — is the server up?" >&2
        echo "  (the port comes from [net:boot] ws_port; override with TORIRS_WS_PORT)" >&2
        exit 1
    fi
    export TORIRS_JAG_CRC
fi

# The script pack is a SEPARATE build from the binary, and a live server loads
# whatever `script.dat` was last compiled — not whatever the tree says today.
# Building the binary and not the pack is how a session ends up running content
# nobody has written for weeks: the C is current, the scripts are stale, and
# nothing anywhere reports the mismatch. A `[debugproc]` added an hour ago
# simply does not exist, which reads as "the cheat is broken".
#
# So the pack is checked here for every embedded/native or mock/web live run —
# but torirsserver-scripts/torirsserver-scripts-summoning have no output file matching
# their own target name, so `make` reruns the ~15k-script sscompile pass
# unconditionally, .PHONY or not. Ask the predicate `make` would apply if the
# target were a real file — see tools/server_scripts_stale.py, which owns the
# input set and is the only implementation of it (run-live.ps1 calls the same
# script).
#
# Anything other than exit 1 rebuilds. A predicate that could not answer must
# never be read as "up to date": a needless rebuild costs a few seconds, and a
# wrongly skipped one costs a session spent debugging content that was never
# compiled.
build_scripts() {
    # Most manifests carry no scripts= at all -- it only needs stating when
    # build_summoning applies instead of torirsserver-scripts' own default output,
    # which is $TORIRSSERVER_CONTENT_DIR/server/scripts/build (src/makefile). The
    # content-tree discovery above has already run by this point, so
    # TORIRSSERVER_CONTENT_DIR is set unless no candidate tree was found at all --
    # in which case sscompile itself is about to fail on a missing --pack dir,
    # and this predicate failing the same way is the right answer.
    _out="$SERVER_SCRIPTS"
    [ -z "$_out" ] && [ -n "${TORIRSSERVER_CONTENT_DIR:-}" ] && _out="$TORIRSSERVER_CONTENT_DIR/server/scripts/build"
    # `SERVER_SCRIPTS` is already absolute (see where it is set, and why). The
    # fallback below is not, and `make -C src` would resolve it against src/.
    case "$_out" in
        '' | /*) ;;
        *) _out="$PWD/$_out" ;;
    esac
    if [ -n "$_out" ]; then
        # --tree: the script's own default is the OSRS-Content submodule, and
        # the tree in use is often a build/ checkout instead. Left to the
        # default the predicate watches a tree nobody is building from and
        # answers "fresh" for sources that moved.
        if python3 tools/server_scripts_stale.py \
                --out "$_out" \
                ${TORIRSSERVER_CONTENT_DIR:+--tree "$TORIRSSERVER_CONTENT_DIR"} \
                --input src/serverscript --input src/makefile \
                --input tools/ss_allocate.py >&2; then
            _stale=0
        else
            _stale=$?
        fi
        if [ "$_stale" = 1 ]; then
            echo "run-live.sh: server script pack is up to date (TORIRS_FORCE_SCRIPT_BUILD=1 to rebuild)" >&2
            return 0
        fi
    fi
    if [ -n "$MANIFEST_LANES" ]; then
        echo "run-live.sh: building the server script pack (lanes: $MANIFEST_LANES)..." >&2
    else
        echo "run-live.sh: building the server script pack..." >&2
    fi
    # A profile that names lanes gets exactly those, compiled into the directory
    # it named. A profile that names none is the pristine one, and goes through
    # `torirsserver-scripts` — the only target carrying the full set of content
    # contracts (God Wars, quest combat, agility XP, …). Those gates guard the
    # base tree, and the base tree is what that build is.
    if [ -n "$MANIFEST_LANES" ] || [ -n "$RAW_SERVER_SCRIPTS" ]; then
        make -C src torirsserver-scripts-lanes \
            TORIRSSERVER_SCRIPT_LANES="$MANIFEST_LANES" \
            ${_out:+TORIRSSERVER_SCRIPT_OUT="$_out"} || exit 1
    else
        make -C src torirsserver-scripts || exit 1
    fi
    # AND PROVE IT LANDED. A compile that reports success having written
    # somewhere else is the failure this whole comment block is about, and it is
    # invisible from its own output — sscompile prints the path it was given,
    # which is exactly the path that was wrong.
    #
    # So ask the predicate again. It was "stale" a moment ago by construction;
    # if it is still stale after a successful build, the bytes did not reach the
    # pack the server is about to load, and running on would mean running the
    # old one. That is never what the caller wanted.
    if [ -n "$_out" ]; then
        if python3 tools/server_scripts_stale.py \
                --out "$_out" \
                ${TORIRSSERVER_CONTENT_DIR:+--tree "$TORIRSSERVER_CONTENT_DIR"} \
                --input tools/ss_allocate.py >/dev/null 2>&1; then
            :
        elif [ $? = 1 ]; then
            return 0
        fi
        echo "run-live.sh: the script pack at $_out is STILL stale after a" >&2
        echo "  successful compile -- the output did not land where the server" >&2
        echo "  reads it. Refusing to launch on a stale pack." >&2
        exit 1
    fi
}

# The same argument, one layer down, for the marked lanes.
#
# `ported/` is excluded from the ordinary content walk on purpose, so a lane's
# records reach a client through exactly one route: its own bake. Nothing else
# writes cache.osrs239.rs2012 or cache.osrs239.summoning — not `torirsserver-cache`,
# which walks the tree without the lanes, and certainly not the pristine
# cache.osrs239 the other manifests boot. A launcher that skips this step runs
# today's client against whichever bake happened to be on disk, which is how six
# freshly imported QBD models can be committed, staged, and still not in game.
#
# Those targets are .PHONY, though: asking unconditionally spends a full copy of
# a 440 MB base cache plus a repack and a verify on every single launch. So ask
# the predicate `make` would apply if the target were a real file — see
# tools/cache_overlay_stale.py, which owns the input set for both lanes and is
# the only implementation of it (run-live.ps1 calls the same script).
#
# Anything other than exit 1 bakes. A predicate that could not answer must never
# be read as "up to date": a needless bake costs two minutes, and a wrongly
# skipped one costs a session spent debugging content that was never packed.
#
# $6 is the cache the named bake writes, and defaults to the one the manifest
# boots. It is a parameter because a composed lane layers two bakes: the first
# writes an intermediate the client never opens, and asking the predicate about
# $CACHE_DIR there would compare a lane against a cache it did not produce.
cache_overlay() {
    _label=$1
    _lane=$2
    _base=$3
    _stager=$4
    _target=$5
    _cache=${6:-$CACHE_DIR}
    # --tree: the script's own default is the OSRS-Content submodule, and the
    # whole point of the discovery above is that the tree in use is often a
    # build/ checkout instead. Left to the default the predicate watches a tree
    # nobody is building from and answers "fresh" for a lane that moved.
    if python3 tools/cache_overlay_stale.py \
            --cache "$_cache" --lane "$_lane" --base "$_base" \
            ${TORIRSSERVER_CONTENT_DIR:+--tree "$TORIRSSERVER_CONTENT_DIR"} \
            --input "$_stager" --input src/makefile \
            --input 3rd/rscache/tools/cachepack >&2; then
        _stale=0
    else
        _stale=$?
    fi
    if [ "$_stale" = 1 ]; then
        echo "run-live.sh: $_label cache overlay is up to date (TORIRS_FORCE_CACHE_BAKE=1 to rebake)" >&2
        return 0
    fi
    echo "run-live.sh: building the $_label cache overlay..." >&2
    make -C src "$_target" || exit 1
}

# Layer 0: the ORDINARY content bake, under everything above.
#
# `torirsserver-cache` is the target that walks the tree proper — configs/,
# interfaces, CS2, sprites, maps — and writes cache.osrs239.baked. Nothing asked
# for it here until now, and nothing else asks for it either, so the hole was
# exactly as wide as the one the lane check closes and shaped the same way: a
# manifest naming `cache.osrs239.baked` booted whichever bake somebody last ran
# by hand, and the summoning/curses overlays stacked on that base inherited its
# staleness silently. `configs/all.hitsplat` was a day newer than the cache
# every client in this tree was booting when this was written.
#
# The predicate is the same script with no --lane (tools/cache_overlay_stale.py),
# for the same reason the lane check is: one implementation, and `make` would
# apply this rule itself if the target were a real file.
#
# $1 is the cache to bake. That is the OVERLAY'S BASE, not $CACHE_DIR, whenever
# this runs as a layer under one — and once it rebakes, the overlay's own
# predicate sees a base newer than itself and rebakes in turn, which is how one
# content edit propagates all the way up a composition.
cache_base_bake() {
    _cache=$1
    # `make -C src` resolves a relative path against src/, not the repo root.
    case "$_cache" in
        /*) _abs=$_cache ;;
        *) _abs=$(pwd)/$_cache ;;
    esac
    if python3 tools/cache_overlay_stale.py \
            --cache "$_cache" \
            --base "${TORIRSSERVER_CACHE_BASE:-cache.osrs239}" \
            ${TORIRSSERVER_CONTENT_DIR:+--tree "$TORIRSSERVER_CONTENT_DIR"} \
            --input src/makefile \
            --input 3rd/rscache/tools/cachepack >&2; then
        _stale=0
    else
        _stale=$?
    fi
    if [ "$_stale" = 1 ]; then
        echo "run-live.sh: content cache $_cache is up to date (TORIRS_FORCE_CACHE_BAKE=1 to rebake)" >&2
        return 0
    fi
    echo "run-live.sh: baking the content cache $_cache..." >&2
    make -C src torirsserver-cache TORIRSSERVER_CACHE_DIR="$_abs" || exit 1
}

# The same question where there is no bake that could answer it.
#
# A cache in the rs2012 chain is composed on the PRISTINE `cache.osrs239`
# (RS2012_CACHE_BASE, src/makefile) rather than on the content bake, and each
# stager copies only its own `ported/` lane on top. So the ordinary tree is not
# an input to that chain at any freshness: rebaking every layer would not put a
# single edited config in it. Say so instead — the alternative is what this file
# spent six sessions learning to avoid, a launcher that looks prepared.
#
# Note the inverted rule: a predicate that could not answer stays SILENT here.
# For a bake, "cannot answer" must mean bake; for a warning, it would mean crying
# wolf on every launch of a checkout without python3.
cache_base_warn() {
    _cache=$1
    if python3 tools/cache_overlay_stale.py \
            --cache "$_cache" \
            ${TORIRSSERVER_CONTENT_DIR:+--tree "$TORIRSSERVER_CONTENT_DIR"} >&2; then
        _stale=0
    else
        _stale=$?
    fi
    [ "$_stale" = 0 ] || return 0
    echo "run-live.sh: ^^ $_cache is composed on the pristine cache.osrs239, so" >&2
    echo "  those ordinary-content edits are NOT in it and no bake here can put" >&2
    echo "  them there. Server-side work (.rs2, params, npc/loc server fields)" >&2
    echo "  travels by the script pack and is unaffected. For client-visible" >&2
    echo "  edits, rebase the chain: make -C src torirsserver-cache, then rerun with" >&2
    echo "  RS2012_CACHE_BASE=\$PWD/cache.osrs239.baked TORIRS_FORCE_CACHE_BAKE=1" >&2
}

# The bases match the Makefile's `?=` defaults, and honour the same overrides:
# a caller that moves SUMMONING_CACHE_BASE must not leave the freshness check
# watching a cache the bake no longer reads.
#
# A composed cache is deleted and repacked from scratch, which takes minutes and
# tears the cache out from under anything else reading it (a second client, an
# osrsify search wave). TORIRS_NO_CACHE_BAKE=1 runs against the cache as it
# already stands -- the right choice while iterating on C or on scripts, and the
# wrong one the moment the content tree changed.
build_cache_overlay() {
    if [ "${TORIRS_NO_CACHE_BAKE:-0}" = "1" ]; then
        echo "run-live.sh: TORIRS_NO_CACHE_BAKE=1 -- using $CACHE_DIR as it stands" >&2
        return 0
    fi
    case "$CACHE_DIR" in
        # The composed lane, in the order the Makefile composes it: the QBD/TD
        # bake writes the intermediate this one then uses as its base, so a
        # rebuilt first layer makes the second stale by its own predicate
        # (--base is one of the inputs it stamps against). Neither arm below
        # can match this name -- `*cache.osrs239.rs2012` does not match a
        # string that continues past `rs2012` -- so it needs its own.
        # Three layers, composed in Makefile order. Longest name first: the
        # `*cache.osrs239.rs2012-summoning` arm below would otherwise never
        # match this one, but a `*`-prefixed glob DOES match a longer suffix, so
        # this has to be tested before it.
        *cache.osrs239.rs2012-summoning-curses)
            _rs2012_base=${RS2012_CACHE_DIR:-cache.osrs239.rs2012}
            _rs2012_summ=cache.osrs239.rs2012-summoning
            cache_overlay "RS2012 QBD/TD" rs2012_qbd_td \
                "${RS2012_CACHE_BASE:-cache.osrs239}" \
                tools/stage_rs2012_overlay.py torirsserver-cache-rs2012 \
                "$_rs2012_base"
            cache_overlay "Summoning over QBD/TD" scape2009_summoning \
                "$_rs2012_base" \
                tools/stage_summoning_overlay.py torirsserver-cache-rs2012-summoning \
                "$_rs2012_summ"
            cache_overlay "Ancient Curses over Summoning" rs558_ancient_curses \
                "$_rs2012_summ" \
                tools/stage_curses_overlay.py torirsserver-cache-all
            cache_base_warn "$CACHE_DIR"
            ;;
        # Rooted on the content bake, so layer 0 runs first: a rebuilt base is
        # one of the overlay predicate's own inputs, which is what carries an
        # ordinary-content edit up into the overlay without a second rule.
        *cache.osrs239.curses)
            cache_base_bake "${CURSES_CACHE_BASE:-cache.osrs239.baked}"
            cache_overlay "Ancient Curses" rs558_ancient_curses \
                "${CURSES_CACHE_BASE:-cache.osrs239.baked}" \
                tools/stage_curses_overlay.py torirsserver-cache-curses
            ;;
        *cache.osrs239.rs2012-summoning)
            _rs2012_base=${RS2012_CACHE_DIR:-cache.osrs239.rs2012}
            cache_overlay "RS2012 QBD/TD" rs2012_qbd_td \
                "${RS2012_CACHE_BASE:-cache.osrs239}" \
                tools/stage_rs2012_overlay.py torirsserver-cache-rs2012 \
                "$_rs2012_base"
            cache_overlay "Summoning over QBD/TD" scape2009_summoning \
                "$_rs2012_base" \
                tools/stage_summoning_overlay.py torirsserver-cache-rs2012-summoning
            cache_base_warn "$CACHE_DIR"
            ;;
        *cache.osrs239.summoning)
            cache_base_bake "${SUMMONING_CACHE_BASE:-cache.osrs239.baked}"
            cache_overlay "Summoning" scape2009_summoning \
                "${SUMMONING_CACHE_BASE:-cache.osrs239.baked}" \
                tools/stage_summoning_overlay.py torirsserver-cache-summoning
            ;;
        # The QBD/TD lane's cache is composed, not shipped, so a manifest
        # naming it is a manifest asking for this bake.
        #
        # The merge that left conflict markers in this file also left a second,
        # unreachable copy of this arm — same pattern, so `case` could never
        # take it. Its one distinct act was `make torirsserver-servpack` alongside
        # the bake (the server half of the same tree: the npc/loc server fields
        # the boot reads out of <content>/server/pack). That never ran, so it
        # is not restored here; add it deliberately if the lane turns out to
        # need it.
        *cache.osrs239.rs2012)
            cache_overlay "RS2012 QBD/TD" rs2012_qbd_td \
                "${RS2012_CACHE_BASE:-cache.osrs239}" \
                tools/stage_rs2012_overlay.py torirsserver-cache-rs2012
            cache_base_warn "$CACHE_DIR"
            ;;
        # A manifest that boots the ordinary bake itself (the osrs239-net profile (profiles/osrs239-net.ini)).
        # It had no arm at all, so the one cache every non-lane profile runs on was
        # the only one nothing ever checked.
        *cache.osrs239.baked)
            cache_base_bake "$CACHE_DIR"
            ;;
    esac
}

# Cache and scripts are one consistency boundary: skipping only one leaves a
# run that looks prepared while its two server-side inputs describe different
# content revisions. Keep the fast path explicit and centralized at every
# native, web, and prepare-only call site.
prepare_live_content() {
    if [ "$SKIP_CHECKS" = 1 ]; then
        echo "run-live.sh: --skip-checks -- using the cache and server script pack as they stand (they may be stale)" >&2
        # The server refuses to boot on a stale pack (torirs_server_scripts.c). That
        # refusal is the guarantee; this flag is the one place a caller has
        # already said, in as many words, that stale is what they want. Carrying
        # the opt-out here keeps the two statements consistent instead of
        # letting the flag hit a wall it was designed to walk through.
        TORIRSSERVER_ALLOW_STALE_SCRIPTS=1
        export TORIRSSERVER_ALLOW_STALE_SCRIPTS
        return 0
    fi
    build_cache_overlay
    build_scripts
}

# TORIRS_PREPARE_ONLY=1 runs the manifest-driven preparation above -- the lane
# cache bake and the server script pack -- and stops, without building or
# launching a client. run-runelite.sh uses it: a RuneLite run needs the same
# baked cache and the same compiled scripts as a native run, and a second
# implementation of those two predicates is exactly the drift the comments above
# spend so long guarding against. Nothing else in this file runs in that mode.
if [ "${TORIRS_PREPARE_ONLY:-0}" = 1 ]; then
    prepare_live_content
    exit 0
fi

# Web OSRS runs put the world in a native process. ToriRSServer accepts both raw TCP
# and RFC 6455 on one port, so Emscripten's WebSocket-backed TCP implementation
# needs no proxy. Keep it a child of this script: a Ctrl-C must release both the
# page server and the game port.
MOCK_PID=''
start_torirsserver() {
    [ "$USE_TORIRSSERVER" = 1 ] || return 0
    [ "${TORIRS_NO_MOCK:-0}" != 1 ] || return 0

    # Build a deterministic, non-embedded native lane. The outer environment
    # may carry OPT/MEMTRACE/sanitizer variables for a client experiment; it
    # must not make the launcher look for build_opt/torirsserver after building a
    # different lane. A custom binary is already the caller's responsibility.
    MOCK_BIN="${TORIRS_MOCK_BIN:-src/build_opt/torirsserver}"
    MOCK_PORT="$WEB_GAME_PORT"
    if [ -z "${TORIRS_MOCK_BIN:-}" ]; then
        echo "run-live.sh: building the native mock server..." >&2
        make -C src PLATFORM=native OPT=1 MEMTRACE=0 ENABLE_ASAN=0 ENABLE_UBSAN=0 \
            TORIDRAW_NO_SIMD=0 TORIDRAW_OPT=0 EMBED_SERVER=0 ToriRSServer || return 1
    fi
    if [ ! -x "$MOCK_BIN" ]; then
        echo "run-live.sh: mock binary '$MOCK_BIN' is not executable" >&2
        return 1
    fi

    (
        if [ -z "${TORIRSSERVER_CACHE:-}" ] && [ -n "$CACHE_DIR" ]; then
            export TORIRSSERVER_CACHE="$CACHE_DIR"
        fi
        if [ -z "${TORIRSSERVER_SCRIPTS:-}" ] && [ -n "$SERVER_SCRIPTS" ]; then
            export TORIRSSERVER_SCRIPTS="$SERVER_SCRIPTS"
        fi
        export TORIRSSERVER_REV="$CLIENT_REV"
        exec "$MOCK_BIN" "$MOCK_PORT" --rev "$CLIENT_REV"
    ) &
    MOCK_PID=$!

    # A stale listener is worse than an early failure: it can make this run
    # appear to work against yesterday's world. Fail and let cleanup release
    # io_server instead of silently attaching to it.
    sleep 1
    if ! kill -0 "$MOCK_PID" 2>/dev/null; then
        wait "$MOCK_PID" 2>/dev/null || true
        MOCK_PID=''
        echo "run-live.sh: ToriRSServer exited during startup (game port $MOCK_PORT unavailable or mock failed to boot; see output above)" >&2
        return 1
    fi
}

stop_torirsserver() {
    if [ -n "$MOCK_PID" ]; then
        kill "$MOCK_PID" 2>/dev/null || true
        wait "$MOCK_PID" 2>/dev/null || true
        MOCK_PID=''
    fi
}

# TORIRS_SANITIZE=1 builds and runs the AddressSanitizer + UndefinedBehavior
# flavor instead of the ordinary one. Use it when something is corrupting memory
# or reading past a buffer and the symptom is visual rather than a crash -- ASan
# turns "it glitches sometimes" into a stack trace at the write that did it.
#
# It is its OWN target and object dir, so it never clobbers src/torirs and the
# next unsanitized run does not have to rebuild the world. Expect ~2x slower and
# a much larger RSS; that is the sanitizer, not a defect.
#
# On macOS this needs two things the Makefile already arranges under
# ENABLE_ASAN=1 and nothing here has to restate: a static SDL link (a dynamic
# one may allocate before ASan's interceptors are ready) and the dyld interpose
# dylib in src/platform/asan_dyld_shim.c, without which the process hangs at
# startup instead of running (LLVM #182943). The shim is linked as
# @loader_path/<objdir>/asan_dyld_shim.dylib, so the binary has to stay in src/
# -- which is where the build leaves it.
#
# UBSan is scoped to ToriDraw and to signed-integer-overflow (see src/makefile),
# and TORIDRAW_NO_SIMD=1 goes with it because UBSan cannot see inside the vector
# kernels -- without it the checked build is not checking the code that runs.
SANITIZE="${TORIRS_SANITIZE:-0}"
CLIENT_BIN=src/torirs
if [ "$SANITIZE" != 0 ]; then
    CLIENT_BIN=src/torirs_asan
    SAN_MAKE_ARGS="ENABLE_ASAN=1 ENABLE_UBSAN=1 TORIDRAW_NO_SIMD=1 \
        PLATFORM_OBJ_BASE=build_asan PLATFORM_TARGET=torirs_asan"
    # halt_on_error=0: keep reporting after the first finding. A visual glitch
    # usually has more than one cause and stopping at the first hides the rest.
    # detect_leaks defaults off because LSan reports the whole steady-state heap
    # on this client and buries the findings; TORIRS_SANITIZE=leaks turns it on.
    _leaks=0
    [ "$SANITIZE" = leaks ] && _leaks=1
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=$_leaks:halt_on_error=0:abort_on_error=0:print_stacktrace=1:log_path=stderr}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1}"
fi

if [ "$MODE" = native ]; then
    if [ "$USE_EMBED" = 1 ]; then
        echo "run-live.sh: $REV — building with EMBED_SERVER=1 (in-process server, TORIRSSERVER_REV=$TORIRSSERVER_REV)" >&2
        prepare_live_content
        if [ "$SANITIZE" != 0 ]; then
            echo "run-live.sh: TORIRS_SANITIZE=$SANITIZE — building $CLIENT_BIN with ASan+UBSan" >&2
            # Unquoted on purpose: SAN_MAKE_ARGS is a list of make assignments.
            # shellcheck disable=SC2086
            make -C src EMBED_SERVER=1 $SAN_MAKE_ARGS torirs_asan || exit 1
        else
            make -C src EMBED_SERVER=1 torirs
        fi
    elif [ "$SANITIZE" != 0 ]; then
        echo "run-live.sh: TORIRS_SANITIZE=$SANITIZE — building $CLIENT_BIN with ASan+UBSan" >&2
        # shellcheck disable=SC2086
        make -C src $SAN_MAKE_ARGS torirs_asan || exit 1
    elif [ ! -x src/torirs ]; then
        echo "run-live.sh: building src/torirs..." >&2
        make -C src torirs
    fi
    if [ "$SANITIZE" != 0 ]; then
        # Prove the two macOS prerequisites actually made it into the link
        # rather than discovering it as a silent hang.
        if command -v otool >/dev/null 2>&1; then
            otool -L "$CLIENT_BIN" | grep -q 'libclang_rt.asan' \
                || { echo "run-live.sh: $CLIENT_BIN is not linked against the ASan runtime" >&2; exit 1; }
            otool -L "$CLIENT_BIN" | grep -q 'asan_dyld_shim' \
                || echo "run-live.sh: warning — no asan_dyld_shim in $CLIENT_BIN; a startup hang is the expected failure" >&2
        fi
    fi
    exec "$CLIENT_BIN" --manifest "$MANIFEST" --user "$USER_NAME" --pass "$PASS" "$@"
fi

# ---------------------------------------------------------------------- web

PORT="${TORIRS_WEB_PORT:-8088}"

WEB_TARGET=web
[ "${TORIRS_WEB_DEBUG:-0}" = "1" ] && WEB_TARGET=web-debug

# The module contains no manifests: the page fetches whichever one its query
# string names from the server, so a build never has to be redone for a new
# manifest, and nothing here depends on which one this run uses. In particular,
# never link EMBED_SERVER into the web module: the native mock below owns that
# world and can open the host cache/content filesystem.
if [ "$USE_TORIRSSERVER" = 1 ] && [ "${TORIRS_NO_MOCK:-0}" != 1 ]; then
    prepare_live_content
fi
# Command-line assignment is deliberate: even an inherited EMBED_SERVER=1
# must not turn a `run-live.sh web` build into an embedded-world module.
make -C src EMBED_SERVER=0 "$WEB_TARGET"

if [ ! -x src/build/io_server ]; then
    echo "run-live.sh: building the IO server..." >&2
    make -C src io-server
fi

# The client's argv, and every TORIRS_* variable, become the page's query
# string. Each value is percent-encoded on its own, so a password or a
# TORIRS_NET_CHEAT string may contain commas and spaces.
URL=$(MANIFEST="$MANIFEST" REPO_ROOT="$(pwd)" TORIRS_USER="$USER_NAME" TORIRS_PASS="$PASS" PORT="$PORT" \
      python3 - "$@" <<'PY'
import os, sys, urllib.parse

# Boot files are served beneath io_server's repo-rooted `/boot/` route and
# placed in MEMFS at the same relative name. Preserve a nested manifest path so
# its cache/scripts/revconfig relatives resolve exactly like a native run.
root = os.path.abspath(os.environ['REPO_ROOT'])
manifest = os.path.abspath(os.environ['MANIFEST'])
try:
    manifest_arg = os.path.relpath(manifest, root)
except ValueError:
    manifest_arg = '..'
if manifest_arg == '..' or manifest_arg.startswith('..' + os.sep):
    raise SystemExit('run-live.sh: web manifests must be under the repository root')
manifest_arg = manifest_arg.replace(os.sep, '/')

args = ['--manifest', manifest_arg,
        '--user', os.environ['TORIRS_USER'],
        '--pass', os.environ['TORIRS_PASS']] + sys.argv[1:]

query = [('arg', a) for a in args]
for key, value in sorted(os.environ.items()):
    # TORIRS_WEB_* are this script's own knobs, and USER/PASS were just spliced
    # into argv above; neither belongs in the client's environment.
    if key.startswith('TORIRS_') and not key.startswith('TORIRS_WEB_') \
            and key not in ('TORIRS_USER', 'TORIRS_PASS'):
        query.append(('env', '%s=%s' % (key, value)))

print('http://localhost:%s/?%s' % (os.environ['PORT'], urllib.parse.urlencode(query)))
PY
)

# The IO server is this script's child: it dies when this script does, however
# this script dies. Without that, a Ctrl-C leaves a process holding the port and
# the next run fails to bind.
IO_PID=''
cleanup() {
    stop_torirsserver
    if [ -n "$IO_PID" ]; then
        kill "$IO_PID" 2>/dev/null || true
        wait "$IO_PID" 2>/dev/null || true
        IO_PID=''
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

./src/build/io_server --manifest "$MANIFEST" --root build-web --port "$PORT" &
IO_PID=$!

# Give it a moment to bind, and fail loudly rather than opening a dead page.
sleep 1
if ! kill -0 "$IO_PID" 2>/dev/null; then
    echo "run-live.sh: io_server exited during startup (port $PORT already in use?)" >&2
    exit 1
fi

if ! start_torirsserver; then
    exit 1
fi

# A browser tab has no TCP. emscripten implements the client's sockets over
# WebSockets, so whatever the page dials must speak RFC 6455 — the manifest's
# transport=tcp describes what the *native* client dials, and says nothing
# about what the page ends up doing. The local mock accepts that upgrade on the
# game port; other servers name their WebSocket endpoint with ws_port.
WS_ENDPOINT_KNOWN=0
[ -n "$(sed -n 's/^[[:space:]]*ws_port[[:space:]]*=.*/x/p' "$MANIFEST" | head -1)" ] && WS_ENDPOINT_KNOWN=1
[ -n "${TORIRS_WS_PORT:-}" ] && WS_ENDPOINT_KNOWN=1
[ "$USE_TORIRSSERVER" = 1 ] && WS_ENDPOINT_KNOWN=1
if [ "$OFFLINE" = 0 ] && [ "$WS_ENDPOINT_KNOWN" = 0 ]; then
    echo "run-live.sh: note — a browser reaches this server over a WebSocket, and" >&2
    echo "  this manifest names no ws_port, so the page will dial ${HOST:-localhost}:${GAME_PORT}." >&2
    echo "  If that port speaks raw TCP only, add ws_port= to [net:boot], put a" >&2
    echo "  bridge in front (websockify ${HOST:-localhost}:8443 ${HOST:-localhost}:${GAME_PORT})," >&2
    echo "  or pass --offline to run against the cache alone." >&2
fi

echo "run-live.sh: $URL"
if [ "${TORIRS_WEB_NO_OPEN:-0}" != "1" ]; then
    if command -v open >/dev/null 2>&1; then
        open "$URL"
    elif command -v xdg-open >/dev/null 2>&1; then
        xdg-open "$URL"
    fi
fi
echo "run-live.sh: serving on port $PORT — Ctrl-C to stop (child services stop with it)" >&2

# io_server and ToriRSServer are one live run. Do not leave a page server advertising
# a dead game endpoint if the mock crashes after its startup check.
while kill -0 "$IO_PID" 2>/dev/null; do
    if [ -n "$MOCK_PID" ] && ! kill -0 "$MOCK_PID" 2>/dev/null; then
        wait "$MOCK_PID" 2>/dev/null || true
        MOCK_PID=''
        echo "run-live.sh: ToriRSServer stopped unexpectedly; stopping the web run" >&2
        exit 1
    fi
    sleep 1
done

if wait "$IO_PID"; then
    exit 0
else
    exit $?
fi
