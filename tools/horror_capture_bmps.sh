#!/usr/bin/env bash
# Headless Gate D capture for Horror from the Deep.
# Requires an EMBED_SERVER=1 client and a current osrs239 script pack.
# Plugins off. Player unkillable. Each shot must show the mesbox/chathead/journal/reward.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/OSRS-Content/osrs239-content/server/scripts/selftest/quest_horror}"
CLIENT="${TORIRS_CLIENT:-}"
if [[ -z "$CLIENT" ]]; then
  for c in /tmp/gp-horror-img-obj_opt_es/torirs /tmp/gp-horror-img-obj_opt/torirs "$ROOT/src/torirs"; do
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
MANIFEST="${HORROR_MANIFEST:-/tmp/gp-horror-img-capture.ini}"
if [[ -z "${HORROR_MANIFEST:-}" ]]; then
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

mapfile -t shots < <(grep -oE '\[debugproc,horrorbmp_[a-z0-9_]+\]' \
  "$ROOT/OSRS-Content/osrs239-content/server/scripts/quests/quest_horror/scripts/horror_bmp.rs2" \
  | sed 's/\[debugproc,horrorbmp_//;s/\]//')

export TORIRS_PLUGINS=0
export TORIRSSERVER_GOD=1
export SDL_VIDEODRIVER=dummy
export PLATFORM_OBJ_BASE="${PLATFORM_OBJ_BASE:-/tmp/gp-horror-img-obj}"

frames="${HORROR_MAX_FRAMES:-420}"
for name in "${shots[@]}"; do
  dest="$OUT/${name}.bmp"
  if [[ -s "$dest" && "${HORROR_RECAPTURE:-}" != "1" ]]; then
    echo "skip existing $name"
    continue
  fi
  echo "CAPTURING $name via ::horrorbmp_$name"
  TORIRS_PLUGINS=0 \
  TORIRSSERVER_GOD=1 \
  SDL_VIDEODRIVER=dummy \
  TORIRS_NET_CHEAT="god 1;horrorbmp_$name" \
  TORIRS_MAX_FRAMES="$frames" \
  TORIRS_EXIT_BMP="$dest" \
  "$CLIENT" --manifest "$MANIFEST" --user horror --pass a --soft3d \
    >"/tmp/gp-horror-img-$name.log" 2>&1 || true
  if [[ ! -s "$dest" ]]; then
    echo "WARN missing $dest" >&2
  fi
done

echo "BMP count: $(find "$OUT" -name '*.bmp' | wc -l)"
echo "Unique MD5s: $(find "$OUT" -name '*.bmp' -exec md5sum {} + | awk '{print $1}' | sort -u | wc -l)"
