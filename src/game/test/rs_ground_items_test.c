/*
 * The ground-item pile ops, which are the whole input to the cache's
 * ground-items overlay.
 *
 * `torirs_ground_items_overlay` (clientscript 7227) asks eight questions and
 * draws whatever they answer: how many things are on this tile, what and how
 * many each is, and -- for the entry it has selected -- how long until it
 * despawns, how long until everyone can see it, whether that has happened
 * already, and whose it is. Until this landed, all eight fell through to
 * CS2VM2_Op_StackMetaStub, which faked a zero apiece; a faked zero for
 * `OBJSTACK_COUNT` is "the tile is empty", so the overlay was not visibly
 * broken, it was invisibly absent.
 *
 * Two halves, for the reason rs_social_test.c splits the same way:
 *
 *   - dispatch. Whether the opcode reaches a handler at all and with which
 *     arguments. A routing-table edit that drops one is exactly the regression
 *     and a direct call to the handler cannot see it. The pop ORDER matters
 *     here as much as the count: `_7121(coord, index)` pushes the coord first,
 *     so a handler that pops them the other way round reads the index as a
 *     coord and answers about a tile in the sea.
 *
 *   - the real host. Whether the answers are right, including the two that are
 *     conversions rather than lookups: the wire counts a despawn in GAME ticks
 *     and this client's clock counts LOGIC ticks, and the cache's own
 *     `~buff_bar_time_string` divides by 50 to get seconds -- so a factor
 *     dropped anywhere along that chain shows up as a timer that is thirty
 *     times wrong rather than as anything failing.
 *
 * No cache and no world: the pile is a stub callback, because the App's own
 * walk of the obj-stack pool is a different subject (world/) and nothing on
 * this path reads a cache record.
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_host.h"
#include "cs2vm2/cs2vm2_script.h"
#include "engine/dat2/dat2_buildcache.h"
#include "game/rs_cs2_host.h"
#include "inv/inv_manager.h"
#include "ui/uitree.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( cond )                                                                                 \
        {                                                                                          \
            printf("  ok   ");                                                                     \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL ");                                                                     \
            printf(__VA_ARGS__);                                                                   \
            printf("\n       (%s at %s:%d)\n", #cond, __FILE__, __LINE__);                         \
            g_fail++;                                                                              \
        }                                                                                          \
    } while( 0 )

/* An arbitrary absolute coord, packed the way `_6950` answers: plane 0,
 * x 3222, z 3218. Deliberately not a small number -- a coord that survives a
 * mixed-up pop order by accident would prove nothing. */
#define TEST_COORD ((0 << 28) | (3222 << 14) | 3218)
#define OTHER_COORD ((0 << 28) | (3000 << 14) | 3000)

/* ==========================================================================
 * Part 1 — VM dispatch, against a recording host
 * ========================================================================== */

struct RecordingHost
{
    int calls;
    enum CS2VM_HostRequestKind kind;
    int opcode;
    int coord;
    int index;
    int pushes;
};

static int
recording_host_exec(struct CS2VM2_Thread* thread, struct CS2VM_HostRequest* request)
{
    struct RecordingHost* host = (struct RecordingHost*)thread->vm->user;

    host->calls++;
    host->kind = request->kind;
    switch( request->kind )
    {
#define RECORD_GROUND(name)                                                 \
    case CS2VM_HOST_REQUEST_##name:                                         \
        host->opcode = request->u.name.opcode;                              \
        host->coord = request->u.name.coord;                                \
        host->index = request->u.name.index;                                \
        break
        RECORD_GROUND(OBJSTACK_COUNT);
        RECORD_GROUND(OBJSTACK_ID);
        RECORD_GROUND(OBJSTACK_QUANTITY);
        RECORD_GROUND(OBJ_FIND);
        RECORD_GROUND(OBJ_DESPAWNTIME);
        RECORD_GROUND(OBJ_VISIBLETIME);
        RECORD_GROUND(OBJ_ISPUBLIC);
        RECORD_GROUND(OBJ_OWNER);
#undef RECORD_GROUND
    default:
        return CS2VM_EXECNO_ERROR;
    }
    /* Every one of the eight pushes exactly one int. */
    host->pushes++;
    return CS2VM2_PushInt(thread, 0);
}

struct DispatchCase
{
    int opcode;
    enum CS2VM_HostRequestKind kind;
    char const* name;
    int int_in;
    /* What the handler must have read out of the pushed 101, 102 sequence. */
    int want_coord;
    int want_index;
};

#define DISPATCH_CASE(name_, int_in, coord, index)                                                 \
    { CS2_OP_##name_, CS2VM_HOST_REQUEST_##name_, #name_, int_in, coord, index }

static struct DispatchCase const DISPATCH[] = {
    /* (coord) */
    DISPATCH_CASE(OBJSTACK_COUNT, 1, 101, -1),
    /* (coord, index) -- the coord is pushed first, so it is 101. */
    DISPATCH_CASE(OBJSTACK_ID, 2, 101, 102),
    DISPATCH_CASE(OBJSTACK_QUANTITY, 2, 101, 102),
    DISPATCH_CASE(OBJ_FIND, 2, 101, 102),
    /* () -- the four getters read the entry OBJ_FIND selected. */
    DISPATCH_CASE(OBJ_DESPAWNTIME, 0, -1, -1),
    DISPATCH_CASE(OBJ_VISIBLETIME, 0, -1, -1),
    DISPATCH_CASE(OBJ_ISPUBLIC, 0, -1, -1),
    DISPATCH_CASE(OBJ_OWNER, 0, -1, -1),
};

static void
test_dispatch(void)
{
    printf("ground items: every op reaches the host, with the arguments it was given\n");

    for( size_t i = 0; i < sizeof(DISPATCH) / sizeof(DISPATCH[0]); i++ )
    {
        struct DispatchCase const* c = &DISPATCH[i];
        struct RecordingHost host;
        struct CS2VM2 vm;
        struct CS2VM2_Script script;
        struct CS2VM2_Thread* thread;
        int const op_count = c->int_in + 2;
        int at = 0;

        memset(&host, 0, sizeof(host));
        CS2VM2_Init(&vm);
        CS2VM2_BindHost(&vm, &host, recording_host_exec);

        CS2VM2_ScriptInit(&script);
        script.script_id = 7227;
        script.op_count = op_count;
        script.opcodes = calloc((size_t)op_count, sizeof(uint16_t));
        script.int_operands = calloc((size_t)op_count, sizeof(int));
        script.string_operands = calloc((size_t)op_count, sizeof(char*));

        for( int p = 0; p < c->int_in; p++ )
        {
            script.opcodes[at] = (uint16_t)CS2_OP_PUSH_CONSTANT_INT;
            script.int_operands[at++] = 101 + p;
        }
        script.opcodes[at++] = (uint16_t)c->opcode;
        script.opcodes[at] = (uint16_t)CS2_OP_RETURN;

        thread = CS2VM2_ThreadMain(&vm);
        CS2VM2_PushCallScript(thread, &script);
        CS2VM2_RunScript(thread);

        CHECK(host.calls == 1, "%s reached the host once (%d)", c->name, host.calls);
        CHECK(
            host.kind == c->kind,
            "%s carried its own request kind (%d)",
            c->name,
            (int)host.kind);
        CHECK(host.opcode == c->opcode, "%s carried its opcode (%d)", c->name, host.opcode);
        if( c->int_in >= 1 )
            CHECK(
                host.coord == c->want_coord,
                "%s read the coord as %d, want %d",
                c->name,
                host.coord,
                c->want_coord);
        if( c->int_in >= 2 )
            CHECK(
                host.index == c->want_index,
                "%s read the index as %d, want %d",
                c->name,
                host.index,
                c->want_index);
        /* Every argument consumed and exactly one answer left. A handler that
         * popped one too few leaves the stack deep and the script's next read
         * is somebody else's number. */
        CHECK(
            thread->ints_stack_top == 1,
            "%s left one int on the stack (%d)",
            c->name,
            thread->ints_stack_top);

        free(script.opcodes);
        free(script.int_operands);
        free(script.string_operands);
        CS2VM2_Free(&vm);
    }
}

/* ==========================================================================
 * Part 2 — the real host, over a stub pile
 * ========================================================================== */

struct StubPile
{
    struct RS_CS2GroundObj entries[2];
    int count;
    int calls;
};

static int
stub_objs_on_coord(void* user, int coord, int index, struct RS_CS2GroundObj* out)
{
    struct StubPile* pile = (struct StubPile*)user;

    pile->calls++;
    if( coord != TEST_COORD )
        return 0;
    if( index >= 0 && index < pile->count )
        *out = pile->entries[index];
    return pile->count;
}

static int
call_ground(struct CS2VM2_Thread* t, int opcode, int coord, int index)
{
    struct CS2VM_HostRequest req;
    memset(&req, 0, sizeof(req));
    req.kind = (enum CS2VM_HostRequestKind)opcode;
    switch( req.kind )
    {
#define SET_GROUND_REQUEST(name_)                                           \
    case CS2VM_HOST_REQUEST_##name_:                                        \
        req.u.name_.opcode = opcode;                                        \
        req.u.name_.coord = coord;                                          \
        req.u.name_.index = index;                                          \
        break
        SET_GROUND_REQUEST(OBJSTACK_COUNT);
        SET_GROUND_REQUEST(OBJSTACK_ID);
        SET_GROUND_REQUEST(OBJSTACK_QUANTITY);
        SET_GROUND_REQUEST(OBJ_FIND);
        SET_GROUND_REQUEST(OBJ_DESPAWNTIME);
        SET_GROUND_REQUEST(OBJ_VISIBLETIME);
        SET_GROUND_REQUEST(OBJ_ISPUBLIC);
        SET_GROUND_REQUEST(OBJ_OWNER);
#undef SET_GROUND_REQUEST
    default:
        assert(0 && "call_ground: unexpected opcode");
        return CS2VM_EXECNO_ERROR;
    }
    return RS_CS2Host_Exec(t, &req);
}

/* Run one op and take its answer off the stack, so each check starts empty. */
static int
ask(struct CS2VM2_Thread* t, int opcode, int coord, int index)
{
    int value = 0;
    call_ground(t, opcode, coord, index);
    if( CS2VM2_PopInt(t, &value) != CS2VM_EXECNO_OK )
    {
        printf("  FAIL opcode %d pushed nothing\n", opcode);
        g_fail++;
        return INT32_MIN;
    }
    return value;
}

static void
test_host_ops(void)
{
    struct UITree* tree = UITree_New(256);
    struct Dat2BuildCache* bc = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(bc);
    struct InvManager invs;
    struct RS_CS2Host host;
    struct StubPile pile;
    struct CS2VM2 vm;
    struct CS2VM2_Thread* t;
    int now;

    printf("ground items: the host answers from the pile, and converts the two clocks\n");

    InvManager_Init(&invs);
    RS_CS2Host_Init(&host, tree, provider, &invs, NULL, NULL, NULL);
    now = host.client_clock;

    memset(&pile, 0, sizeof(pile));
    pile.count = 2;
    /* Someone else's coin pile: ten game ticks from being public, a hundred
     * from despawning. */
    pile.entries[0].obj_id = 995;
    pile.entries[0].count = 5000;
    pile.entries[0].public_clock = now + 10 * RS_CS2_HOST_CLOCKS_PER_TICK;
    pile.entries[0].despawn_clock = now + 100 * RS_CS2_HOST_CLOCKS_PER_TICK;
    pile.entries[0].owner = 2;
    pile.entries[0].never_becomes_public = 0;
    /* Bones the server said nothing about, flagged never-public. */
    pile.entries[1].obj_id = 526;
    pile.entries[1].count = 1;
    pile.entries[1].public_clock = -1;
    pile.entries[1].despawn_clock = -1;
    pile.entries[1].owner = 0;
    pile.entries[1].never_becomes_public = 1;

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &host, RS_CS2Host_Exec);
    t = CS2VM2_ThreadMain(&vm);

    /*
     * A host with no world first. This is the login screen and every frame
     * before the scene lands, and the honest answer is an empty tile -- which
     * is also what makes the overlay script destroy its overlay rather than
     * leave a stale one over a scene that no longer exists.
     */
    CHECK(
        ask(t, CS2_OP_OBJSTACK_COUNT, TEST_COORD, -1) == 0,
        "with no world callback every tile is empty");
    CHECK(
        ask(t, CS2_OP_OBJSTACK_ID, TEST_COORD, 0) == -1,
        "and asking what is on it answers nothing");

    host.objs_on_coord = stub_objs_on_coord;
    host.world_user = &pile;

    CHECK(ask(t, CS2_OP_OBJSTACK_COUNT, TEST_COORD, -1) == 2, "the pile is two entries deep");
    CHECK(
        ask(t, CS2_OP_OBJSTACK_COUNT, OTHER_COORD, -1) == 0,
        "and a tile outside the scene is empty rather than an error");

    CHECK(ask(t, CS2_OP_OBJSTACK_ID, TEST_COORD, 0) == 995, "entry 0 is the coins");
    CHECK(ask(t, CS2_OP_OBJSTACK_QUANTITY, TEST_COORD, 0) == 5000, "five thousand of them");
    CHECK(ask(t, CS2_OP_OBJSTACK_ID, TEST_COORD, 1) == 526, "entry 1 is the bones");
    CHECK(ask(t, CS2_OP_OBJSTACK_QUANTITY, TEST_COORD, 1) == 1, "one of those");
    /* -1 and not 0: `if ($int12 ! null)` is the overlay's own test for the end
     * of the list, and obj 0 is a real obj. */
    CHECK(ask(t, CS2_OP_OBJSTACK_ID, TEST_COORD, 2) == -1, "an index past the end answers null");
    CHECK(ask(t, CS2_OP_OBJSTACK_ID, TEST_COORD, -1) == -1, "and so does a negative one");

    /*
     * The four getters before anything has been selected. -1, not 0: a zero
     * despawn timer is a pile about to vanish, which the overlay draws, and
     * describing entry zero of nothing that way would put a countdown on a
     * tile the script never asked about.
     */
    CHECK(
        ask(t, CS2_OP_OBJ_OWNER, -1, -1) == -1,
        "with nothing selected the owner is unknown");
    CHECK(
        ask(t, CS2_OP_OBJ_DESPAWNTIME, -1, -1) == -1,
        "and so is the despawn time");

    CHECK(ask(t, CS2_OP_OBJ_FIND, TEST_COORD, 5) == 0, "selecting past the end fails");
    CHECK(
        ask(t, CS2_OP_OBJ_OWNER, -1, -1) == -1,
        "and leaves nothing selected");
    CHECK(
        ask(t, CS2_OP_OBJ_FIND, OTHER_COORD, 0) == 0,
        "selecting on an empty tile fails too");

    CHECK(ask(t, CS2_OP_OBJ_FIND, TEST_COORD, 0) == 1, "entry 0 can be selected");
    /*
     * GAME ticks out, because that is what the cache multiplies by 30 to reach
     * the 20 ms units `~buff_bar_time_string` formats. The clocks stored are
     * deadlines in those 20 ms units, so this is the whole conversion.
     */
    CHECK(
        ask(t, CS2_OP_OBJ_DESPAWNTIME, -1, -1) == 100,
        "its despawn is a hundred game ticks off");
    CHECK(
        ask(t, CS2_OP_OBJ_VISIBLETIME, -1, -1) == 10,
        "and it goes public in ten");
    CHECK(ask(t, CS2_OP_OBJ_ISPUBLIC, -1, -1) == 0, "so it is not public yet");
    CHECK(ask(t, CS2_OP_OBJ_OWNER, -1, -1) == 2, "and it is somebody else's");

    /* The clock runs past the public deadline: the answer floors at zero
     * rather than going negative, and the pile becomes public. */
    host.client_clock = now + 20 * RS_CS2_HOST_CLOCKS_PER_TICK;
    CHECK(
        ask(t, CS2_OP_OBJ_VISIBLETIME, -1, -1) == 0,
        "once the deadline passes the countdown floors at zero");
    CHECK(ask(t, CS2_OP_OBJ_ISPUBLIC, -1, -1) == 1, "and the pile reads as public");
    CHECK(
        ask(t, CS2_OP_OBJ_DESPAWNTIME, -1, -1) == 80,
        "while the despawn keeps counting down");
    host.client_clock = now;

    CHECK(ask(t, CS2_OP_OBJ_FIND, TEST_COORD, 1) == 1, "entry 1 can be selected in turn");
    /*
     * A revision whose OBJ_ADD carries no timers stores -1, and -1 is not a
     * deadline in the past: zero is the only honest duration for a question
     * with no answer, and it is what stops the overlay drawing "[0s]" beside
     * every pile on a classic world.
     */
    CHECK(
        ask(t, CS2_OP_OBJ_DESPAWNTIME, -1, -1) == 0,
        "an entry the server said nothing about has no despawn timer");
    CHECK(
        ask(t, CS2_OP_OBJ_VISIBLETIME, -1, -1) == 0,
        "nor a visibility one");
    /* neverBecomesPublic outranks the clock: the pile is private for its whole
     * life however far past a deadline that never existed the clock has run. */
    CHECK(
        ask(t, CS2_OP_OBJ_ISPUBLIC, -1, -1) == 0,
        "and a never-public pile stays private whatever the clock says");
    CHECK(ask(t, CS2_OP_OBJ_OWNER, -1, -1) == 0, "its owner is the public one");

    CS2VM2_Free(&vm);
    UITree_Free(tree);
    dat2_buildcache_free(bc);
}

/* ==========================================================================
 * Part 3 — the settings row a threshold is typed into
 *
 * The five price tiers and the overlay's line limit are NUMBER-INPUT rows, and
 * a number row's op script writes nothing: the reference opens a numeric entry
 * of its own and writes the row's varp itself. So this client has to know
 * which varp a row is, and the only place the cache states it is inside
 * `settings_get_number_input` -- a switch from setting id to `%var<n>`.
 *
 * Watching the varp READ that hub performs is therefore the map, and it is
 * learned rather than tabulated for the reason the colour rows' twin is: the
 * row builder calls the hub while laying the row out, so every row on screen
 * has taught it before its field exists to be clicked, and no table here has
 * to be kept in step with the cache by hand.
 *
 * What breaks without this: the entry box opens on a varp of -1 and every
 * threshold typed is discarded, which looks exactly like a box that works.
 * ========================================================================== */

static void
test_settings_number_varp(void)
{
    struct UITree* tree = UITree_New(256);
    struct Dat2BuildCache* bc = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(bc);
    struct InvManager invs;
    struct RS_CS2Host host;
    struct CS2VM2 vm;
    struct CS2VM2_Script hub;
    struct CS2VM2_Script other;
    struct CS2VM2_Thread* thread;
    uint16_t hub_ops[2];
    int hub_ints[2];
    char* hub_strs[2] = { NULL, NULL };

    printf("ground items: a number row's varp is learned from the cache's own read hub\n");

    InvManager_Init(&invs);
    RS_CS2Host_Init(&host, tree, provider, &invs, NULL, NULL, NULL);
    /* Stated here rather than read from a revconfig: the test is about what
     * happens once the id is known, and a run with no revconfig switches the
     * feature off (id 0) exactly as it is meant to. */
    host.script_settings_number_get = 3964;

    CHECK(
        RS_CS2Host_SettingsNumberVarp(&host, 328) == -1,
        "before the hub has run, no row has a varp");

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &host, RS_CS2Host_Exec);
    thread = CS2VM2_ThreadMain(&vm);

    /* `[proc,settings_get_number_input](int $int0)` -> `return(%var3800)`, as
     * the rev-239 cache spells the tier-5 threshold row. */
    hub_ops[0] = (uint16_t)CS2_OP_PUSH_VAR;
    hub_ints[0] = 3800;
    hub_ops[1] = (uint16_t)CS2_OP_RETURN;
    hub_ints[1] = 0;
    CS2VM2_ScriptInit(&hub);
    hub.script_id = 3964;
    hub.op_count = 2;
    hub.opcodes = hub_ops;
    hub.int_operands = hub_ints;
    hub.string_operands = hub_strs;
    hub.int_argument_count = 1;
    hub.local_int_count = 1;

    CS2VM2_PushCallScript(thread, &hub);
    (void)CS2VM2_SetIntCurrentFrameLocal(thread, 0, 328);
    CS2VM2_RunScript(thread);

    CHECK(
        RS_CS2Host_SettingsNumberVarp(&host, 328) == 3800,
        "the hub's own varp read names the row (%d)",
        RS_CS2Host_SettingsNumberVarp(&host, 328));
    CHECK(
        RS_CS2Host_SettingsNumberVarp(&host, 329) == -1,
        "and says nothing about a row it has not been asked for");

    /*
     * The same varp read from somewhere that is NOT the hub must teach
     * nothing. Every clientscript in the cache reads varps; a watcher that did
     * not check whose frame it is in would map whichever setting id happened
     * to be in local 0 onto whatever varp the script was reading.
     */
    CS2VM2_ScriptInit(&other);
    other.script_id = 3965;
    other.op_count = 2;
    other.opcodes = hub_ops;
    other.int_operands = hub_ints;
    other.string_operands = hub_strs;
    other.int_argument_count = 1;
    other.local_int_count = 1;

    CS2VM2_PushCallScript(thread, &other);
    (void)CS2VM2_SetIntCurrentFrameLocal(thread, 0, 999);
    CS2VM2_RunScript(thread);

    CHECK(
        RS_CS2Host_SettingsNumberVarp(&host, 999) == -1,
        "a varp read outside the hub teaches nothing");

    CS2VM2_Free(&vm);
    UITree_Free(tree);
    dat2_buildcache_free(bc);
}

int
main(void)
{
    test_dispatch();
    test_host_ops();
    test_settings_number_varp();
    if( g_fail )
    {
        printf("ground items: %d FAILURE(S)\n", g_fail);
        return 1;
    }
    printf("ground items: all checks passed\n");
    return 0;
}
