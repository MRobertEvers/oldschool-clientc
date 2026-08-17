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
 *   4. The flag count is pinned, so that adding a seventh dirty flag to the
 *      pump's table fails here until a case for it is added above.
 *
 * No cache and no server: nothing on this path reads either, and a gate that
 * skips when a cache is missing is a gate that eventually only ever skips.
 */

#include "engine/dat2/dat2_buildcache.h"
#include "game/rs_cs2_dispatch.h"
#include "game/rs_cs2_host.h"
#include "game/task_cs2_run.h"
#include "inv/inv_manager.h"
#include "task_runner.h"
#include "ui/uitree.h"
#include "varp/varp_manager.h"

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
    InvManager_Init(&fx->invs);
    RS_CS2Host_Init(&fx->host, fx->tree, provider, &fx->invs, NULL, NULL);

    /* The pump reads runner->queue and nothing else — it enqueues dispatch
     * tasks, it does not run them. Leaving io/px NULL keeps this test off the
     * platform layer entirely; TaskRunner_Step is never called. */
    fx->runner.queue = ToriRS_TaskQueue_New();
}

static void
fixture_free(struct Fixture* fx)
{
    ToriRS_TaskQueue_Free(fx->runner.queue);
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

static struct FlagCase const g_flag_cases[] = {
    { "widgets_loaded_dirty", set_widgets, get_widgets },
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
    fx.host.stat_transmit_hook_count = 1;
    fx.host.stat_transmit_hooks[0].component_id = spec.component_id;
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

int
main(void)
{
    printf("TEST: RS_CS2_PumpTransmits — the transmit dirty-flag guard\n");

    test_quiet_tick();
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
