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

/** RUN (the Live panel's button, the Script panel's Run, the File > Run
 *  hotkey): SAVE_ALL -> packs -> client cache -> reload, through the editor
 *  server's live link. 1 sent, 0 refused (a RUN or build in flight, or the
 *  connection is gone; the console says which), -1 when this window has no
 *  Live panel. */
int
Editor_Live_Run(struct EditorUi* ui);

/** ::tele `player` to ABSOLUTE tile (x, z) on `level` through the live link,
 *  as the panel's "Teleport to selection" does (a scene's Enter). 1 sent, 0
 *  refused (the live lane is not attached, or the connection is gone; the
 *  console says which), -1 when this window has no Live panel. */
int
Editor_Live_Teleport(
    struct EditorUi* ui,
    const char* player,
    int x,
    int z,
    int level);

/** The cheat `line` ("goto 3222 3218 0", no "::") for `player` through the
 *  live link (LIVE_CHEAT), logged in the console as the panel's own cheats
 *  are: the shop builder's and the encounter builder's Test go this way.
 *  1 sent, 0 refused (the live lane is not attached, or the connection is
 *  gone; the console says which), -1 when this window has no Live panel. */
int
Editor_Live_Cheat(
    struct EditorUi* ui,
    const char* player,
    const char* line);

/** 1 while a RUN or a build is in flight (RUN is disabled), 0 when idle,
 *  -1 without a Live panel. */
int
Editor_Live_Busy(struct EditorUi* ui);

#endif
