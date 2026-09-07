/*
 * RS_CS2_PumpTransmits' early-return guard, as a test.
 *
 * Why this file exists. The pump is the only thing that turns a server-side
 * change into a re-run of the clientscript hooks that repaint a panel, and its
 * guard was, for a long time, a hand-written condition list that had drifted
 * from the clear-down at the bottom of the same function:
 *
 *     if( !host->widgets_loaded_dirty && !host->var_transmit_dirty &&
 *         !host->inv_transmit_dirty && !host->misc_transmit_dirty &&
 *         !host->friend_transmit_dirty )
 *         return;
 *
 * `stat_transmit_dirty` is missing from that list and is cleared at the bottom.
 * The stat branch landed after the guard was written; `misc` and `friend` were
 * each appended later, and each appended only itself.
 *
 * The consequence is specific and was expensive to find. A tick carrying only
 * UPDATE_STAT — no varp, no container, no run-energy change, which is exactly
 * what an xp gain is — took the early return. The dispatch never ran, and
 * because the clear-down is *past* the return the flag was never cleared: it
 * sat set until some unrelated change happened to open the guard, at which
 * point one dispatch fired for however many stat changes had accumulated.
 * Measured on the real client before the fix: 447 consecutive ticks held on a
 * single `::setlevel`, and two stat changes delivered as one dispatch.
 *
 * Downstream, interface 122 (the XP-drop panel) drew nothing at all. That was
 * attributed for two lanes to a "non-terminating varc queue-shift loop in
 * clientscript 1004" — a CS2 VM bug that does not exist. Script 1004's loop is
 * 7-bounded by construction and is on the *timer* path, which a stat transmit
 * never enters. The defect was four missing characters in the line above.
 *
 * So what is checked here is the property the guard actually has to have, and
 * it is checked one flag at a time, because that is the only shape in which the
 * bug is visible: with any second flag also set, the guard opens and everything
 * downstream looks correct.
 *
 *   1. Each dirty flag, set ALONE, opens the guard.
 *   2. Each dirty flag, set ALONE, is cleared by the pump — the starvation half.
 *      A flag that survives its own pump is one that will accumulate.
 *   3. stat_transmit_dirty specifically reaches exactly its queued dispatch
 *      task — not the wildcard var dispatch that used to fan a stat-only XP
 *      update out through every visible var listener — driven
 *      through RS_CS2Host_NotifyStatChanged rather than by poking the field, so
 *      that the notify -> flag -> guard -> task chain is covered end to end.
 *      This is the XP-drop channel.
 *   4. The flag count is pinned, so that adding another dirty flag to the
 *      pump's table fails here until a case for it is added above. It has
 *      earned its keep once already: `chat_transmit_dirty` reached the pump's
 *      drain and its dispatch without reaching the GUARD, so a chat message
 *      arriving on an otherwise-quiet tick left the chatbox unredrawn until
 *      some unrelated transmit happened to open the early return.
 *
 * No cache files and no server: the pump path reads neither. The inventory
 * regression below uses only capacities inserted into the in-memory provider.
 */

#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"
#include "cs2vm2/cs2_opcode.h"
#include "3rd/rscache/src/datatypes/clientscript.h"
#include "engine/cache_provider.h"
#include "engine/uitree_anim.h"
#include "engine/torirs_component_from_rscache.h"
#include "engine/uitree_from_component.h"
#include "ui/uitree_build.h"
#include "render/torirs_frame.h"
#include "ui/uitree_emit.h"
#include "ui/uitree_layout.h"
#include "toridraw_model.h"
#include "toridraw_model_transform.h"
#include "toridraw_animation.h"
#include "engine/dat2/dat2_buildcache.h"
#include "game/rs_cs2_dispatch.h"
#include "game/rs_cs2_host.h"
#include "game/task_cs2_run.h"
#include "inv/inv_manager.h"
#include "task_runner.h"
#include "ui/uitree.h"
#include "varp/varp_manager.h"

#include <assert.h>
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

/* ==========================================================================
 * Harness
 * ========================================================================== */

struct Fixture
{
    struct UITree* tree;
    struct Dat2BuildCache* bc;
    struct InvManager invs;
    struct RS_CS2Host host;
    struct TaskRunner runner;
};

static void
fixture_init(struct Fixture* fx)
{
    struct CacheProvider* provider;

    memset(fx, 0, sizeof(*fx));
    fx->tree = UITree_New(256);
    fx->bc = dat2_buildcache_new();
    provider = dat2_buildcache_as_provider(fx->bc);
    struct RSCache profile;
    if( !RSCache_ProfileByName("osrs239", &profile) ) abort();
    CacheProvider_SetProfile(provider, &profile);
    InvManager_Init(&fx->invs);
    RS_CS2Host_Init(&fx->host, fx->tree, provider, &fx->invs, NULL, NULL, NULL);

    /* The pump reads runner->queue and nothing else — it enqueues dispatch
     * tasks, it does not run them. Leaving io/px NULL keeps this test off the
     * platform layer entirely; TaskRunner_Step is never called. */
    fx->runner.queue = ToriRS_TaskQueue_New();
}

static void
fixture_free(struct Fixture* fx)
{
    ToriRS_TaskQueue_Free(fx->runner.queue);
    RS_CS2Host_Free(&fx->host);
    InvManager_Free(&fx->invs);
    UITree_Free(fx->tree);
    dat2_buildcache_free(fx->bc);
}

/* Drop everything the pump queued, so the next case starts from an empty
 * queue and its own count is meaningful. */
static void
fixture_drain_queue(struct Fixture* fx)
{
    ToriRS_TaskQueue_Free(fx->runner.queue);
    fx->runner.queue = ToriRS_TaskQueue_New();
}

static int
queued_count(struct Fixture const* fx)
{
    struct ToriRS_Task const* t;
    int n = 0;
    for( t = fx->runner.queue->head; t; t = t->next )
        n++;
    return n;
}

static int
queued_named_count(
    struct Fixture const* fx,
    char const* name)
{
    struct ToriRS_Task const* t;
    int n = 0;
    for( t = fx->runner.queue->head; t; t = t->next )
    {
        if( strcmp(t->name, name) == 0 )
            n++;
    }
    return n;
}

/* Clear every dirty flag by hand, so a case starts from a genuinely quiet
 * host rather than from whatever the previous case left behind. Deliberately
 * NOT done by calling the pump: the pump's clear-down is one of the things
 * under test, and using it to set up would let a broken clear-down hide. */
static void
host_quiesce(struct RS_CS2Host* host)
{
    host->widgets_loaded_dirty = 0;
    host->var_transmit_dirty = 0;
    host->var_changed_count = 0;
    host->var_changed_all = 0;
    host->inv_transmit_dirty = 0;
    host->inv_changed_count = 0;
    host->inv_changed_all = 0;
    host->stat_transmit_dirty = 0;
    host->stat_changed_count = 0;
    host->stat_changed_all = 0;
    host->misc_transmit_dirty = 0;
    host->friend_transmit_dirty = 0;
    host->chat_transmit_dirty = 0;
}

/* ==========================================================================
 * Part 1 — a quiet tick is free
 * ========================================================================== */

static void
test_quiet_tick(void)
{
    struct Fixture fx;

    printf("pump: a tick with nothing dirty queues nothing\n");

    fixture_init(&fx);
    host_quiesce(&fx.host);

    CHECK(RS_CS2_TransmitsPending(&fx.host) == 0, "a quiet host reports no pending transmits");
    RS_CS2_PumpTransmits(&fx.host, &fx.runner);
    CHECK(queued_count(&fx) == 0, "and the pump queues no tasks, got %d", queued_count(&fx));

    fixture_free(&fx);
}

/* ==========================================================================
 * Inventory/equipment onLoad must see their type capacities before the first
 * UPDATE_INV_FULL.
 *
 * Interface group 387's equipment slots are not static inventory widgets.
 * Component 15's onLoad script 3281 starts with `inv_size(inv_94)` and loops
 * over that result; each iteration creates the three children for one slot and
 * registers script 545 as its onInvTransmit listener. If a fresh host reports
 * zero here, 3281 returns without registering anything, so the later
 * container-94 packet has no listener it can wake. The visible result is the
 * cache's untouched `*` operations and no slot graphics.
 * ========================================================================== */

static void
test_standard_sizes_exist_before_first_packet(void)
{
    struct Fixture fx;
    struct CS2VM2 vm;
    struct CS2VM2_Thread* thread;
    struct CS2VM_HostRequest request = { 0 };
    struct CacheProvider* provider;
    int size = -1;

    printf("pump: sidebar onLoads see type capacities before the first packet\n");

    fixture_init(&fx);
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, &fx.host, RS_CS2Host_Exec);
    thread = CS2VM2_ThreadMain(&vm);
    provider = dat2_buildcache_as_provider(fx.bc);

    CHECK(
        fx.invs.source_count == 0 && fx.invs.container_count == 0,
        "the host starts with zero live inventory sources and containers");

    request.kind = CS2VM_HOST_REQUEST_INV_SIZE;
    request.u.INV_SIZE.inv_id = INV_MANAGER_CONTAINER_BACKPACK;

    CHECK(
        RS_CS2Host_Exec(thread, &request) == CS2VM_EXECNO_YIELD,
        "uncached INV_SIZE(93) yields once for its InvType");
    CacheProvider_InvtypeAdd(provider, INV_MANAGER_CONTAINER_BACKPACK, 28);
    CHECK(
        RS_CS2Host_Exec(thread, &request) == CS2VM_EXECNO_OK,
        "INV_SIZE(93) completes after its InvType arrives");
    CHECK(
        CS2VM2_PopInt(thread, &size) == CS2VM_EXECNO_OK,
        "INV_SIZE(93) pushes a result");
    CHECK(
        size == 28,
        "INV_SIZE(93) is the 28-slot type capacity, got %d",
        size);

    request.u.INV_SIZE.inv_id = INV_MANAGER_CONTAINER_WORN;

    CHECK(
        RS_CS2Host_Exec(thread, &request) == CS2VM_EXECNO_YIELD,
        "uncached INV_SIZE(94) yields once for its InvType");
    CacheProvider_InvtypeAdd(provider, INV_MANAGER_CONTAINER_WORN, 14);
    CHECK(
        RS_CS2Host_Exec(thread, &request) == CS2VM_EXECNO_OK,
        "INV_SIZE(94) completes after its InvType arrives");
    CHECK(
        CS2VM2_PopInt(thread, &size) == CS2VM_EXECNO_OK,
        "INV_SIZE(94) pushes a result");
    CHECK(
        size == 14,
        "INV_SIZE(94) is the 14-slot type capacity, got %d",
        size);
    CHECK(
        fx.invs.source_count == 0 && fx.invs.container_count == 0,
        "InvType lookups leave the live inventory manager completely unseeded");

    CS2VM2_Free(&vm);
    fixture_free(&fx);
}

/* ==========================================================================
 * Part 2 — every flag, ALONE, opens the guard and is consumed
 *
 * One case per dirty flag the pump's table lists. `setter` sets ONLY that flag,
 * `is_set` reads it back. The pinned count in Part 4 is what makes forgetting
 * to add a case here loud.
 * ========================================================================== */

struct FlagCase
{
    char const* name;
    void (*set)(struct RS_CS2Host* host);
    int (*get)(struct RS_CS2Host const* host);
};

static void
set_widgets(struct RS_CS2Host* h)
{
    h->widgets_loaded_dirty = 1;
}
static int
get_widgets(struct RS_CS2Host const* h)
{
    return h->widgets_loaded_dirty;
}

static void
set_var(struct RS_CS2Host* h)
{
    h->var_transmit_dirty = 1;
    h->var_changed_all = 1;
}
static int
get_var(struct RS_CS2Host const* h)
{
    return h->var_transmit_dirty;
}

static void
set_inv(struct RS_CS2Host* h)
{
    h->inv_transmit_dirty = 1;
    h->inv_changed_all = 1;
}
static int
get_inv(struct RS_CS2Host const* h)
{
    return h->inv_transmit_dirty;
}

static void
set_stat(struct RS_CS2Host* h)
{
    h->stat_transmit_dirty = 1;
    h->stat_changed_all = 1;
}
static int
get_stat(struct RS_CS2Host const* h)
{
    return h->stat_transmit_dirty;
}

static void
set_misc(struct RS_CS2Host* h)
{
    h->misc_transmit_dirty = 1;
}
static int
get_misc(struct RS_CS2Host const* h)
{
    return h->misc_transmit_dirty;
}

static void
set_friend(struct RS_CS2Host* h)
{
    h->friend_transmit_dirty = 1;
}
static int
get_friend(struct RS_CS2Host const* h)
{
    return h->friend_transmit_dirty;
}

static void
set_chat(struct RS_CS2Host* h)
{
    h->chat_transmit_dirty = 1;
}

static int
get_chat(struct RS_CS2Host const* h)
{
    return h->chat_transmit_dirty;
}

static struct FlagCase const g_flag_cases[] = {
    { "widgets_loaded_dirty", set_widgets, get_widgets },
    { "chat_transmit_dirty", set_chat, get_chat },
    { "var_transmit_dirty", set_var, get_var },
    { "inv_transmit_dirty", set_inv, get_inv },
    { "stat_transmit_dirty", set_stat, get_stat },
    { "misc_transmit_dirty", set_misc, get_misc },
    { "friend_transmit_dirty", set_friend, get_friend },
};

static void
test_each_flag_alone(void)
{
    size_t i;

    printf("pump: each dirty flag, set ALONE, opens the guard and is consumed\n");

    for( i = 0; i < sizeof(g_flag_cases) / sizeof(g_flag_cases[0]); i++ )
    {
        struct FlagCase const* c = &g_flag_cases[i];
        struct Fixture fx;

        fixture_init(&fx);
        host_quiesce(&fx.host);
        fixture_drain_queue(&fx);

        c->set(&fx.host);

        /* The guard. This is the assertion `stat` failed for months: with any
         * other flag also set it passes regardless, which is why every case
         * here sets exactly one. */
        CHECK(
            RS_CS2_TransmitsPending(&fx.host) != 0,
            "%s alone opens the early-return guard",
            c->name);

        RS_CS2_PumpTransmits(&fx.host, &fx.runner);

        /* The starvation half: an unconsumed flag accumulates behind the guard
         * and eventually delivers a merged, mistimed dispatch. */
        CHECK(c->get(&fx.host) == 0, "%s alone is cleared by the pump", c->name);
        CHECK(
            RS_CS2_TransmitsPending(&fx.host) == 0,
            "%s alone leaves the host quiet afterwards",
            c->name);
        CHECK(
            queued_count(&fx) == (i == 0 ? 3 : 1),
            "%s alone queues only its dispatch%s, got %d",
            c->name,
            i == 0 ? "es (inv + var + stat on unhide)" : "",
            queued_count(&fx));

        fixture_free(&fx);
    }
}

static void
test_widgets_loaded_queues_stat_unhide(void)
{
    struct Fixture fx;
    struct UITreeNodeSpec spec = { 0 };
    struct ToriRS_IO* io;
    int32_t listener;

    printf("pump: reopening the XP tracker resumes a stat update received while hidden\n");
    fixture_init(&fx);
    host_quiesce(&fx.host);
    fixture_drain_queue(&fx);

    spec.type = UIELEM_RS_LAYER;
    spec.component_id = (728 << 16) | 3;
    listener = UITree_Push(fx.tree, -1, &spec);
    CHECK(listener >= 0, "XP tracker listener component");
    fx.tree->components[listener].behavior.hide = 1;
    /* The registry is heap-grown now, so a test that pokes slot 0 directly has
     * to give itself the slot first. */
    fx.host.stat_transmit_hooks = calloc(1, sizeof(*fx.host.stat_transmit_hooks));
    assert(fx.host.stat_transmit_hooks);
    fx.host.stat_transmit_hook_cap = 1;
    fx.host.stat_transmit_hook_count = 1;
    fx.host.stat_transmit_hooks[0].component_id = spec.component_id;
    fx.host.stat_transmit_hooks[0].ref = UITree_RefAt(fx.tree, listener);
    fx.host.stat_transmit_hooks[0].script_id = 5451;

    RS_CS2Host_NotifyStatChanged(&fx.host, 0);
    RS_CS2_PumpTransmits(&fx.host, &fx.runner);
    io = ToriRS_IO_New();
    CHECK(
        ToriRS_TaskQueue_Run(fx.runner.queue, io) == TORIRS_ASYNCIO_STAT_DONE,
        "the hidden stat dispatch drains without running its clientscript");
    ToriRS_IO_Free(io);
    CHECK(
        fx.host.stat_transmit_hooks[0].pending_unhide == 1,
        "the hidden skill update remains pending");

    fx.tree->components[listener].behavior.hide = 0;
    fx.host.widgets_loaded_dirty = 1;
    RS_CS2_PumpTransmits(&fx.host, &fx.runner);

    CHECK(
        queued_named_count(&fx, "CS2StatTransmitUnhideDispatch") == 1,
        "widgets-loaded queues the stat unhide dispatch");
    fixture_free(&fx);
}

/* ==========================================================================
 * Part 3 — the XP-drop channel, end to end through the real notify
 * ========================================================================== */

static void
test_stat_notify_reaches_a_dispatch(void)
{
    struct Fixture fx;
    int const stat_attack = 0;

    printf("pump: a stat change alone reaches a queued dispatch (the XP-drop channel)\n");

    fixture_init(&fx);
    host_quiesce(&fx.host);
    fixture_drain_queue(&fx);

    /* Driven through the notify the packet layer actually calls
     * (rs_gameproto_exec.c calls this on every valid UPDATE_STAT) rather than
     * by poking the field, so the whole notify -> flag -> guard -> task chain
     * is covered. `stat_attack` is a wire slot index, not a game id: the test
     * needs "some skill changed", and which one is irrelevant here. */
    RS_CS2Host_NotifyStatChanged(&fx.host, stat_attack);

    CHECK(fx.host.stat_transmit_dirty != 0, "NotifyStatChanged marks the host dirty");
    CHECK(
        RS_CS2_TransmitsPending(&fx.host) != 0,
        "and a stat-only tick opens the guard — no varp, no container, no run energy");

    RS_CS2_PumpTransmits(&fx.host, &fx.runner);

    CHECK(queued_count(&fx) == 1, "the pump queues one dispatch, got %d tasks", queued_count(&fx));
    CHECK(
        queued_named_count(&fx, "CS2StatTransmitDispatch") == 1,
        "the queued task is the stat dispatch");
    CHECK(
        queued_named_count(&fx, "CS2VarTransmitDispatch") == 0,
        "a stat-only XP update does not fan out through every var hook");
    CHECK(fx.host.stat_transmit_dirty == 0, "and consumes the flag rather than accumulating it");

    /* A second, immediately following quiet tick must not re-dispatch: that is
     * what tells the two halves (guard key, clear-down entry) apart. */
    fixture_drain_queue(&fx);
    RS_CS2_PumpTransmits(&fx.host, &fx.runner);
    CHECK(queued_count(&fx) == 0, "the next quiet tick queues nothing, got %d", queued_count(&fx));

    fixture_free(&fx);
}

/* ==========================================================================
 * Part 4 — the count is pinned
 * ========================================================================== */

static void
test_flag_count_pinned(void)
{
    size_t const cases = sizeof(g_flag_cases) / sizeof(g_flag_cases[0]);

    printf("pump: the dirty-flag table and the cases above are the same size\n");

    /*
     * If this fires, a dirty flag was added to rs_cs2_dirty_flags() in
     * rs_cs2_dispatch.c and no case was added to g_flag_cases. Add one. The
     * whole reason the table exists is that this family of flags is added to
     * one at a time, by different people, months apart — the guard, the
     * clear-down and this test all drifted independently before, and this is
     * the join that stops the next one silently.
     */
    CHECK(
        RS_CS2_TransmitDirtyFlagCount() == cases,
        "pump consumes %zu dirty flags and %zu are exercised alone above",
        RS_CS2_TransmitDirtyFlagCount(),
        cases);
}

/* ==========================================================================
 * Part 5 — ClearHooks keeps gameframe on_op when a sibling pack closes
 * ========================================================================== */

static void
test_clear_hooks_preserves_compass_on_op(void)
{
    struct Fixture fx;
    struct UITreeNodeSpec spec;
    int32_t root;
    int32_t compass_parent;
    int32_t compass;
    int32_t panel;
    int32_t leaf;
    struct UITreeRuntimeHooks* hooks;

    printf("pump: ClearHooksForInterfaceGroup preserves sibling on_op\n");

    fixture_init(&fx);

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = (161 << 16) | 0;
    root = UITree_Push(fx.tree, -1, &spec);
    CHECK(root >= 0, "gameframe root");

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = (161 << 16) | 31;
    compass_parent = UITree_Push(fx.tree, root, &spec);
    CHECK(compass_parent >= 0, "compassclick");

    CHECK(
        UITree_CcCreate(fx.tree, compass_parent, (161 << 16) | 31, 4, 0) >= 0,
        "compass active");
    compass = UITree_CcCreate(fx.tree, compass_parent, (161 << 16) | 31, 4, 1);
    CHECK(compass >= 0, "compass dot");
    CHECK(
        fx.tree->components[compass].component_id == ((161 << 16) | 0x8001),
        "id 0xa18001");
    hooks = UITree_HooksMut(&fx.tree->components[compass]);
    hooks->on_op.script_id = 1050;
    UITree_SyncHookMembership(fx.tree, compass);

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_LAYER;
    spec.component_id = (12 << 16) | 0;
    panel = UITree_Push(fx.tree, root, &spec);
    CHECK(panel >= 0, "bank");

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_RECT;
    spec.component_id = (12 << 16) | 1;
    leaf = UITree_Push(fx.tree, panel, &spec);
    CHECK(leaf >= 0, "bank leaf");
    hooks = UITree_HooksMut(&fx.tree->components[leaf]);
    hooks->on_timer.script_id = 99;
    hooks->on_click.script_id = 88;
    UITree_SyncHookMembership(fx.tree, leaf);

    RS_CS2Host_ClearHooksForInterfaceGroup(&fx.host, 12);

    CHECK(
        fx.tree->components[compass].runtime_hooks &&
            fx.tree->components[compass].runtime_hooks->on_op.script_id == 1050,
        "compass on_op survives ClearHooks(bank)");
    CHECK(
        fx.tree->components[leaf].runtime_hooks &&
            fx.tree->components[leaf].runtime_hooks->on_click.script_id == 88,
        "bank on_click kept");
    CHECK(
        fx.tree->components[leaf].runtime_hooks->on_timer.script_id == 0,
        "bank on_timer cleared");
    CHECK(fx.tree->timer_hooks.count == 0, "timer live set empty");

    fixture_free(&fx);
}

static void
test_dynamic_drag_target_uses_parent_address(void)
{
    struct Fixture fx;
    struct UITreeNodeSpec spec = { 0 };
    int const parent_id = 0x12340005;
    int32_t parent;
    int32_t child;

    printf("pump: dynamic event_drop uses parent component plus sub-id\n");
    fixture_init(&fx);

    spec.type = UIELEM_RS_LAYER;
    spec.component_id = parent_id;
    parent = UITree_Push(fx.tree, -1, &spec);
    CHECK(parent >= 0, "drag-target parent");
    child = UITree_CcCreate(fx.tree, parent, parent_id, 5, 7);
    CHECK(child >= 0, "drag-target dynamic child");
    if( child >= 0 )
    {
        RS_CS2_SetEventDragTarget(
            &fx.host, fx.tree, fx.tree->components[child].component_id);
        CHECK(fx.host.event_drag_target_id == parent_id, "event_drop is parent address");
        CHECK(fx.host.event_drag_target_child_index == 7, "event_dropsubid is child index");
    }

    fixture_free(&fx);
}

/* ==========================================================================
 * A script-side varp write announces itself — but only on a real change
 *
 * Interface 116's mute icon (clientscript 9255) writes %var3796 and then
 * re-syncs only the icon (9254). The four slider bobbles are re-coloured by
 * script 7101, which runs off var3796's *transmit hook* — the drag handlers
 * (9232/9238/9244/9250) call ~script9256 by hand precisely because they are
 * the path that does not rely on it. So a POP_VARP that does not notify leaves
 * every bobble grey after a mute or unmute.
 *
 * The other half is why this cannot simply notify unconditionally: a hook whose
 * script re-asserts the var it watches would re-trigger itself every tick, which
 * is what once had rev230's gameframe rebuilding its popout strip forever. The
 * equal-write case below is that guard.
 * ========================================================================== */

static void
test_script_varp_write_notifies_on_change_only(void)
{
    struct Fixture fx;
    struct VarPManager varps;
    struct VarBitType vb;

    printf("pump: a script varp write announces a real change, and only that\n");

    fixture_init(&fx);
    VarPManager_Init(&varps);
    fx.host.varps = &varps;

    /* One varbit over varp 3796's low bits, so the varbit path can be driven
     * without a cache. */
    memset(&vb, 0, sizeof(vb));
    vb.basevar = 3796;
    vb.startbit = 0;
    vb.endbit = 7;
    VarPManager_SetVarbitTypes(&varps, &vb, 1);

    host_quiesce(&fx.host);
    RS_CS2Host_ScriptWriteVarp(&fx.host, 3796, 100);
    CHECK(
        VarPManager_GetVarp(&varps, 3796) == 100,
        "the write lands, got %d",
        VarPManager_GetVarp(&varps, 3796));
    CHECK(fx.host.var_transmit_dirty == 1, "a 0 -> 100 write marks the host dirty");

    /* The loop guard: same value again, nothing announced. */
    host_quiesce(&fx.host);
    RS_CS2Host_ScriptWriteVarp(&fx.host, 3796, 100);
    CHECK(
        fx.host.var_transmit_dirty == 0,
        "re-writing the same value announces nothing (the self-retrigger guard)");

    /* Muting is the case the bobbles depend on. */
    host_quiesce(&fx.host);
    RS_CS2Host_ScriptWriteVarp(&fx.host, 3796, 0);
    CHECK(fx.host.var_transmit_dirty == 1, "muting (100 -> 0) marks the host dirty");
    CHECK(
        fx.host.var_changed_all || fx.host.var_changed_count == 1,
        "and names varp 3796 as the changed id");

    /* A varbit write has to announce its BASE varp — hooks list varps, so
     * naming the varbit id would match nothing. */
    host_quiesce(&fx.host);
    RS_CS2Host_ScriptWriteVarbit(&fx.host, 0, 42);
    CHECK(fx.host.var_transmit_dirty == 1, "a varbit write marks the host dirty");
    CHECK(
        fx.host.var_changed_all ||
            (fx.host.var_changed_count == 1 && fx.host.var_changed_ids[0] == 3796),
        "and names the base varp, not the varbit id");

    host_quiesce(&fx.host);
    RS_CS2Host_ScriptWriteVarbit(&fx.host, 0, 42);
    CHECK(fx.host.var_transmit_dirty == 0, "an equal varbit write announces nothing");

    fx.host.varps = NULL;
    VarPManager_Free(&varps);
    fixture_free(&fx);
}

static void
test_var_trigger_ids_are_not_reinterpreted_as_varbits(void)
{
    struct RS_CS2VarTransmitHook hook;
    int changed = 1105;

    printf("pump: var trigger ids remain varps when their number also names a varbit\n");

    memset(&hook, 0, sizeof(hook));
    hook.trigger_ids[0] = 1055;
    hook.trigger_count = 1;

    CHECK(
        !RS_CS2_VarTransmitTriggersMatch(&hook, &changed, 1),
        "varp 1105 does not falsely match trigger varp 1055");
    changed = 1055;
    CHECK(
        RS_CS2_VarTransmitTriggersMatch(&hook, &changed, 1),
        "varp 1055 still matches its exact trigger");
}

/* Execute a real cooperative CS2 task after queue-time identity changes. */
static void
test_queued_callback_identity(void)
{
    struct Fixture fx;
    fixture_init(&fx);
    struct UITreeNodeSpec spec = {.type=UIELEM_RS_LAYER, .component_id=0x160000,
                                 .width=100, .height=100};
    int parent = UITree_Push(fx.tree, -1, &spec);
    int source = UITree_CcCreate(fx.tree, parent, spec.component_id, 3, 0);
    int id = fx.tree->components[source].component_id;
    uint16_t opcodes[] = {CS2_OP_PUSH_CONSTANT_INT, CS2_OP_CC_SETCOLOUR, CS2_OP_RETURN};
    int operands[] = {0x123456, 0, 0};
    char* strings[] = {NULL, NULL, NULL};
    struct CS2VM2_Script script = {.script_id=99999, .op_count=3, .opcodes=opcodes,
        .int_operands=operands, .string_operands=strings, .int_stack_depth=1};
    struct ToriRS_Task* live = CreateTask_CS2RunScript(&fx.host, &script, id, id, NULL, 0);
    live->vtable->run(live, NULL);
    CHECK(fx.tree->components[source].colour == 0x123456, "live CS2 callback writes native color");
    live->vtable->free(live);
    struct ToriRS_Task* queued = CreateTask_CS2RunScript(&fx.host, &script, id, id, NULL, 0);
    UITree_CcDelete(fx.tree, source);
    fx.tree->next_dynamic_uid = (uint16_t)(id & 0xffff);
    int replacement = UITree_CcCreate(fx.tree, parent, spec.component_id, 3, 0);
    CHECK(fx.tree->components[replacement].component_id == id, "callback fixture reuses native ID");
    queued->vtable->run(queued, NULL);
    CHECK(fx.tree->components[replacement].colour == 0, "queued CS2 callback cannot write recycled native ID");
    queued->vtable->free(queued);
    uint16_t global_ops[] = {CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_INT,
                            CS2_OP_IF_SETCOLOUR, CS2_OP_RETURN};
    int global_operands[] = {0xabcdef, id, 0, 0};
    char* global_strings[] = {NULL, NULL, NULL, NULL};
    struct CS2VM2_Script global_script = {.script_id=99997, .op_count=4,
        .opcodes=global_ops, .int_operands=global_operands,
        .string_operands=global_strings, .int_stack_depth=2};
    struct ToriRS_Task* global = CreateTask_CS2RunScript(&fx.host, &global_script, -1, -1, NULL, 0);
    global->vtable->run(global, NULL);
    CHECK(fx.tree->components[replacement].colour == 0xabcdef,
          "unbound native script can explicitly address current component");
    global->vtable->free(global);
    fixture_free(&fx);
}

static void
test_transmit_registry_identity(void)
{
    struct Fixture fx;
    fixture_init(&fx);
    struct UITreeNodeSpec spec = {.type=UIELEM_RS_LAYER, .component_id=0x170000,
                                 .width=100, .height=100};
    int parent = UITree_Push(fx.tree, -1, &spec);
    int source = UITree_CcCreate(fx.tree, parent, spec.component_id, 3, 0);
    int id = fx.tree->components[source].component_id;
    uint16_t opcodes[] = {CS2_OP_PUSH_CONSTANT_INT, CS2_OP_CC_SETCOLOUR, CS2_OP_RETURN};
    int operands[] = {0x654321, 0, 0};
    char* strings[] = {NULL, NULL, NULL};
    struct CS2VM2_Script script = {.script_id=99998, .op_count=3, .opcodes=opcodes,
        .int_operands=operands, .string_operands=strings, .int_stack_depth=1};
    struct CS2VM2_Script* cached = malloc(sizeof(*cached));
    CS2VM2_ScriptInit(cached);
    CHECK(CS2VM2_ScriptCopy(&script, cached), "cache callback bytecode");
    CacheProvider_ClientScriptAdd(fx.host.provider, script.script_id, cached);
    struct CS2VM2* vm = CS2VM2_Acquire();
    CS2VM2_BindHost(vm, &fx.host, RS_CS2Host_Exec);
    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(vm);
    struct CS2VM_HostRequest request;
#define REGISTER(channel) do { \
    memset(&request, 0, sizeof(request)); \
    request.kind = CS2VM_HOST_REQUEST_CC_SETON##channel##TRANSMIT; \
    request.u.CC_SETON##channel##TRANSMIT.component_id = id; \
    request.u.CC_SETON##channel##TRANSMIT.script_id = script.script_id; \
    CHECK(RS_CS2Host_Exec(thread, &request) == CS2VM_EXECNO_OK, "register " #channel " callback"); \
} while( 0 )
    REGISTER(INV);
    REGISTER(VAR);
    REGISTER(STAT);
    UITree_HookSet(&UITree_HooksMut(&fx.tree->components[source])->on_misc_transmit,
                  script.script_id, NULL, 0, 0, NULL, 0);
    UITree_HookSet(&UITree_HooksMut(&fx.tree->components[source])->on_sub_change,
                  script.script_id, NULL, 0, 0, NULL, 0);
    struct ToriRS_Task* snapshots[] = {CreateTask_CS2SubChangeDispatch(&fx.host),
                                      CreateTask_CS2MiscTransmitDispatch(&fx.host)};
    UITree_CcDelete(fx.tree, source);
    fx.tree->next_dynamic_uid = (uint16_t)(id & 0xffff);
    int replacement = UITree_CcCreate(fx.tree, parent, spec.component_id, 3, 0);
    struct ToriRS_IO* io = ToriRS_IO_New();
    for( int i = 0; i < 2; ++i )
    {
        UITree_SetColourAt(fx.tree, replacement, 0);
        CHECK(task_run(snapshots[i], io) == PT_ENDED, "stale snapshot dispatch %d drains", i);
        CHECK(fx.tree->components[replacement].colour == 0, "snapshot callback %d cannot reach replacement", i);
        task_free(snapshots[i]);
    }
    struct ToriRS_Task* tasks[] = {
        CreateTask_CS2InvTransmitDispatch(&fx.host, -1),
        CreateTask_CS2VarTransmitDispatch(&fx.host, -1),
        CreateTask_CS2StatTransmitDispatchSet(&fx.host, NULL, 0)
    };
    for( int i = 0; i < 3; ++i )
    {
        CHECK(task_run(tasks[i], io) == PT_ENDED, "stale registry dispatch %d drains", i);
        CHECK(fx.tree->components[replacement].colour == 0, "registry callback %d cannot reach replacement", i);
        task_free(tasks[i]);
    }
    REGISTER(INV);
    REGISTER(VAR);
    REGISTER(STAT);
    tasks[0] = CreateTask_CS2InvTransmitDispatch(&fx.host, -1);
    tasks[1] = CreateTask_CS2VarTransmitDispatch(&fx.host, -1);
    tasks[2] = CreateTask_CS2StatTransmitDispatchSet(&fx.host, NULL, 0);
    for( int i = 0; i < 3; ++i )
    {
        UITree_SetColourAt(fx.tree, replacement, 0);
        CHECK(task_run(tasks[i], io) == PT_ENDED, "new registry dispatch %d drains", i);
        CHECK(fx.tree->components[replacement].colour == 0x654321,
              "new registration %d gets initial update after ID reuse", i);
        task_free(tasks[i]);
    }
    memset(&request, 0, sizeof(request));
    request.kind = CS2VM_HOST_REQUEST_CC_COPY;
    request.u.CC_COPY.parent_id = spec.component_id;
    request.u.CC_COPY.src_sub_id = 0;
    request.u.CC_COPY.dst_sub_id = 2;
    CHECK(RS_CS2Host_Exec(thread, &request) == CS2VM_EXECNO_OK, "native CC_COPY clones listener component");
    int copy = UITree_FindChildBySubid(fx.tree, parent, spec.component_id, 2);
    CHECK(copy >= 0, "copied listener exists");
    tasks[0] = CreateTask_CS2InvTransmitDispatch(&fx.host, -1);
    tasks[1] = CreateTask_CS2VarTransmitDispatch(&fx.host, -1);
    tasks[2] = CreateTask_CS2StatTransmitDispatchSet(&fx.host, NULL, 0);
    for( int i = 0; i < 3; ++i )
    {
        UITree_SetColourAt(fx.tree, copy, 0);
        CHECK(task_run(tasks[i], io) == PT_ENDED, "copied registry dispatch %d drains", i);
        CHECK(fx.tree->components[copy].colour == 0x654321,
              "copied native listener %d receives its initial update", i);
        task_free(tasks[i]);
    }
#undef REGISTER
    ToriRS_IO_Free(io);
    CS2VM2_Release(vm);
    fixture_free(&fx);
}

static void
test_queued_widget_operations(void)
{
    struct Fixture fx;
    fixture_init(&fx);
    struct UITreeNodeSpec spec = {.type=UIELEM_RS_LAYER, .component_id=0x180000};
    int parent = UITree_Push(fx.tree, -1, &spec);
    int child = UITree_CcCreate(fx.tree, parent, spec.component_id, 3, 0);
    int id = fx.tree->components[child].component_id;
    struct CS2VM2* vm = CS2VM2_Acquire();
    CS2VM2_BindHost(vm, &fx.host, RS_CS2Host_Exec);
    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(vm);
    for( int stale = 0; stale <= 1; ++stale )
    {
        struct CS2VM_HostRequest req = {.kind=CS2VM_HOST_REQUEST_IF_CALLONRESIZE};
        req.u.IF_CALLONRESIZE.component_id = id;
        CHECK(RS_CS2Host_Exec(thread, &req) == CS2VM_EXECNO_OK, "queue native resize");
        req.kind = CS2VM_HOST_REQUEST_CC_TRIGGEROP;
        req.u.CC_TRIGGEROP.component_id = id;
        req.u.CC_TRIGGEROP.op_index = 3;
        CHECK(RS_CS2Host_Exec(thread, &req) == CS2VM_EXECNO_OK, "queue native operation");
        req.kind = CS2VM_HOST_REQUEST_IF_TRIGGEROPLOCAL;
        req.u.IF_TRIGGEROPLOCAL.component_id = spec.component_id;
        req.u.IF_TRIGGEROPLOCAL.sub = 0;
        CHECK(RS_CS2Host_Exec(thread, &req) == CS2VM_EXECNO_OK, "queue native child operation");
        if( stale )
        {
            UITree_CcDelete(fx.tree, child);
            fx.tree->next_dynamic_uid = (uint16_t)(id & 0xffff);
            child = UITree_CcCreate(fx.tree, parent, spec.component_id, 3, 0);
        }
        int resized = -1;
        struct RS_CS2TriggerOp op = {0};
        struct RS_CS2TriggerOpLocal local = {0};
        CHECK(RS_CS2Host_TakeCallOnResize(&fx.host, &resized) == !stale,
              "queued resize respects incarnation stale=%d", stale);
        CHECK(RS_CS2Host_TakeTriggerOp(&fx.host, &op) == !stale,
              "queued operation respects incarnation stale=%d", stale);
        CHECK(RS_CS2Host_TakeTriggerOpLocal(&fx.host, &local) == !stale,
              "queued child operation respects incarnation stale=%d", stale);
        if( !stale )
            CHECK(resized == id && op.component_id == id && op.op_index == 3 &&
                  local.component_id == spec.component_id && local.sub == 0,
                  "valid operations keep their native payloads");
    }
    CS2VM2_Release(vm);
    fixture_free(&fx);
}

static void
test_callback_context_across_asset_yield(void)
{
    for( int dot = 0; dot <= 1; ++dot )
    for( int stale = 0; stale <= 1; ++stale )
    {
        struct Fixture fx;
        fixture_init(&fx);
        struct UITreeNodeSpec spec = {.type=UIELEM_RS_LAYER, .component_id=0x190000};
        int parent = UITree_Push(fx.tree, -1, &spec);
        int active_node = UITree_CcCreate(fx.tree, parent, spec.component_id, 3, 0);
        int dot_node = UITree_CcCreate(fx.tree, parent, spec.component_id, 3, 1);
        int child = dot ? dot_node : active_node;
        int id = fx.tree->components[child].component_id;
        uint16_t ops[] = {CS2_OP_GOSUB_WITH_PARAMS, CS2_OP_PUSH_CONSTANT_INT,
                         CS2_OP_CC_SETCOLOUR, CS2_OP_PUSH_CONSTANT_INT,
                         CS2_OP_CC_SETCOLOUR, CS2_OP_RETURN};
        int operands[] = {99996, 0x234567, 0, 0x345678, 1, 0};
        char* strings[] = {NULL, NULL, NULL, NULL, NULL, NULL};
        struct CS2VM2_Script script = {.script_id=99995, .op_count=6, .opcodes=ops,
            .int_operands=operands, .string_operands=strings, .int_stack_depth=1};
        struct ToriRS_Task* callback = CreateTask_CS2RunScript(&fx.host, &script,
            fx.tree->components[active_node].component_id, fx.tree->components[dot_node].component_id, NULL, 0);
        struct ToriRS_IO* io = ToriRS_IO_New();
        CHECK(task_run(callback, io) == PT_YIELDED, "native callback yields for missing callee");
        CHECK(io->active_count == 1 && io->io_slots[io->active[0]].kind == TORIRS_IOK_CACHE,
              "callee load reaches real cache IO request");
        if( stale )
        {
            UITree_CcDelete(fx.tree, child);
            fx.tree->next_dynamic_uid = (uint16_t)(id & 0xffff);
            child = UITree_CcCreate(fx.tree, parent, spec.component_id, 3, dot);
        }
        uint16_t callee_ops[] = {CS2_OP_RETURN};
        int callee_args[] = {0};
        char* callee_strings[] = {NULL};
        struct RSCache_ClientScript callee = {.script={.script_id=99996, .op_count=1,
            .opcodes=callee_ops, .int_operands=callee_args, .string_operands=callee_strings}};
        unsigned cap = RSCache_ClientScriptEncodeBound(&callee);
        struct RSCache_Dat2DiskArchive* archive = calloc(1, sizeof(*archive));
        archive->data = malloc(cap);
        archive->data_size = RSCache_ClientScriptEncodeFlags(&callee,
            RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_LEGACY, archive->data, cap);
        CHECK(archive->data_size > 0, "encode callee for native cache decoder");
        io->io_slots[io->active[0]].data = archive;
        ToriRS_IO_ResetActive(io);
        CHECK(task_run(callback, io) == PT_ENDED, "native callback resumes through loaded callee");
        CHECK(fx.tree->components[child].colour == (stale ? 0 : dot ? 0x345678 : 0x234567),
              "resumed callback respects widget incarnation dot=%d stale=%d", dot, stale);
        CHECK(fx.tree->components[dot ? active_node : dot_node].colour == (dot ? 0x234567 : 0x345678),
              "stale context does not cancel independent live context dot=%d stale=%d", dot, stale);
        task_free(callback);
        ToriRS_IO_Free(io);
        fixture_free(&fx);
    }
}

static void
register_test_transmit(struct CS2VM2_Thread* thread, int channel, int id, int script_id)
{
    struct CS2VM_HostRequest req = {0};
#define REGISTER_CASE(n, opcode_name) case n: \
    req.kind = CS2VM_HOST_REQUEST_##opcode_name; \
    req.u.opcode_name.component_id = id; req.u.opcode_name.script_id = script_id; break
    switch( channel )
    {
        REGISTER_CASE(0, CC_SETONINVTRANSMIT);
        REGISTER_CASE(1, CC_SETONVARTRANSMIT);
        REGISTER_CASE(2, CC_SETONSTATTRANSMIT);
    }
#undef REGISTER_CASE
    CHECK(RS_CS2Host_Exec(thread, &req) == CS2VM_EXECNO_OK, "register listener channel=%d", channel);
}

static struct ToriRS_Task*
test_transmit_task(struct RS_CS2Host* host, int channel)
{
    if( channel == 0 ) return CreateTask_CS2InvTransmitDispatch(host, -1);
    if( channel == 1 ) return CreateTask_CS2VarTransmitDispatch(host, -1);
    return CreateTask_CS2StatTransmitDispatchSet(host, NULL, 0);
}

static void
test_registry_compaction_during_callback(void)
{
    for( int channel = 0; channel < 3; ++channel )
    {
        struct Fixture fx;
        fixture_init(&fx);
        struct UITreeNodeSpec spec = {.type=UIELEM_RS_LAYER, .component_id=0x1a0000};
        int parent = UITree_Push(fx.tree, -1, &spec);
        int nodes[4];
        for( int i = 0; i < 4; ++i )
            nodes[i] = UITree_CcCreate(fx.tree, parent, spec.component_id, 3, i);
        uint16_t wait_ops[] = {CS2_OP_GOSUB_WITH_PARAMS, CS2_OP_RETURN};
        int wait_args[] = {99992, 0};
        char* wait_strings[] = {NULL, NULL};
        struct CS2VM2_Script wait_script = {.script_id=99991, .op_count=2,
            .opcodes=wait_ops, .int_operands=wait_args, .string_operands=wait_strings};
        uint16_t paint_ops[] = {CS2_OP_PUSH_CONSTANT_INT, CS2_OP_CC_SETCOLOUR, CS2_OP_RETURN};
        int paint_args[] = {0x56789a, 0, 0};
        char* paint_strings[] = {NULL, NULL, NULL};
        struct CS2VM2_Script paint_script = {.script_id=99993, .op_count=3,
            .opcodes=paint_ops, .int_operands=paint_args, .string_operands=paint_strings,
            .int_stack_depth=1};
        struct CS2VM2_Script* sources[] = {&wait_script, &paint_script};
        for( int i = 0; i < 2; ++i )
        {
            struct CS2VM2_Script* cached = malloc(sizeof(*cached));
            CS2VM2_ScriptInit(cached);
            CHECK(CS2VM2_ScriptCopy(sources[i], cached), "copy callback fixture");
            CacheProvider_ClientScriptAdd(fx.host.provider, cached->script_id, cached);
        }
        struct CS2VM2* vm = CS2VM2_Acquire();
        CS2VM2_BindHost(vm, &fx.host, RS_CS2Host_Exec);
        struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(vm);
        for( int i = 0; i < 3; ++i )
            register_test_transmit(thread, channel, fx.tree->components[nodes[i]].component_id,
                                   i == 1 ? 99991 : 99993);
        UITree_CcDelete(fx.tree, nodes[0]);
        struct ToriRS_Task* dispatch = test_transmit_task(&fx.host, channel);
        struct ToriRS_IO* io = ToriRS_IO_New();
        CHECK(task_run(dispatch, io) == PT_YIELDED, "dispatch pauses in native callback channel=%d", channel);
        /* Adding D compacts the dead first entry. A and B move, while the
         * dispatcher is suspended inside A's resource load. */
        register_test_transmit(thread, channel, fx.tree->components[nodes[3]].component_id, 99993);
        uint16_t return_op[] = {CS2_OP_RETURN};
        int return_arg[] = {0};
        char* return_string[] = {NULL};
        struct RSCache_ClientScript callee = {.script={.script_id=99992, .op_count=1,
            .opcodes=return_op, .int_operands=return_arg, .string_operands=return_string}};
        unsigned cap = RSCache_ClientScriptEncodeBound(&callee);
        struct RSCache_Dat2DiskArchive* archive = calloc(1, sizeof(*archive));
        archive->data = malloc(cap);
        archive->data_size = RSCache_ClientScriptEncodeFlags(&callee,
            RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_LEGACY, archive->data, cap);
        io->io_slots[io->active[0]].data = archive;
        ToriRS_IO_ResetActive(io);
        CHECK(task_run(dispatch, io) == PT_ENDED, "dispatch finishes after compaction channel=%d", channel);
        CHECK(fx.tree->components[nodes[2]].colour == 0x56789a,
              "compaction cannot skip original listener channel=%d", channel);
        CHECK(fx.tree->components[nodes[3]].colour == 0,
              "new listener waits for next dispatch channel=%d", channel);
        task_free(dispatch);
        dispatch = test_transmit_task(&fx.host, channel);
        CHECK(task_run(dispatch, io) == PT_ENDED, "next dispatch drains channel=%d", channel);
        CHECK(fx.tree->components[nodes[3]].colour == 0x56789a,
              "new listener gets its initial update channel=%d", channel);
        task_free(dispatch);
        CS2VM2_Release(vm);
        ToriRS_IO_Free(io);
        fixture_free(&fx);
    }
}

static void
test_hidden_focus_does_not_receive_keys(void)
{
    struct Fixture fx;
    fixture_init(&fx);
    struct UITreeNodeSpec spec = {.type=UIELEM_RS_LAYER, .component_id=0x1b0000};
    int parent = UITree_Push(fx.tree, -1, &spec);
    int field = UITree_CcCreate(fx.tree, parent, spec.component_id, 12, 0);
    int id = fx.tree->components[field].component_id;
    UITree_SetTextAt(fx.tree, field, "a");
    UITree_InputSetFocusId(fx.tree, id);
    CHECK(RS_CS2_InputKey(&fx.host, &fx.runner, NULL, -1, 'b'), "visible focused field receives typing");
    for( int mode = 0; mode < 2; ++mode )
    {
        UITree_SetTextAt(fx.tree, field, "ab");
        fx.tree->components[field].u.rs_text.caret = 2;
        if( mode == 0 ) UITree_SetHideAt(fx.tree, parent, 1);
        if( mode == 1 ) UITree_SetMountHiddenAt(fx.tree, parent, 1);
        CHECK(!RS_CS2_InputKey(&fx.host, &fx.runner, NULL, -1, 'x'), "unavailable focus rejects typing mode=%d", mode);
        CHECK(strcmp(fx.tree->components[field].u.rs_text.text, "ab") == 0,
              "unavailable focus preserves text mode=%d", mode);
        UITree_SetHideAt(fx.tree, parent, 0);
        UITree_SetMountHiddenAt(fx.tree, parent, 0);
    }
    CHECK(UITree_InputFocusId(fx.tree) == id, "visibility does not transfer logical focus");
    CHECK(RS_CS2_InputKey(&fx.host, &fx.runner, NULL, -1, 'c'), "same field receives keys when available again");
    CHECK(strcmp(fx.tree->components[field].u.rs_text.text, "abc") == 0, "only available typing changed text");
    fixture_free(&fx);
}

static void
test_widget_animation_instances(void)
{
    struct Fixture fx;
    fixture_init(&fx);
    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    struct ToriDraw_Model* model = ToriDraw_ModelNew(1, 0, 0);
    model->vertices_x = calloc(1, sizeof(vertexint_t));
    model->vertices_y = calloc(1, sizeof(vertexint_t));
    model->vertices_z = calloc(1, sizeof(vertexint_t));
    model->vertex_bones = calloc(1, sizeof(*model->vertex_bones));
    model->vertex_bones->bones_count = 1;
    model->vertex_bones->bones = calloc(1, sizeof(boneint_t*));
    model->vertex_bones->bones[0] = calloc(1, sizeof(boneint_t));
    model->vertex_bones->bones_sizes = calloc(1, sizeof(boneint_t));
    model->vertex_bones->bones_sizes[0] = 1;
    model->animaya_vertex_count = 1;
    model->animaya_group_counts = calloc(1, 1); model->animaya_group_counts[0] = 1;
    model->animaya_groups = calloc(1, sizeof(uint8_t*)); model->animaya_groups[0] = calloc(1, 1);
    model->animaya_scales = calloc(1, sizeof(uint8_t*)); model->animaya_scales[0] = malloc(1);
    model->animaya_scales[0][0] = 255;
    ToriDraw_ModelCaptureOriginalVertices(model);
    struct ToriDraw_ModelHandle handle = {.kind=TORIDRAWMK_MODEL, .u.model.model=model};
    ToriDraw_SceneModelAdd(scene, 101, handle);
    struct ToriDraw_Animation* anim = calloc(1, sizeof(*anim));
    anim->base = calloc(1, sizeof(*anim->base));
    anim->base->length = 1;
    anim->base->types = calloc(1, 1); anim->base->types[0] = 1;
    anim->base->bone_groups = calloc(1, sizeof(uint8_t*));
    anim->base->bone_groups[0] = calloc(1, 1);
    anim->base->bone_group_lengths = calloc(1, sizeof(uint16_t));
    anim->base->bone_group_lengths[0] = 1;
    anim->frame_count = 2; anim->frame_step = 2;
    anim->frames = calloc(2, sizeof(*anim->frames));
    for( int f = 0; f < 2; ++f )
    {
        struct ToriDraw_AnimFrame* frame = &anim->frames[f];
        frame->length = 1; frame->delay = 1;
        frame->groups = calloc(1, sizeof(int16_t));
        frame->x = calloc(1, sizeof(int16_t));
        frame->y = calloc(1, sizeof(int16_t));
        frame->z = calloc(1, sizeof(int16_t));
        frame->x[0] = (f+1)*100;
    }
    ToriDraw_SceneAnimationAdd(scene, 202, anim);
    for( int f = 0; f < 2; ++f )
    {
        struct UITreeNodeSpec spec = {.type=UIELEM_RS_MODEL, .component_id=0x1c0000+f,
                                     .x=f*60, .width=50, .height=50};
        spec.u.rs_model.gamecache_model_id = 101;
        spec.u.rs_model.active_model_id = -1;
        spec.u.rs_model.anim_seq_id = 202;
        spec.u.rs_model.anim_frame = f;
        spec.u.rs_model.anim_hold = 1;
        spec.u.rs_model.zoom = 128;
        UITree_Push(fx.tree, -1, &spec);
    }
    UITree_LayoutResolve(fx.tree, 0, 0, 200, 100);
    UITreeAnim_Advance(fx.tree, scene, 1);
    CHECK(model->vertices_x[0] == 0, "UI animation cannot mutate the shared model asset");
    struct UITreeHost ui_host = {0};
    struct UITreeEmitBuffer emit;
    UITree_EmitBufferInit(&emit);
    UITree_EmitWalk(fx.tree, &ui_host, &emit, -1);
    struct ToriRS_Frame frame;
    ToriRS_FrameInit(&frame);
    ToriRS_FrameSetScene(&frame, scene);
    ToriRS_FrameSetCanvas(&frame, 200, 100);
    ToriRS_FrameSetEmitBuffer(&frame, &emit);
    ToriRS_FrameBegin(&frame);
    struct ToriRS_RenderCommand cmd;
    struct ToriDraw_Model* drawn[2] = {0};
    int count = 0;
    while( ToriRS_FrameNextCommand(&frame, &cmd) )
        if( cmd.kind == TORIRSRC_DRAW_MODEL_WIDGET && count < 2 )
            drawn[count++] = cmd.u.model_widget.model.u.model.model;
    CHECK(count == 2, "both native model widgets reach rendering");
    if( count == 2 )
    {
        CHECK(drawn[0] != drawn[1] && drawn[0] != model && drawn[1] != model,
              "model widgets own independent animation poses");
        CHECK(drawn[0]->vertices_x[0] == 100 && drawn[1]->vertices_x[0] == 200,
              "each rendered widget keeps its requested frame");
    }
    ToriRS_FrameEnd(&frame);
    UITree_EmitBufferFree(&emit);
    fx.tree->components[0].is_dirty = 0;
    uint32_t dirty = fx.tree->dirty_gen;
    UITree_ApplyModelAnim(fx.tree, 0x1c0000, 202);
    CHECK(fx.tree->dirty_gen == dirty, "restating animation is a quiet native no-op");
    struct CS2VM2* vm = CS2VM2_Acquire();
    CS2VM2_BindHost(vm, &fx.host, RS_CS2Host_Exec);
    struct CS2VM_HostRequest repeat = {.kind=CS2VM_HOST_REQUEST_CC_SETMODELANIM};
    repeat.u.CC_SETMODELANIM.component_id = 0x1c0000;
    repeat.u.CC_SETMODELANIM.field = CS2VM_WIDGET_INT_MODEL_ANIM;
    repeat.u.CC_SETMODELANIM.value = 202;
    CHECK(RS_CS2Host_Exec(CS2VM2_ThreadMain(vm), &repeat) == CS2VM_EXECNO_OK, "native animation restatement executes");
    CHECK(fx.tree->dirty_gen == dirty, "CS2 animation restatement does not add a wrapper invalidation");
    CS2VM2_Release(vm);
    struct ToriDraw_Animation* skeletal = calloc(1, sizeof(*skeletal));
    skeletal->frame_count = 2; skeletal->frame_step = 2;
    skeletal->skeletal = calloc(1, sizeof(*skeletal->skeletal));
    skeletal->skeletal->frame_count = 2; skeletal->skeletal->bone_count = 1;
    skeletal->skeletal->matrices = calloc(32, sizeof(float));
    for( int i = 0; i < 2; ++i )
    {
        float* m = skeletal->skeletal->matrices + i*16;
        m[0] = m[5] = m[10] = m[15] = 1;
        m[12] = 300 + 100*i;
    }
    ToriDraw_SceneAnimationAdd(scene, 303, skeletal);
    UITree_SetModelAnimationAt(fx.tree, 0, 303, 0, 0, 0);
    UITreeAnim_Advance(fx.tree, scene, 1);
    CHECK(fx.tree->components[0].u.rs_model.anim_frame == 1, "skeletal widget clock advances");
    struct ToriDraw_ModelHandle posed = UITreeAnim_ModelForDraw(scene,
        UITree_ModelRenderCacheMut(&fx.tree->components[0]), 101, 303,
        fx.tree->components[0].u.rs_model.anim_frame);
    CHECK(posed.u.model.model->vertices_x[0] == 400, "skeletal widget uses native skinning");
    CHECK(model->vertices_x[0] == 0, "skeletal posing preserves the asset");
    struct UITreeModelRenderCache* cache = UITree_ModelRenderCacheMut(&fx.tree->components[0]);
    struct ToriDraw_ModelHandle same = UITreeAnim_ModelForDraw(scene, cache, 101, 303, 1);
    CHECK(same.u.model.model == posed.u.model.model, "unchanged pose reuses its private model");
    uint64_t old_revision = ToriDraw_SceneModelRevision(scene, 101);
    struct ToriDraw_Model* replacement = ToriDraw_ModelCopy(model);
    replacement->vertices_x[0] = 7;
    replacement->original_vertices_x[0] = 7;
    struct ToriDraw_ModelHandle removed = ToriDraw_SceneModelRemove(scene, 101);
    ToriDraw_ModelHandleFree(removed);
    handle.u.model.model = replacement;
    ToriDraw_SceneModelAdd(scene, 101, handle);
    CHECK(ToriDraw_SceneModelRevision(scene, 101) != old_revision, "resource registration cannot reuse its revision");
    posed = UITreeAnim_ModelForDraw(scene, cache, 101, 202, 0);
    CHECK(posed.u.model.model->vertices_x[0] == 107, "resource replacement refreshes the private pose");
    struct ToriDraw_ModelHandle unanimated = UITreeAnim_ModelForDraw(scene, cache, 101, -1, 0);
    CHECK(unanimated.u.model.model == replacement && cache->data == NULL,
          "releasing animation exposes current asset and releases derived pose");
    struct ToriDraw_Model* uncaptured = ToriDraw_ModelCopy(replacement);
    free(uncaptured->original_vertices_x); uncaptured->original_vertices_x = NULL;
    free(uncaptured->original_vertices_y); uncaptured->original_vertices_y = NULL;
    free(uncaptured->original_vertices_z); uncaptured->original_vertices_z = NULL;
    handle.u.model.model = uncaptured;
    ToriDraw_SceneModelAdd(scene, 102, handle);
    (void)UITreeAnim_ModelForDraw(scene, cache, 102, 202, 0);
    posed = UITreeAnim_ModelForDraw(scene, cache, 102, 202, 1);
    CHECK(posed.u.model.model->vertices_x[0] == 207, "private pose captures a missing bind pose before animation");
    fixture_free(&fx);
    ToriDraw_SceneFree(scene);
}

static int
model_test_host(void* user, struct UITreeHostRequest* req)
{
    (void)user;
    if( req->kind == UITREE_HOST_IS_ACTIVE ) return req->u.is_active.component->cs1_active;
    return 0;
}

static void
test_cache_model_animation_variants(void)
{
    for( int dat2 = 0; dat2 < 2; ++dat2 )
    {
        struct ToriRS_Component* component;
        if( dat2 )
        {
            struct RSCache_Dat2Component source;
            RSCache_Dat2ComponentInit(&source);
            source.id=777; source.type=6; source.baseWidth=50; source.baseHeight=50;
            source.modelType=1; source.modelId=101; source.activeModelId=101;
            source.modelSeqId=202; source.activeAnimId=303;
            component=ToriRS_ComponentFromRSCacheDat2(&source);
        }
        else
        {
            struct RSCache_Dat1ConfigComponent source;
            RSCache_Dat1ConfigComponentInit(&source);
            source.id=777; source.type=6; source.width=50; source.height=50;
            source.modelType=1; source.model=101; source.activeModelType=1; source.activeModel=101;
            source.anim=202; source.activeAnim=303;
            component=ToriRS_ComponentFromRSCacheDat1(&source);
        }
        struct UIBuildComponent build;
        UITree_FillBuildFromToriRS(&build, component);
        struct UITree* tree=UITree_New(1);
        int node=UITree_PushBuildComponent(tree,-1,&build,NULL,NULL,NULL);
        UITree_SetCS1ActiveAt(tree,node,1);
        UITree_LayoutResolve(tree,0,0,100,100);
        struct UITreeHost host={.request=model_test_host};
        struct UITreeEmitBuffer emit;
        UITree_EmitBufferInit(&emit);
        UITree_EmitWalk(tree,&host,&emit,-1);
        int sequence=-1;
        for( int i=0; i<emit.count; ++i )
            if( emit.cmds[i].kind==UITREE_EMIT_MODEL ) sequence=emit.cmds[i].model_anim_seq;
        CHECK(sequence==303, "cache active animation reaches native rendering dat2=%d",dat2);
        UITree_EmitBufferFree(&emit);
        UITree_Free(tree);
        ToriRS_ComponentFree(component);
    }
}

int
main(void)
{
    printf("TEST: RS_CS2_PumpTransmits — the transmit dirty-flag guard\n");

    test_queued_callback_identity();
    test_queued_widget_operations();
    test_callback_context_across_asset_yield();
    test_registry_compaction_during_callback();
    test_hidden_focus_does_not_receive_keys();
    test_widget_animation_instances();
    test_cache_model_animation_variants();
    test_transmit_registry_identity();
    test_quiet_tick();
    test_standard_sizes_exist_before_first_packet();
    test_each_flag_alone();
    test_stat_notify_reaches_a_dispatch();
    test_widgets_loaded_queues_stat_unhide();
    test_flag_count_pinned();
    test_clear_hooks_preserves_compass_on_op();
    test_dynamic_drag_target_uses_parent_address();
    test_script_varp_write_notifies_on_change_only();
    test_var_trigger_ids_are_not_reinterpreted_as_varbits();

    if( g_fail )
    {
        printf("pump: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("pump: all checks passed\n");
    return 0;
}
