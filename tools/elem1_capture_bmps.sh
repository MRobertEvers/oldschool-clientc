#!/usr/bin/env bash
# Capture one named BMP per Elemental Workshop I interaction.
# Plugins off. Player unkillable. Each shot must show the mesbox/journal/reward.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/OSRS-Content/osrs239-content/server/scripts/selftest/quest_elemental_workshop"
MANIFEST="${ELEM1_MANIFEST:-$ROOT/manifests/manifest_osrs239_packed.ini}"
if [[ ! -f "$MANIFEST" ]]; then
  MANIFEST="$ROOT/manifests/manifest_osrs239.ini"
fi
mkdir -p "$OUT"
export TORIRS_PLUGINS=0
export TORIRSSERVER_GOD=1
export SDL_VIDEODRIVER=dummy

shots=(
  "01_search_bookcase_find_book:elem1bmp_01_search_bookcase_find_book"
  "02_search_bookcase_already_have:elem1bmp_02_search_bookcase_already_have"
  "03_search_bookcase_no_room:elem1bmp_03_search_bookcase_no_room"
  "04_read_book_intro:elem1bmp_04_read_book_intro"
  "05_read_book_workshop_closed:elem1bmp_05_read_book_workshop_closed"
  "06_slash_book_need_space:elem1bmp_06_slash_book_need_space"
  "07_slash_book_find_key:elem1bmp_07_slash_book_find_key"
  "08_slash_book_dont_damage:elem1bmp_08_slash_book_dont_damage"
  "09_read_slashed_book:elem1bmp_09_read_slashed_book"
  "10_search_bookcase_replace_slashed:elem1bmp_10_search_bookcase_replace_slashed"
  "11_search_bookcase_replace_key:elem1bmp_11_search_bookcase_replace_key"
  "12_search_bookcase_nothing_else:elem1bmp_12_search_bookcase_nothing_else"
  "13_search_bookcase_slashed_no_room:elem1bmp_13_search_bookcase_slashed_no_room"
  "14_search_bookcase_key_no_room:elem1bmp_14_search_bookcase_key_no_room"
  "15_oddwall_no_key:elem1bmp_15_oddwall_no_key"
  "16_oddwall_wrong_item:elem1bmp_16_oddwall_wrong_item"
  "17_oddwall_use_key:elem1bmp_17_oddwall_use_key"
  "18_climb_down_stairs:elem1bmp_18_climb_down_stairs"
  "19_climb_up_stairs:elem1bmp_19_climb_up_stairs"
  "20_water_east_open:elem1bmp_20_water_east_open"
  "21_water_east_blocked_by_west:elem1bmp_21_water_east_blocked_by_west"
  "22_water_west_open:elem1bmp_22_water_west_open"
  "23_water_control_locked:elem1bmp_23_water_control_locked"
  "24_water_lever_wrong_order:elem1bmp_24_water_lever_wrong_order"
  "25_water_lever_start_wheel:elem1bmp_25_water_lever_start_wheel"
  "26_water_lever_stop_wheel:elem1bmp_26_water_lever_stop_wheel"
  "27_box1_find_bowl:elem1bmp_27_box1_find_bowl"
  "28_box1_no_room:elem1bmp_28_box1_no_room"
  "29_box1_empty:elem1bmp_29_box1_empty"
  "30_box2_find_needle:elem1bmp_30_box2_find_needle"
  "31_box2_empty:elem1bmp_31_box2_empty"
  "32_box4_find_leather:elem1bmp_32_box4_find_leather"
  "33_box4_empty:elem1bmp_33_box4_empty"
  "34_box_empty:elem1bmp_34_box_empty"
  "35_bellows_need_crafting:elem1bmp_35_bellows_need_crafting"
  "36_bellows_need_supplies:elem1bmp_36_bellows_need_supplies"
  "37_bellows_repair:elem1bmp_37_bellows_repair"
  "38_bellows_already_repaired:elem1bmp_38_bellows_already_repaired"
  "39_air_lever_nothing:elem1bmp_39_air_lever_nothing"
  "40_air_lever_start:elem1bmp_40_air_lever_start"
  "41_air_lever_stop:elem1bmp_41_air_lever_stop"
  "42_lava_fill_bowl:elem1bmp_42_lava_fill_bowl"
  "43_lava_bowl_already_full:elem1bmp_43_lava_bowl_already_full"
  "44_lava_wrong_item:elem1bmp_44_lava_wrong_item"
  "45_furnace_light:elem1bmp_45_furnace_light"
  "46_furnace_extra_lava:elem1bmp_46_furnace_extra_lava"
  "47_furnace_cold:elem1bmp_47_furnace_cold"
  "48_furnace_need_bellows:elem1bmp_48_furnace_need_bellows"
  "49_furnace_need_smithing:elem1bmp_49_furnace_need_smithing"
  "50_furnace_need_coal:elem1bmp_50_furnace_need_coal"
  "51_furnace_smelt_bar:elem1bmp_51_furnace_smelt_bar"
  "52_earth_need_mining:elem1bmp_52_earth_need_mining"
  "53_earth_need_pickaxe:elem1bmp_53_earth_need_pickaxe"
  "54_earth_elemental_bursts:elem1bmp_54_earth_elemental_bursts"
  "55_workbench_need_hammer:elem1bmp_55_workbench_need_hammer"
  "56_workbench_need_smithing:elem1bmp_56_workbench_need_smithing"
  "57_workbench_need_book:elem1bmp_57_workbench_need_book"
  "58_workbench_make_shield:elem1bmp_58_workbench_make_shield"
  "59_quest_complete:elem1bmp_59_quest_complete"
  "60_journal_not_started:elem1bmp_60_journal_not_started"
  "61_journal_read_book:elem1bmp_61_journal_read_book"
  "62_journal_found_key:elem1bmp_62_journal_found_key"
  "63_journal_workshop:elem1bmp_63_journal_workshop"
  "64_journal_have_bar:elem1bmp_64_journal_have_bar"
  "65_journal_complete:elem1bmp_65_journal_complete"
  "66_box2_no_room:elem1bmp_66_box2_no_room"
  "67_box4_no_room:elem1bmp_67_box4_no_room"
  "68_furnace_wrong_item:elem1bmp_68_furnace_wrong_item"
  "69_earth_swing_pickaxe:elem1bmp_69_earth_swing_pickaxe"
  "70_earth_npc_shout:elem1bmp_70_earth_npc_shout"
  "71_journal_entered_workshop:elem1bmp_71_journal_entered_workshop"
  "72_journal_waterwheel:elem1bmp_72_journal_waterwheel"
  "73_journal_bellows:elem1bmp_73_journal_bellows"
)

only="${1:-}"
for shot in "${shots[@]}"; do
  name="${shot%%:*}"
  cheat="${shot##*:}"
  if [[ -n "$only" && "$name" != *"$only"* && "$cheat" != *"$only"* ]]; then
    continue
  fi
  dest="$OUT/${name}.bmp"
  echo "CAPTURING $name via ::$cheat -> $dest"
  TORIRS_PLUGINS=0 \
  TORIRSSERVER_GOD=1 \
  TORIRS_NET_CHEAT="god 1;maxstats;$cheat" \
  TORIRS_EXIT_BMP="$dest" \
  TORIRS_MAX_FRAMES="${TORIRS_MAX_FRAMES:-420}" \
  "$ROOT/run-live.sh" --skip-checks "$MANIFEST" testc test || true
  if [[ ! -f "$dest" ]]; then
    echo "MISSING $dest" >&2
  else
    echo "WROTE $dest ($(wc -c < "$dest") bytes)"
  fi
done
echo "done. count=$(ls -1 "$OUT"/*.bmp 2>/dev/null | wc -l)"
