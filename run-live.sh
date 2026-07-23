#!/bin/sh
# Run torirs against a local LostCity_Server (Engine-TS rev 254).
#
#   ./run-live.sh [host] [user] [pass]
#
# Defaults: localhost / debugcc / test. The server must be up: game port
# 43594 and the /crc endpoint on port 80. Extra behavior via env vars
# (see readme "Running against a LostCity server"): TORIRS_NET_DEBUG=1,
# TORIRS_NET_CHEAT="tele 0,50,50,21,21", TORIRS_MAX_FRAMES/TORIRS_EXIT_BMP.
set -eu

cd "$(dirname "$0")"

HOST="${1:-localhost}"
USER_NAME="${2:-asdf}"
PASS="${3:-a}"

if [ ! -x src/torirs ]; then
    echo "run-live.sh: building src/torirs..." >&2
    make -C src torirs
fi

# The client checks its cache CRCs against the server's during login; fetch
# them from the server's web endpoint (9 big-endian int32s).
if [ -z "${TORIRS_JAG_CRC:-}" ]; then
    TORIRS_JAG_CRC=$(curl -sf "http://${HOST}/crc" | python3 -c "
import sys, struct
d = sys.stdin.buffer.read()
print(','.join(str(x) for x in struct.unpack('>%di' % (len(d) // 4), d)))
") || {
        echo "run-live.sh: cannot fetch http://${HOST}/crc — is the server up?" >&2
        exit 1
    }
    export TORIRS_JAG_CRC
fi

# cache254.lostcity is a copy of the live server's own store
# (LostCity_Server/engine/data/pack/main_file_cache.*) — the old cache254
# snapshot is stale vs the server (missing e.g. the quest journal interfaces).
exec src/torirs cache254.lostcity --connect "$HOST" --user "$USER_NAME" --pass "$PASS"
