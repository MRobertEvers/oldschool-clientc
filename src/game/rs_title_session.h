#ifndef SRC_GAME_RS_TITLE_SESSION_H
#define SRC_GAME_RS_TITLE_SESSION_H

/*
 * Getting from the title screen to the world, once.
 *
 * Five rules, and every one of them fails as something other than what it is.
 *
 * ONCE. Credentials that came from the command line or the manifest prefill
 * the form and submit themselves -- and must never do it twice. A rejected
 * login that re-submits itself dials again, is rejected again, and dials
 * again; from the outside that is a client hammering a server it has been told
 * to go away from, and from the inside it is a title screen that flickers and
 * never lets anyone type.
 *
 * A FRAME IN BETWEEN. A submit is not a connect. The submit changes what is on
 * screen -- the message line, the withdrawn buttons -- and the connect then
 * spends the rest of the tick, and several after it, on a handshake and a
 * world's worth of assets. Done in the same tick, the pre-click picture stays
 * up throughout: the player clicks Login and watches nothing happen, and the
 * client reads as hung rather than busy. So the submit ARMS, and the next tick
 * dials.
 *
 * NOT YET IS NOT NO. A link that has never been dialled sits in exactly the
 * same state as one whose handshake was refused. Between the arming tick and
 * the dialling tick, a client that asks "is the link down?" gets yes -- and
 * shows the player a login failure on the way to a login that then succeeds.
 * The pending arm is what separates the two, which is why it is read here and
 * not only consumed.
 *
 * SPENT ON LEAVING. Logging out ends the session the automatic submit was for.
 * Leaving it armed would dial straight back into the world the player just
 * asked to leave.
 *
 * A RELOAD IS NOT A LOGIN. A browser tab that comes back is asking for the
 * session it was already in, not starting a new one, and the difference is
 * visible: no form is filled, no form is shown, and the loading bar carries on
 * into a reconnect. Treating it as a login instead is what produces the thing
 * nobody asked for -- a title screen with the player's own name and password
 * typed into it, submitting itself, in the middle of a fight.
 *
 * Nothing here opens a socket, reads a form or draws a screen. The caller says
 * where the session is and what the link is doing; this says what to do about
 * it.
 */

#include "game/rs_title.h"

#include <stdbool.h>

/** Where the session is, as far as this machine cares. */
enum RS_TitleScreenPhase
{
    /** Booting, or already playing through a screen this does not drive. */
    RS_TITLE_PHASE_OTHER,
    /** The login form is up. */
    RS_TITLE_PHASE_TITLE,
    /** A submit has been made and the result is not known yet. */
    RS_TITLE_PHASE_CONNECTING,
    /** In the world. A profile with no title screen boots straight here. */
    RS_TITLE_PHASE_GAME
};

/** What the link is doing. */
enum RS_TitleLinkState
{
    /** There is no link object at all -- an offline profile. */
    RS_TITLE_LINK_ABSENT,
    /** Never dialled, or dialled and refused. These are the same state. */
    RS_TITLE_LINK_DOWN,
    /** Dialled, handshake under way. */
    RS_TITLE_LINK_BUSY,
    /** Handshake complete. */
    RS_TITLE_LINK_IN_GAME
};

struct RS_TitleSession
{
    /**
     * Credentials to submit, from --user/--pass or the manifest's [net:boot].
     *
     * Kept rather than dialled with: the connect happens on submit, so these
     * prefill the form and drive the one automatic submit. Empty means an
     * interactive login.
     */
    char user[64];
    char password[64];
    /** [net:boot] address, kept for the same reason. */
    char connect_target[256];
    /** The one automatic submit has fired, or been written off. */
    bool autologin_spent;
    /**
     * This boot came back INTO a session rather than starting one: the page
     * handed it a resume token (--resume), and its first dial is a
     * GAMERECONNECT presenting that session's own key.
     *
     * A different thing from the prefill above, and the difference is what is
     * on the screen. A prefill is somebody's credentials typed for them: the
     * form fills in and submits itself, and the player watches it happen. A
     * resume is a page reload, and the player never asked to see a login
     * screen at all -- they were in the world a second ago. So the form is
     * neither filled nor shown; the loading bar the boot has just run stays up
     * with the reconnect's caption, and the player sees the refresh finish.
     *
     * Which is also why there is no password behind it. The page keeps the
     * token and the name and nothing else, so a session the server will not
     * hand back has nowhere to fall through to: it becomes an EMPTY form and a
     * login the player performs. @see RS_TitleSession_ResumeRefused.
     */
    bool resume_pending;
    /**
     * The armed dial is that reconnect.
     *
     * It says where the credentials come from. An ordinary submit reads the
     * form, because the form IS the state -- what the player typed is what
     * gets sent. A reconnect has no form to read: nothing was typed, and the
     * boxes are empty and hidden. It dials the session's own name instead.
     */
    bool resume_dial;
    /** A submitted login, waiting for the frame that shows it before it dials. */
    bool connect_pending;
    /**
     * The title screen opens the moment the warm gameframe bake settles.
     *
     * Set while that bake is in flight so the title replaces it before any
     * frame can render the gameframe it baked.
     */
    bool pending_after_boot;
    /**
     * Per-frame scratch the title host requests hand out, ONE SLOT PER FIELD.
     *
     * Frame-lifetime pointers, the same contract as the hovertext and
     * reboot-timer strings -- but unlike those there are two live at once, and
     * a single shared buffer makes the second compose overwrite the first
     * while the emit list still points at it. Both rows then draw the
     * password, which is exactly as bad as it sounds.
     */
    char field_line[RS_TITLE_FIELD_COUNT][RS_TITLE_FIELD_LEN + 64];
};

void
RS_TitleSession_Reset(struct RS_TitleSession* session);

/** Credentials to prefill and submit once. NULL or empty means interactive. */
void
RS_TitleSession_SetCredentials(
    struct RS_TitleSession* session,
    char const* user,
    char const* password);

void
RS_TitleSession_SetConnectTarget(struct RS_TitleSession* session, char const* target);

static inline bool
RS_TitleSession_HasCredentials(struct RS_TitleSession const* session)
{
    return session->user[0] != '\0';
}

/**
 * Dial now, with no form in the way?
 *
 * True once, for a profile whose revconfig declares no title screen: it boots
 * straight to the gameframe, so there is no screen to show progress on, and
 * the credentials still have to reach the server.
 */
bool
RS_TitleSession_TakeHeadlessLogin(
    struct RS_TitleSession* session,
    bool link_available,
    enum RS_TitleScreenPhase phase);

/**
 * Put the credentials on the form and ask it to submit?
 *
 * True once. The same latch as the headless login above, so a session can do
 * one or the other and never both.
 */
bool
RS_TitleSession_TakePrefill(
    struct RS_TitleSession* session,
    bool ready,
    enum RS_TitleScreenPhase phase);

/**
 * This boot carries a resume token the link has accepted.
 *
 * Called once, at init, and only after ToriRS_Network_ArmResume has taken the
 * token: a revision whose login has no seed reconnect refuses it, and a title
 * screen holding a "reconnecting" bar over a plain GAMELOGIN with no password
 * would be waiting for a handshake nothing can complete.
 */
void
RS_TitleSession_ArmResume(struct RS_TitleSession* session);

/**
 * Dial the reconnect now?
 *
 * True once, on the first tick with a built title tree -- the same latch the
 * prefill and the headless login share, so a session performs one automatic
 * login by exactly one route. The caller submits without touching the form.
 */
bool
RS_TitleSession_TakeResume(
    struct RS_TitleSession* session,
    bool ready,
    enum RS_TitleScreenPhase phase);

/**
 * The server did not hand that session back. Nothing is resumable any more.
 *
 * The credentials go with it, and that is the point rather than tidiness: the
 * name came out of the page's entry, and leaving it behind is what would put
 * it into the form the player is about to be shown -- which is the one thing
 * a reload must not do.
 */
void
RS_TitleSession_ResumeRefused(struct RS_TitleSession* session);

/**
 * A submit was made. Should the screen change to "connecting"?
 *
 * False when there is no link to dial or no name to dial with. An empty name
 * is left on the form rather than sent, because every server answers one with
 * a rejection the player then has to read as if it meant something.
 */
bool
RS_TitleSession_Submit(
    struct RS_TitleSession* session,
    bool link_available,
    bool username_present);

/** Is this the tick that dials? True once per submit. */
bool
RS_TitleSession_TakePendingConnect(struct RS_TitleSession* session);

/**
 * The handshake finished and the world is ready to open.
 *
 * Takes no session, deliberately. Success is a fact about the link alone --
 * there is no state in which a link reports being in the game and the session
 * should disbelieve it. Failure is not symmetric with it, and the parameter
 * list is where that shows.
 */
bool
RS_TitleSession_LoginSucceeded(enum RS_TitleScreenPhase phase, enum RS_TitleLinkState link);

/** The handshake was refused, and it really was a handshake. */
bool
RS_TitleSession_LoginFailed(
    struct RS_TitleSession const* session,
    enum RS_TitleScreenPhase phase,
    enum RS_TitleLinkState link);

/**
 * The player left. Spend the automatic submit and disarm any pending dial.
 *
 * Both, because either one left behind puts the player back in the world they
 * just walked out of.
 */
void
RS_TitleSession_Abandon(struct RS_TitleSession* session);

#endif
