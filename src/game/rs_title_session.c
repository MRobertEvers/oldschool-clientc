#include "game/rs_title_session.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void
RS_TitleSession_Reset(struct RS_TitleSession* session)
{
    assert(session);
    memset(session, 0, sizeof(*session));
}

void
RS_TitleSession_SetCredentials(
    struct RS_TitleSession* session,
    char const* user,
    char const* password)
{
    assert(session);
    snprintf(session->user, sizeof(session->user), "%s", user ? user : "");
    snprintf(session->password, sizeof(session->password), "%s", password ? password : "");
}

void
RS_TitleSession_SetConnectTarget(struct RS_TitleSession* session, char const* target)
{
    assert(session);
    snprintf(session->connect_target, sizeof(session->connect_target), "%s", target ? target : "");
}

bool
RS_TitleSession_TakeHeadlessLogin(
    struct RS_TitleSession* session,
    bool link_available,
    enum RS_TitleScreenPhase phase)
{
    assert(session);
    if( !link_available || phase != RS_TITLE_PHASE_GAME )
        return false;
    if( session->autologin_spent || !RS_TitleSession_HasCredentials(session) )
        return false;
    session->autologin_spent = true;
    return true;
}

bool
RS_TitleSession_TakePrefill(
    struct RS_TitleSession* session,
    bool ready,
    enum RS_TitleScreenPhase phase)
{
    assert(session);
    /* Not before the title tree is up: there is nothing to type into, and a
     * prefill written into a form that has not been built is a prefill spent
     * on nothing -- and it is only spendable once. */
    if( !ready || phase != RS_TITLE_PHASE_TITLE )
        return false;
    if( session->autologin_spent || !RS_TitleSession_HasCredentials(session) )
        return false;
    /* A reload's name is not a prefill. It came out of the page's stored
     * entry, and typing it into a form the player never asked to see is the
     * whole thing the resume exists to stop; RS_TitleSession_TakeResume owns
     * this boot instead. */
    if( session->resume_pending )
        return false;
    session->autologin_spent = true;
    return true;
}

void
RS_TitleSession_ArmResume(struct RS_TitleSession* session)
{
    assert(session);
    session->resume_pending = true;
}

bool
RS_TitleSession_TakeResume(
    struct RS_TitleSession* session,
    bool ready,
    enum RS_TitleScreenPhase phase)
{
    assert(session);
    /* The same wait the prefill makes, for a different reason: the bar the
     * reconnect shows through is a widget of the title tree, so a reconnect
     * dialled before that tree exists runs behind a blank screen. */
    if( !ready || phase != RS_TITLE_PHASE_TITLE )
        return false;
    if( session->autologin_spent || !session->resume_pending )
        return false;
    /* The name travels in GAMERECONNECT's body exactly as it does in a login
     * -- the seed replaces the PASSWORD, not the identity -- so a token with
     * no name beside it names no save for the server to hand back. */
    if( !RS_TitleSession_HasCredentials(session) )
        return false;
    session->autologin_spent = true;
    /* Spent here, not on the answer. What is owed is ONE reconnect, and this
     * is it; leaving the flag up would hold the caller's loading bar at
     * "reconnecting" over the next bake it settles -- the gameframe this
     * handshake is about to open, and the title screen a later logout returns
     * to. `resume_dial` is what the dial itself reads. */
    session->resume_pending = false;
    session->resume_dial = true;
    return true;
}

void
RS_TitleSession_ResumeRefused(struct RS_TitleSession* session)
{
    assert(session);
    session->resume_pending = false;
    session->resume_dial = false;
    /* And the name goes with them. @see the header: what is left here is what
     * the form would be shown holding. */
    session->user[0] = '\0';
    session->password[0] = '\0';
}

bool
RS_TitleSession_Submit(struct RS_TitleSession* session, bool link_available, bool username_present)
{
    assert(session);
    if( !link_available || !username_present )
        return false;
    /* Arm rather than dial. The caller's next tick spends this, with a drawn
     * frame in between. */
    session->connect_pending = true;
    return true;
}

bool
RS_TitleSession_TakePendingConnect(struct RS_TitleSession* session)
{
    assert(session);
    if( !session->connect_pending )
        return false;
    session->connect_pending = false;
    return true;
}

bool
RS_TitleSession_LoginSucceeded(enum RS_TitleScreenPhase phase, enum RS_TitleLinkState link)
{
    return phase == RS_TITLE_PHASE_CONNECTING && link == RS_TITLE_LINK_IN_GAME;
}

bool
RS_TitleSession_LoginFailed(
    struct RS_TitleSession const* session,
    enum RS_TitleScreenPhase phase,
    enum RS_TitleLinkState link)
{
    assert(session);
    if( phase != RS_TITLE_PHASE_CONNECTING || link != RS_TITLE_LINK_DOWN )
        return false;
    /* A link that has not been dialled yet is down in exactly the way a
     * refused one is. Between the arming tick and the dialling tick the
     * screen already says "connecting", so without this the session reads its
     * own un-started handshake as a refusal. */
    return !session->connect_pending;
}

void
RS_TitleSession_Abandon(struct RS_TitleSession* session)
{
    assert(session);
    session->autologin_spent = true;
    session->connect_pending = false;
    /* A logout ends the resumable session too -- the page is told to forget it
     * at the same moment (app_session_resume.c). Left armed, the next title
     * tick would reconnect into the world the player just walked out of. */
    session->resume_pending = false;
    session->resume_dial = false;
}
