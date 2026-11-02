#!/bin/sh
# The live lane's "game server" for the end to end (run.sh): the editor
# server's LIVE_ATTACH launch starts this as `<server_bin> <port> --rev <rev>`
# with TORIRSSERVER_CONTENT / _CACHE / _SCRIPTS / _PACK_DIR pointing at the
# lane. It is a scriptrun whose one bot is ALREADY logged in and talking to
# Hans when the RUN lands, with the control link on the lane's game port, so
# the reload reaches the very world the bot plays in.
#
# Paced at LIVE_E2E_TICK_MS a tick (a live server's clock is 600) so a build
# that takes minutes is a few hundred ticks of the bot's patience, not millions.
port="$1"
: "${LIVE_E2E_REPO:?}" "${LIVE_E2E_DIR:?}" "${LIVE_E2E_OBJ:=build_opt}" "${LIVE_E2E_TICK_MS:=200}"
cd "$LIVE_E2E_REPO" || exit 2
mkdir -p "$LIVE_E2E_DIR/session"
TORIRS_SCRIPTRUN_WIRELOG="$LIVE_E2E_DIR/wire.log" \
exec "$LIVE_E2E_REPO/src/$LIVE_E2E_OBJ/torirsserver" --scriptrun src/maped/test/live_e2e/bot.lua \
    --bots 1 --name srun --ticks 60000 --tick-ms "$LIVE_E2E_TICK_MS" --control-port "$port" \
    --session "$LIVE_E2E_DIR/session" --fixture test/quests/fixtures/fresh_lumbridge.ini \
    > "$LIVE_E2E_DIR/scriptrun.log" 2>&1
