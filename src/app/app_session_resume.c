/*
 * What a reloaded page logs back in as.
 *
 * The client's side of the page's createResumeSession (src/web/torirs_host.js),
 * and it is two events wide: a login the server accepted, and a logout. The
 * page owns what is stored and for how long; this owns WHEN, because only the
 * client knows which of the two just happened.
 *
 * What crosses is what the next boot needs to ask for the SAME session back:
 * the resume token -- the cipher seed the session authenticated with, which
 * GAMERECONNECT presents in place of a password, and the slot RECONNECT_OK
 * does not restate -- plus the credentials, for when the server says that
 * session is gone and the boot has to log in afresh instead. A reload that
 * sent a plain GAMELOGIN was indistinguishable, to the server, from a second
 * player arriving under the same name.
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
#include <stdio.h>
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
EM_JS(void, web_session_remember, (char const* user, char const* password, char const* resume), {
    if( typeof window.torirsSessionRemember !== 'function' )
        return;
    try
    {
        window.torirsSessionRemember(
            UTF8ToString(user), UTF8ToString(password), UTF8ToString(resume));
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
    char const* password,
    char const* resume_token)
{
    assert(user);
    assert(password);
    assert(resume_token);
    /* Not a live check: RS_TitleSession_Submit refuses an empty name, so a
     * session that reached the game has one. A caller that got here without
     * one is asking the page to hold a login nothing can perform. */
    assert(user[0] != '\0');

    strncpy(g_resume_user, user, sizeof(g_resume_user) - 1);
    g_resume_user[sizeof(g_resume_user) - 1] = '\0';
    web_session_remember(user, password, resume_token);
}

void
app_session_resume_token(
    struct ToriRS_Network const* net,
    char* out,
    int out_size)
{
    assert(net);
    assert(out);
    assert(out_size > 0);
    out[0] = '\0';
    if( !net->has_prev_seed || net->local_index < 0 )
        return;
    snprintf(out, (size_t)out_size, "%d,%d,%d,%d,%d", (int)net->prev_seed[0],
             (int)net->prev_seed[1], (int)net->prev_seed[2], (int)net->prev_seed[3],
             net->local_index);
}

void
app_session_resume_remember_net(struct ToriRS_Network const* net)
{
    char token[80];

    assert(net);
    app_session_resume_token(net, token, (int)sizeof(token));
    app_session_resume_remember(net->username, net->password, token);
}

int
app_session_resume_parse(
    char const* token,
    int32_t out_seed[4],
    int* out_local_index)
{
    int seed[4];
    int local_index;
    int consumed = 0;

    assert(token);
    assert(out_seed);
    assert(out_local_index);
    if( sscanf(token, "%d,%d,%d,%d,%d%n", &seed[0], &seed[1], &seed[2], &seed[3], &local_index,
               &consumed) != 5 ||
        token[consumed] != '\0' || local_index < 0 )
        return 0;
    for( int i = 0; i < 4; i++ )
        out_seed[i] = (int32_t)seed[i];
    *out_local_index = local_index;
    return 1;
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
