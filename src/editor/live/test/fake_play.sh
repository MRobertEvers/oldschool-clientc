#!/bin/sh
# A stand-in for the lane's play client (the game client the editor server
# starts after a RUN): records its command line and the environment keys the
# launcher sets or must strip beside the mock peer's log, then idles until it
# is stopped. The editor server owns its lifetime; the test kills it to play
# a client that died.
{
    echo "argv $*"
    echo "env TORIRS_PREFS=${TORIRS_PREFS-<unset>}"
    echo "env TORIRS_SIM_HOTKEY=${TORIRS_SIM_HOTKEY-<unset>}"
    echo "env TORIRSMAPED_LIVE_DIR=${TORIRSMAPED_LIVE_DIR-<unset>}"
    echo "env TORIRS_TEST_MARK=${TORIRS_TEST_MARK-<unset>}"
} >> "$MOCK_PEER_LOG.play"
exec sleep 600
