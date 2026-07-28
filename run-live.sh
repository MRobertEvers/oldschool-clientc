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
# TORIRS_JAG_CRC is already set. Other revisions (e.g. the osrs230 mock) skip it.
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
TRANSPORT=$(sed -n 's/^[[:space:]]*transport[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)

# --offline never logs in, so the CRC handshake below does not apply to it.
OFFLINE=0
case " $* " in
    *" --offline "*) OFFLINE=1 ;;
esac

# lc254 live login checks cache CRCs; fetch the 9 big-endian int32s from the
# server's web endpoint (TORIRS_JAG_CRC env wins over the manifest).
if [ "$REV" = "lc254" ] && [ "$OFFLINE" = 0 ] && [ -z "${TORIRS_JAG_CRC:-}" ]; then
    TORIRS_JAG_CRC=$(curl -sf "http://${HOST:-localhost}/crc" | python3 -c "
import sys, struct
d = sys.stdin.buffer.read()
print(','.join(str(x) for x in struct.unpack('>%di' % (len(d) // 4), d)))
")
    # A failing curl in a pipeline does not fail the pipeline (no pipefail in
    # POSIX sh), so the empty result is the thing to test — otherwise the client
    # is handed a blank TORIRS_JAG_CRC and the server answers "out of date".
    if [ -z "$TORIRS_JAG_CRC" ]; then
        echo "run-live.sh: cannot fetch http://${HOST:-localhost}/crc — is the server up?" >&2
        exit 1
    fi
    export TORIRS_JAG_CRC
fi

if [ "$MODE" = native ]; then
    if [ ! -x src/torirs ]; then
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
make -C src "$WEB_TARGET"

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

if [ "$TRANSPORT" = "tcp" ] && [ "$OFFLINE" = 0 ]; then
    echo "run-live.sh: note — this manifest uses transport=tcp, which a browser" >&2
    echo "  cannot open directly. Put a WebSocket bridge in front of the game" >&2
    echo "  server (e.g. websockify ${HOST:-localhost}:8443 ${HOST:-localhost}:43594)" >&2
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
