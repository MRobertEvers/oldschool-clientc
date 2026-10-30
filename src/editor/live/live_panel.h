#ifndef SRC_EDITOR_LIVE_LIVE_PANEL_H
#define SRC_EDITOR_LIVE_LIVE_PANEL_H

/*
 * The Live panel (docs/MAPED_DESIGN.html #run, "The live inspector"): the
 * live link's state and the players online, Attach / Launch / Detach, RUN and
 * the two builds, the followed player's vars (LIVE_SUBSCRIBE), a cheat line,
 * "teleport me to the selection", the stale units the game server reports,
 * and every build and live line. Following a player publishes
 * STATE_LIVE_FOCUS so the Client's viewer windows know whom to lock to.
 *
 * The panel never reads a socket: it talks through an Editor_LiveTransport
 * (live_client.h). Editor_Live_Register binds that to the editor host
 * (live_host.c); a test calls Editor_Live_RegisterWith with its own.
 */

#include "editor/editor_ui.h"
#include "editor/live/live_client.h"

/** Register the Live panel over the window's editor host connection
 *  (live_host.c). Needs ui->editor; without one it registers nothing. */
void
Editor_Live_Register(struct EditorUi* ui);

/** Register the Live panel over `transport` (copied). */
void
Editor_Live_RegisterWith(
    struct EditorUi* ui,
    const struct Editor_LiveTransport* transport);

/** The panel's model (NULL when the panel is not registered): what the
 *  Live panel shows, for a test or another module. */
const struct Editor_LiveModel*
Editor_LivePanelModel(struct EditorUi* ui);

#endif
