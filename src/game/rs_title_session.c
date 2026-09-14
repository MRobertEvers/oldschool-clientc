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
    session->autologin_spent = true;
    return true;
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
}
