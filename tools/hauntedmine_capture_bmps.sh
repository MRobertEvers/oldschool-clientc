#!/usr/bin/env bash
# Capture one named BMP per Haunted Mine interaction.
# Plugins off. Player unkillable. Each shot must show the mesbox/journal/reward.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/OSRS-Content/osrs239-content/server/scripts/selftest/quest_hauntedmine"
MANIFEST="${HMQ_MANIFEST:-$ROOT/manifests/manifest_osrs239_sailing.ini}"
if [[ ! -f "$MANIFEST" ]]; then
  MANIFEST="$ROOT/manifests/manifest_osrs239.ini"
fi
mkdir -p "$OUT"
export TORIRS_PLUGINS=0
export TORIRSSERVER_GOD=1
export SDL_VIDEODRIVER=dummy
export PLATFORM_OBJ_BASE="${PLATFORM_OBJ_BASE:-/tmp/gp-mine-img-obj}"

shots=(
  "001_zealot_hello:hmqbmp_001_zealot_hello"
  "002_zealot_decline:hmqbmp_002_zealot_decline"
  "003_zealot_purpose:hmqbmp_003_zealot_purpose"
  "004_zealot_prereq_fail:hmqbmp_004_zealot_prereq_fail"
  "005_zealot_nevermind:hmqbmp_005_zealot_nevermind"
  "006_zealot_path_choice:hmqbmp_006_zealot_path_choice"
  "007_zealot_path_refuse:hmqbmp_007_zealot_path_refuse"
  "008_zealot_accept:hmqbmp_008_zealot_accept"
  "009_zealot_key_hint:hmqbmp_009_zealot_key_hint"
  "010_zealot_mid:hmqbmp_010_zealot_mid"
  "011_zealot_post_dayth:hmqbmp_011_zealot_post_dayth"
  "012_zealot_complete:hmqbmp_012_zealot_complete"
  "013_zealot_pickpocket_unknown:hmqbmp_013_zealot_pickpocket_unknown"
  "014_zealot_pickpocket_key:hmqbmp_014_zealot_pickpocket_key"
  "015_zealot_pickpocket_have:hmqbmp_015_zealot_pickpocket_have"
  "016_zealot_pickpocket_done:hmqbmp_016_zealot_pickpocket_done"
  "017_south_blocked:hmqbmp_017_south_blocked"
  "018_north_blocked:hmqbmp_018_north_blocked"
  "019_south_enter:hmqbmp_019_south_enter"
  "020_north_enter:hmqbmp_020_north_enter"
  "021_crawl_out:hmqbmp_021_crawl_out"
  "022_laddertop_south:hmqbmp_022_laddertop_south"
  "023_laddertop_north:hmqbmp_023_laddertop_north"
  "024_laddertop_1sw_cart:hmqbmp_024_laddertop_1sw_cart"
  "025_laddertop_1sw_north:hmqbmp_025_laddertop_1sw_north"
  "026_laddertop_1e_cartroom:hmqbmp_026_laddertop_1e_cartroom"
  "027_laddertop_1e_collect:hmqbmp_027_laddertop_1e_collect"
  "028_ladder_up:hmqbmp_028_ladder_up"
  "029_ladder_1ne_up:hmqbmp_029_ladder_1ne_up"
  "030_ladder_1w_collect:hmqbmp_030_ladder_1w_collect"
  "031_ladder_1w_cart:hmqbmp_031_ladder_1w_cart"
  "032_ladder_1w_lift_loop:hmqbmp_032_ladder_1w_lift_loop"
  "033_fungus_pick:hmqbmp_033_fungus_pick"
  "034_fungus_have:hmqbmp_034_fungus_have"
  "035_fungus_noroom:hmqbmp_035_fungus_noroom"
  "036_cart_deposit_empty:hmqbmp_036_cart_deposit_empty"
  "037_cart_deposit:hmqbmp_037_cart_deposit"
  "038_cart_deposit_have:hmqbmp_038_cart_deposit_have"
  "039_cart_collect_empty:hmqbmp_039_cart_collect_empty"
  "040_cart_collect:hmqbmp_040_cart_collect"
  "041_cart_collect_have:hmqbmp_041_cart_collect_have"
  "042_cart_search_nothing:hmqbmp_042_cart_search_nothing"
  "043_lever1:hmqbmp_043_lever1"
  "044_lever2:hmqbmp_044_lever2"
  "045_lever3:hmqbmp_045_lever3"
  "046_lever4:hmqbmp_046_lever4"
  "047_lever5:hmqbmp_047_lever5"
  "048_lever6:hmqbmp_048_lever6"
  "049_lever7:hmqbmp_049_lever7"
  "050_lever8:hmqbmp_050_lever8"
  "051_points_no_fungus:hmqbmp_051_points_no_fungus"
  "052_points_already:hmqbmp_052_points_already"
  "053_points_start:hmqbmp_053_points_start"
  "054_points_sunk:hmqbmp_054_points_sunk"
  "055_points_back:hmqbmp_055_points_back"
  "056_chisel_find:hmqbmp_056_chisel_find"
  "057_chisel_have:hmqbmp_057_chisel_have"
  "058_chisel_noroom:hmqbmp_058_chisel_noroom"
  "059_valve_locked:hmqbmp_059_valve_locked"
  "060_valve_turn:hmqbmp_060_valve_turn"
  "061_valve_already:hmqbmp_061_valve_already"
  "062_ghost_shut:hmqbmp_062_ghost_shut"
  "063_lift_no_power:hmqbmp_063_lift_no_power"
  "064_lift_no_light:hmqbmp_064_lift_no_light"
  "065_lift_down:hmqbmp_065_lift_down"
  "066_lift_up:hmqbmp_066_lift_up"
  "067_fungus_drop:hmqbmp_067_fungus_drop"
  "068_fungus_outside:hmqbmp_068_fungus_outside"
  "069_stairs_dayth_dark:hmqbmp_069_stairs_dayth_dark"
  "070_stairs_dayth:hmqbmp_070_stairs_dayth"
  "071_stairs_crystal_dark:hmqbmp_071_stairs_crystal_dark"
  "072_stairs_crystal_locked:hmqbmp_072_stairs_crystal_locked"
  "073_stairs_crystal:hmqbmp_073_stairs_crystal"
  "074_stairs_up:hmqbmp_074_stairs_up"
  "075_door_locked:hmqbmp_075_door_locked"
  "076_door_open:hmqbmp_076_door_open"
  "077_key_trap:hmqbmp_077_key_trap"
  "078_dayth_guarding:hmqbmp_078_dayth_guarding"
  "079_dayth_death:hmqbmp_079_dayth_death"
  "080_key_pickup:hmqbmp_080_key_pickup"
  "081_key_have:hmqbmp_081_key_have"
  "082_key_noroom:hmqbmp_082_key_noroom"
  "083_outcrop_guarded:hmqbmp_083_outcrop_guarded"
  "084_outcrop_no_chisel:hmqbmp_084_outcrop_no_chisel"
  "085_outcrop_no_craft:hmqbmp_085_outcrop_no_craft"
  "086_outcrop_noroom:hmqbmp_086_outcrop_noroom"
  "087_quest_complete:hmqbmp_087_quest_complete"
  "088_outcrop_repeat:hmqbmp_088_outcrop_repeat"
  "089_journal_not_started:hmqbmp_089_journal_not_started"
  "090_journal_started:hmqbmp_090_journal_started"
  "091_journal_fungus_sent:hmqbmp_091_journal_fungus_sent"
  "092_journal_lift:hmqbmp_092_journal_lift"
  "093_journal_dayth:hmqbmp_093_journal_dayth"
  "094_journal_key:hmqbmp_094_journal_key"
  "095_journal_complete:hmqbmp_095_journal_complete"
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
  TORIRS_MAX_FRAMES="${TORIRS_MAX_FRAMES:-300}" \
  "$ROOT/run-live.sh" --skip-checks "$MANIFEST" testc test || true
  if [[ ! -f "$dest" ]]; then
    echo "MISSING $dest" >&2
  else
    echo "WROTE $dest ($(wc -c < "$dest") bytes)"
  fi
done
echo "done. count=$(ls -1 "$OUT"/*.bmp 2>/dev/null | wc -l)"
