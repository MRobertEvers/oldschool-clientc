/*
 * The side-app registry (sideapp/side_app.h), driven by fake tools and a fake
 * key predicate.
 *
 * Registration order is delivery order, after an action's rank within its
 * phase; a binding or a declaration naming an action nobody registered is
 * reported; two bindings to one action each fire it, and once per delivery
 * when both keys are down; a phase delivers only its own actions; keys_done
 * follows every delivery of a side-app's phase; keys_probe reports a key
 * going down without firing it; the frame, build_chrome and world_overlay
 * hooks run in registration order for every side-app that has one, the frame
 * hook told its point; inspects_scenery and holds_keyboard are yes when any
 * side-app says yes; the menu flags are set by every side-app with the hook,
 * and a chosen menu action is offered in registration order until one takes
 * it; an
 * action's mode reaches the predicate as declared, press-or-held included;
 * the free hooks run last-registered first.
 *
 * Run: make -C src test-side-app
 */

#include "sideapp/side_app.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;

#define TEST_ASSERT(cond, what)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (what));                                \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

/* What the fake tools saw, in order, as "<tool>:<action>:<args>;". */
static char g_log[1024];
static char g_freed[64];

struct FakeTool
{
    char const* name;
};

static void
fake_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args)
{
    struct FakeTool* tool = self;
    char line[128];

    (void)host;
    snprintf(line, sizeof(line), "%s:%d:%s;", tool->name, action, args);
    strncat(g_log, line, sizeof(g_log) - strlen(g_log) - 1);
}

static void
fake_free(void* self)
{
    struct FakeTool* tool = self;
    strncat(g_freed, tool->name, sizeof(g_freed) - strlen(g_freed) - 1);
}

static void
fake_keys_done(
    void* self,
    struct SideAppHost* host,
    enum SideAppPhase phase)
{
    struct FakeTool* tool = self;
    char line[128];

    (void)host;
    snprintf(line, sizeof(line), "%s:done%d;", tool->name, (int)phase);
    strncat(g_log, line, sizeof(g_log) - strlen(g_log) - 1);
}

static void
fake_keys_probe(
    void* self,
    struct SideAppHost* host,
    enum SideAppPhase phase,
    int any_down)
{
    struct FakeTool* tool = self;
    char line[128];

    (void)host;
    snprintf(line, sizeof(line), "%s:probe%d=%d;", tool->name, (int)phase, any_down);
    strncat(g_log, line, sizeof(g_log) - strlen(g_log) - 1);
}

static struct SideAppHooks const k_hooks = { .key = fake_key, .free = fake_free };
static struct SideAppHooks const k_hooks_phased = {
    .key = fake_key,
    .free = fake_free,
    .keys_done = fake_keys_done,
    .keys_probe = fake_keys_probe,
};

/* The fake predicate: a binding fires when its key is in the down set and its
 * action's mode is the one the frame is testing for. */
struct FakeKeys
{
    enum LibToriRS_KeyCode down[8];
    int down_count;
    enum SideAppKeyMode mode;
    int asked;
};

static int
fake_fires(
    void* ctx,
    struct SideAppBinding const* binding,
    enum SideAppGate gate,
    enum SideAppKeyMode mode)
{
    struct FakeKeys* keys = ctx;

    (void)gate;
    keys->asked++;
    if( mode != keys->mode )
        return 0;
    for( int i = 0; i < keys->down_count; i++ )
        if( keys->down[i] == binding->key )
            return 1;
    return 0;
}

static void
bind(
    struct SideAppBindingTable* table,
    enum LibToriRS_KeyCode key,
    char const* action,
    char const* args)
{
    struct SideAppBinding* b = &table->bindings[table->binding_count++];
    b->key = key;
    snprintf(b->action, sizeof(b->action), "%s", action);
    snprintf(b->args, sizeof(b->args), "%s", args);
}

static void
declare(
    struct SideAppBindingTable* table,
    char const* source,
    char const* action)
{
    struct SideAppDeclaredAction* d = &table->declared[table->declared_count++];
    snprintf(d->source, sizeof(d->source), "%s", source);
    snprintf(d->action, sizeof(d->action), "%s", action);
}

static struct SideAppHost* const g_host = (struct SideAppHost*)&g_failures;

static struct SideAppAction const k_alpha_actions[] = {
    { "alpha_world", SIDE_APP_PHASE_WORLD_HOTKEYS, SIDE_APP_GATE_WORLD, SIDE_APP_KEY_PRESS, 0 },
    { "alpha_chrome", SIDE_APP_PHASE_EARLY_CHROME, SIDE_APP_GATE_CTRL, SIDE_APP_KEY_PRESS, 0 },
};
static struct SideAppAction const k_beta_actions[] = {
    { "beta_world", SIDE_APP_PHASE_WORLD_HOTKEYS, SIDE_APP_GATE_WORLD, SIDE_APP_KEY_PRESS, 0 },
    { "beta_fly", SIDE_APP_PHASE_CAMERA_KEYS, SIDE_APP_GATE_CTRL, SIDE_APP_KEY_HELD, 0 },
};

static void
test_order_phases_and_two_bindings(void)
{
    static struct SideAppBindingTable table;
    struct FakeTool alpha = { "alpha" };
    struct FakeTool beta = { "beta" };
    struct FakeKeys keys = { { 0 }, 0, SIDE_APP_KEY_PRESS, 0 };

    memset(&table, 0, sizeof(table));
    /* beta's binding comes first in the table: delivery order is still
     * registration order. */
    bind(&table, TORIRSK_B, "beta_world", "b=1");
    bind(&table, TORIRSK_A, "alpha_world", "a=1");
    bind(&table, TORIRSK_C, "alpha_chrome", "");
    bind(&table, TORIRSK_W, "beta_fly", "");
    /* A second key on alpha_world. */
    bind(&table, TORIRSK_8, "alpha_world", "a=2");
    declare(&table, "w", "alpha_world");

    struct SideApps* apps = SideApps_New(&table);
    TEST_ASSERT(SideApps_UnresolvedCount(apps) == 5 - 1, "before registering, every name is unresolved");
    TEST_ASSERT(SideApps_Register(apps, "alpha", &alpha, &k_hooks, k_alpha_actions, 2) == 0, "alpha is first");
    TEST_ASSERT(SideApps_Register(apps, "beta", &beta, &k_hooks, k_beta_actions, 2) == 1, "beta is second");
    TEST_ASSERT(SideApps_UnresolvedCount(apps) == 0, "every name answered");
    TEST_ASSERT(SideApps_IndexOf(apps, "beta") == 1, "IndexOf finds beta");
    TEST_ASSERT(SideApps_IndexOf(apps, "gamma") == -1, "IndexOf misses gamma");
    TEST_ASSERT(SideApps_BindingAnswers(apps, 0, 1, 0), "binding 0 is beta_world");
    TEST_ASSERT(SideApps_BindingAnswers(apps, 4, 0, 0), "binding 4 is alpha_world");
    TEST_ASSERT(!SideApps_BindingAnswers(apps, 4, 0, 1), "binding 4 is not alpha_chrome");

    /* Both world keys and the chrome key down: the world phase fires alpha
     * then beta, and not the chrome action. */
    g_log[0] = '\0';
    keys.down[0] = TORIRSK_A;
    keys.down[1] = TORIRSK_B;
    keys.down[2] = TORIRSK_C;
    keys.down_count = 3;
    SideApps_DeliverKeys(apps, SIDE_APP_PHASE_WORLD_HOTKEYS, g_host, fake_fires, &keys);
    TEST_ASSERT(strcmp(g_log, "alpha:0:a=1;beta:0:b=1;") == 0, "world phase: registration order, own phase only");

    g_log[0] = '\0';
    SideApps_DeliverKeys(apps, SIDE_APP_PHASE_EARLY_CHROME, g_host, fake_fires, &keys);
    TEST_ASSERT(strcmp(g_log, "alpha:1:;") == 0, "chrome phase: only the chrome action");

    /* The held action is asked under its own mode; a press predicate says no. */
    g_log[0] = '\0';
    keys.down[0] = TORIRSK_W;
    keys.down_count = 1;
    SideApps_DeliverKeys(apps, SIDE_APP_PHASE_CAMERA_KEYS, g_host, fake_fires, &keys);
    TEST_ASSERT(g_log[0] == '\0', "held action does not fire as a press");
    keys.mode = SIDE_APP_KEY_HELD;
    SideApps_DeliverKeys(apps, SIDE_APP_PHASE_CAMERA_KEYS, g_host, fake_fires, &keys);
    TEST_ASSERT(strcmp(g_log, "beta:1:;") == 0, "held action fires while held");
    keys.mode = SIDE_APP_KEY_PRESS;

    /* Two bindings to one action: each key fires it with its own args... */
    g_log[0] = '\0';
    keys.down[0] = TORIRSK_8;
    keys.down_count = 1;
    SideApps_DeliverKeys(apps, SIDE_APP_PHASE_WORLD_HOTKEYS, g_host, fake_fires, &keys);
    TEST_ASSERT(strcmp(g_log, "alpha:0:a=2;") == 0, "the second binding fires the action");
    g_log[0] = '\0';
    keys.down[0] = TORIRSK_A;
    SideApps_DeliverKeys(apps, SIDE_APP_PHASE_WORLD_HOTKEYS, g_host, fake_fires, &keys);
    TEST_ASSERT(strcmp(g_log, "alpha:0:a=1;") == 0, "the first binding fires the action");
    /* ...and both down in one frame fire it once, with the first's args. */
    g_log[0] = '\0';
    keys.down[0] = TORIRSK_8;
    keys.down[1] = TORIRSK_A;
    keys.down_count = 2;
    SideApps_DeliverKeys(apps, SIDE_APP_PHASE_WORLD_HOTKEYS, g_host, fake_fires, &keys);
    TEST_ASSERT(strcmp(g_log, "alpha:0:a=1;") == 0, "both keys down: once, first binding's args");

    /* Nothing down: nothing fires. */
    g_log[0] = '\0';
    keys.down_count = 0;
    SideApps_DeliverKeys(apps, SIDE_APP_PHASE_WORLD_HOTKEYS, g_host, fake_fires, &keys);
    TEST_ASSERT(g_log[0] == '\0', "no key, no action");

    g_freed[0] = '\0';
    SideApps_Free(apps);
    TEST_ASSERT(strcmp(g_freed, "betaalpha") == 0, "free runs in reverse registration order");
}

static void
test_unknown_names_reported(void)
{
    static struct SideAppBindingTable table;
    struct FakeTool alpha = { "alpha" };
    struct FakeKeys keys = { { TORIRSK_Z, TORIRSK_A }, 2, SIDE_APP_KEY_PRESS, 0 };

    memset(&table, 0, sizeof(table));
    /* Declared and bound to a name nobody answers. */
    declare(&table, "bad_bound", "no_such_tool");
    bind(&table, TORIRSK_Z, "no_such_tool", "");
    /* Declared, never bound: still a bad t=. */
    declare(&table, "bad_unbound", "also_missing");
    /* Bound twice to an undeclared name: reported once. */
    bind(&table, TORIRSK_X, "undeclared", "");
    bind(&table, TORIRSK_Y, "undeclared", "");
    /* A good one. */
    declare(&table, "ok", "alpha_world");
    bind(&table, TORIRSK_A, "alpha_world", "");

    struct SideApps* apps = SideApps_New(&table);
    SideApps_Register(apps, "alpha", &alpha, &k_hooks, k_alpha_actions, 2);

    TEST_ASSERT(SideApps_UnresolvedCount(apps) == 3, "three names nobody answers");
    struct SideAppUnresolved u0 = SideApps_UnresolvedAt(apps, 0);
    struct SideAppUnresolved u1 = SideApps_UnresolvedAt(apps, 1);
    struct SideAppUnresolved u2 = SideApps_UnresolvedAt(apps, 2);
    TEST_ASSERT(strcmp(u0.action, "no_such_tool") == 0, "first: the bound bad target");
    TEST_ASSERT(strcmp(u0.source, "bad_bound") == 0, "first: names its [action:]");
    TEST_ASSERT(strcmp(u1.action, "also_missing") == 0, "second: the unbound bad target");
    TEST_ASSERT(strcmp(u1.source, "bad_unbound") == 0, "second: names its [action:]");
    TEST_ASSERT(strcmp(u2.action, "undeclared") == 0, "third: the undeclared binding, once");
    TEST_ASSERT(u2.source[0] == '\0', "third: no declaration");

    /* An unresolved binding never fires, and is never even asked about. */
    g_log[0] = '\0';
    keys.asked = 0;
    SideApps_DeliverKeys(apps, SIDE_APP_PHASE_WORLD_HOTKEYS, g_host, fake_fires, &keys);
    TEST_ASSERT(strcmp(g_log, "alpha:0:;") == 0, "only the resolved binding fires");
    TEST_ASSERT(keys.asked == 1, "the predicate is asked about resolved bindings only");

    g_freed[0] = '\0';
    SideApps_Free(apps);
    TEST_ASSERT(strcmp(g_freed, "alpha") == 0, "free runs the one hook");
}

static void
test_polled_and_empty(void)
{
    static struct SideAppBindingTable table;
    static struct SideAppAction const polled[] = {
        { "bridge_a", SIDE_APP_PHASE_POLLED, SIDE_APP_GATE_CTRL, SIDE_APP_KEY_PRESS, 0 },
        { "bridge_b", SIDE_APP_PHASE_POLLED, SIDE_APP_GATE_CTRL, SIDE_APP_KEY_PRESS, 0 },
    };
    static struct SideAppHooks const no_hooks = { 0 };
    struct FakeKeys keys = { { TORIRSK_Q }, 1, SIDE_APP_KEY_PRESS, 0 };

    memset(&table, 0, sizeof(table));
    bind(&table, TORIRSK_Q, "bridge_b", "x");

    struct SideApps* apps = SideApps_New(&table);
    int bridge = SideApps_Register(apps, "bridge", NULL, &no_hooks, polled, 2);
    TEST_ASSERT(SideApps_UnresolvedCount(apps) == 0, "a polled action answers its name");
    TEST_ASSERT(SideApps_BindingCount(apps) == 1, "one binding");
    TEST_ASSERT(SideApps_BindingAnswers(apps, 0, bridge, 1), "the binding is the bridge's second action");
    TEST_ASSERT(strcmp(SideApps_BindingAt(apps, 0)->args, "x") == 0, "the binding keeps its args");
    for( int p = 0; p < SIDE_APP_PHASE_POLLED; p++ )
        SideApps_DeliverKeys(apps, (enum SideAppPhase)p, g_host, fake_fires, &keys);
    TEST_ASSERT(keys.asked == 0, "no phase delivers a polled action");
    SideApps_Free(apps);

    /* An empty table and nothing registered. */
    memset(&table, 0, sizeof(table));
    apps = SideApps_New(&table);
    TEST_ASSERT(SideApps_UnresolvedCount(apps) == 0, "empty: nothing unresolved");
    SideApps_DeliverKeys(apps, SIDE_APP_PHASE_WORLD_HOTKEYS, g_host, fake_fires, &keys);
    SideApps_Free(apps);
    SideApps_Free(NULL);
}

/* Ranks interleave two side-apps' actions within a phase -- the world
 * hotkeys' if-chain of one tool's two keys around another's -- and equal
 * ranks keep registration then table order. */
static void
test_rank_interleaves(void)
{
    static struct SideAppBindingTable table;
    static struct SideAppAction const first[] = {
        { "first_a", SIDE_APP_PHASE_WORLD_HOTKEYS, SIDE_APP_GATE_WORLD, SIDE_APP_KEY_PRESS, 1 },
        { "first_c", SIDE_APP_PHASE_WORLD_HOTKEYS, SIDE_APP_GATE_WORLD, SIDE_APP_KEY_PRESS, 3 },
        { "first_d", SIDE_APP_PHASE_WORLD_HOTKEYS, SIDE_APP_GATE_WORLD, SIDE_APP_KEY_PRESS, 3 },
    };
    static struct SideAppAction const second[] = {
        { "second_b", SIDE_APP_PHASE_WORLD_HOTKEYS, SIDE_APP_GATE_WORLD, SIDE_APP_KEY_PRESS, 2 },
        { "second_e", SIDE_APP_PHASE_WORLD_HOTKEYS, SIDE_APP_GATE_WORLD, SIDE_APP_KEY_PRESS, 3 },
        { "second_z", SIDE_APP_PHASE_WORLD_HOTKEYS, SIDE_APP_GATE_WORLD, SIDE_APP_KEY_PRESS, -1 },
    };
    struct FakeTool one = { "one" };
    struct FakeTool two = { "two" };
    struct FakeKeys keys = { { TORIRSK_A }, 1, SIDE_APP_KEY_PRESS, 0 };

    memset(&table, 0, sizeof(table));
    /* One key bound to every action: each fires, in rank order. */
    bind(&table, TORIRSK_A, "second_e", "");
    bind(&table, TORIRSK_A, "first_d", "");
    bind(&table, TORIRSK_A, "second_b", "");
    bind(&table, TORIRSK_A, "first_c", "");
    bind(&table, TORIRSK_A, "first_a", "");
    bind(&table, TORIRSK_A, "second_z", "");

    struct SideApps* apps = SideApps_New(&table);
    SideApps_Register(apps, "one", &one, &k_hooks, first, 3);
    SideApps_Register(apps, "two", &two, &k_hooks, second, 3);
    g_log[0] = '\0';
    SideApps_DeliverKeys(apps, SIDE_APP_PHASE_WORLD_HOTKEYS, g_host, fake_fires, &keys);
    TEST_ASSERT(
        strcmp(g_log, "two:2:;one:0:;two:0:;one:1:;one:2:;two:1:;") == 0,
        "rank first, then registration, then table order");
    SideApps_Free(apps);
}

/* keys_done follows each delivery of a phase the side-app answers, a key or
 * none; keys_probe says whether a key went down (asked as a press, even of a
 * held action) and fires nothing. */
static void
test_done_and_probe(void)
{
    static struct SideAppBindingTable table;
    static struct SideAppAction const stepper[] = {
        { "step_toggle", SIDE_APP_PHASE_CAMERA_KEYS, SIDE_APP_GATE_CTRL, SIDE_APP_KEY_PRESS, 0 },
        { "step_more", SIDE_APP_PHASE_CAMERA_KEYS, SIDE_APP_GATE_CTRL, SIDE_APP_KEY_HELD, 0 },
    };
    static struct SideAppAction const flyer[] = {
        { "fly_up", SIDE_APP_PHASE_CAMERA_FLIGHT, SIDE_APP_GATE_CTRL, SIDE_APP_KEY_HELD, 0 },
    };
    struct FakeTool step = { "step" };
    struct FakeTool fly = { "fly" };
    struct FakeKeys keys = { { 0 }, 0, SIDE_APP_KEY_PRESS, 0 };

    memset(&table, 0, sizeof(table));
    bind(&table, TORIRSK_I, "step_toggle", "");
    bind(&table, TORIRSK_J, "step_more", "");
    bind(&table, TORIRSK_W, "fly_up", "");

    struct SideApps* apps = SideApps_New(&table);
    SideApps_Register(apps, "step", &step, &k_hooks_phased, stepper, 2);
    SideApps_Register(apps, "fly", &fly, &k_hooks_phased, flyer, 1);

    /* No key: keys_done still runs, for the phase's side-app only. */
    g_log[0] = '\0';
    SideApps_DeliverKeys(apps, SIDE_APP_PHASE_CAMERA_KEYS, g_host, fake_fires, &keys);
    TEST_ASSERT(strcmp(g_log, "step:done2;") == 0, "keys_done with no key, own phase only");

    /* Toggle and stepper in one frame: both keys, then done. */
    g_log[0] = '\0';
    keys.down[0] = TORIRSK_I;
    keys.down_count = 1;
    SideApps_DeliverKeys(apps, SIDE_APP_PHASE_CAMERA_KEYS, g_host, fake_fires, &keys);
    keys.down[1] = TORIRSK_J;
    keys.down_count = 2;
    keys.mode = SIDE_APP_KEY_HELD;
    SideApps_DeliverKeys(apps, SIDE_APP_PHASE_CAMERA_KEYS, g_host, fake_fires, &keys);
    TEST_ASSERT(
        strcmp(g_log, "step:0:;step:done2;step:1:;step:done2;") == 0,
        "the keys of a delivery, then its keys_done");

    /* The probe: a held action's key going down counts, read as a press. */
    g_log[0] = '\0';
    keys.down[0] = TORIRSK_J;
    keys.down_count = 1;
    keys.mode = SIDE_APP_KEY_PRESS;
    SideApps_ProbeKeys(apps, SIDE_APP_PHASE_CAMERA_KEYS, g_host, fake_fires, &keys);
    keys.down_count = 0;
    SideApps_ProbeKeys(apps, SIDE_APP_PHASE_CAMERA_KEYS, g_host, fake_fires, &keys);
    keys.mode = SIDE_APP_KEY_HELD;
    keys.down[0] = TORIRSK_I;
    keys.down_count = 1;
    SideApps_ProbeKeys(apps, SIDE_APP_PHASE_CAMERA_KEYS, g_host, fake_fires, &keys);
    TEST_ASSERT(
        strcmp(g_log, "step:probe2=1;step:probe2=0;step:probe2=0;") == 0,
        "probe: down as a press, nothing fired, own phase only");

    /* The flight phase is its own: only the flyer's key and done. */
    g_log[0] = '\0';
    keys.down[0] = TORIRSK_W;
    keys.down_count = 1;
    keys.mode = SIDE_APP_KEY_HELD;
    SideApps_DeliverKeys(apps, SIDE_APP_PHASE_CAMERA_FLIGHT, g_host, fake_fires, &keys);
    TEST_ASSERT(strcmp(g_log, "fly:0:;fly:done1;") == 0, "flight phase delivers the flyer only");

    g_freed[0] = '\0';
    SideApps_Free(apps);
    TEST_ASSERT(strcmp(g_freed, "flystep") == 0, "free in reverse registration order");
}

/* The frame, chrome and overlay hooks: every side-app that has one, in
 * registration order, whatever actions it answers; the scenery question is
 * an OR over the side-apps that answer it. */
struct FakeScenery
{
    char const* name;
    int inspects;
};

static struct LibToriRS_Input g_frame_input;

static void
fake_frame(
    void* self,
    struct SideAppHost* host,
    enum SideAppPhase phase,
    struct LibToriRS_Input* input)
{
    struct FakeScenery* tool = self;
    char line[128];

    TEST_ASSERT(host == g_host, "the frame hook gets the host");
    TEST_ASSERT(input == &g_frame_input, "and the frame's input");
    snprintf(line, sizeof(line), "%s:frame%d;", tool->name, (int)phase);
    strncat(g_log, line, sizeof(g_log) - strlen(g_log) - 1);
}

static void
fake_build_chrome(
    void* self,
    struct SideAppHost* host)
{
    struct FakeScenery* tool = self;
    char line[128];

    TEST_ASSERT(host == g_host, "the chrome hook gets the host");
    snprintf(line, sizeof(line), "%s:build;", tool->name);
    strncat(g_log, line, sizeof(g_log) - strlen(g_log) - 1);
}

static void
fake_world_overlay(
    void* self,
    struct SideAppHost* host)
{
    struct FakeScenery* tool = self;
    char line[128];

    TEST_ASSERT(host == g_host, "the overlay hook gets the host");
    snprintf(line, sizeof(line), "%s:draw;", tool->name);
    strncat(g_log, line, sizeof(g_log) - strlen(g_log) - 1);
}

static int
fake_inspects_scenery(void const* self)
{
    struct FakeScenery const* tool = self;
    return tool->inspects;
}

static void
test_frame_chrome_overlay_scenery(void)
{
    static struct SideAppBindingTable table;
    static struct SideAppAction const panel_actions[] = {
        { "panel_toggle", SIDE_APP_PHASE_EARLY_CHROME, SIDE_APP_GATE_CTRL, SIDE_APP_KEY_PRESS, 0 },
    };
    static struct SideAppAction const marker_actions[] = {
        { "marker_toggle", SIDE_APP_PHASE_DEV_PANELS, SIDE_APP_GATE_CTRL, SIDE_APP_KEY_PRESS, 0 },
    };
    static struct SideAppAction const quiet_actions[] = {
        { "quiet", SIDE_APP_PHASE_WORLD_HOTKEYS, SIDE_APP_GATE_WORLD, SIDE_APP_KEY_PRESS, 0 },
    };
    static struct SideAppHooks const panel_hooks = {
        .key = fake_key,
        .frame = fake_frame,
        .build_chrome = fake_build_chrome,
    };
    static struct SideAppHooks const marker_hooks = {
        .key = fake_key,
        .frame = fake_frame,
        .build_chrome = fake_build_chrome,
        .world_overlay = fake_world_overlay,
        .inspects_scenery = fake_inspects_scenery,
    };
    static struct SideAppHooks const quiet_hooks = { .key = fake_key };
    struct FakeScenery panel = { "panel", 0 };
    struct FakeScenery marker = { "marker", 0 };
    struct FakeScenery quiet = { "quiet", 0 };
    struct FakeKeys keys = { { TORIRSK_M }, 1, SIDE_APP_KEY_PRESS, 0 };

    memset(&table, 0, sizeof(table));
    bind(&table, TORIRSK_M, "marker_toggle", "");

    struct SideApps* apps = SideApps_New(&table);
    SideApps_Register(apps, "panel", &panel, &panel_hooks, panel_actions, 1);
    SideApps_Register(apps, "quiet", &quiet, &quiet_hooks, quiet_actions, 1);
    SideApps_Register(apps, "marker", &marker, &marker_hooks, marker_actions, 1);

    g_log[0] = '\0';
    SideApps_BuildChrome(apps, g_host);
    TEST_ASSERT(strcmp(g_log, "panel:build;marker:build;") == 0, "chrome built in registration order");

    /* The frame hook runs at any point it is asked at, for every side-app
     * that has one, told which point. */
    g_log[0] = '\0';
    SideApps_Frame(apps, SIDE_APP_PHASE_EARLY_CHROME, g_host, &g_frame_input);
    SideApps_Frame(apps, SIDE_APP_PHASE_WORLD_HOTKEYS, g_host, &g_frame_input);
    TEST_ASSERT(
        strcmp(g_log, "panel:frame0;marker:frame0;panel:frame3;marker:frame3;") == 0,
        "frame hooks in registration order, told the point");

    g_log[0] = '\0';
    SideApps_DrawWorldOverlay(apps, g_host);
    TEST_ASSERT(strcmp(g_log, "marker:draw;") == 0, "only the side-app with an overlay draws");

    /* The dev panels' point delivers its own action only. */
    g_log[0] = '\0';
    SideApps_DeliverKeys(apps, SIDE_APP_PHASE_DEV_PANELS, g_host, fake_fires, &keys);
    TEST_ASSERT(strcmp(g_log, "marker:0:;") == 0, "the dev panels point delivers its action");

    TEST_ASSERT(!SideApps_AnyInspectsScenery(apps), "nobody inspects: no");
    marker.inspects = 1;
    TEST_ASSERT(SideApps_AnyInspectsScenery(apps), "one inspects: yes");
    marker.inspects = 0;
    TEST_ASSERT(!SideApps_AnyInspectsScenery(apps), "and no again");
    SideApps_Free(apps);
}

/* The keyboard question is an OR like the scenery one; every side-app with a
 * menu_flags hook sets its flags in the one build context; a chosen menu
 * action is offered in registration order and stops at the first side-app
 * that says it is its own. */
struct FakeMenuCtx
{
    int editor_flag;
    int other_flag;
};

struct FakeMenuTool
{
    char const* name;
    int holds;
    int flag;
    /** The menu action this tool takes, -1 for none. */
    int owns_action;
};

static int
fake_holds_keyboard(void const* self)
{
    struct FakeMenuTool const* tool = self;
    return tool->holds;
}

static void
fake_menu_flags_editor(
    void const* self,
    void* build_ctx)
{
    struct FakeMenuTool const* tool = self;
    struct FakeMenuCtx* ctx = build_ctx;
    ctx->editor_flag = tool->flag;
}

static void
fake_menu_flags_other(
    void const* self,
    void* build_ctx)
{
    struct FakeMenuTool const* tool = self;
    struct FakeMenuCtx* ctx = build_ctx;
    ctx->other_flag = tool->flag;
}

static int
fake_menu_action(
    void* self,
    struct SideAppHost* host,
    int action,
    void const* option)
{
    struct FakeMenuTool* tool = self;
    char line[128];

    TEST_ASSERT(host == g_host, "the menu action hook gets the host");
    TEST_ASSERT(*(int const*)option == 77, "and the host's row");
    snprintf(line, sizeof(line), "%s:menu%d;", tool->name, action);
    strncat(g_log, line, sizeof(g_log) - strlen(g_log) - 1);
    return action == tool->owns_action;
}

static void
test_keyboard_and_menu(void)
{
    static struct SideAppBindingTable table;
    static struct SideAppHooks const editor_hooks = {
        .holds_keyboard = fake_holds_keyboard,
        .menu_flags = fake_menu_flags_editor,
        .menu_action = fake_menu_action,
    };
    static struct SideAppHooks const other_hooks = {
        .menu_flags = fake_menu_flags_other,
        .menu_action = fake_menu_action,
    };
    static struct SideAppHooks const bare_hooks = { 0 };
    static struct SideAppAction const bare_actions[] = {
        { "bare_polled", SIDE_APP_PHASE_POLLED, SIDE_APP_GATE_CTRL, SIDE_APP_KEY_PRESS, 0 },
    };
    static struct SideAppAction const editor_actions[] = {
        { "editor_polled", SIDE_APP_PHASE_POLLED, SIDE_APP_GATE_CTRL, SIDE_APP_KEY_PRESS, 0 },
    };
    static struct SideAppAction const other_actions[] = {
        { "other_polled", SIDE_APP_PHASE_POLLED, SIDE_APP_GATE_CTRL, SIDE_APP_KEY_PRESS, 0 },
    };
    struct FakeMenuTool editor = { "editor", 0, 1, 5 };
    struct FakeMenuTool other = { "other", 0, 2, 6 };
    struct FakeMenuTool bare = { "bare", 1, 0, -1 };
    struct FakeMenuCtx ctx = { 0, 0 };
    int const row = 77;

    memset(&table, 0, sizeof(table));
    struct SideApps* apps = SideApps_New(&table);
    SideApps_Register(apps, "bare", &bare, &bare_hooks, bare_actions, 1);
    SideApps_Register(apps, "editor", &editor, &editor_hooks, editor_actions, 1);
    SideApps_Register(apps, "other", &other, &other_hooks, other_actions, 1);

    TEST_ASSERT(!SideApps_AnyHoldsKeyboard(apps), "nobody holds the keyboard (no hook is no)");
    editor.holds = 1;
    TEST_ASSERT(SideApps_AnyHoldsKeyboard(apps), "one holds it: yes");
    editor.holds = 0;

    SideApps_MenuFlags(apps, &ctx);
    TEST_ASSERT(ctx.editor_flag == 1, "the first side-app set its flag");
    TEST_ASSERT(ctx.other_flag == 2, "and so did the second");

    g_log[0] = '\0';
    TEST_ASSERT(SideApps_MenuAction(apps, g_host, 5, &row), "the editor's action is taken");
    TEST_ASSERT(strcmp(g_log, "editor:menu5;") == 0, "and the offer stops there");
    g_log[0] = '\0';
    TEST_ASSERT(SideApps_MenuAction(apps, g_host, 6, &row), "the other's action is taken");
    TEST_ASSERT(strcmp(g_log, "editor:menu6;other:menu6;") == 0, "offered in registration order");
    g_log[0] = '\0';
    TEST_ASSERT(!SideApps_MenuAction(apps, g_host, 9, &row), "an action nobody owns is not taken");
    TEST_ASSERT(strcmp(g_log, "editor:menu9;other:menu9;") == 0, "after everyone was asked");
    SideApps_Free(apps);
}

/* The predicate is asked under the mode the action declared: a press-or-held
 * action is asked as that, never as a press or a held key. */
static enum SideAppKeyMode g_asked_mode;

static int
record_mode_fires(
    void* ctx,
    struct SideAppBinding const* binding,
    enum SideAppGate gate,
    enum SideAppKeyMode mode)
{
    (void)ctx;
    (void)binding;
    TEST_ASSERT(gate == SIDE_APP_GATE_CTRL, "the action's gate reaches the predicate");
    g_asked_mode = mode;
    return 1;
}

static void
test_press_or_held_mode(void)
{
    static struct SideAppBindingTable table;
    static struct SideAppAction const window_actions[] = {
        { "window_toggle", SIDE_APP_PHASE_PLUGIN_WINDOW, SIDE_APP_GATE_CTRL, SIDE_APP_KEY_PRESS_OR_HELD, 0 },
    };
    struct FakeTool window = { "window" };

    memset(&table, 0, sizeof(table));
    bind(&table, TORIRSK_P, "window_toggle", "");

    struct SideApps* apps = SideApps_New(&table);
    SideApps_Register(apps, "window", &window, &k_hooks_phased, window_actions, 1);
    g_log[0] = '\0';
    g_asked_mode = SIDE_APP_KEY_PRESS;
    SideApps_DeliverKeys(apps, SIDE_APP_PHASE_PLUGIN_WINDOW, g_host, record_mode_fires, NULL);
    TEST_ASSERT(g_asked_mode == SIDE_APP_KEY_PRESS_OR_HELD, "asked as press-or-held");
    TEST_ASSERT(strcmp(g_log, "window:0:;window:done5;") == 0, "fired, then done, at the plugin window point");
    g_freed[0] = '\0';
    SideApps_Free(apps);
}

int
main(void)
{
    test_order_phases_and_two_bindings();
    test_unknown_names_reported();
    test_polled_and_empty();
    test_rank_interleaves();
    test_done_and_probe();
    test_frame_chrome_overlay_scenery();
    test_press_or_held_mode();
    test_keyboard_and_menu();
    if( g_failures )
    {
        printf("side_app: %d check(s) failed\n", g_failures);
        return 1;
    }
    printf("side_app: all checks passed\n");
    return 0;
}
