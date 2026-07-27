#!/bin/sh
# Dump an interface for implementation work — the world map (595) by default.
#
#   ./run-worldmap.sh [manifest.ini] [--show]
#
# Everything comes from the manifest: cache dir + identity in [cache:boot], the
# interface to boot in [ui:boot] interface_id, any sub-interfaces to mount in
# [ui:gameframe]. manifest_osrs239_worldmap.ini / manifest_osrs230_worldmap.ini
# boot 595 as the tree root, so nothing but the world map is in the tree.
#
# Runs the interface's onLoad CS2 scripts headless, then writes every interface
# dump src/main.c supports plus a frame capture:
#
#   build/worldmap/<rev>/tree.txt   widgetTreeDump-format tree (+ boot log)
#   build/worldmap/<rev>/dump.txt   BOUNDS / EMIT_EXIT / HOOKDUMP + script errors
#   build/worldmap/<rev>/wm.bmp     final frame (also .png where sips exists)
#
# --show runs windowed instead of headless. TORIRS_MAX_FRAMES/TORIRS_SIM_HOVER
# and the other harness vars from src/main.c still apply.
set -eu

cd "$(dirname "$0")"

SHOW=0
MANIFEST=manifest_osrs239_worldmap.ini
for a in "$@"; do
    case "$a" in
    --show) SHOW=1 ;;
    -*)
        echo "usage: run-worldmap.sh [manifest.ini] [--show]" >&2
        exit 1
        ;;
    *) MANIFEST="$a" ;;
    esac
done

if [ ! -f "$MANIFEST" ]; then
    echo "run-worldmap.sh: manifest '$MANIFEST' not found" >&2
    exit 1
fi

ini_get() { sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*\([^;#]*\).*/\1/p" "$MANIFEST" | head -1 | sed 's/[[:space:]]*$//'; }

IFACE=$(ini_get interface_id)
REVISION=$(ini_get revision)
CACHE=$(ini_get dir)
: "${IFACE:?no [ui:boot] interface_id= in $MANIFEST}"

# One output dir per manifest, so a 230 dump never overwrites a 239 one.
OUT="build/worldmap/$(basename "$MANIFEST" .ini)"

if [ ! -x src/torirs ]; then
    echo "run-worldmap.sh: building src/torirs..." >&2
    make -C src torirs
fi

mkdir -p "$OUT"

if [ "$SHOW" = 0 ]; then
    SDL_VIDEODRIVER=dummy
    export SDL_VIDEODRIVER
    : "${TORIRS_MAX_FRAMES:=300}"
    export TORIRS_MAX_FRAMES
fi

echo "run-worldmap.sh: iface=$IFACE cache=$CACHE rev=$REVISION -> $OUT" >&2

TORIRS_DUMP_TREE_EXIT=1 \
TORIRS_DUMP_HOOKS_EXIT=1 \
TORIRS_DUMP_BOUNDS="$IFACE" \
TORIRS_DUMP_EMIT_EXIT="$IFACE" \
TORIRS_EXIT_BMP="$OUT/wm.bmp" \
    src/torirs --manifest "$MANIFEST" >"$OUT/tree.txt" 2>"$OUT/dump.txt" || {
    status=$?
    echo "run-worldmap.sh: torirs exited $status — tail of $OUT/dump.txt:" >&2
    tail -20 "$OUT/dump.txt" >&2
    exit $status
}

# BMP is what the client writes; PNG is what viewers open.
if command -v sips >/dev/null 2>&1 && [ -f "$OUT/wm.bmp" ]; then
    sips -s format png "$OUT/wm.bmp" --out "$OUT/wm.png" >/dev/null 2>&1 || true
fi

echo
grep '^InterfaceOpen done:' "$OUT/tree.txt" || true
printf 'tree nodes   %s\n' "$(grep -c 'user_id=' "$OUT/tree.txt" || true)"
printf 'BOUNDS       %s\n' "$(grep -c '^BOUNDS' "$OUT/dump.txt" || true)"
printf 'EMIT_EXIT    %s\n' "$(grep -c '^EMIT_EXIT' "$OUT/dump.txt" || true)"
printf 'HOOKDUMP     %s\n' "$(grep -c '^HOOKDUMP' "$OUT/dump.txt" || true)"

# A script that never ran is why a panel comes up empty, and it is invisible in
# the tree dump — the widgets exist, they are just unpopulated. Surface it.
if grep -q 'Task_CS2Run: failed to resolve script' "$OUT/dump.txt"; then
    echo
    echo "clientscripts that failed to load:"
    sed -n 's/.*failed to resolve script \([0-9]*\)/  \1/p' "$OUT/dump.txt" | sort -u
fi

echo
echo "wrote:"
for f in tree.txt dump.txt wm.bmp wm.png; do
    [ -f "$OUT/$f" ] && echo "  $OUT/$f"
done
exit 0
