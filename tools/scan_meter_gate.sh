#!/bin/sh
# Run the real client headless under every frame provider with the whole-tree
# scan meter set to abort (TORIRS_SCAN_METER_STRICT=1), and fail if any run
# reports a steady frame that keeps walking the UI tree. @see UITREE_SCAN_METER.
#
#   tools/scan_meter_gate.sh <client binary built with EMBED_SERVER=1>
#
# `make -C src check-scan-meter` builds a private binary and runs this.
#
# Why a client run and not only a unit test: the failure this catches is a
# PER-FRAME lookup reached through a plugin, and the unit tests do not run the
# plugins. On 2026-09-21 the Stone Drawer re-walked a 7,000-node tree dozens of
# times a frame -- 68 ms a frame on the Moto X, 1 ms on this desktop -- and
# nothing failed. Here it aborts on the desktop, where the time never showed.
#
# The last arm is the phone's own configuration: the mobile client identity
# (toplevel_osm), the mobile plugin set, the Stone Drawer.
set -eu

BIN="${1:?usage: tools/scan_meter_gate.sh <client binary>}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
case "$BIN" in /*) ;; *) BIN="$(pwd)/$BIN" ;; esac
[ -x "$BIN" ] || { echo "scan_meter_gate: no client at $BIN" >&2; exit 2; }
FRAMES="${SCAN_METER_GATE_FRAMES:-1500}"
OUT="$(mktemp -d "${TMPDIR:-/tmp}/scan_meter_gate.XXXXXX")"

# The checked-in world, pointed at the pristine cache over the embedded
# transport -- what `./launch run osrs239` resolves -- with absolute paths so
# it can live outside the tree.
sed -e "s#^dir=.*#dir=$REPO/cache.osrs239#" \
    -e "s#^transport=.*#transport=embed#" \
    -e "s#^revconfig_ui=\.\./#revconfig_ui=$REPO/#" \
    -e "s#^revconfig_cache=\.\./#revconfig_cache=$REPO/#" \
    -e "s#^out=\.\./#out=$REPO/#" \
    "$REPO/manifests/manifest_osrs239.ini" > "$OUT/osrs239.ini"

failed=0
arm=0
# name | preferred_frame | plugin prefs | extra env
run_arm()
{
    name="$1"; frame="$2"; plugin_prefs="$3"; extra="$4"
    arm=$((arm + 1))
    dir="$OUT/$name"
    mkdir -p "$dir/saves"
    printf '[preferences]\nversion=1\npreferred_frame=%s\nframe_migration_version=1\n' \
        "$frame" > "$dir/preferences.ini"
    cp "$REPO/$plugin_prefs" "$dir/plugin_prefs.ini"
    set +e
    # shellcheck disable=SC2086
    (cd "$REPO" && env SDL_VIDEODRIVER=dummy TORIRS_TRANSPORT=embed \
        TORIRS_MAX_FRAMES="$FRAMES" TORIRS_SCAN_METER_STRICT=1 \
        TORIRS_PREFS="$dir/preferences.ini" TORIRS_PLUGIN_PREFS="$dir/plugin_prefs.ini" \
        TORIRSSERVER_SAVES="$dir/saves" TORIRSSERVER_ALLOW_STALE_SCRIPTS=1 \
        TORIRS_SIM_RESIZE="40,1600x1000" $extra \
        "$BIN" --manifest "$OUT/osrs239.ini" --user "scangate$arm" --pass test \
        --windowmode resizable --window 1600x1000) > "$dir/run.log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 0 ] || grep -q "^uitree: steady frames" "$dir/run.log"; then
        echo "scan_meter_gate: FAIL $name (exit $status)"
        grep "^uitree: steady frames" "$dir/run.log" | head -3
        [ "$status" -ne 0 ] && tail -5 "$dir/run.log"
        failed=1
    elif ! grep -q "session=ok" "$dir/run.log"; then
        # A run that never logged in measured the title screen, not a frame.
        echo "scan_meter_gate: FAIL $name (never reached the world; see $dir/run.log)"
        failed=1
    else
        echo "scan_meter_gate: pass $name"
    fi
}

run_arm auto            auto                            plugin_prefs.ini ""
run_arm stone-drawer    mobile-gameframe/stone-drawer   plugin_prefs.ini ""
run_arm classic-fixed   gameframe-layout/classic-fixed  plugin_prefs.ini ""
run_arm modern-resizable gameframe-layout/modern-resizable plugin_prefs.ini ""
run_arm phone-stone-drawer mobile-gameframe/stone-drawer plugin_prefs.mobile.ini \
    "TORIRS_CLIENTTYPE=7 TORIRS_ON_MOBILE=1"

if [ "$failed" -ne 0 ]; then
    echo "scan_meter_gate: FAIL (logs in $OUT)"
    exit 1
fi
rm -rf "$OUT"
echo "scan_meter_gate: PASS"
