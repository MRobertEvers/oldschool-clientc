/*
 * The XP Tracker and the Loot Tracker, run against a hand-built api table.
 *
 * No plugin host here, for item-stats' reason: both plugins are entirely
 * "given these events and these api answers, what does the page say", and
 * answering the verbs directly makes the test a table of cases rather than a
 * client. The page IS the assertion target -- every readout goes through
 * panel_widget / panel_set_text, so the fake below simply keeps the model and
 * the cases read rows out of it by id.
 *
 * The two halves under test that are worth naming, because neither is obvious
 * from a screenshot:
 *
 *   - The XP tracker's rates are measured over TRAINING time and not over wall
 *     clock, so the clock this fake hands out is stepped by the test rather
 *     than read from the machine. A case can therefore state "two minutes of
 *     woodcutting" exactly.
 *   - The loot tracker attributes a drop by correlating a despawn with the
 *     item spawns that follow it inside the npc's footprint. Every failure
 *     mode of that is a case here: the drop that lands outside the footprint,
 *     the despawn that never drops anything, and the big monster whose loot
 *     lands two tiles from its south-west corner.
 */

#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
static int g_checks;

#define TEST_ASSERT(cond, ...)                                                                 \
    do                                                                                         \
    {                                                                                          \
        g_checks++;                                                                            \
        if( !(cond) )                                                                          \
        {                                                                                      \
            g_failures++;                                                                      \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                        \
            printf(__VA_ARGS__);                                                                \
            printf("\n");                                                                      \
        }                                                                                      \
    } while( 0 )

/* ------------------------------------------------------------- the client */

#define FAKE_SKILLS 25
#define FAKE_WIDGETS 64
#define FAKE_CONFIG 16
#define FAKE_ASSET_MAX 16384

static char const* const FAKE_SKILL_NAME[FAKE_SKILLS] = {
    "Attack", "Defence", "Strength", "Hitpoints", "Ranged", "Prayer", "Magic",
    "Cooking", "Woodcutting", "Fletching", "Fishing", "Firemaking", "Crafting",
    "Smithing", "Mining", "Herblore", "Agility", "Thieving", "Slayer", "Farming",
    "Runecraft", "Hunter", "Construction", "Sailing", "Summoning"
};

/* The client's xp table, far enough to reach the levels the cases use. */
static int const FAKE_LEVEL_XP[100] = {
    0, 0, 83, 174, 276, 388, 512, 650, 801, 969, 1154, 1358, 1584, 1833, 2107,
    2411, 2746, 3115, 3523, 3973, 4470, 5018, 5624, 6291, 7028, 7842, 8740,
    9730, 10824, 12031, 13363, 14833, 16456, 18247, 20224, 22406, 24815, 27473,
    30408, 33648, 37224, 41171, 45529, 50339, 55649, 61512, 67983, 75127, 83014,
    91721, 101333, 111945, 123660, 136594, 150872, 166636, 184040, 203254,
    224466, 247886, 273742, 302288, 333804, 368599, 407015, 449428, 496254,
    547953, 605032, 668051, 737627, 814445, 899257, 992895, 1096278, 1210421,
    1336443, 1475581, 1629200, 1798808, 1986068, 2192818, 2421087, 2673114,
    2951373, 3258594, 3597792, 3972294, 4385776, 4842295, 5346332, 5902831,
    6517253, 7195629, 7944614, 8771558, 9684577, 10692629, 11805606, 13034431
};

struct FakeWidget
{
    char id[TORIRS_PLUGIN_WIDGET_ID_MAX];
    char label[64];
    char text[128];
    int kind;
    int value;
    int height;
    bool live;
};

struct FakeConfig
{
    char key[32];
    char value[192];
};

static struct
{
    uint64_t now_ms;
    bool logged_in;
    struct ToriRS_PluginPlayerSnap me;
    int xp[FAKE_SKILLS];
    int level[FAKE_SKILLS];

    struct FakeWidget widget[FAKE_WIDGETS];
    int widget_count;
    /** True while the plugin is inside its EV_PANEL_BUILD dispatch. */
    bool building;
    /** Raised by panel_clear outside a build: the page wants rebuilding. */
    bool rebuild_wanted;
    int builds;

    struct FakeConfig config[FAKE_CONFIG];
    int config_count;

    char panel_title[64];
    char badge[TORIRS_PLUGIN_PANEL_BADGE_MAX];
    bool attention;

    char notify[200];
    int notifies;

    /* The asset store: one file, which is all either plugin uses. */
    char asset_name[64];
    char asset_bytes[FAKE_ASSET_MAX];
    int asset_size;
    bool asset_present;

    /* What the custom draw pass did. */
    int icons_asked;
    int icons_drawn;
    int last_icon_obj;
    int last_icon_count;
    int last_icon_style;
} g_client;

static ToriRS_PluginHandler g_handler[TORIRS_PLUGIN_EV_COUNT];

/* ------------------------------------------------------------------ verbs */

static void
fake_subscribe(
    struct ToriRS_PluginCtx* ctx,
    enum ToriRS_PluginEvent event,
    ToriRS_PluginHandler handler,
    void* userdata)
{
    (void)ctx;
    (void)userdata;
    assert(event >= 0 && event < TORIRS_PLUGIN_EV_COUNT);
    g_handler[event] = handler;
}

static void
fake_log(struct ToriRS_PluginCtx* ctx, char const* fmt, ...)
{
    (void)ctx;
    (void)fmt;
}

static void
fake_notify(struct ToriRS_PluginCtx* ctx, char const* text)
{
    (void)ctx;
    snprintf(g_client.notify, sizeof(g_client.notify), "%s", text);
    g_client.notifies++;
}

static uint64_t
fake_frame_ms(struct ToriRS_PluginCtx* ctx)
{
    (void)ctx;
    return g_client.now_ms;
}

static int
fake_local_player(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginPlayerSnap* out)
{
    (void)ctx;
    if( !g_client.logged_in )
        return 0;
    *out = g_client.me;
    return 1;
}

static char const*
fake_skill_name(struct ToriRS_PluginCtx* ctx, int skill)
{
    (void)ctx;
    return (skill >= 0 && skill < FAKE_SKILLS) ? FAKE_SKILL_NAME[skill] : NULL;
}

static int
fake_stat(struct ToriRS_PluginCtx* ctx, int skill, int* out_current, int* out_base)
{
    (void)ctx;
    if( skill < 0 || skill >= FAKE_SKILLS )
        return 0;
    if( out_current )
        *out_current = g_client.level[skill];
    if( out_base )
        *out_base = g_client.level[skill];
    return 1;
}

static int
fake_stat_xp(
    struct ToriRS_PluginCtx* ctx,
    int skill,
    int* out_xp,
    int* out_level_xp,
    int* out_next_xp)
{
    (void)ctx;
    if( skill < 0 || skill >= FAKE_SKILLS )
        return 0;
    if( out_xp )
        *out_xp = g_client.xp[skill];
    if( out_level_xp )
        *out_level_xp = FAKE_LEVEL_XP[g_client.level[skill]];
    /* 0 at the top of the table, exactly as the client answers at 99. */
    if( out_next_xp )
        *out_next_xp = g_client.level[skill] >= 99
                           ? 0
                           : FAKE_LEVEL_XP[g_client.level[skill] + 1];
    return 1;
}

/* ---- config ---- */

static struct FakeConfig*
fake_config_find(char const* key)
{
    for( int i = 0; i < g_client.config_count; i++ )
        if( strcmp(g_client.config[i].key, key) == 0 )
            return &g_client.config[i];
    return NULL;
}

static void
fake_config_set_raw(char const* key, char const* value)
{
    struct FakeConfig* row = fake_config_find(key);

    if( !row )
    {
        assert(g_client.config_count < FAKE_CONFIG);
        row = &g_client.config[g_client.config_count++];
        snprintf(row->key, sizeof(row->key), "%s", key);
    }
    snprintf(row->value, sizeof(row->value), "%s", value);
}

static char const*
fake_cfg_str(struct ToriRS_PluginCtx* ctx, char const* key)
{
    struct FakeConfig const* row;
    (void)ctx;
    row = fake_config_find(key);
    return row ? row->value : "";
}

static int
fake_cfg_bool(struct ToriRS_PluginCtx* ctx, char const* key)
{
    return atoi(fake_cfg_str(ctx, key)) != 0;
}

static int
fake_cfg_int(struct ToriRS_PluginCtx* ctx, char const* key)
{
    return atoi(fake_cfg_str(ctx, key));
}

static void
fake_cfg_set(struct ToriRS_PluginCtx* ctx, char const* key, char const* value)
{
    (void)ctx;
    fake_config_set_raw(key, value);
}

/* ---- assets ---- */

static int
fake_asset_load(struct ToriRS_PluginCtx* ctx, char const* name)
{
    (void)ctx;
    /* Resident synchronously when the store holds it, which is the "1" branch
     * of the contract; a miss queues and the plugin is told at EV_ASSET. */
    return g_client.asset_present && strcmp(g_client.asset_name, name) == 0 ? 1 : 0;
}

static void const*
fake_asset_data(struct ToriRS_PluginCtx* ctx, char const* name, int* out_size)
{
    (void)ctx;
    if( !g_client.asset_present || strcmp(g_client.asset_name, name) != 0 )
        return NULL;
    if( out_size )
        *out_size = g_client.asset_size;
    return g_client.asset_bytes;
}

static int
fake_asset_save(
    struct ToriRS_PluginCtx* ctx, char const* name, void const* data, int size)
{
    (void)ctx;
    assert(size >= 0);
    assert(size <= FAKE_ASSET_MAX);
    snprintf(g_client.asset_name, sizeof(g_client.asset_name), "%s", name);
    memcpy(g_client.asset_bytes, data, (size_t)size);
    g_client.asset_size = size;
    g_client.asset_present = true;
    return 1;
}

static void
fake_asset_release(struct ToriRS_PluginCtx* ctx, char const* name)
{
    (void)ctx;
    (void)name;
}

/* ---- the panel model ---- */

static struct FakeWidget*
fake_widget_find(char const* id)
{
    for( int i = 0; i < g_client.widget_count; i++ )
        if( g_client.widget[i].live && strcmp(g_client.widget[i].id, id) == 0 )
            return &g_client.widget[i];
    return NULL;
}

static bool
fake_panel_request(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginPanelDesc const* desc)
{
    (void)ctx;
    assert(desc);
    snprintf(
        g_client.panel_title, sizeof(g_client.panel_title), "%s",
        desc->title ? desc->title : "");
    return true;
}

static bool
fake_panel_widget(struct ToriRS_PluginCtx* ctx, int kind, char const* id, char const* label)
{
    struct FakeWidget* w;
    (void)ctx;

    /* The host refuses a declaration outside the build dispatch; so does this,
     * because a plugin that declared from a tick would work here and not in
     * the client. */
    if( !g_client.building )
        return false;
    if( fake_widget_find(id) )
        return true;
    if( g_client.widget_count >= FAKE_WIDGETS )
        return false;
    w = &g_client.widget[g_client.widget_count++];
    memset(w, 0, sizeof(*w));
    snprintf(w->id, sizeof(w->id), "%s", id);
    snprintf(w->label, sizeof(w->label), "%s", label ? label : "");
    w->kind = kind;
    w->value = -1;
    w->live = true;
    return true;
}

static bool
fake_panel_set_text(struct ToriRS_PluginCtx* ctx, char const* id, char const* text)
{
    struct FakeWidget* w;
    (void)ctx;
    w = fake_widget_find(id);
    if( !w )
        return false;
    snprintf(w->text, sizeof(w->text), "%s", text ? text : "");
    return true;
}

static bool
fake_panel_set_value(struct ToriRS_PluginCtx* ctx, char const* id, int value)
{
    struct FakeWidget* w;
    (void)ctx;
    w = fake_widget_find(id);
    if( !w )
        return false;
    w->value = value;
    return true;
}

static bool
fake_panel_set_height(struct ToriRS_PluginCtx* ctx, char const* id, int height)
{
    struct FakeWidget* w;
    (void)ctx;
    w = fake_widget_find(id);
    if( !w )
        return false;
    w->height = height;
    return true;
}

static bool
fake_panel_set_badge(struct ToriRS_PluginCtx* ctx, char const* text)
{
    (void)ctx;
    snprintf(g_client.badge, sizeof(g_client.badge), "%s", text ? text : "");
    return true;
}

static bool
fake_panel_set_attention(struct ToriRS_PluginCtx* ctx, bool on)
{
    (void)ctx;
    g_client.attention = on;
    return true;
}

static void
fake_panel_clear(struct ToriRS_PluginCtx* ctx)
{
    (void)ctx;
    g_client.widget_count = 0;
    memset(g_client.widget, 0, sizeof(g_client.widget));
    /* Outside a build this is a REQUEST for one, which is what the host does
     * with it. Inside one it is the clear and nothing more. */
    if( !g_client.building )
        g_client.rebuild_wanted = true;
}

static void
fake_panel_invalidate(struct ToriRS_PluginCtx* ctx, char const* id)
{
    (void)ctx;
    (void)id;
}

/* ---- images ---- */

static int
fake_obj_image(struct ToriRS_PluginCtx* ctx, int obj_id, int count, int style)
{
    (void)ctx;
    g_client.icons_asked++;
    g_client.last_icon_obj = obj_id;
    g_client.last_icon_count = count;
    g_client.last_icon_style = style;
    /* An id below 100 stands for an objtype whose inventory model is not
     * resident yet -- the "ask again next frame" answer, which the plugin has
     * to survive without drawing anything. */
    return obj_id < 100 ? -1 : 40 + (obj_id % 8);
}

static int
fake_image_size(struct ToriRS_PluginCtx* ctx, int image, int* out_w, int* out_h)
{
    (void)ctx;
    if( image < 0 )
        return 0;
    if( out_w )
        *out_w = 36;
    if( out_h )
        *out_h = 32;
    return 1;
}

static void
fake_draw_image(
    struct ToriRS_PluginCtx* ctx,
    void* surface,
    int image,
    int x,
    int y,
    int clip_x,
    int clip_y,
    int clip_w,
    int clip_h,
    int trans)
{
    (void)ctx;
    (void)surface;
    (void)image;
    (void)x;
    (void)y;
    (void)clip_x;
    (void)clip_y;
    (void)clip_w;
    (void)clip_h;
    (void)trans;
    g_client.icons_drawn++;
}

static struct ToriRS_PluginApi g_api;

static void
api_init(void)
{
    memset(&g_api, 0, sizeof(g_api));
    g_api.abi_version = TORIRS_PLUGIN_ABI;
    g_api.subscribe = fake_subscribe;
    g_api.log = fake_log;
    g_api.notify = fake_notify;
    g_api.frame_ms = fake_frame_ms;
    g_api.local_player = fake_local_player;
    g_api.skill_name = fake_skill_name;
    g_api.stat = fake_stat;
    g_api.stat_xp = fake_stat_xp;
    g_api.cfg_bool = fake_cfg_bool;
    g_api.cfg_int = fake_cfg_int;
    g_api.cfg_str = fake_cfg_str;
    g_api.cfg_set = fake_cfg_set;
    g_api.asset_load = fake_asset_load;
    g_api.asset_data = fake_asset_data;
    g_api.asset_save = fake_asset_save;
    g_api.asset_release = fake_asset_release;
    g_api.panel_request = fake_panel_request;
    g_api.panel_widget = fake_panel_widget;
    g_api.panel_set_text = fake_panel_set_text;
    g_api.panel_set_value = fake_panel_set_value;
    g_api.panel_set_height = fake_panel_set_height;
    g_api.panel_set_badge = fake_panel_set_badge;
    g_api.panel_set_attention = fake_panel_set_attention;
    g_api.panel_clear = fake_panel_clear;
    g_api.panel_invalidate = fake_panel_invalidate;
    g_api.obj_image = fake_obj_image;
    g_api.image_size = fake_image_size;
    g_api.draw_image = fake_draw_image;
}

/* ---------------------------------------------------------------- driving */

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_XP_TRACKER;
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_LOOT_TRACKER;

/* The plugins assert their context is real and never look inside it -- the
 * host owns that struct -- so any address does, and using one keeps their own
 * contract checks live. */
static struct ToriRS_PluginCtx* const CTX = (struct ToriRS_PluginCtx*)&g_client;

static void
dispatch(enum ToriRS_PluginEvent event, void* payload)
{
    if( g_handler[event] )
        g_handler[event](CTX, payload, NULL);
}

/** Run one EV_PANEL_BUILD for one face, with the model emptied as the host
 *  empties it. @see enum ToriRS_PluginPanelView. */
static void
panel_build_view(int view)
{
    struct ToriRS_PluginEvPanelBuild ev;

    memset(&ev, 0, sizeof(ev));
    ev.selection_generation = 1;
    ev.view = view;
    g_client.widget_count = 0;
    memset(g_client.widget, 0, sizeof(g_client.widget));
    g_client.rebuild_wanted = false;
    g_client.building = true;
    g_client.builds++;
    dispatch(TORIRS_PLUGIN_EV_PANEL_BUILD, &ev);
    g_client.building = false;

    /* The shell then states the allocation, and that is what tells a plugin
     * its page is on SCREEN -- both trackers do no per-tick page work until it
     * arrives, so a harness that skipped it would be testing a hidden page. */
    {
        struct ToriRS_PluginEvPanelLayout layout;

        memset(&layout, 0, sizeof(layout));
        layout.width = 320;
        layout.height = 500;
        layout.scale_milli = 1000;
        layout.size_class = TORIRS_PLUGIN_PANEL_MEDIUM;
        layout.visible = true;
        layout.game_visible = true;
        layout.selection_generation = 1;
        dispatch(TORIRS_PLUGIN_EV_PANEL_LAYOUT, &layout);
    }
}

/** The plugin's own screen, which is what the rail entry opens. */
static void
panel_build(void)
{
    panel_build_view(TORIRS_PLUGIN_PANEL_VIEW_PAGE);
}

/** Advance the clock and run one logic tick, rebuilding the page if the tick
 *  asked for one -- which is what the client's next selected frame does. */
static void
tick(uint64_t advance_ms)
{
    struct ToriRS_PluginEvTick ev;

    memset(&ev, 0, sizeof(ev));
    g_client.now_ms += advance_ms;
    dispatch(TORIRS_PLUGIN_EV_LOGIC_TICK, &ev);
    if( g_client.rebuild_wanted )
        panel_build();
}

static void
press(char const* id, int action, int value)
{
    struct ToriRS_PluginEvPanelAction ev;

    memset(&ev, 0, sizeof(ev));
    ev.id = id;
    ev.action = action;
    ev.value = value;
    ev.text = "";
    ev.selection_generation = 1;
    dispatch(TORIRS_PLUGIN_EV_PANEL_ACTION, &ev);
    if( g_client.rebuild_wanted )
        panel_build();
}

/** The text of one page row, or NULL when the page has no such row. */
static char const*
row_text(char const* id)
{
    struct FakeWidget const* w = fake_widget_find(id);
    return w ? w->text : NULL;
}

static void
client_reset(void)
{
    memset(&g_client, 0, sizeof(g_client));
    memset(g_handler, 0, sizeof(g_handler));
    g_client.now_ms = 100000;
    g_client.logged_in = true;
    g_client.me.true_x = 3200;
    g_client.me.true_z = 3200;
    g_client.me.level = 0;
    for( int i = 0; i < FAKE_SKILLS; i++ )
        g_client.level[i] = 1;
}

/* ====================================================================== */
/* XP tracker                                                              */
/* ====================================================================== */

#define SKILL_WOODCUTTING 8
#define SKILL_ATTACK 0

static void
xp_start(void)
{
    fake_config_set_raw("save_state", "0");
    fake_config_set_raw("hide_maxed", "0");
    fake_config_set_raw("pause_on_logout", "1");
    fake_config_set_raw("pause_skill_after", "0");
    fake_config_set_raw("reset_rate_after", "0");
    TORIRS_PLUGIN_XP_TRACKER.init(CTX, &g_api);
    dispatch(TORIRS_PLUGIN_EV_START, NULL);
    panel_build();
}

static void
test_xp_first_reading_seeds(void)
{
    client_reset();
    g_client.xp[SKILL_WOODCUTTING] = 50000;
    xp_start();

    /* Every stat arrives at once on login and none of it is a gain. */
    tick(20);
    TEST_ASSERT(
        row_text("total_xp") && strcmp(row_text("total_xp"), "0") == 0,
        "the first sight of a stat table seeds rather than reporting a gain (got '%s')",
        row_text("total_xp") ? row_text("total_xp") : "(none)");
    TEST_ASSERT(
        !fake_widget_find("sk8"), "and no skill has a row until it has been trained");
    TEST_ASSERT(fake_widget_find("empty"), "the empty page says so");
}

static void
test_xp_gain_makes_a_row(void)
{
    client_reset();
    g_client.xp[SKILL_WOODCUTTING] = 50000;
    xp_start();
    tick(20);

    g_client.xp[SKILL_WOODCUTTING] = 50100;
    tick(600);

    TEST_ASSERT(fake_widget_find("sk8"), "a trained skill gets a row");
    TEST_ASSERT(
        row_text("total_xp") && strcmp(row_text("total_xp"), "100") == 0,
        "the session total is the gain (got '%s')",
        row_text("total_xp") ? row_text("total_xp") : "(none)");
    TEST_ASSERT(!fake_widget_find("empty"), "and the empty note is gone");
}

static void
test_xp_rate_is_over_training_time(void)
{
    client_reset();
    g_client.xp[SKILL_WOODCUTTING] = 0;
    g_client.level[SKILL_WOODCUTTING] = 1;
    xp_start();
    tick(20);

    /* One minute of training, 600 xp: the elapsed floor is exactly 60s here,
     * so the expected rate is unambiguous -- 600 * 3600 / 60 = 36,000. */
    for( int i = 0; i < 60; i++ )
    {
        g_client.xp[SKILL_WOODCUTTING] += 10;
        tick(1000);
    }
    TEST_ASSERT(
        row_text("total_hr") && strcmp(row_text("total_hr"), "36,000") == 0,
        "a minute of 10xp/second reads as 36,000/hr (got '%s')",
        row_text("total_hr") ? row_text("total_hr") : "(none)");

    /*
     * Idling DILUTES, and that is the reference's behaviour rather than a
     * defect: XpStateSingle::tick advances the clock for any skill with xp
     * gained since its last reset, so the rate decays while you stand there --
     * which is exactly why `pause_skill_after` and `reset_rate_after` exist.
     * Ten more minutes over the same 600 xp is 600 * 3600 / 660.
     */
    for( int i = 0; i < 600; i++ )
        tick(1000);
    TEST_ASSERT(
        row_text("total_hr") && strcmp(row_text("total_hr"), "3,272") == 0,
        "ten idle minutes dilute the rate over the longer span (got '%s')",
        row_text("total_hr") ? row_text("total_hr") : "(none)");

    /*
     * PAUSING is what stops the clock, and this is the assertion that says so:
     * ten further idle minutes past a pause move the number not at all.
     */
    press("sk8", TORIRS_PLUGIN_UI_TOGGLE, 0);
    for( int i = 0; i < 600; i++ )
        tick(1000);
    press("sk8", TORIRS_PLUGIN_UI_TOGGLE, 1);
    TEST_ASSERT(
        row_text("total_hr") && strcmp(row_text("total_hr"), "3,272") == 0,
        "but a paused skill's clock does not run (got '%s')",
        row_text("total_hr") ? row_text("total_hr") : "(none)");
}

static void
test_xp_detail_and_time_to_level(void)
{
    client_reset();
    /* Level 40 woodcutting: 37,224 in, 41,171 for 41. */
    g_client.level[SKILL_WOODCUTTING] = 40;
    g_client.xp[SKILL_WOODCUTTING] = 37224;
    xp_start();
    tick(20);

    for( int i = 0; i < 60; i++ )
    {
        g_client.xp[SKILL_WOODCUTTING] += 10;
        tick(1000);
    }
    press("sk8", TORIRS_PLUGIN_UI_ACTIVATE, -1);

    TEST_ASSERT(fake_widget_find("d_prog"), "the row opens a detail block");
    TEST_ASSERT(
        row_text("d_left") && strcmp(row_text("d_left"), "3,347") == 0,
        "xp to level is the next threshold minus the xp (got '%s')",
        row_text("d_left") ? row_text("d_left") : "(none)");
    /* 3347 remaining at 600 xp per 60s of training = 334 seconds = 05:34. */
    TEST_ASSERT(
        row_text("d_ttl") && strcmp(row_text("d_ttl"), "05:34") == 0,
        "time to level is xp_remaining * elapsed / gained (got '%s')",
        row_text("d_ttl") ? row_text("d_ttl") : "(none)");
    /* Every gain was 10, so ten of them fill the history and the mean is 10:
     * 3347 / 10 rounded UP is 335. */
    TEST_ASSERT(
        row_text("d_actleft") && strcmp(row_text("d_actleft"), "335") == 0,
        "actions to level rounds up off the mean of the last ten (got '%s')",
        row_text("d_actleft") ? row_text("d_actleft") : "(none)");

    press("d_close", TORIRS_PLUGIN_UI_ACTIVATE, -1);
    TEST_ASSERT(!fake_widget_find("d_prog"), "and closes again");
}

static void
test_xp_actions_left_unknown_until_ten(void)
{
    client_reset();
    g_client.level[SKILL_WOODCUTTING] = 40;
    g_client.xp[SKILL_WOODCUTTING] = 37224;
    xp_start();
    tick(20);

    /* Nine actions: not enough history for a mean worth printing. */
    for( int i = 0; i < 9; i++ )
    {
        g_client.xp[SKILL_WOODCUTTING] += 10;
        tick(1000);
    }
    press("sk8", TORIRS_PLUGIN_UI_ACTIVATE, -1);
    TEST_ASSERT(
        row_text("d_actleft") && strcmp(row_text("d_actleft"), "\xe2\x80\x94") == 0,
        "nine actions is not a mean, so the row declines to answer (got '%s')",
        row_text("d_actleft") ? row_text("d_actleft") : "(none)");
}

static void
test_xp_maxed_progress_reads_full(void)
{
    client_reset();
    g_client.level[SKILL_WOODCUTTING] = 99;
    g_client.xp[SKILL_WOODCUTTING] = 13034431;
    fake_config_set_raw("hide_maxed", "0");
    xp_start();
    tick(20);
    g_client.xp[SKILL_WOODCUTTING] += 100;
    tick(1000);
    press("sk8", TORIRS_PLUGIN_UI_ACTIVATE, -1);

    /* next_xp is 0 at the top of the client's table: no next level to progress
     * towards, which a meter reads as full rather than as no progress. */
    TEST_ASSERT(
        fake_widget_find("d_prog") && fake_widget_find("d_prog")->value == 100,
        "a 99 with no next threshold reads as a full meter");
    TEST_ASSERT(
        row_text("d_ttl") && strcmp(row_text("d_ttl"), "\xe2\x80\x94") == 0,
        "and has no time to level");
}

static void
test_xp_hide_maxed(void)
{
    client_reset();
    g_client.level[SKILL_WOODCUTTING] = 99;
    g_client.xp[SKILL_WOODCUTTING] = 13034431;
    g_client.level[SKILL_ATTACK] = 40;
    g_client.xp[SKILL_ATTACK] = 37224;
    xp_start();
    fake_config_set_raw("hide_maxed", "1");
    tick(20);

    g_client.xp[SKILL_WOODCUTTING] += 100;
    g_client.xp[SKILL_ATTACK] += 100;
    tick(1000);

    TEST_ASSERT(!fake_widget_find("sk8"), "hide_maxed drops the 99's row");
    TEST_ASSERT(fake_widget_find("sk0"), "and keeps everything else");
}

static void
test_xp_pause(void)
{
    client_reset();
    g_client.xp[SKILL_WOODCUTTING] = 1000;
    xp_start();
    tick(20);
    g_client.xp[SKILL_WOODCUTTING] += 600;
    tick(1000);

    /* The switch on the row is the per-skill pause. */
    press("sk8", TORIRS_PLUGIN_UI_TOGGLE, 0);
    TEST_ASSERT(
        row_text("sk8") && strstr(row_text("sk8"), "paused"),
        "the row's switch pauses the skill (got '%s')",
        row_text("sk8") ? row_text("sk8") : "(none)");
    TEST_ASSERT(
        row_text("total_hr") && strcmp(row_text("total_hr"), "0") == 0,
        "and a paused skill contributes no rate (got '%s')",
        row_text("total_hr") ? row_text("total_hr") : "(none)");

    press("sk8", TORIRS_PLUGIN_UI_TOGGLE, 1);
    TEST_ASSERT(
        row_text("sk8") && !strstr(row_text("sk8"), "paused"), "and unpauses again");
}

static void
test_xp_auto_pause(void)
{
    client_reset();
    g_client.xp[SKILL_WOODCUTTING] = 1000;
    xp_start();
    fake_config_set_raw("pause_skill_after", "2");
    tick(20);
    g_client.xp[SKILL_WOODCUTTING] += 600;
    tick(1000);
    TEST_ASSERT(
        row_text("sk8") && !strstr(row_text("sk8"), "paused"),
        "a skill just trained is not paused");

    /* Two minutes of nothing. The timer is about xp NOT arriving, which no xp
     * event could announce -- so it lives on the per-second half. */
    for( int i = 0; i < 121; i++ )
        tick(1000);
    TEST_ASSERT(
        row_text("sk8") && strstr(row_text("sk8"), "paused"),
        "two idle minutes pause it (got '%s')",
        row_text("sk8") ? row_text("sk8") : "(none)");

    g_client.xp[SKILL_WOODCUTTING] += 10;
    tick(1000);
    TEST_ASSERT(
        row_text("sk8") && !strstr(row_text("sk8"), "paused"),
        "and any gain wakes it back up");
}

static void
test_xp_logout_pauses_and_keeps_state(void)
{
    client_reset();
    g_client.xp[SKILL_WOODCUTTING] = 1000;
    xp_start();
    tick(20);
    g_client.xp[SKILL_WOODCUTTING] += 600;
    tick(1000);

    g_client.logged_in = false;
    tick(1000);
    TEST_ASSERT(
        row_text("total_xp") && strcmp(row_text("total_xp"), "600") == 0,
        "logging out KEEPS the session -- a hop is not a new one (got '%s')",
        row_text("total_xp") ? row_text("total_xp") : "(none)");
    TEST_ASSERT(
        row_text("sk8") && strstr(row_text("sk8"), "paused"),
        "and pauses every skill when pause_on_logout is set");
}

static void
test_xp_reset(void)
{
    client_reset();
    g_client.xp[SKILL_WOODCUTTING] = 1000;
    xp_start();
    tick(20);
    g_client.xp[SKILL_WOODCUTTING] += 600;
    tick(1000);
    TEST_ASSERT(fake_widget_find("sk8"), "trained, so it has a row");

    press("reset_all", TORIRS_PLUGIN_UI_ACTIVATE, -1);
    TEST_ASSERT(!fake_widget_find("sk8"), "reset all takes the rows with it");
    TEST_ASSERT(
        row_text("total_xp") && strcmp(row_text("total_xp"), "0") == 0,
        "and zeroes the session");

    /* And it re-seeds rather than counting the current xp as a gain. */
    tick(1000);
    TEST_ASSERT(
        row_text("total_xp") && strcmp(row_text("total_xp"), "0") == 0,
        "the reset re-seeds from the client's xp (got '%s')",
        row_text("total_xp") ? row_text("total_xp") : "(none)");
}

static void
test_xp_offline_gains_are_not_the_session(void)
{
    client_reset();
    fake_config_set_raw("save_state", "1");
    g_client.xp[SKILL_WOODCUTTING] = 1000;
    TORIRS_PLUGIN_XP_TRACKER.init(CTX, &g_api);
    dispatch(TORIRS_PLUGIN_EV_START, NULL);
    panel_build();
    tick(20);
    g_client.xp[SKILL_WOODCUTTING] = 1600;
    tick(1000);
    dispatch(TORIRS_PLUGIN_EV_STOP, NULL);
    TEST_ASSERT(g_client.asset_present, "stopping writes the session out");

    /*
     * Back in, a million xp later, earned somewhere this client was not
     * watching. It is not this session's, so the reconciliation must move the
     * start rather than report it -- otherwise the panel opens claiming a
     * million xp in no time at all, which is the reference's own "offline
     * gains" case.
     */
    memset(g_handler, 0, sizeof(g_handler));
    g_client.xp[SKILL_WOODCUTTING] = 1001600;
    TORIRS_PLUGIN_XP_TRACKER.init(CTX, &g_api);
    dispatch(TORIRS_PLUGIN_EV_START, NULL);
    panel_build();
    tick(20);
    TEST_ASSERT(
        row_text("total_xp") && strcmp(row_text("total_xp"), "600") == 0,
        "the saved session comes back and the offline million does not (got '%s')",
        row_text("total_xp") ? row_text("total_xp") : "(none)");
}

/* ====================================================================== */
/* Loot tracker                                                            */
/* ====================================================================== */

static void
loot_start(void)
{
    fake_config_set_raw("remember_loot", "0");
    fake_config_set_raw("price_source", "Cache value");
    fake_config_set_raw("kill_chat_message", "0");
    fake_config_set_raw("chat_value_threshold", "0");
    fake_config_set_raw("ignored_items", "");
    fake_config_set_raw("ignored_sources", "");
    TORIRS_PLUGIN_LOOT_TRACKER.init(CTX, &g_api);
    dispatch(TORIRS_PLUGIN_EV_START, NULL);
    panel_build();
}

/**
 * An npc leaves the scene at a tile, with its health bar where `ratio` says.
 *
 * -1 is "no bar was ever sent", which is the ordinary state of an npc that has
 * not been in combat; 0 is the bar reaching empty, which is the reference's
 * isDying. The two must not be confused, and this is where a test says so.
 */
static void
npc_despawn_health(char const* name, int x, int z, int size, int ratio)
{
    struct ToriRS_PluginEvNpc ev;

    memset(&ev, 0, sizeof(ev));
    ev.npc.npc_id = 3029;
    ev.npc.base_npc_id = 3029;
    snprintf(ev.npc.name, sizeof(ev.npc.name), "%s", name);
    ev.npc.true_x = x;
    ev.npc.true_z = z;
    ev.npc.level = 0;
    ev.npc.size = size;
    ev.npc.element_id = 7;
    ev.npc.health_ratio = ratio;
    ev.npc.health_scale = ratio >= 0 ? 30 : -1;
    dispatch(TORIRS_PLUGIN_EV_NPC_DESPAWN, &ev);
}

/** A DEATH: the bar reached empty. The ordinary case. */
static void
npc_despawn(char const* name, int x, int z, int size)
{
    npc_despawn_health(name, x, z, size, 0);
}

/** A ground stack appears on a tile. */
static void
obj_spawn(int obj_id, char const* name, int count, int cost, int x, int z)
{
    struct ToriRS_PluginEvObj ev;

    memset(&ev, 0, sizeof(ev));
    ev.obj.obj_id = obj_id;
    ev.obj.count = count;
    ev.obj.cost = cost;
    snprintf(ev.obj.name, sizeof(ev.obj.name), "%s", name);
    ev.obj.tile_x = x;
    ev.obj.tile_z = z;
    ev.obj.level = 0;
    ev.obj.element_id = 9;
    dispatch(TORIRS_PLUGIN_EV_OBJ_SPAWN, &ev);
}

/** Past the correlation window, so every candidate settles. */
static void
settle(void)
{
    tick(2000);
}

static void
test_loot_kill_becomes_a_record(void)
{
    client_reset();
    loot_start();

    npc_despawn("Goblin", 3200, 3200, 1);
    obj_spawn(526, "Bones", 1, 100, 3200, 3200);
    settle();

    TEST_ASSERT(fake_widget_find("src0"), "a despawn plus loot on its tile is a record");
    TEST_ASSERT(
        strcmp(fake_widget_find("src0")->label, "Goblin") == 0,
        "named after the monster (got '%s')", fake_widget_find("src0")->label);
    TEST_ASSERT(
        row_text("total_kills") && strcmp(row_text("total_kills"), "1") == 0,
        "one kill (got '%s')", row_text("total_kills") ? row_text("total_kills") : "(none)");
    TEST_ASSERT(
        row_text("total_value") && strcmp(row_text("total_value"), "100") == 0,
        "worth its cache cost (got '%s')",
        row_text("total_value") ? row_text("total_value") : "(none)");
}

/*
 * A despawn with hitpoints LEFT has to earn its record by dropping something.
 *
 * That covers both the npc that simply walked out of view and the gargoyle
 * family, which despawns alive and is finished with an item. The reference
 * separates those with a hand-written id table; this does not carry one, so
 * the loot is the only evidence either way.
 */
static void
test_unconfirmed_dry_despawn_is_not_a_kill(void)
{
    client_reset();
    loot_start();

    /* -1: no bar was ever sent. An npc that was never in combat. */
    npc_despawn_health("Goblin", 3200, 3200, 1, -1);
    settle();
    TEST_ASSERT(!fake_widget_find("src0"), "an npc that walked away is not a kill");

    /* A bar with hitpoints left on it -- alive when it went. */
    npc_despawn_health("Goblin", 3200, 3200, 1, 12);
    settle();
    TEST_ASSERT(!fake_widget_find("src0"), "and neither is one that despawned alive");
    TEST_ASSERT(fake_widget_find("empty"), "the page still says it is empty");

    /* But it IS a record once loot lands on it, which is the gargoyle path. */
    npc_despawn_health("Goblin", 3200, 3200, 1, 12);
    obj_spawn(526, "Bones", 1, 100, 3200, 3200);
    settle();
    TEST_ASSERT(
        fake_widget_find("src0"),
        "a despawn that dropped something is a kill however it left");
}

/*
 * A DEATH is counted whether or not it dropped anything.
 *
 * This is what the health bar buys, and it is the whole difference from
 * attributing by coincident loot alone: a dry kill is a real event, and a kill
 * count that skipped them would drift low all trip.
 */
static void
test_confirmed_death_counts_without_loot(void)
{
    client_reset();
    loot_start();

    npc_despawn_health("Goblin", 3200, 3200, 1, 0);
    settle();
    TEST_ASSERT(fake_widget_find("src0"), "a bar at empty is a kill on its own");
    TEST_ASSERT(
        row_text("total_kills") && strcmp(row_text("total_kills"), "1") == 0,
        "and it is counted (got '%s')",
        row_text("total_kills") ? row_text("total_kills") : "(none)");
    TEST_ASSERT(
        row_text("total_value") && strcmp(row_text("total_value"), "0") == 0,
        "with nothing in it");

    npc_despawn_health("Goblin", 3200, 3200, 1, 0);
    obj_spawn(526, "Bones", 1, 100, 3200, 3200);
    settle();
    TEST_ASSERT(
        row_text("total_kills") && strcmp(row_text("total_kills"), "2") == 0,
        "the next one counts too (got '%s')",
        row_text("total_kills") ? row_text("total_kills") : "(none)");
    TEST_ASSERT(
        row_text("total_value") && strcmp(row_text("total_value"), "100") == 0,
        "and carries its drop");
}

static void
test_loot_off_footprint_is_not_attributed(void)
{
    client_reset();
    loot_start();

    npc_despawn("Goblin", 3200, 3200, 1);
    /* Two tiles away: somebody else's drop, or a spawn that has nothing to do
     * with this. A 1x1 npc's footprint is one tile. */
    obj_spawn(526, "Bones", 1, 100, 3202, 3200);
    settle();
    /* The KILL is still a kill -- the bar reached empty and that is not in
     * doubt. What must not happen is the stray stack being attributed to it. */
    TEST_ASSERT(
        row_text("total_kills") && strcmp(row_text("total_kills"), "1") == 0,
        "the death is recorded (got '%s')",
        row_text("total_kills") ? row_text("total_kills") : "(none)");
    TEST_ASSERT(
        row_text("total_value") && strcmp(row_text("total_value"), "0") == 0,
        "but loot off the footprint belongs to nobody (got '%s')",
        row_text("total_value") ? row_text("total_value") : "(none)");
}

static void
test_loot_big_monster_footprint(void)
{
    client_reset();
    loot_start();

    /* A 5x5 whose SW corner is 3200,3200 drops in the middle of the square.
     * true_x/true_z is the corner and `size` is the footprint, which is the
     * reference's WorldArea restated in the two numbers this bus carries. */
    npc_despawn("Kalphite Queen", 3200, 3200, 5);
    obj_spawn(11286, "Dragon chainbody", 1, 1000000, 3202, 3203);
    settle();

    TEST_ASSERT(fake_widget_find("src0"), "a big monster's drop lands inside its square");
    TEST_ASSERT(
        row_text("total_value") && strcmp(row_text("total_value"), "1,000,000") == 0,
        "at full value (got '%s')",
        row_text("total_value") ? row_text("total_value") : "(none)");
}

static void
test_loot_two_kills_merge_and_sum(void)
{
    client_reset();
    loot_start();

    npc_despawn("Goblin", 3200, 3200, 1);
    obj_spawn(526, "Bones", 1, 100, 3200, 3200);
    settle();
    npc_despawn("Goblin", 3201, 3200, 1);
    obj_spawn(526, "Bones", 1, 100, 3201, 3200);
    obj_spawn(995, "Coins", 25, 1, 3201, 3200);
    settle();

    TEST_ASSERT(
        row_text("total_kills") && strcmp(row_text("total_kills"), "2") == 0,
        "two goblins is two kills on one record (got '%s')",
        row_text("total_kills") ? row_text("total_kills") : "(none)");
    TEST_ASSERT(
        row_text("total_value") && strcmp(row_text("total_value"), "225") == 0,
        "and the values sum (got '%s')",
        row_text("total_value") ? row_text("total_value") : "(none)");

    press("src0", TORIRS_PLUGIN_UI_ACTIVATE, -1);
    TEST_ASSERT(
        row_text("d_kills") && strcmp(row_text("d_kills"), "2") == 0,
        "the detail shows the kill count");
    TEST_ASSERT(
        row_text("d_per_kill") && strcmp(row_text("d_per_kill"), "112") == 0,
        "and the value per kill (got '%s')",
        row_text("d_per_kill") ? row_text("d_per_kill") : "(none)");
}

static void
test_loot_high_alchemy_price(void)
{
    client_reset();
    loot_start();
    fake_config_set_raw("price_source", "High alchemy");

    npc_despawn("Goblin", 3200, 3200, 1);
    obj_spawn(1319, "Rune 2h sword", 1, 100000, 3200, 3200);
    settle();

    /* Three fifths, which is the game's own formula and not a rounding of it. */
    TEST_ASSERT(
        row_text("total_value") && strcmp(row_text("total_value"), "60,000") == 0,
        "high alchemy is three fifths of the cache cost (got '%s')",
        row_text("total_value") ? row_text("total_value") : "(none)");
}

static void
test_loot_ignored_item(void)
{
    client_reset();
    loot_start();
    fake_config_set_raw("ignored_items", " vial , Bones ");

    npc_despawn("Goblin", 3200, 3200, 1);
    obj_spawn(526, "Bones", 1, 100, 3200, 3200);
    settle();
    TEST_ASSERT(
        row_text("total_value") && strcmp(row_text("total_value"), "0") == 0,
        "an ignore list entry is matched trimmed and without case, so the "
        "drop is not recorded (got '%s')",
        row_text("total_value") ? row_text("total_value") : "(none)");
    TEST_ASSERT(
        row_text("total_kills") && strcmp(row_text("total_kills"), "1") == 0,
        "and ignoring an ITEM does not stop the kill being one");
}

static void
test_loot_ignored_source(void)
{
    client_reset();
    loot_start();
    fake_config_set_raw("ignored_sources", "Goblin");

    npc_despawn("Goblin", 3200, 3200, 1);
    obj_spawn(526, "Bones", 1, 100, 3200, 3200);
    settle();
    TEST_ASSERT(!fake_widget_find("src0"), "an ignored source records nothing");
}

static void
test_loot_row_switch_ignores_the_source(void)
{
    client_reset();
    loot_start();

    npc_despawn("Goblin", 3200, 3200, 1);
    obj_spawn(526, "Bones", 1, 100, 3200, 3200);
    settle();
    TEST_ASSERT(fake_widget_find("src0"), "recorded");

    TEST_ASSERT(
        fake_widget_find("src0")->value == 1,
        "the row's switch reads as TRACKED, so it is drawn on");

    /* Turning it back ON is the switch returning to where it already was, and
     * must not be read as a second instruction. */
    press("src0", TORIRS_PLUGIN_UI_TOGGLE, 1);
    TEST_ASSERT(fake_widget_find("src0"), "switching it on again changes nothing");

    press("src0", TORIRS_PLUGIN_UI_TOGGLE, 0);
    TEST_ASSERT(!fake_widget_find("src0"), "and switching it off drops the record");
    TEST_ASSERT(
        strstr(fake_cfg_str(CTX, "ignored_sources"), "Goblin") != NULL,
        "and writes it to the ignore list the user can also type into (got '%s')",
        fake_cfg_str(CTX, "ignored_sources"));

    /* And it stays ignored, which is the point of writing it there. */
    npc_despawn("Goblin", 3200, 3200, 1);
    obj_spawn(526, "Bones", 1, 100, 3200, 3200);
    settle();
    TEST_ASSERT(!fake_widget_find("src0"), "so the next one is not recorded either");
}

static void
test_loot_out_of_range_despawn(void)
{
    client_reset();
    loot_start();

    /* Far enough that its loot could never have been sent to this client. */
    npc_despawn("Goblin", 3400, 3400, 1);
    obj_spawn(526, "Bones", 1, 100, 3400, 3400);
    settle();
    TEST_ASSERT(!fake_widget_find("src0"), "a despawn out of range is not a candidate");
}

static void
test_loot_hollow_despawn_is_skipped(void)
{
    struct ToriRS_PluginEvNpc ev;

    client_reset();
    loot_start();

    /*
     * The world's EntityRemoved event can fire after the pool entry is already
     * gone, and the snapshot then carries an element id and nothing else.
     * Recording that would open a record with no name.
     */
    memset(&ev, 0, sizeof(ev));
    ev.npc.npc_id = -1;
    ev.npc.base_npc_id = -1;
    ev.npc.server_slot = -1;
    ev.npc.element_id = 12;
    dispatch(TORIRS_PLUGIN_EV_NPC_DESPAWN, &ev);
    obj_spawn(526, "Bones", 1, 100, 0, 0);
    settle();
    TEST_ASSERT(!fake_widget_find("src0"), "a hollow despawn snapshot records nothing");
}

static void
test_loot_colour_markup_is_one_record(void)
{
    client_reset();
    loot_start();

    npc_despawn("Goblin", 3200, 3200, 1);
    obj_spawn(526, "Bones", 1, 100, 3200, 3200);
    settle();
    /* The same monster, tinted. A record keyed on the painted spelling would
     * open a second row for it. */
    npc_despawn("<col=00ff00>Goblin</col>", 3201, 3200, 1);
    obj_spawn(526, "Bones", 1, 100, 3201, 3200);
    settle();

    TEST_ASSERT(
        row_text("total_kills") && strcmp(row_text("total_kills"), "2") == 0,
        "markup is stripped, so a tinted name is the same record (got '%s')",
        row_text("total_kills") ? row_text("total_kills") : "(none)");
    TEST_ASSERT(!fake_widget_find("src1"), "and there is only one row");
}

static void
test_loot_chat_message_threshold(void)
{
    client_reset();
    loot_start();
    fake_config_set_raw("kill_chat_message", "1");
    fake_config_set_raw("chat_value_threshold", "1000");

    npc_despawn("Goblin", 3200, 3200, 1);
    obj_spawn(526, "Bones", 1, 100, 3200, 3200);
    settle();
    TEST_ASSERT(g_client.notifies == 0, "a cheap kill is below the threshold");

    npc_despawn("Goblin", 3200, 3200, 1);
    obj_spawn(1319, "Rune 2h sword", 1, 5000, 3200, 3200);
    settle();
    TEST_ASSERT(g_client.notifies == 1, "an expensive one is announced");
    TEST_ASSERT(
        strstr(g_client.notify, "Goblin") && strstr(g_client.notify, "5,000"),
        "and the line names the source and the value (got '%s')", g_client.notify);
}

static void
test_loot_icons_are_asked_for_every_pass(void)
{
    struct ToriRS_PluginEvPanelDraw draw;

    client_reset();
    loot_start();

    npc_despawn("Goblin", 3200, 3200, 1);
    obj_spawn(526, "Bones", 37, 100, 3200, 3200);
    /* An id below 100 stands for an objtype whose model is not resident yet. */
    obj_spawn(12, "Pending item", 1, 0, 3200, 3200);
    settle();
    press("src0", TORIRS_PLUGIN_UI_ACTIVATE, -1);

    TEST_ASSERT(fake_widget_find("d_items"), "the detail declares a drawing well");
    TEST_ASSERT(
        fake_widget_find("d_items")->height > 0,
        "and states a height for it, so one row of drops is not given a page");

    memset(&draw, 0, sizeof(draw));
    draw.id = "d_items";
    draw.surface = (void*)0x1;
    draw.width = 240;
    draw.height = 80;
    dispatch(TORIRS_PLUGIN_EV_PANEL_DRAW, &draw);

    TEST_ASSERT(
        g_client.icons_asked == 2, "every drop asks for an icon (got %d)",
        g_client.icons_asked);
    TEST_ASSERT(
        g_client.icons_drawn == 1,
        "and the one whose model is not resident yet simply is not drawn (got %d)",
        g_client.icons_drawn);
    TEST_ASSERT(
        g_client.last_icon_style == TORIRS_PLUGIN_OBJ_ICON_BORDERED,
        "a dense grid asks for the bordered variant");

    /*
     * ASKED AGAIN, every pass. The handle comes out of a host-owned evicting
     * cache, so a plugin that remembered one across frames would eventually
     * draw nothing -- and this is the assertion that stops that from being
     * written here later. @see ToriRS_PluginApi::obj_image.
     */
    dispatch(TORIRS_PLUGIN_EV_PANEL_DRAW, &draw);
    TEST_ASSERT(
        g_client.icons_asked == 4, "the second pass asks again rather than caching the "
        "handle (got %d)", g_client.icons_asked);
}

static void
test_loot_quantity_is_part_of_the_icon(void)
{
    struct ToriRS_PluginEvPanelDraw draw;

    client_reset();
    loot_start();

    npc_despawn("Goblin", 3200, 3200, 1);
    obj_spawn(995, "Coins", 250, 1, 3200, 3200);
    settle();
    press("src0", TORIRS_PLUGIN_UI_ACTIVATE, -1);

    memset(&draw, 0, sizeof(draw));
    draw.id = "d_items";
    draw.surface = (void*)0x1;
    dispatch(TORIRS_PLUGIN_EV_PANEL_DRAW, &draw);

    /* The client bakes the stack digits into the sprite, so the quantity is
     * asked for as part of the picture rather than drawn over it. */
    TEST_ASSERT(
        g_client.last_icon_obj == 995 && g_client.last_icon_count == 250,
        "the icon is asked for at the stack's quantity (got %d x%d)",
        g_client.last_icon_obj, g_client.last_icon_count);
}

static void
test_loot_persists(void)
{
    client_reset();
    fake_config_set_raw("remember_loot", "1");
    fake_config_set_raw("price_source", "Cache value");
    fake_config_set_raw("kill_chat_message", "0");
    fake_config_set_raw("chat_value_threshold", "0");
    fake_config_set_raw("ignored_items", "");
    fake_config_set_raw("ignored_sources", "");
    TORIRS_PLUGIN_LOOT_TRACKER.init(CTX, &g_api);
    dispatch(TORIRS_PLUGIN_EV_START, NULL);
    panel_build();

    npc_despawn("Goblin", 3200, 3200, 1);
    obj_spawn(526, "Bones", 3, 100, 3200, 3200);
    settle();
    dispatch(TORIRS_PLUGIN_EV_STOP, NULL);
    TEST_ASSERT(g_client.asset_present, "stopping writes the loot out");

    memset(g_handler, 0, sizeof(g_handler));
    TORIRS_PLUGIN_LOOT_TRACKER.init(CTX, &g_api);
    dispatch(TORIRS_PLUGIN_EV_START, NULL);
    panel_build();

    TEST_ASSERT(fake_widget_find("src0"), "and it comes back");
    TEST_ASSERT(
        row_text("total_kills") && strcmp(row_text("total_kills"), "1") == 0,
        "with its kill count (got '%s')",
        row_text("total_kills") ? row_text("total_kills") : "(none)");
    TEST_ASSERT(
        row_text("total_value") && strcmp(row_text("total_value"), "300") == 0,
        "and its quantities (got '%s')",
        row_text("total_value") ? row_text("total_value") : "(none)");
}

static void
test_loot_badge_and_attention(void)
{
    struct ToriRS_PluginEvGameEvent ev;

    client_reset();
    loot_start();

    npc_despawn("Goblin", 3200, 3200, 1);
    obj_spawn(1319, "Rune 2h sword", 1, 100000, 3200, 3200);
    settle();
    /* "100K" and not "100.0K": the reference's quantityToRSDecimalStack drops
     * the decimal at a hundred of a unit, which is what keeps the badge four
     * characters wide across its whole range. */
    TEST_ASSERT(
        strcmp(g_client.badge, "100K") == 0,
        "the rail badge carries the session's value (got '%s')", g_client.badge);

    memset(&ev, 0, sizeof(ev));
    ev.kind = "valuable_drop";
    snprintf(ev.subject, sizeof(ev.subject), "Rune 2h sword");
    ev.value = 100000;
    dispatch(TORIRS_PLUGIN_EV_GAME_EVENT, &ev);
    TEST_ASSERT(g_client.attention, "a valuable drop asks the player to look");

    press("src0", TORIRS_PLUGIN_UI_ACTIVATE, -1);
    TEST_ASSERT(!g_client.attention, "and looking clears it");
}

static void
test_loot_clear(void)
{
    client_reset();
    loot_start();

    npc_despawn("Goblin", 3200, 3200, 1);
    obj_spawn(526, "Bones", 1, 100, 3200, 3200);
    settle();
    press("reset_all", TORIRS_PLUGIN_UI_ACTIVATE, -1);

    TEST_ASSERT(!fake_widget_find("src0"), "clear all takes the records");
    TEST_ASSERT(
        row_text("total_value") && strcmp(row_text("total_value"), "0") == 0,
        "and the session totals");
}

/*
 * Both trackers keep every knob they have in their config schema, so their
 * SETTINGS face is the host's generated form and they must declare nothing
 * for it. A plugin that declared its readouts for both faces would put its
 * loot table above the settings of anyone who opened it from the roster --
 * which is the state this split exists to end.
 */
/*
 * A page the shell has HIDDEN does no per-tick work.
 *
 * Not an optimisation: a plugin that went on rewriting a collapsed page would
 * be formatting two dozen readouts twice a second for a window that is shut,
 * and -- worse -- would go on asking for rebuilds of a model nobody is
 * showing. The state still advances; only the page stops.
 */
static void
test_hidden_page_does_no_work(void)
{
    struct ToriRS_PluginEvPanelLayout hidden;

    client_reset();
    loot_start();

    memset(&hidden, 0, sizeof(hidden));
    hidden.selection_generation = 1;
    hidden.visible = false;
    dispatch(TORIRS_PLUGIN_EV_PANEL_LAYOUT, &hidden);

    npc_despawn("Goblin", 3200, 3200, 1);
    obj_spawn(526, "Bones", 1, 100, 3200, 3200);
    settle();
    TEST_ASSERT(
        row_text("total_kills") && strcmp(row_text("total_kills"), "0") == 0,
        "a hidden page is not rewritten (got '%s')",
        row_text("total_kills") ? row_text("total_kills") : "(none)");

    /* The KILL was still recorded -- only the drawing stopped. */
    panel_build();
    TEST_ASSERT(
        row_text("total_kills") && strcmp(row_text("total_kills"), "1") == 0,
        "and showing it again states everything that happened meanwhile (got '%s')",
        row_text("total_kills") ? row_text("total_kills") : "(none)");
}

static void
test_settings_face_is_the_generated_form(void)
{
    client_reset();
    loot_start();

    npc_despawn("Goblin", 3200, 3200, 1);
    obj_spawn(526, "Bones", 1, 100, 3200, 3200);
    settle();
    TEST_ASSERT(fake_widget_find("src0"), "the PAGE face carries the records");

    panel_build_view(TORIRS_PLUGIN_PANEL_VIEW_SETTINGS);
    TEST_ASSERT(
        g_client.widget_count == 0,
        "the SETTINGS face declares nothing, leaving the generated form (got %d)",
        g_client.widget_count);

    /* And going back is a rebuild, not a resurrection: the model was cleared
     * for the other face, so the page has to state itself again. */
    panel_build();
    TEST_ASSERT(fake_widget_find("src0"), "and the page comes back whole");

    client_reset();
    xp_start();
    tick(20);
    panel_build_view(TORIRS_PLUGIN_PANEL_VIEW_SETTINGS);
    TEST_ASSERT(
        g_client.widget_count == 0, "the xp tracker answers the same way (got %d)",
        g_client.widget_count);
}

int
main(void)
{
    api_init();

    test_xp_first_reading_seeds();
    test_xp_gain_makes_a_row();
    test_xp_rate_is_over_training_time();
    test_xp_detail_and_time_to_level();
    test_xp_actions_left_unknown_until_ten();
    test_xp_maxed_progress_reads_full();
    test_xp_hide_maxed();
    test_xp_pause();
    test_xp_auto_pause();
    test_xp_logout_pauses_and_keeps_state();
    test_xp_reset();
    test_xp_offline_gains_are_not_the_session();

    test_loot_kill_becomes_a_record();
    test_unconfirmed_dry_despawn_is_not_a_kill();
    test_confirmed_death_counts_without_loot();
    test_loot_off_footprint_is_not_attributed();
    test_loot_big_monster_footprint();
    test_loot_two_kills_merge_and_sum();
    test_loot_high_alchemy_price();
    test_loot_ignored_item();
    test_loot_ignored_source();
    test_loot_row_switch_ignores_the_source();
    test_loot_out_of_range_despawn();
    test_loot_hollow_despawn_is_skipped();
    test_loot_colour_markup_is_one_record();
    test_loot_chat_message_threshold();
    test_loot_icons_are_asked_for_every_pass();
    test_loot_quantity_is_part_of_the_icon();
    test_loot_persists();
    test_loot_badge_and_attention();
    test_loot_clear();

    test_settings_face_is_the_generated_form();
    test_hidden_page_does_no_work();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
