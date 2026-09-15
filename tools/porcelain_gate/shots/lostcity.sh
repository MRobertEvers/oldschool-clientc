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
# ---- AND THE ONE CHANGE THE SERVER CHECKOUT NEEDS, WHICH IS NOT IN THIS REPO ----
#
# `content/scripts/_test/scripts/cheats/cheat_torirs.rs2` over there is THIS
# harness's file -- its own header says so -- and `~skiptutorial` in it reads:
#
#     [debugproc,skiptutorial]
#     if_close;
#     if (p_finduid(uid) = true) { %tutorial = 1000; ~tutorial_set_active_tabs; ... }
#
# `if_close` CANNOT CLOSE THE TUTORIAL BOX. `tut_open` puts the parchment in the
# engine's TUT modal slot, and `Player.closeModal` -- which is all IF_CLOSE runs
# -- clears modalMain, modalChat and modalSide and never modalTutorial; only
# `Player.closeTutorial`, reached from `tut_close`, writes `TutOpen(-1)`. The
# content's own `[label,tutorial_complete]` says the pair out loud:
# `tut_close(); if_close;`.
#
# So on an account's FIRST login -- the only login that opens the box, because
# login.rs2 gates @start_tutorial on `%tutorial < ^tutorial_complete` and
# standing on tutorial island -- the cheat mounts the tabs and leaves the
# parchment owning the whole chat region for the rest of the run. The chat
# builtin then does not draw AT ALL, so every plugin that speaks in the chat
# photographs as a plugin that said nothing. Thirty-seven of the forty-seven
# captures of 2026-09-14 taken on this lane's own frame are in that state,
# `control-live` among them.
#
# The fix is one line, in the server checkout, and it is recorded here because
# nothing in this repository can carry it:
#
#     [debugproc,skiptutorial]
#     tut_close();          <-- add this
#     if_close;
#
# Until it is applied, a cs1live row's first run is unusable for anything that
# speaks in the chat, and `live_check.py` will say so: the client writes
# `CHAT_REGION iface=.. log_visible=..` beside every BMP. Running the same row a
# second time under the same name, or `lc_prime.sh`, gets a clear pane without
# touching the server at all.

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
