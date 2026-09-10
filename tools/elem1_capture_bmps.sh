#!/bin/bash
# Capture EVERY Elemental Workshop I player-interaction BMP.
# Highlight reels are a failed close. One named BMP per interaction.
#
# Uses the proven embed + --soft3d path. --offline is rejected (it
# disables embed and dumps a washed-out Lumbridge / Search Crate shot).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-}"
if [[ -z "$OUT" ]]; then
    if [[ -d /tmp/osrs-elem1/osrs239-content/server/scripts/selftest/quest_elemental_workshop ]]; then
        OUT=/tmp/osrs-elem1/osrs239-content/server/scripts/selftest/quest_elemental_workshop
    else
        OUT="$ROOT/OSRS-Content/osrs239-content/server/scripts/selftest/quest_elemental_workshop"
    fi
fi
mkdir -p "$OUT"

CACHE="${TORIRSSERVER_CACHE:-$ROOT/cache.osrs239}"
if [[ ! -f "$CACHE/main_file_cache.dat2" ]]; then
    echo "FATAL: cache missing at $CACHE" >&2
    exit 1
fi

CLIENT="${TORIRS_CLIENT:-}"
if [[ -z "$CLIENT" ]]; then
    if [[ -x /workspace/src/torirs ]]; then
        CLIENT=/workspace/src/torirs
    else
        CLIENT="$ROOT/src/torirs"
    fi
fi
if [[ ! -x "$CLIENT" ]]; then
    echo "FATAL: client missing at $CLIENT" >&2
    exit 1
fi

CONTENT="${TORIRSSERVER_CONTENT:-$ROOT/OSRS-Content/osrs239-content}"
SCRIPTS="${TORIRSSERVER_SCRIPTS:-/tmp/osrs-elem1/osrs239-content/server/scripts/build}"
if [[ ! -f "$SCRIPTS/script.dat" ]]; then
    echo "FATAL: script pack missing at $SCRIPTS/script.dat" >&2
    exit 1
fi

MANIFEST="$ROOT/manifests/manifest_osrs239.ini"

SHOTS=(
    01_search_bookcase_find_book
    02_search_bookcase_already_have
    03_search_bookcase_no_room
    04_read_book_intro
    05_read_book_workshop_closed
    06_slash_book_need_space
    07_slash_book_find_key
    08_slash_book_dont_damage
    09_read_slashed_book
    10_search_bookcase_replace_slashed
    11_search_bookcase_replace_key
    12_search_bookcase_nothing_else
    13_search_bookcase_slashed_no_room
    14_search_bookcase_key_no_room
    15_oddwall_no_key
    16_oddwall_wrong_item
    17_oddwall_use_key
    18_climb_down_stairs
    19_climb_up_stairs
    20_water_east_open
    21_water_east_blocked_by_west
    22_water_west_open
    23_water_control_locked
    24_water_lever_wrong_order
    25_water_lever_start_wheel
    26_water_lever_stop_wheel
    27_box1_find_bowl
    28_box1_no_room
    29_box1_empty
    30_box2_find_needle
    31_box2_empty
    32_box4_find_leather
    33_box4_empty
    34_box_empty
    35_bellows_need_crafting
    36_bellows_need_supplies
    37_bellows_repair
    38_bellows_already_repaired
    39_air_lever_nothing
    40_air_lever_start
    41_air_lever_stop
    42_lava_fill_bowl
    43_lava_bowl_already_full
    44_lava_wrong_item
    45_furnace_light
    46_furnace_extra_lava
    47_furnace_cold
    48_furnace_need_bellows
    49_furnace_need_smithing
    50_furnace_need_coal
    51_furnace_smelt_bar
    52_earth_need_mining
    53_earth_need_pickaxe
    54_earth_elemental_bursts
    55_workbench_need_hammer
    56_workbench_need_smithing
    57_workbench_need_book
    58_workbench_make_shield
    59_quest_complete
    60_journal_not_started
    61_journal_read_book
    62_journal_found_key
    63_journal_workshop
    64_journal_have_bar
    65_journal_complete
    66_box2_no_room
    67_box4_no_room
    68_furnace_wrong_item
    69_earth_swing_pickaxe
    70_earth_npc_shout
    71_journal_entered_workshop
    72_journal_waterwheel
    73_journal_bellows
)

FRAMES="${TORIRS_MAX_FRAMES:-700}"
echo "OUT=$OUT CLIENT=$CLIENT FRAMES=$FRAMES shots=${#SHOTS[@]}"

ok=0
fail=0
for shot in "${SHOTS[@]}"; do
    bmp="$OUT/${shot}.bmp"
    log="/tmp/elem1_cap_${shot}.log"
    echo "=== $shot ==="
    TORIRS_PLUGINS=0 \
    TORIRSSERVER_GOD=1 \
    TORIRS_TRANSPORT=embed \
    SDL_VIDEODRIVER=dummy \
    TORIRSSERVER_CONTENT="$CONTENT" \
    TORIRSSERVER_SCRIPTS="$SCRIPTS" \
    TORIRSSERVER_CACHE="$CACHE" \
    TORIRS_NET_CHEAT="god 1;maxstats;elem1bmp_${shot}" \
    TORIRS_EXIT_BMP="$bmp" \
    TORIRS_MAX_FRAMES="$FRAMES" \
    "$CLIENT" --manifest "$MANIFEST" --user testc --pass test --soft3d \
        >"$log" 2>&1 || true
    if [[ ! -f "$bmp" ]]; then
        echo "FAIL $shot: no BMP"
        fail=$((fail+1))
        continue
    fi
    sz=$(stat -c%s "$bmp")
    if [[ "$sz" -lt 100000 ]]; then
        echo "FAIL $shot: BMP too small ($sz)"
        fail=$((fail+1))
        continue
    fi
    if ! grep -q "cheat 'elem1bmp_${shot}' -> debugproc ran" "$log"; then
        echo "WARN $shot: debugproc ran line missing (still have ${sz}B BMP)"
    fi
    echo "OK $shot ${sz}B"
    ok=$((ok+1))
done

echo "CAPTURE $ok ok / $fail fail / ${#SHOTS[@]} total"
exit "$fail"
