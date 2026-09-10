#!/usr/bin/env bash
# Capture one named BMP per Death Plateau interaction.
# Plugins off. Player unkillable. Each shot must show the mesbox/chathead/journal/reward.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/OSRS-Content/osrs239-content/server/scripts/selftest/quest_death"
MANIFEST="${DP_MANIFEST:-$ROOT/manifests/manifest_osrs239_sailing.ini}"
CLIENT="${DP_CLIENT:-$ROOT/src/torirs}"
mkdir -p "$OUT"
export TORIRS_PLUGINS=0
export TORIRSSERVER_GOD=1
export SDL_VIDEODRIVER=dummy
export PLATFORM_OBJ_BASE="${PLATFORM_OBJ_BASE:-/tmp/gp-plateau-img-obj}"

shots=(
  "001_denulth_offer:dpbmp_001_denulth_offer"
  "002_denulth_decline:dpbmp_002_denulth_decline"
  "003_denulth_accept:dpbmp_003_denulth_accept"
  "004_denulth_place:dpbmp_004_denulth_place"
  "005_denulth_whiteknights:dpbmp_005_denulth_whiteknights"
  "006_denulth_remind:dpbmp_006_denulth_remind"
  "007_denulth_youcant:dpbmp_007_denulth_youcant"
  "008_denulth_hello:dpbmp_008_denulth_hello"
  "009_denulth_cert:dpbmp_009_denulth_cert"
  "010_denulth_map_handin:dpbmp_010_denulth_map_handin"
  "011_denulth_combo_handin:dpbmp_011_denulth_combo_handin"
  "012_complete_scroll:dpbmp_012_denulth_complete"
  "013_denulth_seeyou:dpbmp_013_denulth_seeyou"
  "014_denulth_welcome:dpbmp_014_denulth_welcome"
  "015_saba_not_started:dpbmp_015_saba_not_started"
  "016_saba_buzz:dpbmp_016_saba_buzz"
  "017_saba_secret:dpbmp_017_saba_secret"
  "018_saba_where:dpbmp_018_saba_where"
  "019_saba_choice:dpbmp_019_saba_choice"
  "020_eohric_castle:dpbmp_020_eohric_castle"
  "021_eohric_prince:dpbmp_021_eohric_prince"
  "022_eohric_cesspit:dpbmp_022_eohric_cesspit"
  "023_eohric_guard:dpbmp_023_eohric_guard"
  "024_eohric_plat:dpbmp_024_eohric_plat"
  "025_eohric_lastnight:dpbmp_025_eohric_lastnight"
  "026_eohric_wont_talk:dpbmp_026_eohric_wont_talk"
  "027_eohric_weakness:dpbmp_027_eohric_weakness"
  "028_eohric_looking:dpbmp_028_eohric_looking"
  "029_harold_duty:dpbmp_029_harold_duty"
  "030_harold_wont:dpbmp_030_harold_wont"
  "031_harold_ale:dpbmp_031_harold_ale"
  "032_harold_combo:dpbmp_032_harold_combo"
  "033_harold_gamble:dpbmp_033_harold_gamble"
  "034_harold_dice:dpbmp_034_harold_dice"
  "035_harold_iou:dpbmp_035_harold_iou"
  "036_harold_blur:dpbmp_036_harold_blur"
  "037_harold_reclaim:dpbmp_037_harold_reclaim"
  "038_iou_read:dpbmp_038_iou_read"
  "039_combo_read:dpbmp_039_combo_read"
  "040_harold_door:dpbmp_040_harold_door"
  "041_harold_knock:dpbmp_041_harold_knock"
  "042_sherpa_knock:dpbmp_042_sherpa_knock"
  "043_sherpa_help:dpbmp_043_sherpa_help"
  "044_sherpa_back:dpbmp_044_sherpa_back"
  "045_castle_locked:dpbmp_045_castle_locked"
  "046_stone_already:dpbmp_046_stone_already"
  "047_stone_unlock:dpbmp_047_stone_unlock"
  "048_tenzing_ask:dpbmp_048_tenzing_ask"
  "049_tenzing_accept:dpbmp_049_tenzing_accept"
  "050_tenzing_decline:dpbmp_050_tenzing_decline"
  "051_tenzing_wait:dpbmp_051_tenzing_wait"
  "052_tenzing_missing:dpbmp_052_tenzing_missing"
  "053_tenzing_give:dpbmp_053_tenzing_give"
  "054_tenzing_map:dpbmp_054_tenzing_map"
  "055_tenzing_lost_map:dpbmp_055_tenzing_lost_map"
  "056_tenzing_lost:dpbmp_056_tenzing_lost"
  "057_tenzing_boots:dpbmp_057_tenzing_boots"
  "058_tenzing_sherpa:dpbmp_058_tenzing_sherpa"
  "059_tenzing_how:dpbmp_059_tenzing_how"
  "060_tenzing_place:dpbmp_060_tenzing_place"
  "061_dunstan_boots:dpbmp_061_dunstan_boots"
  "062_dunstan_son:dpbmp_062_dunstan_son"
  "063_dunstan_wait:dpbmp_063_dunstan_wait"
  "064_dunstan_cert:dpbmp_064_dunstan_cert"
  "065_dunstan_spikes:dpbmp_065_dunstan_spikes"
  "066_dunstan_iron:dpbmp_066_dunstan_iron"
  "067_dunstan_anvil:dpbmp_067_dunstan_anvil"
  "068_dunstan_guard:dpbmp_068_dunstan_guard"
  "069_dunstan_plat:dpbmp_069_dunstan_plat"
  "070_dunstan_repair:dpbmp_070_dunstan_repair"
  "071_dunstan_nothing:dpbmp_071_dunstan_nothing"
  "072_scout:dpbmp_072_scout"
  "073_dangersign:dpbmp_073_dangersign"
  "074_boots_small:dpbmp_074_boots_small"
  "075_spiked_carry:dpbmp_075_spiked_carry"
  "076_rocks:dpbmp_076_rocks"
  "077_wounded:dpbmp_077_wounded"
  "078_wander:dpbmp_078_wander"
  "079_archer:dpbmp_079_archer"
  "080_archer_trapped:dpbmp_080_archer_trapped"
  "081_journal_start:dpbmp_081_journal_start"
  "082_journal_mid:dpbmp_082_journal_mid"
  "083_journal_complete:dpbmp_083_journal_complete"
  "084_denulth_thatsall:dpbmp_084_denulth_thatsall"
  "085_eohric_choice:dpbmp_085_eohric_choice"
  "086_tenzing_shop:dpbmp_086_tenzing_shop"
  "087_harold_choice:dpbmp_087_harold_choice"
  "088_complete_honor:dpbmp_088_complete_honor"
)

if [[ ! -x "$CLIENT" ]]; then
  echo "missing client $CLIENT" >&2
  exit 1
fi

frames="${DP_MAX_FRAMES:-420}"
for spec in "${shots[@]}"; do
  name="${spec%%:*}"
  cheat="${spec##*:}"
  dest="$OUT/${name}.bmp"
  echo "CAPTURING $name via ::$cheat"
  TORIRS_PLUGINS=0 \
  TORIRSSERVER_GOD=1 \
  SDL_VIDEODRIVER=dummy \
  TORIRS_NET_CHEAT="god 1;$cheat" \
  TORIRS_MAX_FRAMES="$frames" \
  TORIRS_EXIT_BMP="$dest" \
  "$CLIENT" --manifest "$MANIFEST" --user dplateau --pass a --soft3d || true
  if [[ ! -s "$dest" ]]; then
    echo "WARN missing $dest" >&2
  fi
done

echo "BMP count: $(find "$OUT" -name '*.bmp' | wc -l)"
echo "Unique MD5s: $(find "$OUT" -name '*.bmp' -exec md5sum {} + | awk '{print $1}' | sort -u | wc -l)"
