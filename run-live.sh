#!/bin/sh
# Run torirs from a boot manifest, natively or in a browser.
#
#   ./run-live.sh       <manifest.ini> [user] [pass] [client args...]
#   ./run-live.sh web   <manifest.ini> [user] [pass] [client args...]
#
# The manifest (manifest_rs254.ini, manifest_osrs230.ini, manifest_xrsps.ini, …)
# specifies cache, rev, transport, host/port and RSA keys. user/pass default to
# asdf/a. Anything after them is passed straight through to the client, so
# `./run-live.sh manifest_rs254.ini asdf a --offline` works. Extra behavior via
# env vars: TORIRS_NET_DEBUG=1, TORIRS_NET_CHEAT="tele 0,50,50,21,21",
# TORIRS_MAX_FRAMES/TORIRS_EXIT_BMP.
#
# For osrs230 / osrs239 (not --offline), this script always runs the in-process
# server: it builds with EMBED_SERVER=1, sets TORIRS_TRANSPORT=embed, and exports
# MOCK230_REV from the manifest so the embed world writes the same wire the
# client speaks. Hand-start `src/build/mock230 --rev …` + a TCP manifest
# yourself when you need a socket server (debugger, multiplayer, MOCK230_VERBOSE
# against a live listener).
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
# TORIRS_JAG_CRC is already set. Other revisions (e.g. the osrs230 embed) skip it.
set -eu

cd "$(dirname "$0")"

USAGE='usage: run-live.sh [web] <manifest.ini> [user] [pass] [client args...]'

MODE=native
if [ "${1:-}" = "web" ]; then
    MODE=web
    shift
fi

MANIFEST="${1:?$USAGE}"
shift
USER_NAME="${1:-asdf}"
[ $# -gt 0 ] && shift || true
PASS="${1:-a}"
[ $# -gt 0 ] && shift || true
# "$@" is now whatever should be handed to the client verbatim.

if [ ! -f "$MANIFEST" ]; then
    echo "run-live.sh: manifest '$MANIFEST' not found" >&2
    exit 1
fi

# rev + host come from the manifest [net:boot] section.
REV=$(sed -n 's/^[[:space:]]*rev[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)
HOST=$(sed -n 's/^[[:space:]]*host[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)
GAME_PORT=$(sed -n 's/^[[:space:]]*port[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)

# ws_host/ws_port: where a browser reaches the same server (the web build's
# sockets are WebSockets). For LostCity that is also where /crc lives, which is
# why the CRC fetch below uses it rather than assuming port 80. TORIRS_WS_* wins
# — the same override the client itself honours, so the two cannot disagree.
WS_HOST=$(sed -n 's/^[[:space:]]*ws_host[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)
WS_PORT=$(sed -n 's/^[[:space:]]*ws_port[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)
WS_HOST="${TORIRS_WS_HOST:-${WS_HOST:-${HOST:-localhost}}}"
WS_PORT="${TORIRS_WS_PORT:-${WS_PORT:-80}}"

# --offline never logs in, so the CRC handshake below does not apply to it.
OFFLINE=0
case " $* " in
    *" --offline "*) OFFLINE=1 ;;
esac

# osrs230 / osrs239 live runs use the in-process server (no socket, no mock230 child).
USE_EMBED=0
if { [ "$REV" = "osrs230" ] || [ "$REV" = "osrs239" ]; } && [ "$OFFLINE" = 0 ]; then
    USE_EMBED=1
    export TORIRS_TRANSPORT=embed
    # Embed defaults to osrs230 unless told otherwise; keep server wire = client rev.
    export MOCK230_REV="${MOCK230_REV:-$REV}"
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

# The script pack is a SEPARATE build from the binary, and an embedded server
# loads whatever `script.dat` was last compiled — not whatever the tree says
# today. Building the binary and not the pack is how a session ends up running
# content nobody has written for weeks: the C is current, the scripts are
# stale, and nothing anywhere reports the mismatch. A `[debugproc]` added an
# hour ago simply does not exist, which reads as "the cheat is broken".
#
# So the pack is built here, next to the binary that will load it, for every
# embedded run.
build_scripts() {
    echo "run-live.sh: building the server script pack..." >&2
    make -C src mock230-scripts || exit 1
}

if [ "$MODE" = native ]; then
    if [ "$USE_EMBED" = 1 ]; then
        echo "run-live.sh: $REV — building with EMBED_SERVER=1 (in-process server, MOCK230_REV=$MOCK230_REV)" >&2
        build_scripts
        make -C src EMBED_SERVER=1 torirs
    elif [ ! -x src/torirs ]; then
        echo "run-live.sh: building src/torirs..." >&2
        make -C src torirs
    fi
    exec src/torirs --manifest "$MANIFEST" --user "$USER_NAME" --pass "$PASS" "$@"
fi

# ---------------------------------------------------------------------- web

PORT="${TORIRS_WEB_PORT:-8088}"

WEB_TARGET=web
[ "${TORIRS_WEB_DEBUG:-0}" = "1" ] && WEB_TARGET=web-debug

# The module contains no manifests: the page fetches whichever one its query
# string names from the server, so a build never has to be redone for a new
# manifest, and nothing here depends on which one this run uses.
if [ "$USE_EMBED" = 1 ]; then
    echo "run-live.sh: $REV — building web with EMBED_SERVER=1 (in-process server, MOCK230_REV=$MOCK230_REV)" >&2
    build_scripts
    make -C src EMBED_SERVER=1 "$WEB_TARGET"
else
    make -C src "$WEB_TARGET"
fi

if [ ! -x src/build/io_server ]; then
    echo "run-live.sh: building the IO server..." >&2
    make -C src io-server
fi

# The client's argv, and every TORIRS_* variable, become the page's query
# string. Each value is percent-encoded on its own, so a password or a
# TORIRS_NET_CHEAT string may contain commas and spaces.
URL=$(MANIFEST="$MANIFEST" TORIRS_USER="$USER_NAME" TORIRS_PASS="$PASS" PORT="$PORT" \
      python3 - "$@" <<'PY'
import os, sys, urllib.parse

# The manifest lives at the root of the virtual filesystem (see PRELOAD above),
# so the client is told its basename, not the host path.
args = ['--manifest', os.path.basename(os.environ['MANIFEST']),
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

# A browser tab has no TCP. emscripten implements the client's sockets over
# WebSockets, so whatever the page dials must speak RFC 6455 — the manifest's
# transport=tcp describes what the *native* client dials, and says nothing
# about what the page ends up doing. osrs230 uses the in-process embed (no
# socket). For other revs, ws_port names where `/` upgrades (LostCity).
WS_ENDPOINT_KNOWN=0
[ -n "$(sed -n 's/^[[:space:]]*ws_port[[:space:]]*=.*/x/p' "$MANIFEST" | head -1)" ] && WS_ENDPOINT_KNOWN=1
[ "$USE_EMBED" = 1 ] && WS_ENDPOINT_KNOWN=1
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
echo "run-live.sh: serving on port $PORT — Ctrl-C to stop (the IO server stops with it)" >&2

wait "$IO_PID"
