#!/usr/bin/env bash
# Headless Gate D capture for Troll Stronghold.
# Requires an EMBED_SERVER=1 client and a current osrs239 script pack.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/OSRS-Content/osrs239-content/server/scripts/selftest/quest_troll}"
CLIENT="${TORIRS_CLIENT:-}"
if [[ -z "$CLIENT" ]]; then
  for c in /tmp/gp-stronghold-img-obj_opt_es/torirs /tmp/gp-stronghold-img-obj_opt/torirs "$ROOT/src/torirs"; do
    if [[ -x "$c" ]]; then
      CLIENT="$c"
      break
    fi
  done
fi
if [[ -z "$CLIENT" || ! -x "$CLIENT" ]]; then
  echo "no torirs client; set TORIRS_CLIENT" >&2
  exit 1
fi

mkdir -p "$OUT"
MANIFEST="/tmp/gp-stronghold-img-capture.ini"
cat > "$MANIFEST" <<'EOF'
[cache:boot]
epoch=dat2
game=oldschool
revision=239
quirks=none
dir=/workspace/cache.osrs239
spawn=50,50

[net:boot]
rev=osrs239
transport=embed
host=localhost
port=43595
client_version=239
user=testc
pass=test
scripts=/workspace/OSRS-Content/osrs239-content/server/scripts/build
rsa_exp=10001
rsa_mod=c30fcbc01e071ff224ea1a6508052d1140f87abaf8f40f7004efa59926708e5d99e2bc832fdca8276482dd0d690f644156850f47886f8032b3e9aa52508d24e8c9b7c50b8d8b8716fb8c3993bb6ce15e2124883edb7aaa7241a8b530f806c61cd1345879413fc105980a4f5fcdb3f0d743b14b16228b4d1496c83d3755a78a19

[ui:boot]
logic=cs2
chrome=revconfig
plugins=0
interface_id=161
EOF

SHOTS=(
  001_denulth_offer
  002_denulth_bad_news
  003_denulth_ambush
  004_denulth_help_choice
  005_denulth_decline
  006_denulth_help_ask
  007_denulth_treacherous
  008_denulth_accept_choice
  009_denulth_decline_nothing
  010_denulth_accept
  011_denulth_godspeed
  012_denulth_remind_no_boots
  013_denulth_remind_climb
  014_denulth_remind_hurry
  015_denulth_remind_boots
  016_denulth_remind_still_here
  017_denulth_remind_dad
  018_denulth_remind_stronghold
  019_denulth_remind_prison
  020_denulth_remind_and
  021_denulth_remind_free
  022_denulth_freed_godric
  023_denulth_tell_dunstan
  024_denulth_post_complete
  025_rocks_refuse_not_started
  026_rocks_refuse_low_agility
  027_rocks_refuse_no_boots
  028_rocks_success
  029_rocks_top_low_agility
  030_rocks_fall
  031_arena_entrance_shout
  032_arena_exit_blocked
  033_dad_talk
  034_dad_choice
  035_dad_why_called
  036_dad_named_after
  037_dad_decline
  038_dad_coward
  039_dad_accept
  040_dad_squish
  041_dad_not_interested
  042_dad_surrender
  043_dad_spare_choice
  044_dad_spare
  045_dad_to_the_death
  046_dad_no_refight
  047_dad_fight
  048_secret_door_unknown
  049_secret_door_open
  050_stronghold_exit
  051_pass_entrance
  052_stronghold_door
  053_prison_need_key
  054_prison_unlock
  055_cell_godric_need_key
  056_cell_godric_wrong_key
  057_cell_godric_unlock
  058_cell_godric_thanks
  059_cell_eadgar_need_key
  060_cell_eadgar_wrong_key
  061_cell_eadgar_unlock
  062_cell_eadgar_thanks
  063_cell_already_freed
  064_eadgar_stew_pot
  065_godric_in_cage
  066_godric_weakened
  067_godric_no_key
  068_godric_guard_hint
  069_godric_after_freed
  070_eadgar_in_cage
  071_eadgar_fireplace
  072_eadgar_no_key
  073_eadgar_belt_hint
  074_eadgar_after_freed
  075_eadgar_home_hi
  076_eadgar_home_welcome
  077_eadgar_home_sample
  078_eadgar_home_choice
  079_eadgar_live_close
  080_eadgar_too_skinny
  081_eadgar_no_thanks
  082_eadgar_your_loss
  083_guard_low_thieving
  084_guard_attempt
  085_guard_key
  086_guard_empty
  087_guard_fail
  088_guard_combat
  089_guard_no_room
  090_dunstan_mid_rescue
  091_dunstan_not_yet
  092_dunstan_hurry
  093_dunstan_godric_home
  094_dunstan_safe
  095_dunstan_heirloom
  096_complete_scroll
  097_dunstan_no_room
  098_dunstan_law_replace
  099_dunstan_law_cost
  100_dunstan_law_hands
  101_dunstan_son_after
  102_journal_not_started
  103_journal_started
  104_journal_defeated_dad
  105_journal_prison
  106_journal_freed
  107_journal_complete
)

cd "$ROOT"
for shot in "${SHOTS[@]}"; do
  dest="$OUT/${shot}.bmp"
  if [[ -s "$dest" ]]; then
    echo "skip existing $shot"
    continue
  fi
  echo "capture $shot"
  SDL_VIDEODRIVER=dummy \
  TORIRS_PLUGINS=0 \
  TORIRSSERVER_GOD=1 \
  TORIRS_MAX_FRAMES="${TORIRS_MAX_FRAMES:-420}" \
  TORIRS_EXIT_BMP="$dest" \
  TORIRS_NET_CHEAT="god 1;trollstrongholdbmp_${shot}" \
    "$CLIENT" --manifest "$MANIFEST" --soft3d >/tmp/gp-stronghold-img-"$shot".log 2>&1 || true
  if [[ ! -s "$dest" ]]; then
    echo "FAIL $shot (no bmp)" >&2
  fi
done

echo "BMP count $(ls -1 "$OUT"/*.bmp 2>/dev/null | wc -l)"
echo "unique MD5 $(md5sum "$OUT"/*.bmp 2>/dev/null | awk '{print $1}' | sort -u | wc -l)"
