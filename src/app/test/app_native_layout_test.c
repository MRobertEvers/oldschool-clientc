/*
 * Asking the lane for a native top-level chrome.
 *
 * A provided gameframe arranges OVER the lane's own top level, and on a CS2
 * lane that is one of four interfaces with different node numbering and
 * different surface geometry. A provider authored for one of them and handed
 * another lays out badly; `app_native_layout_select` is how it asks for the
 * one it is for instead.
 *
 * What the act has to be is the whole subject of this file. The client does
 * not own which toplevel is up -- the server opens it and mounts every panel
 * and every armed op into it -- so the request is the press a PLAYER makes:
 * the Display panel's layout row, sub-id = choice + 1, op 1. Three properties
 * of that press are tested because getting any of them wrong is silent:
 *
 *   - it goes out only when the server ARMED that row, because the arming is
 *     the whole licence for the packet;
 *   - it is ONE packet however often a layout pass asks, because the asker is
 *     a layout pass and the answer is three server ticks away;
 *   - it is not sent at all when the lane is already wearing the chrome, or
 *     is wearing the phone's, which nothing offers a way out of.
 *
 * The fixture is a zeroed App carrying the real rev-239 profile, so the two
 * ids the act needs -- the four toplevels and settings_side -- are the ones a
 * running client would resolve, and a profile that stopped declaring them
 * fails here rather than in a frame nobody is looking at.
 */

#include "app.h"
#include "app/app_internal.h"
#include "net/isaac.h"
#include "net/rev/gameproto_revisions.h"
#include "test_harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_failures;

/* The rev-239 Display row, as the profile and the .if file name it: interface
 * 116, component 40. Spelled out here rather than read from the code under
 * test, so a change to either half is a failure and not a tautology. */
#define TEST_SETTINGS_SIDE_IFACE 116
#define TEST_LAYOUT_ROW_CHILD 40
#define TEST_LAYOUT_ROW ((TEST_SETTINGS_SIDE_IFACE << 16) | TEST_LAYOUT_ROW_CHILD)

/* ^if_event_op1 as the client reads it: `events & (1 << op_num)`. */
#define TEST_IF_EVENT_OP1 2

struct Fixture
{
    struct App* app;
    struct ToriRS_Network* net;
    struct Isaac* random_out;
};

/* A session in the game stream, at the revision whose Display row this is. */
static void
fixture_init(struct Fixture* fixture)
{
    struct App* app = calloc(1, sizeof(*app));
    struct ToriRS_Network* net = calloc(1, sizeof(*net));
    /* A real one: the opcode byte is ciphered, and isaac_next asserts. The
     * seed is nobody's -- what the packet's opcode encrypts to is not what
     * this file reads back. */
    int seed[4] = { 1, 2, 3, 4 };
    struct Isaac* random_out = isaac_new(seed, 4);

    TEST_ASSERT(app && net && random_out, "allocated the fixture");
    net->rev = GameProtoRev_OSRS239();
    net->state = TORIRS_NET_GAME;
    net->random_out = random_out;

    app->cfg.ui_logic = APP_UI_LOGIC_CS2;
    app->net = net;
    app->net_enabled = 1;
    /* What RS_CS2Host_Init states, and a zeroed host does not: -1 is "nothing
     * outstanding". Zero is Fixed, and a fixture that left it there would have
     * every test below asking for a chrome the host already believes it asked
     * for -- which is the same swallowed first request the real boot avoids by
     * writing this. */
    app->host.client_layout_wanted = -1;
    RevConfigRefs_Init(&app->revconfig_refs);
    RevConfigRefs_LoadSources(
        &app->revconfig_refs,
        "../revconfig/osrs239/osrs239_ui.ini",
        "../revconfig/osrs239/osrs239_dat2_cache.ini",
        NULL);

    fixture->app = app;
    fixture->net = net;
    fixture->random_out = random_out;
}

static void
fixture_free(struct Fixture* fixture)
{
    RevConfigRefs_Free(&fixture->app->revconfig_refs);
    UIIfEventTable_Free(&fixture->app->if_events);
    free(fixture->app);
    free(fixture->net);
    isaac_free(fixture->random_out);
    memset(fixture, 0, sizeof(*fixture));
}

/* What the server's `[proc,settings_side_login]` does: subs 1..3 of the row,
 * op 1, armed for the session. */
static void
arm_the_layout_row(struct Fixture* fixture)
{
    UIIfEventTable_Set(&fixture->app->if_events, TEST_LAYOUT_ROW, 1, 3, TEST_IF_EVENT_OP1);
}

static void
open_toplevel(struct Fixture* fixture, char const* iface_name)
{
    int const root = RevConfigRefs_Get(&fixture->app->revconfig_refs, "iface", iface_name);

    if( root <= 0 )
        fprintf(stderr, "  (no [iface:%s] in the profile)\n", iface_name);
    TEST_ASSERT(root > 0, "the profile declares the toplevel asked for");
    fixture->app->host.top_interface_id = root;
}

/*
 * The outbound ring, read as packets rather than as bytes.
 *
 * The opcode byte is ciphered by the outbound ISAAC stream and cannot be
 * compared against a constant, so the assertion is over the PAYLOAD, whose
 * shape rev 239 fixes: `p4 combinedId, p2 sub, p2 obj, p1 op`. `obj` is the
 * not-an-item sentinel for a plain widget press, and that is checked too --
 * it is what tells the server this is IF_BUTTON1 and not an inventory verb.
 */
struct SentPress
{
    int count;
    int component_id;
    int sub;
    int obj;
    int op;
};

static struct SentPress
drain_presses(struct Fixture* fixture)
{
    struct SentPress sent;
    struct ToriRS_CmdHeader header;
    uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];

    memset(&sent, 0, sizeof(sent));
    while( CmdRing_Pop(&fixture->net->out, &header, payload) )
    {
        if( header.type != TORIRS_NET_OUT_SEND_DATA || header.length < 10 )
            continue;
        sent.count++;
        sent.component_id = (payload[1] << 24) | (payload[2] << 16) | (payload[3] << 8) |
                            payload[4];
        sent.sub = (payload[5] << 8) | payload[6];
        sent.obj = (payload[7] << 8) | payload[8];
        sent.op = payload[9];
    }
    return sent;
}

/* The one that matters: a frame authored for the fixed root, standing on a
 * resizable one, asks for the root it is for. */
static void
test_a_request_is_the_display_rows_own_press(void)
{
    struct Fixture fixture;
    struct SentPress sent;

    printf("TEST: the request is the Display row's own press\n");
    fixture_init(&fixture);
    open_toplevel(&fixture, "toplevel_resizable_classic");
    arm_the_layout_row(&fixture);

    TEST_ASSERT(
        app_native_layout_mode(fixture.app) == APP_NATIVE_LAYOUT_RESIZABLE_CLASSIC,
        "the live root is read as the chrome it is");
    TEST_ASSERT(
        app_native_layout_select(fixture.app, APP_NATIVE_LAYOUT_FIXED),
        "the lane accepted the request");

    sent = drain_presses(&fixture);
    TEST_ASSERT(sent.count == 1, "exactly one packet went out");
    TEST_ASSERT(
        sent.component_id == TEST_LAYOUT_ROW, "it names the Display layout row");
    TEST_ASSERT(sent.sub == 1, "Fixed is sub 1");
    TEST_ASSERT(sent.op == 1, "pressed with op 1");
    TEST_ASSERT(sent.obj == 0xffff, "a plain widget press carries no item");
    TEST_ASSERT(
        fixture.app->host.client_layout_wanted == APP_NATIVE_LAYOUT_FIXED,
        "the request is remembered until the lane answers");

    fixture_free(&fixture);
}

/*
 * Classic to Modern, which is the transition WINDOW_STATUS cannot carry: both
 * are the same window class on the wire, so the row's op is the only thing
 * that distinguishes them and the client must send it.
 */
static void
test_classic_to_modern_is_asked_for(void)
{
    struct Fixture fixture;
    struct SentPress sent;

    printf("TEST: Classic to Modern, which the window class cannot say\n");
    fixture_init(&fixture);
    open_toplevel(&fixture, "toplevel_resizable_classic");
    arm_the_layout_row(&fixture);

    TEST_ASSERT(
        app_native_layout_select(fixture.app, APP_NATIVE_LAYOUT_RESIZABLE_MODERN),
        "the lane accepted the request");
    sent = drain_presses(&fixture);
    TEST_ASSERT(sent.count == 1, "one packet");
    TEST_ASSERT(sent.sub == 3, "Modern is sub 3");

    fixture_free(&fixture);
}

/* The asker is a layout pass, so it asks every pass. One request. */
static void
test_asking_every_pass_costs_one_packet(void)
{
    struct Fixture fixture;
    struct SentPress sent;

    printf("TEST: a frame that asks on every pass sends one packet\n");
    fixture_init(&fixture);
    open_toplevel(&fixture, "toplevel_resizable_modern");
    arm_the_layout_row(&fixture);

    for( int pass = 0; pass < 60; pass++ )
    {
        TEST_ASSERT(
            app_native_layout_select(fixture.app, APP_NATIVE_LAYOUT_FIXED),
            "every pass is accepted");
        fixture.app->logic_cycle++;
    }
    sent = drain_presses(&fixture);
    TEST_ASSERT(sent.count == 1, "one packet for sixty asks");

    /* ...and a server that never answered is asked again, once the window the
     * whole path fits inside has passed. */
    fixture.app->logic_cycle += APP_NATIVE_LAYOUT_RETRY_CYCLES;
    TEST_ASSERT(
        app_native_layout_select(fixture.app, APP_NATIVE_LAYOUT_FIXED),
        "the retry is accepted");
    sent = drain_presses(&fixture);
    TEST_ASSERT(sent.count == 1, "the unanswered request was retried once");

    fixture_free(&fixture);
}

/* Arrived: the root the lane opened is the one that was asked for. Nothing to
 * send, and the outstanding request is dropped rather than left to suppress a
 * move straight back. */
static void
test_the_chrome_it_is_already_wearing(void)
{
    struct Fixture fixture;
    struct SentPress sent;

    printf("TEST: the chrome it is already wearing is asked for with no packet\n");
    fixture_init(&fixture);
    open_toplevel(&fixture, "toplevel_fixed");
    arm_the_layout_row(&fixture);
    fixture.app->host.client_layout_wanted = APP_NATIVE_LAYOUT_FIXED;

    TEST_ASSERT(
        app_native_layout_select(fixture.app, APP_NATIVE_LAYOUT_FIXED),
        "an arrival is a satisfied request, not a refusal");
    sent = drain_presses(&fixture);
    TEST_ASSERT(sent.count == 0, "nothing was sent");
    TEST_ASSERT(
        fixture.app->host.client_layout_wanted == -1,
        "the arrived request was dropped");

    fixture_free(&fixture);
}

/*
 * The arming gate. A server that never armed the row is one this press would
 * be a message to nobody on, so the client says nothing at all.
 */
static void
test_an_unarmed_row_is_never_pressed(void)
{
    struct Fixture fixture;
    struct SentPress sent;

    printf("TEST: an unarmed row is never pressed\n");
    fixture_init(&fixture);
    open_toplevel(&fixture, "toplevel_resizable_classic");

    TEST_ASSERT(
        !app_native_layout_select(fixture.app, APP_NATIVE_LAYOUT_FIXED),
        "the lane refused, because nothing armed the row");
    sent = drain_presses(&fixture);
    TEST_ASSERT(sent.count == 0, "and sent nothing");

    /* Armed for the other two choices only: the sub is part of the question,
     * so a choice outside the armed range is still unarmed. */
    UIIfEventTable_Set(&fixture.app->if_events, TEST_LAYOUT_ROW, 2, 3, TEST_IF_EVENT_OP1);
    TEST_ASSERT(
        !app_native_layout_select(fixture.app, APP_NATIVE_LAYOUT_FIXED),
        "sub 1 is not armed by a range that starts at 2");
    sent = drain_presses(&fixture);
    TEST_ASSERT(sent.count == 0, "still nothing");

    fixture_free(&fixture);
}

/* The phone's root is reported and refused: the session opened 601 because the
 * login said phone, and the Display row offers no way in or out of it. */
static void
test_the_phones_root_is_not_a_choice(void)
{
    struct Fixture fixture;
    struct SentPress sent;

    printf("TEST: the phone's root is reported, and refused as a request\n");
    fixture_init(&fixture);
    open_toplevel(&fixture, "toplevel_mobile");
    arm_the_layout_row(&fixture);

    TEST_ASSERT(
        app_native_layout_mode(fixture.app) == APP_NATIVE_LAYOUT_MOBILE,
        "the phone's root is named, not reported as unknown");
    TEST_ASSERT(
        !app_native_layout_select(fixture.app, APP_NATIVE_LAYOUT_FIXED),
        "and nothing can take a phone off it");
    sent = drain_presses(&fixture);
    TEST_ASSERT(sent.count == 0, "no packet");

    fixture_free(&fixture);
}

/* No session, no lane to ask: the toplevel is the server's to open. */
static void
test_an_offline_lane_cannot_be_asked(void)
{
    struct Fixture fixture;

    printf("TEST: a lane with no session cannot be asked\n");
    fixture_init(&fixture);
    open_toplevel(&fixture, "toplevel_resizable_classic");
    arm_the_layout_row(&fixture);
    fixture.app->net->state = TORIRS_NET_DISCONNECTED;

    TEST_ASSERT(
        !app_native_layout_select(fixture.app, APP_NATIVE_LAYOUT_FIXED),
        "refused with no session to carry the request");

    fixture_free(&fixture);
}

/* A lane whose frame is revconfig builtins has no such chrome at all, and the
 * reader says so rather than naming one of the four. */
static void
test_a_dat1_lane_has_no_native_layout(void)
{
    struct Fixture fixture;

    printf("TEST: a lane with no cache toplevel answers unknown\n");
    fixture_init(&fixture);
    open_toplevel(&fixture, "toplevel_fixed");
    fixture.app->cfg.ui_logic = APP_UI_LOGIC_CS1;

    TEST_ASSERT(app_native_layout_mode(fixture.app) == -1, "no native layout to report");
    TEST_ASSERT(
        !app_native_layout_select(fixture.app, APP_NATIVE_LAYOUT_FIXED),
        "and none to ask for");

    fixture_free(&fixture);
}

/* Before the server has opened anything -- the login screen, the boot -- there
 * is no root to report. Not "fixed by default": a caller that read the boot as
 * a chrome would plan a frame against a tree that does not exist. */
static void
test_no_root_is_unknown_and_not_a_default(void)
{
    struct Fixture fixture;

    printf("TEST: a lane with no root open answers unknown\n");
    fixture_init(&fixture);

    TEST_ASSERT(app_native_layout_mode(fixture.app) == -1, "no root, no answer");

    fixture_free(&fixture);
}

int
main(void)
{
    printf("=== asking the lane for a native top-level chrome ===\n");
    test_a_request_is_the_display_rows_own_press();
    test_classic_to_modern_is_asked_for();
    test_asking_every_pass_costs_one_packet();
    test_the_chrome_it_is_already_wearing();
    test_an_unarmed_row_is_never_pressed();
    test_the_phones_root_is_not_a_choice();
    test_an_offline_lane_cannot_be_asked();
    test_a_dat1_lane_has_no_native_layout();
    test_no_root_is_unknown_and_not_a_default();

    if( g_failures )
    {
        printf("%d failure(s)\n", g_failures);
        return 1;
    }
    printf("all native-layout checks passed\n");
    return 0;
}
