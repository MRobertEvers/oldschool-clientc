/*
 * What a reloaded page logs back in as.
 *
 * The client's side of the page's createResumeSession (src/web/torirs_host.js),
 * and it is two events wide: a login the server accepted, and a logout. The
 * page owns what is stored and for how long; this owns WHEN, because only the
 * client knows which of the two just happened.
 *
 * There is no session to resume on the wire. A reload takes the wasm heap with
 * it, and the reconnect handshake presents the previous session's cipher seeds
 * (net/net.h) -- so what crosses here is the credentials the next boot logs in
 * with, exactly as the player would have retyped them.
 *
 * The credentials handed over are the ones the session was DIALLED with
 * (ToriRS_Network::username/password), which are already kept for the
 * in-process reconnect the link watch drives. A page reload is the same fact
 * one layer out: the same session, wanted back, by a client that no longer has
 * a socket to keep it on.
 *
 * Native builds have no page to reload into, so every body below is dead code
 * off the web lane -- but the RULE is not web-specific and is tested on the
 * native lane through app_session_resume_user(), which is why the calls are
 * made unconditionally at both sites rather than compiled out there.
 */

#include "app/app_internal.h"

#include <assert.h>
#include <string.h>

#if defined(TORIRS_PLATFORM_WEB)
#include <emscripten.h>
#else
/* So the file still compiles natively for a syntax check, exactly as
 * ui/torirs_chrome_exec_web.c does. Every EM_JS body below is then dead code
 * that never runs. */
#define EM_JS(ret, name, args, ...) static ret name args { return (ret)0; }
#endif

/* ---- the page's side of the wall ----------------------------------------- */

/*
 * A page that predates these hooks is not an error.
 *
 * The client and the page are versioned separately -- a player with a cached
 * index.html can be running last week's host against this week's wasm -- and
 * the honest answer to "this page cannot remember a session" is a client that
 * behaves as it did before, not a failed login.
 */
// clang-format off
EM_JS(void, web_session_remember, (char const* user, char const* password), {
    if( typeof window.torirsSessionRemember !== 'function' )
        return;
    try
    {
        window.torirsSessionRemember(UTF8ToString(user), UTF8ToString(password));
    }
    catch( e )
    {
        console.warn('[torirs] session remember failed', e);
    }
});

EM_JS(void, web_session_forget, (void), {
    if( typeof window.torirsSessionForget !== 'function' )
        return;
    try
    {
        window.torirsSessionForget();
    }
    catch( e )
    {
        console.warn('[torirs] session forget failed', e);
    }
});
// clang-format on

/*
 * Who the page has been told to hold, or "" for nobody.
 *
 * File scope because the thing it mirrors is: one page, one tab, one stored
 * session -- and struct App is not the place for it (`make check-app-boundary`
 * counts its fields, and this is not one of them).
 *
 * The PASSWORD is deliberately not here. It crosses to the page and is not
 * kept a second time in this process; the username alone answers the only
 * question anything on this side asks, which is whether a session is being
 * held and whose.
 */
static char g_resume_user[64];

void
app_session_resume_remember(
    char const* user,
    char const* password)
{
    assert(user);
    assert(password);
    /* Not a live check: RS_TitleSession_Submit refuses an empty name, so a
     * session that reached the game has one. A caller that got here without
     * one is asking the page to hold a login nothing can perform. */
    assert(user[0] != '\0');

    strncpy(g_resume_user, user, sizeof(g_resume_user) - 1);
    g_resume_user[sizeof(g_resume_user) - 1] = '\0';
    web_session_remember(user, password);
}

void
app_session_resume_forget(void)
{
    g_resume_user[0] = '\0';
    web_session_forget();
}

char const*
app_session_resume_user(void)
{
    return g_resume_user;
}
