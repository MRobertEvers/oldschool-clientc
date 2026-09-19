/*
 * When a session is dead, when to dial again, and when to stop.
 *
 * Nothing about this is observable after the fact, which is the whole reason
 * it is worth a test. A session that ends and comes back looks -- from a log,
 * from a screenshot, from a player's chair -- exactly like a session that was
 * never lost. A session that ends and does NOT come back looks like a slow
 * server. And a session that is dropped when it was perfectly healthy looks
 * like a bad connection, which is what this codebase already once shipped:
 * at the old stall threshold a Windows XP client working flat out concluded
 * twelve times in a thousand frames that it had not been running, and dropped
 * a good session each time.
 *
 * So the cases here are the three detectors on their own, the boundary of
 * each, the backoff, the give-up, and the one thing that must NOT happen --
 * a client that has never heard from a server deciding it has lost one.
 *
 *   make -C src test-net-link-watch
 */
#include "net/net_link_watch.h"

#include <stdio.h>
#include <string.h>

static int g_failures;

#define CHECK(condition, ...)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if( !(condition) )                                                                         \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

static char const*
action_name(enum NetLinkAction action)
{
    switch( action )
    {
    case NET_LINK_IDLE:
        return "IDLE";
    case NET_LINK_LOST:
        return "LOST";
    case NET_LINK_REESTABLISHED:
        return "REESTABLISHED";
    case NET_LINK_RECONNECT:
        return "RECONNECT";
    case NET_LINK_GAVE_UP:
        return "GAVE_UP";
    }
    return "?";
}

/* A healthy in-game frame at `now_ms`, with nothing unusual seen. */
static struct NetLinkSighting
healthy(uint64_t now_ms)
{
    struct NetLinkSighting seen;

    memset(&seen, 0, sizeof(seen));
    seen.now_ms = now_ms;
    seen.frame_gap_ms = 16;
    seen.in_game = true;
    return seen;
}

/* A watch that has been up and talking since t=1000. */
static struct NetLinkWatch
live_session(void)
{
    struct NetLinkWatch watch;

    NetLinkWatch_Reset(&watch);
    NetLinkWatch_NotePacket(&watch, 1000);
    return watch;
}

static enum NetLinkAction
step(struct NetLinkWatch* watch, struct NetLinkSighting const* seen)
{
    char const* why = NULL;
    return NetLinkWatch_Step(watch, seen, &why);
}

/* ------------------------------------------------------------------ */

static void
test_a_healthy_link_is_left_alone(void)
{
    struct NetLinkWatch watch = live_session();
    uint64_t now;

    printf("TEST: a healthy link is left alone\n");

    /* A quarter of an hour of ordinary frames, each with a packet in it. */
    for( now = 1000; now < 1000 * 900; now += 16 )
    {
        struct NetLinkSighting seen = healthy(now);
        enum NetLinkAction action = step(&watch, &seen);

        if( action != NET_LINK_IDLE )
        {
            CHECK(0, "a healthy link produced %s at t=%llu",
                action_name(action), (unsigned long long)now);
            break;
        }
        NetLinkWatch_NotePacket(&watch, now);
    }
    CHECK(!NetLinkWatch_Lost(&watch), "a healthy link ended up lost");
}

static void
test_a_client_that_was_never_connected_loses_nothing(void)
{
    struct NetLinkWatch watch;
    struct NetLinkSighting seen;

    printf("TEST: a client that has never heard a packet has nothing to lose\n");

    /*
     * Every detector is gated on having heard from the server at least once.
     * A boot frame can legitimately run for a very long time -- a cold cache,
     * a browser IO round trip -- and there is no session to lose yet. Without
     * the gate, a slow start is a lost connection before there was one, and
     * the client redials a server it never reached.
     */
    NetLinkWatch_Reset(&watch);

    seen = healthy(100000);
    seen.frame_gap_ms = NET_LINK_STALL_MS * 4;
    seen.in_game = false;
    CHECK(step(&watch, &seen) == NET_LINK_IDLE, "a long boot frame lost a session");

    /* The socket also reads as closed before it was ever opened -- that is
     * simply the initial state of a transport. */
    seen = healthy(100000);
    seen.in_game = false;
    seen.socket_closed = true;
    CHECK(step(&watch, &seen) == NET_LINK_IDLE, "an unopened socket read as a closed one");

    /* And silence: there is no last packet to measure from. */
    seen = healthy(100000);
    CHECK(step(&watch, &seen) == NET_LINK_IDLE, "silence before the first packet lost a session");

    CHECK(!NetLinkWatch_Lost(&watch), "nothing was connected, and something was lost");
}

static void
test_the_session_origin_is_the_first_packet(void)
{
    struct NetLinkWatch watch;

    printf("TEST: the session's origin is its first packet, not its latest\n");

    /*
     * `first_recv_ms` is when THIS session started talking, and it is not
     * advanced by later packets. Nothing in the policy reads it -- the harness
     * drop hook does, measuring "sever the link this long into the session" --
     * and a stamp that moved with every packet would make that duration
     * unreachable, because the origin would always be now.
     */
    NetLinkWatch_Reset(&watch);
    CHECK(watch.first_recv_ms == 0, "a fresh watch claims a session origin");

    NetLinkWatch_NotePacket(&watch, 1000);
    NetLinkWatch_NotePacket(&watch, 1200);
    NetLinkWatch_NotePacket(&watch, 9999);
    CHECK(watch.first_recv_ms == 1000, "the origin moved to %llu",
        (unsigned long long)watch.first_recv_ms);
    CHECK(watch.last_recv_ms == 9999, "the latest packet was not recorded");

    /* A new session starts a new origin. */
    NetLinkWatch_Reset(&watch);
    NetLinkWatch_NotePacket(&watch, 50000);
    CHECK(watch.first_recv_ms == 50000, "a reset session kept the old origin");
}

static void
test_the_server_going_quiet(void)
{
    struct NetLinkWatch watch = live_session();
    struct NetLinkSighting seen;
    char const* why = NULL;

    printf("TEST: the server stops speaking\n");

    /* One millisecond inside the bound is still a live session. */
    seen = healthy(1000 + NET_LINK_TIMEOUT_MS - 1);
    CHECK(step(&watch, &seen) == NET_LINK_IDLE, "dropped one ms early");

    /* On it, it is gone. */
    seen = healthy(1000 + NET_LINK_TIMEOUT_MS);
    CHECK(NetLinkWatch_Step(&watch, &seen, &why) == NET_LINK_LOST, "did not drop on the bound");
    CHECK(why && strstr(why, "packets"), "the reason does not name the silence: %s", why ? why : "");
    CHECK(NetLinkWatch_Lost(&watch), "the watch does not report the session as lost");
}

static void
test_a_clock_that_went_backwards_is_not_silence(void)
{
    struct NetLinkWatch watch = live_session();
    struct NetLinkSighting seen;

    printf("TEST: a frame stamped before the last packet is not a silent server\n");

    /*
     * now_ms and last_recv_ms are unsigned. A frame whose clock reads EARLIER
     * than the last packet subtracts to something near 2^64, which is over
     * every threshold there is, so the naive difference declares a session
     * that has just been spoken to dead on the spot.
     *
     * That is not a hypothetical: the harness drove its post-loop hover frames
     * from a clock that restarted at 20 ms, and every screenshot taken with
     * TORIRS_SIM_HOVER came back showing "Connection lost" over an empty
     * world. The frames were fixed to continue the loop's clock, and this is
     * the other half -- the detector cannot measure silence with a clock that
     * went backwards, so it does not try.
     */
    seen = healthy(20);
    CHECK(step(&watch, &seen) == NET_LINK_IDLE, "a backwards clock read as a silent server");
    CHECK(!NetLinkWatch_Lost(&watch), "a backwards clock lost a live session");

    /* And the link is still watched afterwards: once the clock is ahead of the
     * last packet again, the ordinary timeout still bites on the bound. */
    seen = healthy(1000 + NET_LINK_TIMEOUT_MS);
    CHECK(step(&watch, &seen) == NET_LINK_LOST, "the timeout stopped working after the jump");
}

static void
test_the_login_handshake_may_sit_quiet(void)
{
    struct NetLinkWatch watch = live_session();
    struct NetLinkSighting seen;

    printf("TEST: a login handshake is allowed to be silent\n");

    /*
     * The silence detector is armed only in the GAME stream. A login
     * handshake legitimately sits quiet while a proof-of-work is solved, and
     * fifteen seconds of that is normal -- dropping it would make the client
     * unable to log in to exactly the servers that ask for the most work.
     */
    seen = healthy(1000 + NET_LINK_TIMEOUT_MS * 4);
    seen.in_game = false;
    seen.logging_in = true;
    CHECK(step(&watch, &seen) == NET_LINK_IDLE, "a quiet handshake was dropped");
    CHECK(!NetLinkWatch_Lost(&watch), "a quiet handshake lost the session");
}

static void
test_the_client_not_running(void)
{
    struct NetLinkWatch watch = live_session();
    struct NetLinkSighting seen;
    char const* why = NULL;

    printf("TEST: this client stopped running\n");

    /*
     * A different question from the one above -- not "has the server gone
     * quiet" but "was this client scheduled" -- and it has its own, much
     * larger number. A hidden tab's socket keeps filling while nothing drains
     * it, and replaying that backlog is what made a returning tab spend
     * seconds fast-forwarding with input ignored.
     *
     * Note the sighting: a packet arrived THIS frame. The stall detector must
     * not be satisfied by recency, because a backlog delivers a packet the
     * instant the tab comes back.
     */
    seen = healthy(2000);
    seen.frame_gap_ms = NET_LINK_STALL_MS - 1;
    CHECK(step(&watch, &seen) == NET_LINK_IDLE, "dropped one ms below the stall bound");

    seen = healthy(2000);
    seen.frame_gap_ms = NET_LINK_STALL_MS;
    CHECK(NetLinkWatch_Step(&watch, &seen, &why) == NET_LINK_LOST, "did not drop on the stall bound");
    CHECK(why && strstr(why, "running"), "the reason does not name the stall: %s", why ? why : "");
}

static void
test_a_long_frame_is_not_a_stall(void)
{
    struct NetLinkWatch watch;
    struct NetLinkSighting seen;

    printf("TEST: a very slow frame is not a suspended process\n");

    /*
     * The worst legitimate frame measured on the Windows XP lane, hydrating a
     * sparse cache over JS5 while rebuilding the world, was 11.04 seconds.
     * The threshold has to clear it -- at the old value of 4000 a client
     * working flat out dropped a healthy session twelve times in a thousand
     * frames, which is what made that profile unusable.
     *
     * The frame ran from t=8960 to t=20000 and drained the socket on its way
     * in, so the silence is that same 11.04s. Both detectors are therefore in
     * play, which is the real shape of this: a client too busy to drain a
     * socket is also a client that has not heard anything.
     */
    NetLinkWatch_Reset(&watch);
    NetLinkWatch_NotePacket(&watch, 8960);
    seen = healthy(20000);
    seen.frame_gap_ms = 11040;
    CHECK(step(&watch, &seen) == NET_LINK_IDLE, "the worst measured legitimate frame was dropped");
    CHECK(!NetLinkWatch_Lost(&watch), "a slow frame lost the session");
}

static void
test_the_socket_going_away(void)
{
    struct NetLinkWatch watch = live_session();
    struct NetLinkSighting seen;
    char const* why = NULL;

    printf("TEST: the transport says the socket is gone\n");

    seen = healthy(1200);
    seen.in_game = false;
    seen.socket_closed = true;
    CHECK(NetLinkWatch_Step(&watch, &seen, &why) == NET_LINK_LOST, "a closed socket was not noticed");
    CHECK(why && strstr(why, "socket"), "the reason does not name the socket: %s", why ? why : "");
}

static void
test_losing_it_is_idempotent(void)
{
    struct NetLinkWatch watch = live_session();
    struct NetLinkSighting seen;

    printf("TEST: two detectors firing together tear the session down once\n");

    /*
     * A stalled tab both misses packets and reports a huge frame gap, so more
     * than one detector is true in the same frame and they are true again in
     * the next. The first to arrive owns the transition; a second LOST would
     * have the caller queue a second DISCONNECT and reset a session it has
     * already reset.
     */
    seen = healthy(1000 + NET_LINK_TIMEOUT_MS * 2);
    seen.frame_gap_ms = NET_LINK_STALL_MS * 2;
    seen.socket_closed = true;
    CHECK(step(&watch, &seen) == NET_LINK_LOST, "the first frame did not declare it lost");

    /* The next frame is in the reconnect machine, not the detectors. */
    seen = healthy(1000 + NET_LINK_TIMEOUT_MS * 2);
    seen.in_game = false;
    seen.frame_gap_ms = NET_LINK_STALL_MS * 2;
    seen.socket_closed = true;
    CHECK(step(&watch, &seen) != NET_LINK_LOST, "the session was declared lost twice");

    /* And an out-of-band drop on an already-lost session is not news either. */
    CHECK(!NetLinkWatch_Drop(&watch), "dropping an already-lost session reported news");
}

static void
test_the_backoff(void)
{
    struct NetLinkWatch watch = live_session();
    struct NetLinkSighting seen;
    uint64_t now = 5000;
    int attempts = 0;

    printf("TEST: the retry cadence, and giving up\n");

    seen = healthy(now);
    seen.frame_gap_ms = NET_LINK_STALL_MS;
    CHECK(step(&watch, &seen) == NET_LINK_LOST, "did not lose the session");

    /*
     * The FIRST attempt is immediate. The delay exists to space out retries,
     * and the attempt most likely to work is the one made the moment the link
     * is noticed to be down.
     */
    seen = healthy(now);
    seen.in_game = false;
    CHECK(step(&watch, &seen) == NET_LINK_RECONNECT, "the first attempt was not immediate");
    attempts = 1;

    /* And then nothing for the delay. Not a retry storm at frame rate against
     * a server that is already struggling. */
    seen = healthy(now + NET_LINK_RECONNECT_DELAY_MS - 1);
    seen.in_game = false;
    CHECK(step(&watch, &seen) == NET_LINK_IDLE, "retried before the delay had passed");

    while( attempts < NET_LINK_RECONNECT_MAX_ATTEMPTS )
    {
        now += NET_LINK_RECONNECT_DELAY_MS;
        seen = healthy(now);
        seen.in_game = false;
        CHECK(step(&watch, &seen) == NET_LINK_RECONNECT,
            "attempt %d did not happen at its due time", attempts + 1);
        attempts++;
    }

    /*
     * Bounded. The reference retries once and falls back to the login screen;
     * a phone that backgrounded the tab deserves more than one try, but an
     * unbounded loop against a server that is gone is a client that never
     * tells the player anything.
     */
    now += NET_LINK_RECONNECT_DELAY_MS;
    seen = healthy(now);
    seen.in_game = false;
    CHECK(step(&watch, &seen) == NET_LINK_GAVE_UP,
        "did not give up after %d attempts", NET_LINK_RECONNECT_MAX_ATTEMPTS);
    CHECK(NetLinkWatch_GaveUp(&watch), "giving up is not reported");

    /* And then it stays given up rather than announcing it every frame. */
    now += NET_LINK_RECONNECT_DELAY_MS * 4;
    seen = healthy(now);
    seen.in_game = false;
    CHECK(step(&watch, &seen) == NET_LINK_IDLE, "gave up twice");
}

static void
test_an_attempt_in_flight_is_waited_for(void)
{
    struct NetLinkWatch watch = live_session();
    struct NetLinkSighting seen;

    printf("TEST: an attempt that is still dialling is not overtaken\n");

    seen = healthy(5000);
    seen.frame_gap_ms = NET_LINK_STALL_MS;
    CHECK(step(&watch, &seen) == NET_LINK_LOST, "did not lose the session");

    seen = healthy(5000);
    seen.in_game = false;
    CHECK(step(&watch, &seen) == NET_LINK_RECONNECT, "the first attempt did not happen");

    /*
     * While the login machine is running, the attempt is in flight. Counting
     * down to another one on top of it would have two handshakes racing for
     * the same character, which the server resolves by refusing both.
     */
    seen = healthy(5000 + NET_LINK_RECONNECT_DELAY_MS * 10);
    seen.in_game = false;
    seen.logging_in = true;
    CHECK(step(&watch, &seen) == NET_LINK_IDLE, "dialled over an attempt in flight");
}

static void
test_a_refused_dial_stops_the_machine(void)
{
    struct NetLinkWatch watch = live_session();
    struct NetLinkSighting seen;

    printf("TEST: a dial that could not be started is the end of it\n");

    seen = healthy(5000);
    seen.frame_gap_ms = NET_LINK_STALL_MS;
    (void)step(&watch, &seen);
    seen = healthy(5000);
    seen.in_game = false;
    CHECK(step(&watch, &seen) == NET_LINK_RECONNECT, "the first attempt did not happen");

    /*
     * Distinct from an attempt that fails later: this one never reached the
     * login machine at all, so there is nothing in flight to wait for. Left
     * counting down, the client would spend its remaining attempts on a
     * transport that has already said it cannot.
     */
    NetLinkWatch_NoteReconnectRefused(&watch);
    CHECK(NetLinkWatch_GaveUp(&watch), "a refused dial did not stop the machine");

    seen = healthy(5000 + NET_LINK_RECONNECT_DELAY_MS * 4);
    seen.in_game = false;
    CHECK(step(&watch, &seen) == NET_LINK_IDLE, "kept dialling after the transport refused");
}

static void
test_coming_back(void)
{
    struct NetLinkWatch watch = live_session();
    struct NetLinkSighting seen;

    printf("TEST: the session comes back\n");

    seen = healthy(5000);
    seen.frame_gap_ms = NET_LINK_STALL_MS;
    CHECK(step(&watch, &seen) == NET_LINK_LOST, "did not lose the session");
    seen = healthy(5000);
    seen.in_game = false;
    CHECK(step(&watch, &seen) == NET_LINK_RECONNECT, "did not dial");

    /*
     * The handshake reached the game stream again, after an outage LONGER than
     * the silence bound. That is the ordinary case -- five attempts two
     * seconds apart is already ten -- and it is the only one that can see the
     * next assertion.
     */
    seen = healthy(60000);
    CHECK(step(&watch, &seen) == NET_LINK_REESTABLISHED, "coming back was not noticed");
    CHECK(!NetLinkWatch_Lost(&watch), "still reads as lost after coming back");

    /*
     * And the silence clock is stamped, not left where the outage left it.
     * Unstamped, the very first frame back measures its silence from before
     * the outage -- which by then is longer than the timeout -- and drops the
     * session it has only just recovered. Every time, for ever: the recovery
     * restamps nothing, so the next one lands in the same place.
     */
    seen = healthy(60000 + 16);
    CHECK(step(&watch, &seen) == NET_LINK_IDLE, "dropped the session it had just recovered");
    CHECK(watch.reconnect_attempts == 0, "the attempt count survived the recovery");
}

static void
test_a_deliberate_logout_disarms_the_watch(void)
{
    struct NetLinkWatch watch = live_session();
    struct NetLinkSighting seen;

    printf("TEST: a logout is not a lost connection\n");

    /*
     * Every detector is armed off `last_recv_ms` -- a session that WAS heard
     * from and then went quiet -- which is exactly what a socket the client
     * closed on purpose looks like. Left armed, the client spends its way back
     * to the title screen redialling the world the player just left.
     */
    NetLinkWatch_Reset(&watch);

    seen = healthy(1000 + NET_LINK_TIMEOUT_MS * 10);
    seen.in_game = false;
    seen.socket_closed = true;
    seen.frame_gap_ms = NET_LINK_STALL_MS * 2;
    CHECK(step(&watch, &seen) == NET_LINK_IDLE, "a deliberate logout read as a lost connection");
    CHECK(!NetLinkWatch_Lost(&watch), "a deliberate logout left the watch armed");
}

static void
test_an_out_of_band_drop(void)
{
    struct NetLinkWatch watch = live_session();
    struct NetLinkSighting seen;

    printf("TEST: ::clientdrop severs a healthy session\n");

    CHECK(NetLinkWatch_Drop(&watch), "dropping a live session was not news");
    CHECK(NetLinkWatch_Lost(&watch), "the drop did not take");

    /* And the machine picks it up from there like any other loss. */
    seen = healthy(2000);
    seen.in_game = false;
    CHECK(step(&watch, &seen) == NET_LINK_RECONNECT, "a dropped session did not try to come back");
}

static void
test_the_drop_hook(void)
{
    struct NetLinkWatch watch = live_session();
    struct NetLinkSighting seen;
    char const* why = NULL;

    printf("TEST: the harness drop request\n");

    /* A harness has no chat box to type `::clientdrop` into, so it asks
     * through the sighting instead. Same transition, and it beats every
     * detector below it so a test does not have to wait fifteen seconds. */
    seen = healthy(1016);
    seen.drop_requested = true;
    CHECK(NetLinkWatch_Step(&watch, &seen, &why) == NET_LINK_LOST, "a requested drop did nothing");
    CHECK(why && strstr(why, "drop"), "the reason does not name the request: %s", why ? why : "");
}

int
main(void)
{
    test_a_healthy_link_is_left_alone();
    test_a_client_that_was_never_connected_loses_nothing();
    test_the_session_origin_is_the_first_packet();
    test_the_server_going_quiet();
    test_a_clock_that_went_backwards_is_not_silence();
    test_the_login_handshake_may_sit_quiet();
    test_the_client_not_running();
    test_a_long_frame_is_not_a_stall();
    test_the_socket_going_away();
    test_losing_it_is_idempotent();
    test_the_backoff();
    test_an_attempt_in_flight_is_waited_for();
    test_a_refused_dial_stops_the_machine();
    test_coming_back();
    test_a_deliberate_logout_disarms_the_watch();
    test_an_out_of_band_drop();
    test_the_drop_hook();

    if( g_failures )
    {
        printf("net_link_watch_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("net_link_watch_test: OK\n");
    return 0;
}
