#!/usr/bin/env bash
# Headless Gate D capture for Priest in Peril.
# Requires an EMBED_SERVER=1 client and a current osrs239 script pack.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/OSRS-Content/osrs239-content/server/scripts/selftest/quest_priestperil}"
CLIENT="${TORIRS_CLIENT:-}"
if [[ -z "$CLIENT" ]]; then
  for c in /tmp/gp-peril-img-obj_opt_es/torirs /tmp/gp-peril-img-obj_opt/torirs "$ROOT/src/torirs"; do
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
MANIFEST="/tmp/gp-peril-img-capture.ini"
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
  001_roald_offer
  002_roald_decline
  003_roald_accept
  004_roald_started_nudge
  005_roald_dog_ask
  006_roald_angry_dog
  007_roald_still_here
  008_temple_door_locked
  009_temple_door_knock
  010_temple_door_choice
  011_temple_door_roald_sent
  012_temple_door_joke_moved_in
  013_temple_door_joke_historic
  014_temple_door_joke_pipes
  015_temple_door_help_choice
  016_temple_door_nope
  017_temple_door_agree_kill_dog
  018_temple_door_kill_dog_ask
  019_temple_door_after_dog
  020_guardian_refuse_before_start
  021_guardian_fight_allowed
  022_prisondoor_locked
  023_drezel_first_talk
  024_drezel_tale_choice
  025_drezel_tale
  026_drezel_skip_tale
  027_drezel_aid_choice
  028_drezel_aid_no
  029_drezel_aid_yes
  030_drezel_key_progress
  031_prisondoor_gold_key_fail
  032_prisondoor_iron_key_unlock
  033_prisondoor_walkthrough
  034_drezel_bless_water
  035_well_look_murky
  036_well_fill_bucket
  037_monument_study
  038_monument_swap_iron
  039_monument_swap_other
  040_monument_steal_prevented
  041_coffin_look
  042_coffin_murky_refuse
  043_coffin_plain_water
  044_coffin_pour_blessed
  045_drezel_after_pour
  046_drezel_salve_explain
  047_drezel_essence_ask
  048_drezel_essence_need
  049_drezel_essence_progress
  050_drezel_last_essence
  051_complete_scroll
  052_holy_barrier_blessing
  053_holy_barrier_stop
  054_holy_barrier_pass
  055_well_look_clean
  056_journal_not_started
  057_journal_mid
  058_journal_complete
  059_roald_find_key
  060_roald_vampire
  061_drezel_blessed_hint
  062_drezel_meet_downstairs
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
  TORIRS_NET_CHEAT="god 1;priestperilbmp_${shot}" \
    "$CLIENT" --manifest "$MANIFEST" --soft3d >/tmp/gp-peril-img-"$shot".log 2>&1 || true
  if [[ ! -s "$dest" ]]; then
    echo "FAIL $shot (no bmp)" >&2
  fi
done

echo "BMP count $(ls -1 "$OUT"/*.bmp 2>/dev/null | wc -l)"
echo "unique MD5 $(md5sum "$OUT"/*.bmp 2>/dev/null | awk '{print $1}' | sort -u | wc -l)"
