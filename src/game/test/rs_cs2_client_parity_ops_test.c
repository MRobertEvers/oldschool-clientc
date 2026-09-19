/*
 * The opcodes that used to decode as raw `_<number>` and now have handlers,
 * driven through the real VM dispatch into the real RS_CS2Host_Exec.
 *
 * Each case pins behaviour taken from a reference client, not from this
 * client's own code: the rev-239 Java deob (Statics.java) where it has a
 * handler, the native rev-216 client where it does not. The comment on each
 * names which.
 *
 *   216   if_query_refine       filters the children iterator by param
 *   1506  cc_getparentlayer     the layer, or the component an interface is
 *                               mounted into
 *   4127  safeparseint          Integer.parseInt, value then success flag
 *   7451, 7453..7456, 7460      the indexed minimenu family
 *   7800..7820                  hiscores, with no transport
 *   8020  enum_getinputs        the enum's keys as an array
 *   3630, 3639                  a friend-list sort through dispatch
 *
 * No cache: the provider is an empty dat2 build cache and every config a case
 * reads is added by hand, so nothing here can skip.
 */

#include "cs2vm2/cs2_opcode.h"
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_types.h"
#include "game/rs_clientop.h"
#include "game/rs_cs2_host.h"
#include "game/rs_clan.h"
#include "game/rs_social.h"
#include "inv/inv_manager.h"
#include "ui/uitree.h"

#include <assert.h>
#include <limits.h>
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

struct Fixture
{
    struct UITree* tree;
    struct Dat2BuildCache* bc;
    struct CacheProvider* provider;
    struct InvManager invs;
    struct RS_CS2Host host;
    struct CS2VM2 vm;
    struct CS2VM2_Thread* thread;
    struct CS2VM2_Script script;
    uint16_t opcodes[16];
    int int_operands[16];
    char* string_operands[16];
};

static void
fixture_init(struct Fixture* fx)
{
    memset(fx, 0, sizeof(*fx));
    fx->tree = UITree_New(256);
    fx->bc = dat2_buildcache_new();
    fx->provider = dat2_buildcache_as_provider(fx->bc);
    InvManager_Init(&fx->invs);
    RS_CS2Host_Init(&fx->host, fx->tree, fx->provider, &fx->invs, NULL, NULL, NULL);
    CS2VM2_Init(&fx->vm);
    CS2VM2_BindHost(&fx->vm, &fx->host, RS_CS2Host_Exec);
    fx->thread = CS2VM2_ThreadMain(&fx->vm);
}

static void
fixture_free(struct Fixture* fx)
{
    CS2VM2_Free(&fx->vm);
    RS_CS2Host_Free(&fx->host);
    InvManager_Free(&fx->invs);
    UITree_Free(fx->tree);
    dat2_buildcache_free(fx->bc);
}

/* A script step: an int constant, a string constant, or the opcode under test. */
struct Step
{
    int opcode;
    int int_operand;
    char const* string_operand;
};

/* A children iterator to install once the script is pushed -- starting a script
 * resets the iterator, so seeding it before `run` would be wiped. */
struct IteratorSeed
{
    int parent_id;
    int cursor;
    int count;
    int ids[8];
};
static struct IteratorSeed const* g_iterator_seed = NULL;

#define INT(v) { CS2_OP_PUSH_CONSTANT_INT, (v), NULL }
#define STR(s) { CS2_OP_PUSH_CONSTANT_STRING, 0, (s) }
#define OP(o) { (o), 0, NULL }

/* Run `steps` then RETURN on the fixture's thread, leaving whatever the steps
 * pushed on its stacks. Returns the run's exec code. */
static int
run(struct Fixture* fx, struct Step const* steps, int count)
{
    assert(count + 1 <= (int)(sizeof(fx->opcodes) / sizeof(fx->opcodes[0])));
    CS2VM2_ScriptInit(&fx->script);
    fx->script.script_id = 998216;
    fx->script.op_count = count + 1;
    fx->script.opcodes = fx->opcodes;
    fx->script.int_operands = fx->int_operands;
    fx->script.string_operands = fx->string_operands;
    for( int i = 0; i < count; i++ )
    {
        fx->opcodes[i] = (uint16_t)steps[i].opcode;
        fx->int_operands[i] = steps[i].int_operand;
        fx->string_operands[i] = (char*)steps[i].string_operand;
    }
    fx->opcodes[count] = (uint16_t)CS2_OP_RETURN;
    CS2VM2_PushCallScript(fx->thread, &fx->script);
    if( g_iterator_seed )
    {
        fx->thread->children_iter_parent = g_iterator_seed->parent_id;
        fx->thread->children_iter_index = g_iterator_seed->cursor;
        fx->thread->children_iter_count = g_iterator_seed->count;
        for( int i = 0; i < g_iterator_seed->count; i++ )
            fx->thread->children_iter_indices[i] = g_iterator_seed->ids[i];
    }
    return CS2VM2_RunScript(fx->thread);
}

static int
pop_int(struct Fixture* fx)
{
    int value = INT_MIN;
    CHECK(CS2VM2_PopInt(fx->thread, &value) == CS2VM_EXECNO_OK, "an int was pushed");
    return value;
}

static int32_t
push_layer(struct Fixture* fx, int32_t parent, int component_id)
{
    struct UITreeNodeSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = component_id;
    int32_t const idx = UITree_Push(fx->tree, parent, &spec);
    assert(idx >= 0);
    return idx;
}

/*
 * 216. class332.method7945: keep an entry when the component's param -- or the
 * ParamType default where it has none -- equals the value; keep the order;
 * leave the cursor alone; push the new count.
 */
static void
test_query_refine(void)
{
    struct Fixture fx;
    int const parent_id = (900 << 16) | 3;
    printf("216 if_query_refine\n");

    fixture_init(&fx);
    int32_t const root = push_layer(&fx, -1, (900 << 16) | 0);
    int32_t const parent = push_layer(&fx, root, parent_id);
    for( int sub = 0; sub < 3; sub++ )
    {
        int32_t const child = UITree_CcCreate(fx.tree, parent, parent_id, 5, sub);
        CHECK(child >= 0, "dynamic child %d created", sub);
    }

    struct ToriRS_ParamType* int_param = calloc(1, sizeof(*int_param));
    assert(int_param);
    int_param->id = 50;
    int_param->default_int = 7;
    CacheProvider_ParamAdd(fx.provider, 50, int_param);
    struct ToriRS_ParamType* string_param = calloc(1, sizeof(*string_param));
    assert(string_param);
    string_param->id = 51;
    string_param->is_string = 1;
    CacheProvider_ParamAdd(fx.provider, 51, string_param);

    int const child1 = fx.tree->components[UITree_FindChildBySubid(fx.tree, parent, parent_id, 1)].component_id;
    int const child0 = fx.tree->components[UITree_FindChildBySubid(fx.tree, parent, parent_id, 0)].component_id;
    UITree_ApplyComponentParam(fx.tree, child0, 50, 7, NULL);
    UITree_ApplyComponentParam(fx.tree, child1, 50, 9, NULL);
    UITree_ApplyComponentParam(fx.tree, child1, 51, 0, "a");

    /* Int value 7: child 0 by its entry, child 2 by the default. */
    struct IteratorSeed const seed = { parent_id, 1, 3, { 0, 1, 2 } };
    g_iterator_seed = &seed;
    struct Step const by_int[] = { INT(50), INT(7), INT(0), OP(CS2_OP_IF_QUERY_REFINE) };
    CHECK(run(&fx, by_int, 4) == CS2VM_EXECNO_DONE, "refine by an int runs");
    int count = pop_int(&fx);
    CHECK(count == 2, "two children match 7, one by the default, got %d", count);
    CHECK(fx.thread->children_iter_indices[0] == 0 && fx.thread->children_iter_indices[1] == 2,
        "survivors keep their order: 0 then 2");
    CHECK(fx.thread->children_iter_index == 1, "the walk cursor is not reset");

    /* Null value: only a string param with no entry and no default. */
    struct Step const by_null[] = { INT(51), INT(-1), OP(CS2_OP_IF_QUERY_REFINE) };
    CHECK(run(&fx, by_null, 3) == CS2VM_EXECNO_DONE, "refine by null runs");
    count = pop_int(&fx);
    CHECK(count == 2, "the two children with no string entry are null, got %d", count);

    /* String value, and an int never equals a string. */
    struct Step const by_string[] = { INT(51), STR("a"), INT(2), OP(CS2_OP_IF_QUERY_REFINE) };
    CHECK(run(&fx, by_string, 4) == CS2VM_EXECNO_DONE, "refine by a string runs");
    count = pop_int(&fx);
    CHECK(count == 1, "one child holds \"a\", got %d", count);
    CHECK(fx.thread->children_iter_indices[0] == 1, "and it is child 1");

    /* A long value has no stack here: the script aborts. */
    struct Step const by_long[] = { INT(50), INT(1), OP(CS2_OP_IF_QUERY_REFINE) };
    CHECK(run(&fx, by_long, 3) == CS2VM_EXECNO_ERROR, "a long base type aborts");
    g_iterator_seed = NULL;

    fixture_free(&fx);
}

/* 1506. method8110: the layer, else the mount of the component's interface. */
static void
test_parent_layer(void)
{
    struct Fixture fx;
    printf("1506 cc_getparentlayer\n");

    fixture_init(&fx);
    int32_t const top = push_layer(&fx, -1, (161 << 16) | 0);
    int32_t const mount = push_layer(&fx, top, (161 << 16) | 5);
    int32_t const root = push_layer(&fx, mount, (900 << 16) | 0);
    (void)push_layer(&fx, root, (900 << 16) | 1);

    struct Step const get[] = { OP(CS2_OP_CC_GETPARENTLAYER) };
    CS2VM2_SetActiveAndDotComponentId(fx.thread, (900 << 16) | 1);
    CHECK(run(&fx, get, 1) == CS2VM_EXECNO_DONE, "runs");
    CHECK(pop_int(&fx) == ((900 << 16) | 0), "a component with a layer answers its layer");

    CS2VM2_SetActiveAndDotComponentId(fx.thread, (900 << 16) | 0);
    CHECK(run(&fx, get, 1) == CS2VM_EXECNO_DONE, "runs");
    CHECK(pop_int(&fx) == ((161 << 16) | 5), "an interface root answers the component it is mounted into");

    CS2VM2_SetActiveAndDotComponentId(fx.thread, (161 << 16) | 0);
    CHECK(run(&fx, get, 1) == CS2VM_EXECNO_DONE, "runs");
    CHECK(pop_int(&fx) == -1, "a top-level interface answers -1");

    fixture_free(&fx);
}

/* 4127. Statics.java:52960, Integer.parseInt's rules. */
static void
test_safe_parse_int(void)
{
    struct Case
    {
        char const* text;
        int value;
        int ok;
    } const cases[] = {
        { "123", 123, 1 },
        { "+5", 5, 1 },
        { "-2147483648", INT_MIN, 1 },
        { "2147483647", INT_MAX, 1 },
        { "2147483648", 0, 0 },
        { " 5", 0, 0 },
        { "12a", 0, 0 },
        { "", 0, 0 },
        { "-", 0, 0 },
        { "0x10", 0, 0 },
    };
    printf("4127 safeparseint\n");
    for( size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++ )
    {
        struct Fixture fx;
        fixture_init(&fx);
        struct Step const steps[] = { STR(cases[i].text), OP(CS2_OP_SAFEPARSEINT) };
        CHECK(run(&fx, steps, 2) == CS2VM_EXECNO_DONE, "\"%s\" runs", cases[i].text);
        int const ok = pop_int(&fx);
        int const value = pop_int(&fx);
        CHECK(ok == cases[i].ok && value == cases[i].value,
            "\"%s\" -> (%d, %d), want (%d, %d)", cases[i].text, value, ok, cases[i].value, cases[i].ok);
        fixture_free(&fx);
    }
}

/* 7451, 7453..7456, 7460. The native client's ExecuteCommand7400To7499. */
static void
test_minimenu_indexed(void)
{
    struct Fixture fx;
    struct RS_ClientOpContext cancel;
    struct RS_ClientOpContext loc;
    struct RS_ClientOpContext player;
    printf("7451..7460 indexed minimenu\n");

    fixture_init(&fx);
    memset(&cancel, 0, sizeof(cancel));
    cancel.kind = -1;
    memset(&loc, 0, sizeof(loc));
    loc.kind = RS_CLIENTOP_LOC;
    loc.type = 1276;
    loc.coord = RS_CLIENTOP_COORD(0, 3200, 3200);
    memset(&player, 0, sizeof(player));
    player.kind = RS_CLIENTOP_PLAYER;
    player.uid = 42;
    RS_ClientOpMenuEntrySet(&fx.host.clientop, 0, RS_MINIMENU_TYPE_NONE, &cancel);
    RS_ClientOpMenuEntrySet(&fx.host.clientop, 1, RS_MINIMENU_TYPE_LOC, &loc);
    RS_ClientOpMenuEntrySet(&fx.host.clientop, 2, RS_MINIMENU_TYPE_PLAYER, &player);
    fx.host.clientop.menu_entry_count = 3;
    fx.host.clientop.menu_hovered_index = 2;

    struct Step const type_at_1[] = { INT(1), OP(CS2_OP_MINIMENU_TYPEAT) };
    run(&fx, type_at_1, 2);
    CHECK(pop_int(&fx) == RS_MINIMENU_TYPE_LOC, "typeat(1) is the loc row's type");
    struct Step const type_at_9[] = { INT(9), OP(CS2_OP_MINIMENU_TYPEAT) };
    run(&fx, type_at_9, 2);
    CHECK(pop_int(&fx) == 0, "typeat out of range is 0");

    struct Step const loc_at_1[] = { INT(1), OP(CS2_OP_MINIMENU_FINDLOCAT) };
    run(&fx, loc_at_1, 2);
    CHECK(pop_int(&fx) == 1, "findlocat(1) finds the loc");
    CHECK(fx.host.clientop.active[RS_CLIENTOP_LOC].type == 1276, "and makes it the active loc");

    struct Step const loc_at_2[] = { INT(2), OP(CS2_OP_MINIMENU_FINDLOCAT) };
    run(&fx, loc_at_2, 2);
    CHECK(pop_int(&fx) == 0, "findlocat on a player row is 0");
    CHECK(fx.host.clientop.active[RS_CLIENTOP_LOC].type == 1276, "and leaves the active loc alone");

    struct Step const npc_at_1[] = { INT(1), OP(CS2_OP_MINIMENU_FINDNPCAT) };
    run(&fx, npc_at_1, 2);
    CHECK(pop_int(&fx) == 0, "findnpcat on a loc row is 0");

    struct Step const player_at_2[] = { INT(2), OP(CS2_OP_MINIMENU_FINDPLAYERAT) };
    run(&fx, player_at_2, 2);
    CHECK(pop_int(&fx) == 1, "findplayerat(2) finds the player");
    CHECK(fx.host.clientop.active[RS_CLIENTOP_PLAYER].uid == 42, "and makes it the active player");

    struct Step const hovered[] = { OP(CS2_OP_MINIMENU_HOVERED_INDEX) };
    run(&fx, hovered, 1);
    CHECK(pop_int(&fx) == 2, "hovered_index answers the popup's hovered row");

    fixture_free(&fx);
}

/* 7800..7820 with no hiscores transport: a lookup fails, the getters say no data. */
static void
test_hiscores(void)
{
    struct Fixture fx;
    printf("7800..7820 hiscores\n");

    fixture_init(&fx);
    struct Step const status[] = { OP(CS2_OP_HISCORE_GETSTATUS) };
    run(&fx, status, 1);
    CHECK(pop_int(&fx) == 0, "status is 0 before any lookup");

    struct Step const lookup[] = { STR("bob"), INT(0), OP(CS2_OP_HISCORE_LOOKUP) };
    CHECK(run(&fx, lookup, 3) == CS2VM_EXECNO_DONE, "a lookup runs");
    CHECK(fx.thread->ints_stack_top == 0 && fx.thread->strs_stack_top == 0, "consuming its name and type");
    run(&fx, status, 1);
    CHECK(pop_int(&fx) == 3, "and fails: status 3");

    struct Step const skill_rank[] = { INT(0), OP(CS2_OP_HISCORE_GETSKILLRANK) };
    run(&fx, skill_rank, 2);
    CHECK(pop_int(&fx) == -1, "a skill rank with no data is -1");

    struct Step const overall_xp[] = { OP(CS2_OP_HISCORE_GETOVERALLXP) };
    run(&fx, overall_xp, 1);
    CHECK(pop_int(&fx) == -1 && pop_int(&fx) == -1, "overall xp with no data is -1, -1");

    struct Step const clear[] = { OP(CS2_OP_HISCORE_CLEAR) };
    run(&fx, clear, 1);
    run(&fx, status, 1);
    CHECK(pop_int(&fx) == 0, "clear resets the status");

    fixture_free(&fx);
}

/* 8020. Statics.java:57736: the keys, and a script error on a type mismatch. */
static void
test_enum_get_inputs(void)
{
    struct Fixture fx;
    printf("8020 enum_getinputs\n");

    fixture_init(&fx);
    struct ToriRS_Enum* e = calloc(1, sizeof(*e));
    assert(e);
    e->id = 77;
    e->input_type = 'i';
    e->count = 3;
    e->keys = malloc(3 * sizeof(int));
    e->int_values = calloc(3, sizeof(int));
    assert(e->keys);
    assert(e->int_values);
    e->keys[0] = 3;
    e->keys[1] = 1;
    e->keys[2] = 4;
    CacheProvider_EnumAdd(fx.provider, 77, e);

    struct Step const inputs[] = { INT('i'), INT(77), OP(CS2_OP_ENUM_GETINPUTS), OP(CS2_OP_ARRAY_SIZE) };
    CHECK(run(&fx, inputs, 4) == CS2VM_EXECNO_DONE, "enum_getinputs runs");
    CHECK(pop_int(&fx) == 3, "and its array holds the three keys");

    struct Step const inputs_again[] = { INT('i'), INT(77), OP(CS2_OP_ENUM_GETINPUTS) };
    run(&fx, inputs_again, 3);
    char* handle = NULL;
    CHECK(CS2VM2_PopStr(fx.thread, &handle) == CS2VM_EXECNO_OK, "an array handle was pushed");
    struct CS2VM2_Array const* array = (struct CS2VM2_Array const*)handle;
    CHECK(array->size == 3 && array->cells.ints[0] == 3 && array->cells.ints[1] == 1 &&
              array->cells.ints[2] == 4,
        "in the enum's own order");

    struct Step const wrong_type[] = { INT('o'), INT(77), OP(CS2_OP_ENUM_GETINPUTS) };
    CHECK(run(&fx, wrong_type, 3) == CS2VM_EXECNO_ERROR, "a mismatched input type aborts the script");

    fixture_free(&fx);
}

/* 3630 + 3639 through dispatch: the chain reaches the store and apply sorts. */
static void
test_friend_sort_dispatch(void)
{
    struct Fixture fx;
    struct RS_Social social;
    int filter_modes[3] = { 0, 0, 0 };
    printf("3630/3639 friendlist sort through dispatch\n");

    fixture_init(&fx);
    RS_Social_Init(&social);
    RS_Social_AddFriend(&social, "amy", 1);
    RS_Social_AddFriend(&social, "zed", 1);
    RS_CS2Host_SetSocial(&fx.host, &social, filter_modes, 1);

    struct Step const sort[] = {
        OP(CS2_OP_FRIENDLIST_SORT_RESET),
        INT(0),
        OP(CS2_OP_FRIENDLIST_SORT_NAME),
        OP(CS2_OP_FRIENDLIST_SORT_APPLY),
    };
    CHECK(run(&fx, sort, 4) == CS2VM_EXECNO_DONE, "reset, name descending, apply runs");
    CHECK(fx.thread->ints_stack_top == 0, "the comparator consumed its boolean");
    CHECK(strcmp(social.friend_name[0], "zed") == 0, "the list is sorted by name descending");

    fixture_free(&fx);
}

/* 3903-3913 through dispatch: the slot UPDATE_STOCKMARKET_SLOT stored. */
static void
test_stockmarket_offer(void)
{
    struct Fixture fx;
    printf("3903-3913 Grand Exchange offer getters\n");

    fixture_init(&fx);
    fx.host.stockmarket[3] = (struct RS_CS2StockmarketOffer){
        .status = 8 | 5, .obj = 4151, .price = 1200, .count = 10, .completed_count = 4,
        .completed_gold = 4800,
    };
    struct Step const read[] = {
        INT(3), OP(CS2_OP_STOCKMARKET_GETOFFERTYPE),
        INT(3), OP(CS2_OP_STOCKMARKET_GETOFFERITEM),
        INT(3), OP(CS2_OP_STOCKMARKET_GETOFFERCOMPLETEDGOLD),
        INT(3), OP(CS2_OP_STOCKMARKET_ISOFFERFINISHED),
        INT(0), OP(CS2_OP_STOCKMARKET_ISOFFEREMPTY),
    };
    CHECK(run(&fx, read, 10) == CS2VM_EXECNO_DONE, "the getters run");
    CHECK(pop_int(&fx) == 1, "slot 0 is empty");
    CHECK(pop_int(&fx) == 1, "state 5 is finished");
    CHECK(pop_int(&fx) == 4800, "completed gold");
    CHECK(pop_int(&fx) == 4151, "item");
    CHECK(pop_int(&fx) == 1, "status bit 8 is a sell offer");

    struct Step const outside[] = { INT(8), OP(CS2_OP_STOCKMARKET_GETOFFERPRICE) };
    CHECK(run(&fx, outside, 2) == CS2VM_EXECNO_ERROR, "slot 8 aborts the script, as the client throws");
    fixture_free(&fx);
}

/* 3914-3926: the list UPDATE_TRADINGPOST carries, sorted stably. */
static void
test_trading_post(void)
{
    struct Fixture fx;
    printf("3914-3926 trading post\n");

    fixture_init(&fx);
    fx.host.map_world = 302;
    uint8_t payload[256];
    int n = 0;
#define P1(v) (payload[n++] = (uint8_t)(v))
#define P2(v) (P1((v) >> 8), P1(v))
#define P4(v) (P2((int)((uint32_t)(v) >> 16)), P2(v))
#define P8(v) (P4((int)((uint64_t)(v) >> 32)), P4((int)(v)))
#define PSTR(t) do { for( char const* c_ = (t); *c_; c_++ ) P1(*c_); P1(0); } while( 0 )
    P1(1);
    P8(1000000);
    P2(995);
    P1(1);
    P2(3);
    PSTR("carl"); PSTR(""); P2(301); P8(1000000 - 61000); P4(5); P4(100);
    PSTR("anna"); PSTR("old"); P2(302); P8(1000000); P4(9); P4(50);
    PSTR("bert"); PSTR(""); P2(303); P8(1000000); P4(5); P4(75);
    CHECK(RS_CS2Host_ApplyTradingPost(&fx.host, payload, n, 5000), "the list decodes");

    fx.host.client.now_ms = 5000 + 3723000; /* 1h 2m 3s after the list arrived */
    struct Step const read[] = {
        OP(CS2_OP_TRADINGPOST_GETTOTALOFFERS),
        INT(1), OP(CS2_OP_TRADINGPOST_GETOFFERNAME),
        INT(1), OP(CS2_OP_TRADINGPOST_GETOFFERPREVIOUSNAME),
        INT(0), OP(CS2_OP_TRADINGPOST_GETOFFERAGE),
        INT(2), OP(CS2_OP_TRADINGPOST_GETOFFERITEM),
    };
    CHECK(run(&fx, read, 9) == CS2VM_EXECNO_DONE, "the getters run");
    CHECK(pop_int(&fx) == 995, "every offer answers the list's item");
    char* text = NULL;
    CHECK(CS2VM2_PopStr(fx.thread, &text) == CS2VM_EXECNO_OK && text &&
              strcmp(text, "1:03:04") == 0,
        "age is h:mm:ss on the server clock, got %s", text ? text : "(null)");
    CHECK(CS2VM2_PopStr(fx.thread, &text) == CS2VM_EXECNO_OK && text && strcmp(text, "old") == 0,
        "previous name");
    CHECK(CS2VM2_PopStr(fx.thread, &text) == CS2VM_EXECNO_OK && text && strcmp(text, "anna") == 0,
        "name");
    CHECK(pop_int(&fx) == 3, "three offers");

    /* Price ascending keeps carl before bert (both 5): the sort is stable. */
    struct Step const by_price[] = { INT(1), OP(CS2_OP_TRADINGPOST_SORTBY_PRICE) };
    CHECK(run(&fx, by_price, 2) == CS2VM_EXECNO_DONE, "sort by price");
    CHECK(strcmp(fx.host.trading_post.offers[0].name, "carl") == 0 &&
              strcmp(fx.host.trading_post.offers[1].name, "bert") == 0 &&
              strcmp(fx.host.trading_post.offers[2].name, "anna") == 0,
        "stable ascending price order");

    /* World ascending with the own world first. */
    struct Step const by_world[] = { INT(1), INT(1), OP(CS2_OP_TRADINGPOST_SORTFILTERBY_WORLD) };
    CHECK(run(&fx, by_world, 3) == CS2VM_EXECNO_DONE, "sort by world");
    CHECK(fx.host.trading_post.offers[0].world == 302 && fx.host.trading_post.offers[1].world == 301,
        "own world 302 first, then ascending");

    struct Step const outside[] = { INT(3), OP(CS2_OP_TRADINGPOST_GETOFFERWORLD) };
    CHECK(run(&fx, outside, 2) == CS2VM_EXECNO_ERROR, "an index past the list aborts");

    P1(0);
    CHECK(RS_CS2Host_ApplyTradingPost(&fx.host, payload + n - 1, 1, 5000), "absent decodes");
    struct Step const total[] = { OP(CS2_OP_TRADINGPOST_GETTOTALOFFERS) };
    CHECK(run(&fx, total, 1) == CS2VM_EXECNO_DONE && pop_int(&fx) == 0, "no list is zero offers");
#undef P1
#undef P2
#undef P4
#undef P8
#undef PSTR
    fixture_free(&fx);
}

/* 3890 / core 76: the varclan profile read through dispatch. */
static void
test_clan_reads(void)
{
    struct Fixture fx;
    printf("3850/3852/3890 clan reads through dispatch\n");

    fixture_init(&fx);
    struct Step const none[] = { OP(CS2_OP_CLANPROFILE_FIND), INT(0), OP(CS2_OP_ACTIVECLANCHANNEL_FIND_AFFINED) };
    CHECK(run(&fx, none, 3) == CS2VM_EXECNO_DONE, "finds run with nothing received");
    CHECK(pop_int(&fx) == 0, "no affined channel");
    CHECK(pop_int(&fx) == 0, "no clan profile");

    uint8_t const channel[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 5, 'C', 0, 0, 2, 3, 0, 1,
        'a', 'm', 'y', 0, 7, 1, 0x2d,
    };
    CHECK(RS_ClanStore_ApplyChannelFull(&fx.host.clan, channel, (int)sizeof(channel)), "channel decodes");
    struct Step const read[] = {
        INT(0), OP(CS2_OP_ACTIVECLANCHANNEL_FIND_AFFINED),
        OP(CS2_OP_ACTIVECLANCHANNEL_GETUSERCOUNT),
        INT(0), OP(CS2_OP_ACTIVECLANCHANNEL_GETUSERWORLD),
    };
    CHECK(run(&fx, read, 5) == CS2VM_EXECNO_DONE, "the channel reads run");
    CHECK(pop_int(&fx) == 301, "user world");
    CHECK(pop_int(&fx) == 1, "one user");
    CHECK(pop_int(&fx) == 1, "affined 0 found");

    struct Step const unfound[] = { OP(CS2_OP_ACTIVECLANSETTINGS_GETCLANNAME) };
    CHECK(run(&fx, unfound, 1) == CS2VM_EXECNO_ERROR, "a settings getter with none found aborts");
    fixture_free(&fx);
}

int
main(void)
{
    test_query_refine();
    test_parent_layer();
    test_safe_parse_int();
    test_minimenu_indexed();
    test_hiscores();
    test_enum_get_inputs();
    test_friend_sort_dispatch();
    test_stockmarket_offer();
    test_trading_post();
    test_clan_reads();

    if( g_fail )
    {
        printf("rs_cs2_client_parity_ops_test: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("rs_cs2_client_parity_ops_test: all checks passed\n");
    return 0;
}
