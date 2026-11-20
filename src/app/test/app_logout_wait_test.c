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
#include "mem/torirs_mem.h"
#include "net/rev/gameproto_revisions.h"
#include "test_harness.h"

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int g_failures;

/*
 * Enough App for App_Logout to run, and not one field more.
 *
 * The attack-option reset asserts `features` holds an era, and a name is what
 * says so; the rest stays zeroed, the table's default models. `esync` has to
 * be Init'd because a zeroed one carries local pid 0, which the clear would
 * then try to remove from a world that does not exist here. The logout
 * cancels every pending parallel task, so the pending index (and the assets
 * table it is made after) is made too.
 */
static struct App*
make_app(struct ToriRS_Network* net)
{
    struct App* app = calloc(1, sizeof(*app));

    TEST_ASSERT(app != NULL, "allocated the app fixture");
    app->features.name = "test";
    RS_EntitySync_Init(&app->esync);
    App_AssetsInit(app);
    App_PendingInit(app);
    app->net = net;
    if( net )
        ToriRS_Network_Enable(net);
    return app;
}

static void
free_app(struct App* app)
{
    App_PendingFree(app);
    App_AssetsFree(app);
    free(app);
}

/* A network that is in the game stream and has never been connected to
 * anything. Only the fields the logout path reads are set: the out ring is a
 * plain array, and login_free needs a rev table to ask about. */
static struct ToriRS_Network*
make_net_in_game(void)
{
    struct ToriRS_Network* net = calloc(1, sizeof(*net));

    TEST_ASSERT(net != NULL, "allocated the net fixture");
    net->rev = GameProtoRev_RS254();
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

    SessionEnd_RequestLogout(&app->session_end);
    TEST_ASSERT(!app_logout_tick(app), "the tick that drained the request ended nothing");
    TEST_ASSERT(
        app->session_end.logout_wait_cycles == SESSION_END_LOGOUT_WAIT_CYCLES,
        "the wait for the server's answer is armed, at the reference's 250 cycles");
    TEST_ASSERT(!app->session_end.logout_requested, "the request was drained, not left to re-arm");
    TEST_ASSERT(net->state == TORIRS_NET_GAME, "still in the game stream");
    TEST_ASSERT(!disconnect_queued(net), "nothing closed the socket on the player's behalf");

    free_app(app);
    free(net);
}

/*
 * A logout the server REFUSES -- the 16-tick combat rule -- is answered by
 * nothing at all: no LOGOUT, no closed socket, only a chat line. The player is
 * still in the world and still in the fight, so the client must be too, for as
 * long as it takes. This used to end the session itself when 250 cycles ran
 * out (CONTENT_BUGS ENG-29). The rev-239 client never counts its logoutTimer
 * down: the only things that end the wait are the server's answer and the
 * session reset (app/app_net.c, app_logout_tick).
 */
static void
test_a_refused_logout_keeps_the_session(void)
{
    struct ToriRS_Network* net = make_net_in_game();
    struct App* app = make_app(net);
    int ended_on_cycle = -1;

    printf("TEST: a logout nothing answers leaves the player in the world\n");

    SessionEnd_RequestLogout(&app->session_end);
    app_logout_tick(app);
    for( int cycle = 1; cycle <= 4 * SESSION_END_LOGOUT_WAIT_CYCLES && ended_on_cycle < 0; cycle++ )
    {
        if( app_logout_tick(app) )
            ended_on_cycle = cycle;
        /* A second click half way through: the one a player makes once the
         * combat delay has run out. It goes to the server; it ends nothing
         * here either. */
        if( cycle == 2 * SESSION_END_LOGOUT_WAIT_CYCLES )
            SessionEnd_RequestLogout(&app->session_end);
    }

    TEST_ASSERT(ended_on_cycle < 0, "nothing but the server ended the session");
    TEST_ASSERT(
        app->session_end.logout_wait_cycles == SESSION_END_LOGOUT_WAIT_CYCLES,
        "the wait is still armed, so the server's answer is still read as a logout");
    TEST_ASSERT(net->state == TORIRS_NET_GAME, "still in the game stream");
    TEST_ASSERT(!disconnect_queued(net), "nothing closed the socket on the player's behalf");

    /* ...and the answer, whenever it comes, is still the logout. */
    app_net_lost(app, "socket closed");
    TEST_ASSERT(app->session_end.logout_wait_cycles == 0, "the answer ended the wait");
    TEST_ASSERT(
        !NetLinkWatch_Lost(&app->net_link),
        "the late answer was not read as a lost connection to redial");
    TEST_ASSERT(net->state == TORIRS_NET_DISCONNECTED, "the session ended");

    free_app(app);
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

    SessionEnd_RequestLogout(&app->session_end);
    app_logout_tick(app);
    app_net_lost(app, "socket closed");

    TEST_ASSERT(app->session_end.logout_wait_cycles == 0, "the answer ended the wait");
    TEST_ASSERT(
        !NetLinkWatch_Lost(&app->net_link),
        "the link was not left lost, so nothing redials the world the player left");
    TEST_ASSERT(net->state == TORIRS_NET_DISCONNECTED, "the session ended");

    free_app(app);
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

    free_app(app);
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

    SessionEnd_RequestLogout(&app->session_end);
    app_logout_tick(app);
    app_net_lost(app, "socket closed"); /* the server's answer: see above */

    TEST_ASSERT(
        app_session_resume_user()[0] == '\0',
        "a reload would have logged back into the world the player left");

    free_app(app);
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
    free_app(app);
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

    SessionEnd_RequestLogout(&app->session_end);
    TEST_ASSERT(app_logout_tick(app), "the session ended on the tick that drained the request");
    TEST_ASSERT(app->session_end.logout_wait_cycles == 0, "nothing was armed to wait for");

    free_app(app);
}

/*
 * A client too busy to read its packets is not a server that went quiet.
 *
 * The 15 s silence bound (NET_LINK_TIMEOUT_MS) is about the SERVER: it is
 * measured from the last packet popped, and a client that pops nothing --
 * booting the gameframe after IF_OPENTOP, or a REBUILD holding the packet
 * queue on its reads -- while the server's packets keep arriving used to read
 * its own backlog as silence: with every cache read 400 ms late
 * (TORIRS_IO_DEFER_MS) the gameframe bake took longer than 15 s and the
 * session was dropped and refused. The reference counts its silence in
 * gameState 30's cycles and resets it on every packet it has read off the
 * socket (deob client.java, field709.field1553 > 750); a whole packet that is
 * waiting to be popped has been read. The control: the same watch with the
 * queue empty still declares the link lost 15 s after that packet.
 */
static void
test_a_backlog_is_not_silence(void)
{
    struct ToriRS_Network* net = make_net_in_game();
    struct App* app = make_app(net);
    struct RevPacketItem* item = ToriMem_Calloc(ToriMem_Net(), 1, sizeof(*item));
    uint64_t const heard = 1000;
    uint64_t const busy_until = heard + NET_LINK_TIMEOUT_MS + 5000;

    printf("TEST: a backlog the client has not popped is not a silent server\n");
    assert(item);

    NetLinkWatch_NotePacket(&app->net_link, heard);
    net->packets_head = item;
    net->packets_tail = item;
    app_net_link_watch(app, busy_until);
    TEST_ASSERT(
        !NetLinkWatch_Lost(&app->net_link),
        "a packet waited 20 s to be popped and the watch called the server silent");

    if( net->packets_head == item )
    {
        net->packets_head = NULL;
        net->packets_tail = NULL;
        ToriMem_Free(ToriMem_Net(), item);
    }
    if( !NetLinkWatch_Lost(&app->net_link) )
    {
        app_net_link_watch(app, busy_until + NET_LINK_TIMEOUT_MS - 1);
        TEST_ASSERT(!NetLinkWatch_Lost(&app->net_link), "the silence is counted from the waiting packet");
        app_net_link_watch(app, busy_until + NET_LINK_TIMEOUT_MS);
        TEST_ASSERT(
            NetLinkWatch_Lost(&app->net_link), "15 s with nothing waiting is still a lost link");
    }

    free_app(app);
    free(net);
}

/*
 * The packet pump's settle of the parallel queue is bounded.
 *
 * While the main queue's head is parked on the parallel queue's work, the
 * packet pump (app_pump_net_packets) steps the parallel queue and tries the
 * head again for as long as a pass ran anything. A parallel task that never
 * converges -- it yields without ever parking -- kept that loop going for
 * ever, inside one call, where the parallel pump's own runaway backstop
 * (app_pump_backstop, APP_ASYNC_STEP_LIMIT passes) never sees it: the client
 * hung instead of aborting. "Tasks never hang - that is just a game crash."
 */
struct Spinner
{
    struct ToriRS_Task task;
    struct pt pt;
};

static int
Spinner_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct Spinner* self = (struct Spinner*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    for( ;; )
        PT_YIELD(&self->pt);
    PT_END(&self->pt);
}

static void
spinner_free(struct ToriRS_Task* task)
{
    free(task);
}

static struct ToriRS_TaskVTable k_spinner_vtable = { .run = Spinner_Run, .free = spinner_free };

struct HeldHead
{
    struct ToriRS_Task task;
    struct pt pt;
    struct ToriRS_Trigger* trigger;
};

static int
HeldHead_Run(struct ToriRS_Task* base, struct ToriRS_IOBatch* io)
{
    struct HeldHead* self = (struct HeldHead*)base;
    (void)io;
    PT_BEGIN(&self->pt);
    TASK_AWAIT_TRIGGER(&self->task, &self->pt, TORIRS_TASK_WAIT_TRIGGER, self->trigger);
    PT_END(&self->pt);
}

static struct ToriRS_TaskVTable k_held_head_vtable = { .run = HeldHead_Run, .free = spinner_free };

static void
pump_over_a_spinner(void)
{
    static struct ToriRS_Trigger gate;
    static struct ToriRS_TriggerOwner outside;
    struct App* app = make_app(NULL);
    struct PlatformX_IO* px = PlatformX_IO_New();
    struct HeldHead* head = calloc(1, sizeof(*head));
    struct Spinner* spinner = calloc(1, sizeof(*spinner));

    assert(head);
    assert(spinner);
    app->phase.app_state = APP_STATE_READY;
    app->packet_runner.queue = ToriRS_TaskQueue_New();
    app->packet_runner.io = ToriRS_IOBatch_New();
    app->packet_runner.px = px;
    app->packet_runner.role = TASK_RUNNER_ROLE_MAIN;
    app->frame_runner.queue = ToriRS_TaskQueue_New();
    app->frame_runner.io = ToriRS_IOBatch_New();
    app->frame_runner.px = px;
    app->frame_runner.parallel = 1;
    app->frame_runner.role = TASK_RUNNER_ROLE_PARALLEL;
    ToriRS_Trigger_Init(&gate, "what the head waits on");
    ToriRS_TriggerOwner_Init(&outside, TORIRS_OWNER_EXTERNAL, "the outside");
    ToriRS_Trigger_Prime(&gate, &outside);
    head->task.vtable = &k_held_head_vtable;
    strcpy(head->task.name, "HeldHead");
    head->trigger = &gate;
    PT_INIT(&head->pt);
    TaskRunner_AddRenderBlockingSerialTask(&app->packet_runner, &head->task);
    spinner->task.vtable = &k_spinner_vtable;
    strcpy(spinner->task.name, "Spinner");
    PT_INIT(&spinner->pt);
    ToriRS_TaskQueue_Add(app->frame_runner.queue, &spinner->task);
    /* Killed by the alarm if it hangs: that is the failure. */
    alarm(20);
    (void)app_pump_net_packets(app);
    _exit(0);
}

static void
test_a_parallel_task_that_never_converges_aborts_the_packet_pump(void)
{
    pid_t pid;
    int status = 0;

    printf("TEST: a parallel task that never converges under a parked packet aborts, not hangs\n");
    fflush(stdout);
    fflush(stderr);
    pid = fork();
    assert(pid >= 0);
    if( pid == 0 )
        pump_over_a_spinner();
    (void)waitpid(pid, &status, 0);
    if( WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM )
        printf("  the packet pump was still spinning 20 s later\n");
    TEST_ASSERT(
        WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
        "the packet pump's settle of the parallel queue hit a runaway backstop");
}

int
main(void)
{
    printf("=== logout waits for the server ===\n");

    test_the_click_does_not_end_the_session();
    test_a_refused_logout_keeps_the_session();
    test_a_closed_socket_is_the_answer();
    test_a_closed_socket_with_no_logout_pending_still_reconnects();
    test_a_logout_forgets_the_resumable_session();
    test_a_lost_connection_keeps_the_resumable_session();
    test_no_session_means_no_wait();
    test_a_backlog_is_not_silence();
    test_a_parallel_task_that_never_converges_aborts_the_packet_pump();

    if( g_failures )
    {
        printf("FAILED: %d check(s)\n", g_failures);
        return 1;
    }
    printf("PASS\n");
    return 0;
}
