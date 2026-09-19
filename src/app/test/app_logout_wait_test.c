/*
 * A logout is a REQUEST. The server is what ends the session.
 *
 * The click used to end it locally, one tick after the button's IF_BUTTON went
 * out, and that is wrong in the one moment the two can disagree: a server that
 * refuses the logout -- ten seconds of combat delay, a script that says "you
 * can't log out here" -- had already lost the client, which was sitting on the
 * title screen with its socket closed. The reference arms `logoutTimer = 250`
 * and keeps playing until the server answers (Client.ts:11212).
 *
 * The answer arrives two ways, and both are tested here because only one of
 * them is a packet: the LOGOUT packet, and the socket the server closes
 * instead of sending one -- which is what every server in this tree does,
 * `p_logout` being a session kill. That second one is why the connection-lost
 * path has to ask whether a logout was pending: reading it as a lost link
 * instead would REDIAL, and log the player back into the world they just asked
 * to leave.
 *
 * The fixture is a zeroed App with no title screen declared, so App_Logout
 * takes its "nowhere to return to" branch and the session teardown is what is
 * observed rather than the login form. What each test watches is the teardown
 * itself: the DISCONNECT in the outbound ring, the net state, and the
 * reconnect watch.
 */

#include "app.h"
#include "app/app_internal.h"
#include "net/rev/gameproto_revisions.h"
#include "test_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_failures;

/*
 * Enough App for App_Logout to run, and not one field more.
 *
 * `features` is asserted by the attack-option reset; `esync` has to be Init'd
 * because a zeroed one carries local pid 0, which the clear would then try to
 * remove from a world that does not exist here.
 */
static struct App*
make_app(struct ToriRS_Network* net)
{
    struct App* app = calloc(1, sizeof(*app));

    TEST_ASSERT(app != NULL, "allocated the app fixture");
    app->features = &app->features_storage;
    RS_EntitySync_Init(&app->esync);
    app->net = net;
    app->net_enabled = 1;
    return app;
}

/* A network that is in the game stream and has never been connected to
 * anything. Only the fields the logout path reads are set: the out ring is a
 * plain array, and login_free needs a rev table to ask about. */
static struct ToriRS_Network*
make_net_in_game(void)
{
    struct ToriRS_Network* net = calloc(1, sizeof(*net));

    TEST_ASSERT(net != NULL, "allocated the net fixture");
    net->rev = GameProtoRev_LC254();
    net->state = TORIRS_NET_GAME;
    return net;
}

/* True when a DISCONNECT is sitting in the outbound ring -- the one byte of
 * evidence that the session was actually ended rather than merely asked to. */
static bool
disconnect_queued(struct ToriRS_Network* net)
{
    struct ToriRS_CmdHeader header;
    uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];

    while( CmdRing_Pop(&net->out, &header, payload) )
    {
        if( header.type == TORIRS_NET_OUT_DISCONNECT )
            return true;
    }
    return false;
}

static void
test_the_click_does_not_end_the_session(void)
{
    struct ToriRS_Network* net = make_net_in_game();
    struct App* app = make_app(net);

    printf("TEST: the click asks, and the session stays up\n");

    app->logout_requested = 1;
    TEST_ASSERT(!app_logout_tick(app), "the tick that drained the request ended nothing");
    TEST_ASSERT(
        app->logout_wait_cycles == APP_LOGOUT_WAIT_CYCLES,
        "the wait for the server's answer is armed, at the reference's 250 cycles");
    TEST_ASSERT(!app->logout_requested, "the request was drained, not left to re-arm");
    TEST_ASSERT(net->state == TORIRS_NET_GAME, "still in the game stream");
    TEST_ASSERT(!disconnect_queued(net), "nothing closed the socket on the player's behalf");

    free(app);
    free(net);
}

static void
test_a_wait_that_nothing_answers_ends_it_anyway(void)
{
    struct ToriRS_Network* net = make_net_in_game();
    struct App* app = make_app(net);
    int ended_on_cycle = -1;

    printf("TEST: a server that never answers costs the wait, not the logout\n");

    app->logout_requested = 1;
    app_logout_tick(app);
    for( int cycle = 1; cycle <= APP_LOGOUT_WAIT_CYCLES && ended_on_cycle < 0; cycle++ )
    {
        if( app_logout_tick(app) )
            ended_on_cycle = cycle;
    }

    TEST_ASSERT(
        ended_on_cycle == APP_LOGOUT_WAIT_CYCLES,
        "the session ended on the last cycle of the wait and not before");
    TEST_ASSERT(app->logout_wait_cycles == 0, "the wait is disarmed once it is spent");
    TEST_ASSERT(net->state == TORIRS_NET_DISCONNECTED, "the session really did end");
    TEST_ASSERT(disconnect_queued(net), "the DISCONNECT was queued behind the request");

    free(app);
    free(net);
}

/*
 * The one that the old code could not have got right: the server answers the
 * button by killing the session, and the socket simply goes away.
 */
static void
test_a_closed_socket_is_the_answer(void)
{
    struct ToriRS_Network* net = make_net_in_game();
    struct App* app = make_app(net);

    printf("TEST: a socket that closes during the wait IS the logout\n");

    app->logout_requested = 1;
    app_logout_tick(app);
    app_net_lost(app, "socket closed");

    TEST_ASSERT(app->logout_wait_cycles == 0, "the answer ended the wait");
    TEST_ASSERT(
        !NetLinkWatch_Lost(&app->net_link),
        "the link was not left lost, so nothing redials the world the player left");
    TEST_ASSERT(net->state == TORIRS_NET_DISCONNECTED, "the session ended");

    free(app);
    free(net);
}

/* The control for the one above: the SAME closed socket, with no logout
 * pending, is still a connection to get back. */
static void
test_a_closed_socket_with_no_logout_pending_still_reconnects(void)
{
    struct ToriRS_Network* net = make_net_in_game();
    struct App* app = make_app(net);

    printf("TEST: the same closed socket, unasked for, is still a lost connection\n");

    app_net_lost(app, "socket closed");

    TEST_ASSERT(NetLinkWatch_Lost(&app->net_link), "the link is lost and being re-established");

    free(app);
    free(net);
}

/*
 * A logout also ends the session a RELOADED PAGE would come back into.
 *
 * The browser client keeps the key a successful handshake authenticated on
 * so that a refresh reconnects into it (app/app_session_resume.c). A
 * logout is the player saying that session is over, and a tab that then
 * reloads must open on an empty form -- being put back into the world you
 * just walked out of is the one outcome nobody asks for.
 *
 * Observed through app_session_resume_user() because the fixture has no page:
 * the native lane keeps the record and has nothing to hand it to, which is
 * exactly what makes the RULE testable here rather than only in a browser.
 */
static void
test_a_logout_forgets_the_resumable_session(void)
{
    struct ToriRS_Network* net = make_net_in_game();
    struct App* app = make_app(net);

    printf("TEST: a logout drops the session a reload would resume\n");

    app_session_resume_remember("zezima", "1,2,3,4,5");
    TEST_ASSERT(
        strcmp(app_session_resume_user(), "zezima") == 0, "the session was there to lose");

    app->logout_requested = 1;
    app_logout_tick(app);
    app_net_lost(app, "socket closed"); /* the server's answer: see above */

    TEST_ASSERT(
        app_session_resume_user()[0] == '\0',
        "a reload would have logged back into the world the player left");

    free(app);
    free(net);
}

/*
 * The control for the one above, and the reason forgetting is a LOGOUT's job
 * and not a closed socket's: a connection that is merely lost is the case the
 * whole resume exists for. A phone that killed the page, a proxy that dropped
 * the WebSocket, F5 -- that key is the way back in.
 */
static void
test_a_lost_connection_keeps_the_resumable_session(void)
{
    struct ToriRS_Network* net = make_net_in_game();
    struct App* app = make_app(net);

    printf("TEST: a lost connection keeps it -- that is what it is for\n");

    app_session_resume_remember("zezima", "1,2,3,4,5");
    app_net_lost(app, "socket closed");

    TEST_ASSERT(
        strcmp(app_session_resume_user(), "zezima") == 0,
        "a dropped socket threw away the session key that gets the player back");

    app_session_resume_forget();
    free(app);
    free(net);
}

/* Nothing to wait for: an offline profile, or a session already gone. The
 * request is the ending, and making the player watch five seconds of a client
 * that looks like it ignored the button is not a fidelity improvement. */
static void
test_no_session_means_no_wait(void)
{
    struct App* app = make_app(NULL);

    printf("TEST: with no session to answer, the click ends it at once\n");

    app->logout_requested = 1;
    TEST_ASSERT(app_logout_tick(app), "the session ended on the tick that drained the request");
    TEST_ASSERT(app->logout_wait_cycles == 0, "nothing was armed to wait for");

    free(app);
}

int
main(void)
{
    printf("=== logout waits for the server ===\n");

    test_the_click_does_not_end_the_session();
    test_a_wait_that_nothing_answers_ends_it_anyway();
    test_a_closed_socket_is_the_answer();
    test_a_closed_socket_with_no_logout_pending_still_reconnects();
    test_a_logout_forgets_the_resumable_session();
    test_a_lost_connection_keeps_the_resumable_session();
    test_no_session_means_no_wait();

    if( g_failures )
    {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
