#!/bin/sh
# Run torirs from a boot manifest.
#
#   ./run-live.sh <manifest.ini> [user] [pass]
#
# The manifest (manifest_rs254.ini, manifest_osrs230.ini, manifest_xrsps.ini, …)
# specifies cache, rev, transport, host/port and RSA keys. user/pass default to
# asdf/a. Extra behavior via env vars: TORIRS_NET_DEBUG=1,
# TORIRS_NET_CHEAT="tele 0,50,50,21,21", TORIRS_MAX_FRAMES/TORIRS_EXIT_BMP.
#
# For lc254 against a live LostCity server, login checks the cache CRCs against
# the server; this script fetches them from http://<host>/crc (port 80) unless
# TORIRS_JAG_CRC is already set. Other revisions (e.g. the osrs230 mock) skip it.
set -eu

cd "$(dirname "$0")"

MANIFEST="${1:?usage: run-live.sh <manifest.ini> [user] [pass]}"
USER_NAME="${2:-asdf}"
PASS="${3:-a}"

if [ ! -f "$MANIFEST" ]; then
    echo "run-live.sh: manifest '$MANIFEST' not found" >&2
    exit 1
fi

if [ ! -x src/torirs ]; then
    echo "run-live.sh: building src/torirs..." >&2
    make -C src torirs
fi

# rev + host come from the manifest [net:boot] section.
REV=$(sed -n 's/^[[:space:]]*rev[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)
HOST=$(sed -n 's/^[[:space:]]*host[[:space:]]*=[[:space:]]*//p' "$MANIFEST" | head -1)

# lc254 live login checks cache CRCs; fetch the 9 big-endian int32s from the
# server's web endpoint (TORIRS_JAG_CRC env wins over the manifest).
if [ "$REV" = "lc254" ] && [ -z "${TORIRS_JAG_CRC:-}" ]; then
    TORIRS_JAG_CRC=$(curl -sf "http://${HOST:-localhost}/crc" | python3 -c "
import sys, struct
d = sys.stdin.buffer.read()
print(','.join(str(x) for x in struct.unpack('>%di' % (len(d) // 4), d)))
") || {
        echo "run-live.sh: cannot fetch http://${HOST:-localhost}/crc — is the server up?" >&2
        exit 1
    }
    export TORIRS_JAG_CRC
fi

exec src/torirs --manifest "$MANIFEST" --user "$USER_NAME" --pass "$PASS" 
