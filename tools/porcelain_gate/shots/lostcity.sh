#!/bin/zsh
# lostcity.sh -- the live CS1 lane.
#
# The CS1 shots used to run `--offline`, which boots the world but never logs
# in. Half of every plugin's output is invisible there: a skill with no reading
# draws no orb, an empty inventory has nothing to hover, and a chat pane nobody
# talks in cannot show a notification. Those shots were not a CS1 test.
#
# This drives the real LostCity server instead. Three things cost a run each to
# learn, so they are here rather than in anybody's head:
#
#   1. The server is revision 289, not 254. `engine.revision` in
#      data/config/world.json is the authority; a 254 manifest is refused with
#      login reply 6 before the server logs anything at all.
#   2. /crc returns TEN int32s and the login block carries NINE. It is the
#      FIRST nine -- including the leading zero -- because the server compares
#      a CRC taken over exactly those 36 bytes (World.ts, CrcBuffer32). Sending
#      the last nine is refused with the same reply 6 as a bad revision, which
#      is why this looks like a revision problem and is not.
#   3. Reply 6 is also what an RSA failure returns ("sending out of date
#      intentionally"), so the reply does not identify which of the three gates
#      refused. Check them in the order above.
#
# Start the server first:
#   cd ~/Documents/git_repos/LostCity_Server/engine && npm run quickstart
set -u

here=${0:A:h}
repo=${TORIRS_LC_REPO:-/Users/matthewevers/Documents/git_repos/3draster}
manifest=${TORIRS_LC_MANIFEST:-$repo/manifests/manifest_rs289lc.ini}

# Live, every run: the server repacks and a stale set is refused like a stale
# revision. Cheap enough that caching it is not worth the failure mode.
crc=$(python3 - <<'PY'
import struct, urllib.request
data = urllib.request.urlopen('http://localhost/crc', timeout=10).read()
print(','.join(map(str, struct.unpack('>%di' % (len(data) // 4), data)[:9])))
PY
)
[ -n "$crc" ] || { echo "lostcity.sh: no CRCs -- is the server up on port 80?"; exit 2; }
echo "$crc"
