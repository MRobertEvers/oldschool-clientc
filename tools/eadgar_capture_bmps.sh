#!/usr/bin/env bash
# Headless Gate D capture for Eadgar's Ruse.
# Requires an EMBED_SERVER=1 client and a current osrs239 script pack.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/OSRS-Content/osrs239-content/server/scripts/selftest/quest_eadgar}"
CLIENT="${TORIRS_CLIENT:-}"
if [[ -z "$CLIENT" ]]; then
  for c in /tmp/gp-eadgar-img-obj_opt_es/torirs /tmp/gp-eadgar-img-obj_opt/torirs "$ROOT/src/torirs"; do
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
MANIFEST="/tmp/gp-eadgar-img-capture.ini"
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
  001_sanfew_prereq_druid
  002_sanfew_prereq_herblore
  003_sanfew_prereq_eadgar
  004_sanfew_offer
  005_sanfew_offer_trolls
  006_sanfew_offer_what
  007_sanfew_offer_goutweed
  008_sanfew_offer_eadgar
  009_sanfew_offer_choice
  010_sanfew_decline
  011_sanfew_decline_ack
  012_sanfew_accept
  013_sanfew_accept_mes
  014_sanfew_started_remind
  015_sanfew_mid_remind
  016_sanfew_turnin_ask
  017_sanfew_turnin_none
  018_sanfew_turnin_wait
  019_sanfew_turnin_have
  020_sanfew_turnin_thanks
  021_complete_scroll
  022_sanfew_postquest
  023_sanfew_postquest_trade
  024_sanfew_postquest_take
  025_eadgar_goutweed_choice
  026_eadgar_goutweed_ask
  027_eadgar_goutweed_cooks
  028_eadgar_goutweed_thanks
  029_eadgar_remind_burntmeat
  030_eadgar_remind_kitchen
  031_eadgar_tasty_human
  032_eadgar_hoho
  033_eadgar_looks_human
  034_eadgar_how
  035_eadgar_parrot_plan
  036_eadgar_want_parrot_none
  037_eadgar_want_parrot_zoo
  038_eadgar_show_parrot
  039_eadgar_ingenious
  040_eadgar_hide_rack
  041_eadgar_hide_reminder
  042_eadgar_hide_rack_again
  043_eadgar_hidden
  044_eadgar_scarecrow
  045_eadgar_chickens
  046_eadgar_clothes
  047_eadgar_items_check
  048_eadgar_items_list
  049_eadgar_give_logs
  050_eadgar_logs_ok
  051_eadgar_give_robe
  052_eadgar_robe_ok
  053_eadgar_chicken_more
  054_eadgar_chicken_done
  055_eadgar_grain_more
  056_eadgar_grain_done
  057_eadgar_items_complete
  058_eadgar_potion_none
  059_eadgar_potion_howto
  060_eadgar_potion_give
  061_eadgar_fetch_parrot
  062_eadgar_fetch_reminder
  063_eadgar_fake_man
  064_eadgar_fake_man_take
  065_eadgar_postfakeman
  066_eadgar_goodluck
  067_eadgar_post_burntmeat
  068_burntmeat_hello
  069_burntmeat_tasty
  070_burntmeat_tough
  071_burntmeat_what
  072_burntmeat_quest
  073_burntmeat_secret
  074_burntmeat_also_quest
  075_burntmeat_bring_human
  076_burntmeat_working
  077_burntmeat_not_yet
  078_burntmeat_fake_man
  079_burntmeat_look
  080_burntmeat_stew
  081_burntmeat_reward
  082_burntmeat_burnt
  083_burntmeat_precious
  084_burntmeat_stew_choice
  085_burntmeat_where_goutweed
  086_burntmeat_key_drawer
  087_burntmeat_ill_be_going
  088_burntmeat_bye
  089_pete_aviary
  090_pete_choice
  091_pete_nice
  092_pete_isnt_it
  093_pete_when_add
  094_pete_vodka
  095_pete_what_feed
  096_pete_pineapple
  097_pete_drunk_parrot
  098_pete_vet
  099_alco_chunks
  100_hatch_pour
  101_hatch_catch
  102_hatch_already
  103_hide_parrot
  104_rack_empty
  105_rack_fetch
  106_rack_spleen
  107_tegid_laundry
  108_tegid_what
  109_tegid_ask_robe
  110_tegid_refuse
  111_tegid_threat_choice
  112_tegid_nevermind
  113_tegid_threat
  114_tegid_relent
  115_tegid_give_robe
  116_thistle_pick
  117_thistle_dry
  118_thistle_need_dry
  119_thistle_too_big
  120_troll_potion
  121_thistle_wither
  122_drawers_empty
  123_drawers_key
  124_drawers_already
  125_storeroom_locked
  126_storeroom_unlock
  127_goutweed_search
  128_goutweed_found
  129_goutweed_guard
  130_journal_not_started
  131_journal_started
  132_journal_parrot
  133_journal_items
  134_journal_potion
  135_journal_storeroom
  136_journal_complete
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
  TORIRS_NET_CHEAT="god 1;eadgarbmp_${shot}" \
    "$CLIENT" --manifest "$MANIFEST" --soft3d >/tmp/gp-eadgar-img-"$shot".log 2>&1 || true
  if [[ ! -s "$dest" ]]; then
    echo "FAIL $shot (no bmp)" >&2
  fi
done

echo "BMP count $(ls -1 "$OUT"/*.bmp 2>/dev/null | wc -l)"
echo "unique MD5 $(md5sum "$OUT"/*.bmp 2>/dev/null | awk '{print $1}' | sort -u | wc -l)"
