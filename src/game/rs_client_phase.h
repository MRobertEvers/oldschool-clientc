#ifndef SRC_GAME_RS_CLIENT_PHASE_H
#define SRC_GAME_RS_CLIENT_PHASE_H

/*
 * Where the client is in its life: whether the tree it is baking has
 * finished, which top-level screen that tree is, and whether a bake must keep
 * the last frame on screen instead of painting.
 *
 * Two answers that run independently. The bake state is per BAKE: it goes to
 * BOOTING every time a tree is rebuilt -- the boot, the title swap, the
 * post-login gameframe, a display-mode remount -- and to READY when that
 * build finishes. The screen says which tree it is, so a title tree can be
 * BOOTING, and so can the gameframe that replaces it.
 *
 * The hold is the one bake with something worth keeping behind it: a logout
 * comes off a live world and goes straight to the login form, so its bake
 * declines to paint until the title tree can draw. Every bake that starts
 * clears it, and so does the bake that settles; a READY phase never holds,
 * whatever the flag says.
 *
 * The transitions are named for the events that cause them. None of them
 * checks where the phase came from: each sets what its event sets.
 *
 * This does not know what a tree, a session or a socket is, when a bake
 * starts or settles, or what a screen draws. The caller reports the events
 * and reads the answers.
 */

#include <stdbool.h>

/** The bake lifecycle: BOOTING until the root-interface build task (and its
 * dependent loads) complete through the per-frame task pump; the render path
 * draws a loading bar until READY. */
enum AppState
{
    APP_STATE_BOOTING = 0,
    APP_STATE_READY,
};

/**
 * Which screen the session is on, orthogonal to AppState.
 *
 * The values are the deob's own gameState numbers, so a state lifted from that
 * reference reads across without a translation table. Its 25 (map load), 40
 * (connection lost) and 45 (world hop) are deliberately not here: this client
 * already draws all three as overlays inside a live gameframe, which is what
 * they are.
 */
enum AppScreen
{
    /** Engine coming up; no title tree rooted yet. deob 0/5. */
    APP_SCREEN_BOOT = 0,
    /** Title tree rooted; RS_Title says which of its screens shows. deob 10. */
    APP_SCREEN_TITLE = 10,
    /** Login handshake in flight. The title tree stays up and keeps drawing --
     *  that is where "Connecting to server..." appears. deob 20. */
    APP_SCREEN_CONNECTING = 20,
    /** Gameframe rooted. deob 25/30. */
    APP_SCREEN_GAME = 30,
};

struct RS_ClientPhase
{
    /** enum AppState. */
    int app_state;
    /** enum AppScreen. Orthogonal to app_state: that says whether the current
     *  tree has finished baking, this says which tree it is. */
    int screen;
    /** This bake must not paint: the screen keeps the frame it already has.
     *  Raised by a logout, cleared by the next bake that starts and by the
     *  bake that settles. */
    int hold_last_frame;
};

/** BOOTING on the boot screen, nothing held: a client that has not started. */
void
RS_ClientPhase_Reset(struct RS_ClientPhase* phase);

/* --- transitions ---------------------------------------------------------- */

/** A tree bake started: BOOTING, and it may paint (only a logout says
 *  otherwise, after this). */
void
RS_ClientPhase_BakeStarted(struct RS_ClientPhase* phase);

/** The bake finished: READY, and nothing is left to hold. */
void
RS_ClientPhase_BakeSettled(struct RS_ClientPhase* phase);

/** The session ended on a logout: the bake that follows keeps the last frame
 *  on screen until it settles. */
void
RS_ClientPhase_LogoutHold(struct RS_ClientPhase* phase);

/** The warm-up gameframe bake that runs before the title screen: the BOOT
 *  screen. */
void
RS_ClientPhase_BootBake(struct RS_ClientPhase* phase);

/** The title tree is rooted, or the session is sent back to it (a refused
 *  sign-in or login, an expired session): the TITLE screen. */
void
RS_ClientPhase_ShowTitle(struct RS_ClientPhase* phase);

/** A login was submitted and the handshake is in flight: CONNECTING. */
void
RS_ClientPhase_LoginSubmitted(struct RS_ClientPhase* phase);

/** The gameframe is rooted (a login accepted, or a re-root): the GAME
 *  screen. */
void
RS_ClientPhase_GameRooted(struct RS_ClientPhase* phase);

/* --- readers -------------------------------------------------------------- */

/** The current tree has finished baking. */
bool
RS_ClientPhase_Ready(struct RS_ClientPhase const* phase);

/** A tree is baking. */
bool
RS_ClientPhase_Booting(struct RS_ClientPhase const* phase);

/** enum AppScreen. */
int
RS_ClientPhase_Screen(struct RS_ClientPhase const* phase);

/** The title tree is the screen: TITLE or CONNECTING. */
bool
RS_ClientPhase_OnTitle(struct RS_ClientPhase const* phase);

/** A bake is running AND it was told to keep the last frame. */
bool
RS_ClientPhase_HoldsLastFrame(struct RS_ClientPhase const* phase);

#endif /* SRC_GAME_RS_CLIENT_PHASE_H */
