#!/usr/bin/env bash
# Measure the non-embedded server-mode client's real peak working set in both
# renderer lanes and report against the budget. See docs/memory_budget/BASELINE.md.
#
#   tools/perf/memory_gate.sh [label]
#
# Requires a live torirsserver on 127.0.0.1:43596, started from the repo root.
set -u

LABEL="${1:-run}"
OUT="${MEMGATE_OUT:-build/memgate}"
EXE="${MEMGATE_EXE:-src/torirs_win64.exe}"
FRAMES="${MEMGATE_FRAMES:-900}"
mkdir -p "$OUT"

measure() {
    local lane="$1" flag="$2" budget="$3"
    local log="$OUT/${LABEL}_${lane}.log"
    local line
    line=$(powershell -NoProfile -ExecutionPolicy Bypass -Command \
        "& { \$e=@{TORIRS_TRANSPORT='tcp'; TORIRS_MAX_FRAMES='$FRAMES'}; \
             & ./tools/perf/measure_peak_ws.ps1 -Exe '$EXE' -Log '$log' -Env \$e -TimeoutSec 300 \
               -ClientArgs @('--manifest','manifests/manifest_osrs239.ini','--connect','127.0.0.1', \
                             '--port','43596','--user','testc','--pass','test','$flag') }" \
        2>/dev/null | tr -d '\r' | grep PEAK_WS_MB)
    local mb=${line#PEAK_WS_MB=}
    mb=${mb%% *}

    # A run that never reached a world has a meaningless number. Say so rather
    # than printing it next to a budget as though it passed.
    if ! grep -q "login: OK" "$log" 2>/dev/null; then
        printf '%-14s %8s MB   NO LOGIN -- number is meaningless (%s)\n' "$lane" "$mb" "$log"
        return 1
    fi
    if ! grep -q "world_load:" "$log" 2>/dev/null; then
        printf '%-14s %8s MB   NO WORLD -- number is meaningless (%s)\n' "$lane" "$mb" "$log"
        return 1
    fi

    local verdict
    verdict=$(awk -v m="$mb" -v b="$budget" 'BEGIN{print (m<b)?"PASS":"FAIL"}')
    printf '%-14s %8s MB   budget %s MB   %s\n' "$lane" "$mb" "$budget" "$verdict"
    [ "$verdict" = PASS ]
}

echo "=== memory gate: $LABEL ($EXE, $FRAMES frames) ==="
rc=0
measure soft3d       --soft3d        128 || rc=1
measure d3d9-zbuffer --d3d9-zbuffer  256 || rc=1
exit $rc
