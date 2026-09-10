#!/usr/bin/env bash
# Capture one named BMP per Regicide interaction.
# Plugins off. Player unkillable. Each shot must show the mesbox/chathead/journal/reward.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/OSRS-Content/osrs239-content/server/scripts/selftest/quest_regicide"
CLIENT="${REGICIDE_CLIENT:-}"
if [[ -z "$CLIENT" ]]; then
  for c in /tmp/gp-regicide-img-obj_opt_es/torirs /tmp/gp-regicide-img-obj_es/torirs "$ROOT/src/torirs"; do
    if [[ -x "$c" ]]; then
      CLIENT="$c"
      break
    fi
  done
fi
mkdir -p "$OUT"
MANIFEST="${REGICIDE_MANIFEST:-/tmp/gp-regicide-img-capture.ini}"
if [[ -z "${REGICIDE_MANIFEST:-}" ]]; then
  cat > "$MANIFEST" <<EOF
[cache:boot]
epoch=dat2
game=oldschool
revision=239
quirks=none
dir=$ROOT/cache.osrs239
spawn=50,50

[net:boot]
rev=osrs239
transport=embed
host=localhost
port=43595
client_version=239
user=testc
pass=test
scripts=$ROOT/OSRS-Content/osrs239-content/server/scripts/build
rsa_exp=10001
rsa_mod=c30fcbc01e071ff224ea1a6508052d1140f87abaf8f40f7004efa59926708e5d99e2bc832fdca8276482dd0d690f644156850f47886f8032b3e9aa52508d24e8c9b7c50b8d8b8716fb8c3993bb6ce15e2124883edb7aaa7241a8b530f806c61cd1345879413fc105980a4f5fcdb3f0d743b14b16228b4d1496c83d3755a78a19

[ui:boot]
logic=cs2
chrome=revconfig
plugins=0
interface_id=161
EOF
fi
export TORIRS_PLUGINS=0
export TORIRSSERVER_GOD=1
export SDL_VIDEODRIVER=dummy
export PLATFORM_OBJ_BASE="${PLATFORM_OBJ_BASE:-/tmp/gp-regicide-img-obj}"

if [[ ! -x "${CLIENT:-}" ]]; then
  echo "missing client $CLIENT" >&2
  exit 1
fi

mapfile -t shots < <(grep -oE '\[debugproc,regicidebmp_[a-z0-9_]+\]' \
  "$ROOT/OSRS-Content/osrs239-content/server/scripts/quests/quest_regicide/scripts/regicide_selftest.rs2" \
  | sed 's/\[debugproc,regicidebmp_//;s/\]//')

frames="${REGICIDE_MAX_FRAMES:-480}"
for name in "${shots[@]}"; do
  dest="$OUT/${name}.bmp"
  if [[ -s "$dest" && "${REGICIDE_RECAPTURE:-}" != "1" ]]; then
    echo "skip existing $name"
    continue
  fi
  echo "CAPTURING $name via ::regicidebmp_$name"
  TORIRS_PLUGINS=0 \
  TORIRSSERVER_GOD=1 \
  SDL_VIDEODRIVER=dummy \
  TORIRS_NET_CHEAT="god 1;regicidebmp_$name" \
  TORIRS_MAX_FRAMES="$frames" \
  TORIRS_EXIT_BMP="$dest" \
  "$CLIENT" --manifest "$MANIFEST" --user regicide --pass a --soft3d \
    >"/tmp/gp-regicide-img-$name.log" 2>&1 || true
  if [[ ! -s "$dest" ]]; then
    echo "WARN missing $dest" >&2
  fi
done

echo "BMP count: $(find "$OUT" -name '*.bmp' | wc -l)"
echo "Unique MD5s: $(find "$OUT" -name '*.bmp' -exec md5sum {} + | awk '{print $1}' | sort -u | wc -l)"
