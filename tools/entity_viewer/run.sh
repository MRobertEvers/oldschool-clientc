#!/bin/sh
#
# Run the entity viewer, refusing to serve a stale build.
#
# ## Why staleness is the thing this script is for
#
# The viewer compiles the SAME C twice: once into the native server and once
# into web/ev_wasm.wasm through emcc. Edit ev_render.c, rebuild with `make`, and
# the server is current while the browser silently keeps running the old
# renderer — the page loads, the model draws, and nothing anywhere says the two
# halves disagree. That failure has a particular flavour: you change a kernel,
# see no difference, and conclude the change did nothing.
#
# So this checks both artefacts against their sources before serving, and by
# default rebuilds what is stale rather than warning about it.
#
# Usage:
#   tools/entity_viewer/run.sh [--cache DIR] [--rev NAME] [--catalog DIR]
#                              [--names DIR] [--port N]
#                              [--check-only] [--no-build] [--no-wasm]
#
# Everything not recognised here is passed through to ev_server.

set -e

HERE=$(CD=$(dirname "$0") && cd "$CD" && pwd)
REPO=$(cd "$HERE/../.." && pwd)

CACHE="$REPO/cache.osrs239"
REV="osrs239"
CATALOG="$REPO/out/osrs239_anims"
NAMES="$REPO/OSRS-Content/osrs239-content"
PORT=8099
CHECK_ONLY=0
DO_BUILD=1
DO_WASM=1
PASSTHRU=""

while [ $# -gt 0 ]; do
    case "$1" in
        --cache)      CACHE="$2"; shift 2 ;;
        --rev)        REV="$2"; shift 2 ;;
        --catalog)    CATALOG="$2"; shift 2 ;;
        --names)      NAMES="$2"; shift 2 ;;
        --port)       PORT="$2"; shift 2 ;;
        --check-only) CHECK_ONLY=1; shift ;;
        --no-build)   DO_BUILD=0; shift ;;
        --no-wasm)    DO_WASM=0; shift ;;
        *)            PASSTHRU="$PASSTHRU $1"; shift ;;
    esac
done

# Sources both halves share. A change to any of these invalidates BOTH the
# server and the wasm, which is exactly the case that goes unnoticed.
SHARED="$HERE/ev_render.c $HERE/ev_render.h $HERE/ev_wire.c $HERE/ev_wire.h"
TORIDRAW="$REPO/3rd/toridraw"

# newest_mtime <files...> -> prints the newest mtime as an epoch second.
# `find -newer` would be simpler but answers a yes/no per pair; the newest
# timestamp lets one comparison cover a whole set.
newest_mtime() {
    newest=0
    for f in "$@"; do
        [ -e "$f" ] || continue
        if [ -d "$f" ]; then
            t=$(find "$f" -type f \( -name '*.c' -o -name '*.h' -o -name '*.inc' \) \
                    -exec stat -f %m {} + 2>/dev/null \
                || find "$f" -type f \( -name '*.c' -o -name '*.h' -o -name '*.inc' \) \
                    -exec stat -c %Y {} + 2>/dev/null)
        else
            t=$(stat -f %m "$f" 2>/dev/null || stat -c %Y "$f" 2>/dev/null)
        fi
        for one in $t; do
            [ "$one" -gt "$newest" ] 2>/dev/null && newest=$one
        done
    done
    echo "$newest"
}

artefact_mtime() {
    if [ -e "$1" ]; then
        stat -f %m "$1" 2>/dev/null || stat -c %Y "$1" 2>/dev/null
    else
        echo 0
    fi
}

SRC_T=$(newest_mtime $SHARED "$HERE/ev_server.c" "$HERE/ev_build.c" "$TORIDRAW")
SRV_T=$(artefact_mtime "$HERE/ev_server")
WASM_T=$(artefact_mtime "$HERE/web/ev_wasm.wasm")

SRV_STALE=0
WASM_STALE=0
[ "$SRV_T" -lt "$SRC_T" ] && SRV_STALE=1
[ "$WASM_T" -lt "$SRC_T" ] && WASM_STALE=1

report() {
    if [ "$1" -eq 1 ]; then
        printf '  %-22s STALE\n' "$2"
    elif [ "$3" -eq 0 ]; then
        printf '  %-22s MISSING\n' "$2"
    else
        printf '  %-22s ok\n' "$2"
    fi
}

echo "entity viewer: build freshness"
report "$SRV_STALE" "ev_server" "$SRV_T"
report "$WASM_STALE" "web/ev_wasm.wasm" "$WASM_T"

if [ "$CHECK_ONLY" -eq 1 ]; then
    [ "$SRV_STALE" -eq 1 ] || [ "$WASM_STALE" -eq 1 ] && exit 1
    exit 0
fi

if [ "$SRV_STALE" -eq 1 ] || [ "$SRV_T" -eq 0 ]; then
    if [ "$DO_BUILD" -eq 1 ]; then
        echo "  rebuilding ev_server…"
        make -C "$HERE" ev_server
    else
        echo "  WARNING: ev_server is stale and --no-build was given"
    fi
fi

if [ "$WASM_STALE" -eq 1 ] || [ "$WASM_T" -eq 0 ]; then
    if [ "$DO_WASM" -eq 0 ]; then
        echo "  WARNING: the wasm is stale and --no-wasm was given —"
        echo "           the BROWSER will run the old renderer while the server is current."
    elif command -v emcc >/dev/null 2>&1; then
        echo "  rebuilding web/ev_wasm.wasm…"
        make -C "$HERE" wasm
    else
        # Refuse rather than serve a mismatch: this is the failure the script
        # exists to prevent, and a warning scrolls past.
        echo ""
        echo "  ERROR: web/ev_wasm.wasm is stale and emcc is not on PATH."
        echo "         The page would render with a build older than the server."
        echo "         Install emsdk and re-run, or pass --no-wasm to accept it."
        exit 1
    fi
fi

ARGS="--rev $REV $CACHE --port $PORT --web $HERE/web"
[ -d "$CATALOG" ] && ARGS="$ARGS --catalog $CATALOG"
[ -d "$NAMES" ] && ARGS="$ARGS --names $NAMES"

echo ""
echo "  http://127.0.0.1:$PORT/"
echo ""
# shellcheck disable=SC2086
exec "$HERE/ev_server" $ARGS $PASSTHRU
