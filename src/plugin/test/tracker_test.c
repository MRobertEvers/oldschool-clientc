/*
 * The XP Tracker and the Loot Tracker, run against a hand-built api table.
 *
 * No plugin host here, for item-stats' reason: both plugins are entirely
 * "given these events and these api answers, what does the page say", and
 * answering the verbs directly makes the test a table of cases rather than a
 * client. The page IS the assertion target -- every readout goes through
 * the panel builder and retained setters, so the fake below simply keeps the
 * model and the cases read rows out of it by id.
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

#include "plugin/torirs_plugin_v2.h"

#include <assert.h>
#include <limits.h>
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
    struct ToriRS_PlayerSnapshot me;
    int xp[FAKE_SKILLS];
    /* Whether the SERVER has stated this skill. The client seeds a fresh
     * account's defaults so its value scripts evaluate before a session
     * exists, so "no reading" is a state the engine reports rather than a
     * zero in the table.  app_plugin_stat_xp. */
    bool xp_stated[FAKE_SKILLS];
    int level[FAKE_SKILLS];

    struct FakeWidget widget[FAKE_WIDGETS];
    int widget_count;
    /** True while the plugin is inside its on_ui_build callback. */
    bool building;
    /** Raised by panel_clear outside a build: the page wants rebuilding. */
    bool rebuild_wanted;
    int builds;
    int exact_text_sets;
    int exact_height_sets;
    int redraws;
    int loot_source_visits;

    struct FakeConfig config[FAKE_CONFIG];
    int config_count;

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
static int g_lane_game = TORIRS_GAME_OLDSCHOOL;

/* ------------------------------------------------------------------ verbs */

static void
fake_notify(void* ctx, char const* text)
{
    (void)ctx;
    snprintf(g_client.notify, sizeof(g_client.notify), "%s", text);
    g_client.notifies++;
}

static uint64_t
fake_frame_ms(void* ctx)
{
    (void)ctx;
    return g_client.now_ms;
}

static int
fake_local_player(void* ctx, struct ToriRS_PlayerSnapshot* out)
{
    (void)ctx;
    if( !g_client.logged_in )
        return 0;
    *out = g_client.me;
    return 1;
}

static int
fake_stat_xp(
    void* ctx,
    int skill,
    int* out_xp,
    int* out_level_xp,
    int* out_next_xp)
{
    (void)ctx;
    if( skill < 0 || skill >= FAKE_SKILLS )
        return 0;
    if( !g_client.xp_stated[skill] )
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
fake_cfg_str(void* ctx, char const* key)
{
    struct FakeConfig const* row;
    (void)ctx;
    row = fake_config_find(key);
    return row ? row->value : "";
}

static int
fake_cfg_bool(void* ctx, char const* key)
{
    return atoi(fake_cfg_str(ctx, key)) != 0;
}

static int
fake_cfg_int(void* ctx, char const* key)
{
    return atoi(fake_cfg_str(ctx, key));
}

static void
fake_cfg_set(void* ctx, char const* key, char const* value)
{
    (void)ctx;
    fake_config_set_raw(key, value);
}

/* ---- assets ---- */

static int
fake_asset_load(void* ctx, char const* name)
{
    (void)ctx;
    /* Resident synchronously when the store holds it, which is the "1" branch
     * of the contract; a miss queues and the plugin is told through on_asset. */
    return g_client.asset_present && strcmp(g_client.asset_name, name) == 0 ? 1 : 0;
}

static void const*
fake_asset_data(void* ctx, char const* name, int* out_size)
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
    void* ctx, char const* name, void const* data, int size)
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
fake_asset_release(void* ctx, char const* name)
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
fake_panel_request(void* ctx, struct ToriRS_PanelDescriptor const* desc)
{
    (void)ctx;
    (void)desc;
    assert(desc);
    /* The registration carries icon and width only: the entry is named by the
     * plugin, so there is nothing here for the fake to record. */
    return true;
}

static bool
fake_panel_widget(void* ctx, int kind, char const* id, char const* label)
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
fake_panel_set_text(void* ctx, char const* id, char const* text)
{
    struct FakeWidget* w;
    (void)ctx;
    w = fake_widget_find(id);
    if( !w )
        return false;
    g_client.exact_text_sets++;
    snprintf(w->text, sizeof(w->text), "%s", text ? text : "");
    return true;
}

static bool
fake_panel_set_value(void* ctx, char const* id, int value)
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
fake_panel_set_height(void* ctx, char const* id, int height)
{
    struct FakeWidget* w;
    (void)ctx;
    w = fake_widget_find(id);
    if( !w )
        return false;
    g_client.exact_height_sets++;
    w->height = height;
    return true;
}

static bool
fake_panel_set_attention(void* ctx, bool on)
{
    (void)ctx;
    g_client.attention = on;
    return true;
}

static void
fake_panel_clear(void* ctx)
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
fake_panel_invalidate(void* ctx, char const* id)
{
    (void)ctx;
    (void)id;
    g_client.redraws++;
}

/* ---- images ---- */

static int
fake_obj_image(void* ctx, int obj_id, int count, int style)
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

/* ---- the client's own loot store, faked -----------------------------------
 *
 * The loot tracker reads game/rs_loot_store.c rather than correlating deaths
 * with item spawns, so what a case seeds is a STORE: a source per kind of
 * kill, rows under it, and a kill count bumped once per event id exactly as
 * LootStore_AddKillLoot bumps it.
 */
#define FAKE_LOOT_SOURCES 8
#define FAKE_LOOT_ROWS 16

static struct
{
    struct
    {
        int id;
        char name[64];
        int kill_count;
        int last_event;
        struct ToriRS_LootRow rows[FAKE_LOOT_ROWS];
        int row_count;
    } source[FAKE_LOOT_SOURCES];
    int count;
    int next_id;
    uint64_t revision;
} g_store;

/** LootStore_AddKillLoot, as far as this test needs it. */
static void
loot_add(char const* name, int obj_id, int qty, int value, int event_id)
{
    int index = -1;

    g_store.revision++;

    for( int i = 0; i < g_store.count; i++ )
        if( strcmp(g_store.source[i].name, name) == 0 )
            index = i;
    if( index < 0 )
    {
        assert(g_store.count < FAKE_LOOT_SOURCES);
        index = g_store.count++;
        memset(&g_store.source[index], 0, sizeof(g_store.source[index]));
        g_store.source[index].id = ++g_store.next_id;
        snprintf(
            g_store.source[index].name, sizeof(g_store.source[index].name), "%s",
            name);
        g_store.source[index].last_event = -1;
    }
    /* One kill per EVENT, so a multi-item drop counts once. */
    if( g_store.source[index].last_event != event_id )
    {
        g_store.source[index].kill_count++;
        g_store.source[index].last_event = event_id;
    }
    if( obj_id < 0 )
        return;
    for( int r = 0; r < g_store.source[index].row_count; r++ )
        if( g_store.source[index].rows[r].obj_id == obj_id )
        {
            g_store.source[index].rows[r].quantity += qty;
            return;
        }
    assert(g_store.source[index].row_count < FAKE_LOOT_ROWS);
    {
        struct ToriRS_LootRow* row =
            &g_store.source[index].rows[g_store.source[index].row_count++];
        row->obj_id = obj_id;
        row->quantity = qty;
        row->value = value;
    }
}

static int
fake_loot_source_next(
    void* ctx, int iter, struct ToriRS_LootSource* out)
{
    int const next = iter + 1;
    (void)ctx;
    if( next < 0 || next >= g_store.count )
        return -1;
    memset(out, 0, sizeof(*out));
    out->id = g_store.source[next].id;
    snprintf(out->name, sizeof(out->name), "%s", g_store.source[next].name);
    out->row_count = g_store.source[next].row_count;
    out->kill_count = g_store.source[next].kill_count;
    return next;
}

static int
fake_loot_row_next(
    void* ctx,
    int source_id,
    int iter,
    struct ToriRS_LootRow* out)
{
    int const next = iter + 1;
    (void)ctx;
    for( int i = 0; i < g_store.count; i++ )
    {
        if( g_store.source[i].id != source_id )
            continue;
        if( next < 0 || next >= g_store.source[i].row_count )
            return -1;
        *out = g_store.source[i].rows[next];
        return next;
    }
    return -1;
}

static int
fake_obj_info(
    void* ctx, int obj_id, struct ToriRS_ItemInfo* out)
{
    (void)ctx;
    memset(out, 0, sizeof(*out));
    out->obj_id = obj_id;
    /* Every case names its items, so the store's ids map back by a table the
     * case seeds beside them. */
    for( int i = 0; i < g_store.count; i++ )
        for( int r = 0; r < g_store.source[i].row_count; r++ )
            if( g_store.source[i].rows[r].obj_id == obj_id )
            {
                snprintf(out->name, sizeof(out->name), "obj%d", obj_id);
                return 1;
            }
    return 0;
}

static struct ToriRS_ApiV2 g_api;
static struct ToriRS_GameApiV2 g_game_api;
static struct ToriRS_PluginDefV2 const* g_plugin;
static void* g_plugin_state;
static int g_heading_id;

static void v2_log(struct ToriRS_ApiV2* api, char const* format, ...)
{ (void)api; (void)format; }
static void v2_notify(struct ToriRS_ApiV2* api, char const* text)
{ (void)api; fake_notify(NULL, text); }
static uint64_t v2_frame_ms(struct ToriRS_ApiV2* api)
{ (void)api; return fake_frame_ms(NULL); }
static bool v2_lane(struct ToriRS_ApiV2* api, struct ToriRS_LaneInfo* out)
{
    (void)api;
    memset(out, 0, sizeof(*out));
    out->game = g_lane_game;
    out->epoch = g_lane_game == TORIRS_GAME_RS2
                     ? TORIRS_CACHE_EPOCH_DAT1
                     : TORIRS_CACHE_EPOCH_DAT2;
    out->revision = g_lane_game == TORIRS_GAME_RS2 ? 289 : 239;
    return true;
}
static bool v2_local_player(
    struct ToriRS_ApiV2* api, struct ToriRS_PlayerSnapshot* out)
{ (void)api; return fake_local_player(NULL, out) != 0; }
static bool v2_config_has(struct ToriRS_ApiV2* api, char const* key)
{ (void)api; return fake_config_find(key) != NULL; }
static bool v2_config_bool(struct ToriRS_ApiV2* api, char const* key, bool* out)
{ (void)api; *out = fake_cfg_bool(NULL, key) != 0; return true; }
static bool v2_config_int(struct ToriRS_ApiV2* api, char const* key, int* out)
{ (void)api; *out = fake_cfg_int(NULL, key); return true; }
static bool v2_config_string(
    struct ToriRS_ApiV2* api, char const* key, char const** out)
{ (void)api; *out = fake_cfg_str(NULL, key); return true; }
static enum ToriRS_Result v2_config_set(
    struct ToriRS_ApiV2* api, char const* key, char const* value)
{
    (void)api;
    fake_cfg_set(NULL, key, value);
    if( g_plugin && g_plugin_state && g_plugin->callbacks.on_config_changed )
        g_plugin->callbacks.on_config_changed(&g_api, g_plugin_state, key);
    return TORIRS_RESULT_OK;
}
static enum ToriRS_AssetState v2_asset_request(
    struct ToriRS_ApiV2* api, char const* name)
{
    (void)api;
    return fake_asset_load(NULL, name) ? TORIRS_ASSET_READY : TORIRS_ASSET_PENDING;
}
static bool v2_asset_bytes(
    struct ToriRS_ApiV2* api, char const* name, void const** out, size_t* size)
{
    int n = 0;
    (void)api;
    *out = fake_asset_data(NULL, name, &n);
    *size = n > 0 ? (size_t)n : 0;
    return *out != NULL;
}
static enum ToriRS_Result v2_asset_save(
    struct ToriRS_ApiV2* api, char const* name, void const* data, size_t size)
{
    (void)api;
    return size <= INT_MAX && fake_asset_save(NULL, name, data, (int)size)
               ? TORIRS_RESULT_OK : TORIRS_RESULT_INVALID;
}
static void v2_asset_release(struct ToriRS_ApiV2* api, char const* name)
{ (void)api; fake_asset_release(NULL, name); }
static void v2_image_release(
    struct ToriRS_ApiV2* api, struct ToriRS_ImageRef image)
{ (void)api; (void)image; }
static bool v2_skill(
    struct ToriRS_ApiV2* api, int skill, struct ToriRS_SkillSnapshot* out)
{
    int xp = 0, level_xp = 0, next_xp = 0;
    (void)api;
    if( skill < 0 || skill >= FAKE_SKILLS ||
        !fake_stat_xp(NULL, skill, &xp, &level_xp, &next_xp) )
        return false;
    memset(out, 0, sizeof(*out));
    out->struct_size = sizeof(*out);
    out->index = skill;
    snprintf(out->name, sizeof(out->name), "%s", FAKE_SKILL_NAME[skill]);
    out->current_level = g_client.level[skill];
    out->base_level = g_client.level[skill];
    out->xp = xp;
    out->level_xp = level_xp;
    out->next_level_xp = next_xp;
    return true;
}
static bool v2_item_info(
    struct ToriRS_ApiV2* api, int obj, struct ToriRS_ItemInfo* out)
{ (void)api; return fake_obj_info(NULL, obj, out) != 0; }
static enum ToriRS_AssetState v2_item_image(
    struct ToriRS_ApiV2* api, int obj, int count, int style,
    struct ToriRS_ImageRef* out)
{
    int const image = fake_obj_image(NULL, obj, count, style);
    (void)api;
    out->value = image >= 0 ? (uint32_t)image + 1u : 0;
    return image >= 0 ? TORIRS_ASSET_READY : TORIRS_ASSET_PENDING;
}
static int v2_loot_source_next(
    struct ToriRS_ApiV2* api, int iter, struct ToriRS_LootSource* out)
{ (void)api; g_client.loot_source_visits++; return fake_loot_source_next(NULL, iter, out); }
static int v2_loot_row_next(
    struct ToriRS_ApiV2* api, int source, int iter, struct ToriRS_LootRow* out)
{ (void)api; return fake_loot_row_next(NULL, source, iter, out); }
static uint64_t v2_loot_revision(struct ToriRS_ApiV2* api)
{ (void)api; return g_store.revision; }
static bool v2_loot_source_clear(struct ToriRS_ApiV2* api, int source_id)
{
    (void)api;
    for( int i = 0; i < g_store.count; i++ )
    {
        if( g_store.source[i].id != source_id ) continue;
        if( i + 1 < g_store.count )
            memmove(&g_store.source[i], &g_store.source[i + 1],
                (size_t)(g_store.count - i - 1) * sizeof(g_store.source[0]));
        g_store.count--;
        g_store.revision++;
        return true;
    }
    return false;
}
static enum ToriRS_Result v2_panel_request(
    struct ToriRS_ApiV2* api, struct ToriRS_PanelDescriptor const* desc)
{ (void)api; return fake_panel_request(NULL, desc) ? TORIRS_RESULT_OK : TORIRS_RESULT_ERROR; }
static void v2_panel_invalidate(struct ToriRS_ApiV2* api)
{ (void)api; fake_panel_clear(NULL); }
static void v2_panel_attention(struct ToriRS_ApiV2* api, bool wanted)
{ (void)api; (void)fake_panel_set_attention(NULL, wanted); }
static enum ToriRS_Result v2_panel_set_text(
    struct ToriRS_ApiV2* api, char const* id, char const* text)
{
    struct FakeWidget* widget;
    (void)api;
    widget = fake_widget_find(id);
    if( !widget ) return TORIRS_RESULT_NOT_FOUND;
    (void)fake_panel_set_text(NULL, id, text);
    if( widget->kind == TORIRS_PANEL_HEADING )
        snprintf(widget->label, sizeof(widget->label), "%s", text ? text : "");
    return TORIRS_RESULT_OK;
}
static enum ToriRS_Result v2_panel_set_value(
    struct ToriRS_ApiV2* api, char const* id, int value)
{ (void)api; return fake_panel_set_value(NULL, id, value) ? TORIRS_RESULT_OK : TORIRS_RESULT_NOT_FOUND; }
static enum ToriRS_Result v2_panel_set_height(
    struct ToriRS_ApiV2* api, char const* id, int height)
{ (void)api; return fake_panel_set_height(NULL, id, height) ? TORIRS_RESULT_OK : TORIRS_RESULT_NOT_FOUND; }
static void v2_panel_redraw(struct ToriRS_ApiV2* api, char const* id)
{ (void)api; fake_panel_invalidate(NULL, id); }

static void v2_build_heading(struct ToriRS_PanelBuilder* panel, char const* text)
{
    char id[32];
    (void)panel;
    snprintf(id, sizeof(id), "_v2_heading_%d", g_heading_id++);
    (void)fake_panel_widget(NULL, TORIRS_PANEL_HEADING, id, text);
}
static void v2_build_paragraph(struct ToriRS_PanelBuilder* panel, char const* text)
{ (void)panel; (void)text; }
static void v2_build_toggle(
    struct ToriRS_PanelBuilder* panel, char const* id, char const* label, bool value)
{ (void)panel; (void)fake_panel_widget(NULL, TORIRS_PANEL_TOGGLE, id, label); (void)fake_panel_set_value(NULL, id, value); }
static void v2_build_select(
    struct ToriRS_PanelBuilder* panel, char const* id, char const* label,
    char const* value, struct ToriRS_SelectOption const* options, int count)
{ (void)panel; (void)value; (void)options; (void)count; (void)fake_panel_widget(NULL, TORIRS_PANEL_SELECT, id, label); }
static void v2_build_button(
    struct ToriRS_PanelBuilder* panel, char const* id, char const* label, bool enabled)
{ (void)panel; (void)enabled; (void)fake_panel_widget(NULL, TORIRS_PANEL_BUTTON, id, label); }
static void v2_build_custom(
    struct ToriRS_PanelBuilder* panel, char const* id, int height)
{ (void)panel; (void)fake_panel_widget(NULL, TORIRS_PANEL_CUSTOM, id, ""); (void)fake_panel_set_height(NULL, id, height); }
static void v2_build_label(
    struct ToriRS_PanelBuilder* panel, char const* id, char const* text)
{ (void)panel; (void)fake_panel_widget(NULL, TORIRS_PANEL_LABEL, id, text); (void)fake_panel_set_text(NULL, id, text); }
static void v2_build_key_value(
    struct ToriRS_PanelBuilder* panel, char const* id, char const* label, char const* value)
{ (void)panel; (void)fake_panel_widget(NULL, TORIRS_PANEL_KEY_VALUE, id, label); (void)fake_panel_set_text(NULL, id, value); }
static enum ToriRS_Result v2_build_node(
    struct ToriRS_PanelBuilder* panel, struct ToriRS_PanelNode const* node)
{
    int kind = TORIRS_PANEL_LABEL;
    (void)panel;
    if( node->kind == TORIRS_PANEL_HEADING ) kind = TORIRS_PANEL_HEADING;
    else if( node->kind == TORIRS_PANEL_KEY_VALUE ) kind = TORIRS_PANEL_KEY_VALUE;
    else if( node->kind == TORIRS_PANEL_CUSTOM ) kind = TORIRS_PANEL_CUSTOM;
    if( !fake_panel_widget(NULL, kind, node->id, node->label ? node->label : node->text) )
        return TORIRS_RESULT_BUDGET;
    if( node->text ) (void)v2_panel_set_text(&g_api, node->id, node->text);
    if( node->preferred_height ) (void)fake_panel_set_height(NULL, node->id, node->preferred_height);
    return TORIRS_RESULT_OK;
}

static void
api_init(void)
{
    memset(&g_api, 0, sizeof(g_api));
    memset(&g_game_api, 0, sizeof(g_game_api));
    g_api.struct_size = sizeof(g_api);
    g_api.major_version = TORIRS_PLUGIN_API_V2_MAJOR;
    g_api.minor_version = TORIRS_PLUGIN_API_V2_MINOR;
    g_api.core.log = v2_log;
    g_api.core.notify = v2_notify;
    g_api.core.frame_ms = v2_frame_ms;
    g_api.core.lane = v2_lane;
    g_api.config.has = v2_config_has;
    g_api.config.get_bool = v2_config_bool;
    g_api.config.get_int = v2_config_int;
    g_api.config.get_string = v2_config_string;
    g_api.config.set = v2_config_set;
    g_api.world.local_player = v2_local_player;
    g_api.assets.request = v2_asset_request;
    g_api.assets.bytes = v2_asset_bytes;
    g_api.assets.save = v2_asset_save;
    g_api.assets.release = v2_asset_release;
    g_api.assets.image_release = v2_image_release;
    g_api.panel.request = v2_panel_request;
    g_api.panel.invalidate = v2_panel_invalidate;
    g_api.panel.attention = v2_panel_attention;
    g_api.panel.set_text = v2_panel_set_text;
    g_api.panel.set_value = v2_panel_set_value;
    g_api.panel.set_height = v2_panel_set_height;
    g_api.panel.redraw = v2_panel_redraw;
    g_game_api.struct_size = sizeof(g_game_api);
    g_game_api.skill = v2_skill;
    g_game_api.item_info = v2_item_info;
    g_game_api.item_image = v2_item_image;
    g_game_api.loot_source_next = v2_loot_source_next;
    g_game_api.loot_row_next = v2_loot_row_next;
    g_game_api.loot_revision = v2_loot_revision;
    g_game_api.loot_source_clear = v2_loot_source_clear;
    g_api.game = &g_game_api;
}

/* ---------------------------------------------------------------- driving */

extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_XP_TRACKER;
extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_LOOT_TRACKER;

/* Opaque marker used only by the small fake-client helper functions above. */
static void* const FAKE_CONTEXT = &g_client;

static void
plugin_prepare(struct ToriRS_PluginDefV2 const* definition)
{
    assert(definition);
    assert(!g_plugin_state);
    g_plugin = definition;
    g_plugin_state = calloc(1, definition->state_size);
    assert(g_plugin_state);
}

static void
dispatch_start(void)
{
    assert(g_plugin && g_plugin_state);
    if( g_plugin->callbacks.on_start )
        g_plugin->callbacks.on_start(&g_api, g_plugin_state);
}

static void
dispatch_stop(void)
{
    assert(g_plugin);
    if( g_plugin_state && g_plugin->callbacks.on_stop )
        g_plugin->callbacks.on_stop(&g_api, g_plugin_state);
    free(g_plugin_state);
    g_plugin_state = NULL;
    g_plugin = NULL;
}

static void
dispatch_logic_tick(struct ToriRS_TickEvent const* event)
{
    assert(g_plugin && g_plugin_state && event);
    if( g_plugin->callbacks.on_logic_tick )
        g_plugin->callbacks.on_logic_tick(&g_api, g_plugin_state, event);
}

static void
dispatch_npc_despawn(struct ToriRS_NpcSnapshot const* npc)
{
    assert(g_plugin && g_plugin_state && npc);
    if( g_plugin->callbacks.on_npc_despawn )
        g_plugin->callbacks.on_npc_despawn(&g_api, g_plugin_state, npc);
}

static void
dispatch_item_spawn(struct ToriRS_GroundItemSnapshot const* item)
{
    assert(g_plugin && g_plugin_state && item);
    if( g_plugin->callbacks.on_item_spawn )
        g_plugin->callbacks.on_item_spawn(&g_api, g_plugin_state, item);
}

static void
dispatch_game_event(struct ToriRS_GameEvent const* event)
{
    assert(g_plugin && g_plugin_state && event);
    if( g_plugin->callbacks.on_game_event )
        g_plugin->callbacks.on_game_event(&g_api, g_plugin_state, event);
}

static void
dispatch_panel_action(struct ToriRS_PanelActionEvent const* event)
{
    assert(g_plugin && g_plugin_state && event);
    if( g_plugin->callbacks.on_ui_action )
        g_plugin->callbacks.on_ui_action(&g_api, g_plugin_state, event);
}

static void
dispatch_panel_layout(struct ToriRS_PanelLayoutEvent const* event)
{
    assert(g_plugin && g_plugin_state && event);
    if( g_plugin->callbacks.on_ui_layout )
        g_plugin->callbacks.on_ui_layout(&g_api, g_plugin_state, event);
}

/** Run one on_ui_build callback for one face, with the model emptied as the host
 *  empties it. @see enum ToriRS_PanelView. */
static void
panel_build_view(int view)
{
    struct ToriRS_PanelBuilder panel = {
        .struct_size = sizeof(panel),
        .heading = v2_build_heading,
        .paragraph = v2_build_paragraph,
        .toggle = v2_build_toggle,
        .select = v2_build_select,
        .button = v2_build_button,
        .custom = v2_build_custom,
        .label = v2_build_label,
        .key_value = v2_build_key_value,
        .node = v2_build_node,
    };

    g_client.widget_count = 0;
    memset(g_client.widget, 0, sizeof(g_client.widget));
    g_client.rebuild_wanted = false;
    g_client.building = true;
    g_client.builds++;
    g_heading_id = 0;
    if( g_plugin->callbacks.on_ui_build )
        g_plugin->callbacks.on_ui_build(&g_api, g_plugin_state, &panel, view);
    g_client.building = false;

    /* The shell then states the allocation, and that is what tells a plugin
     * its page is on SCREEN -- both trackers do no per-tick page work until it
     * arrives, so a harness that skipped it would be testing a hidden page. */
    {
        struct ToriRS_PanelLayoutEvent layout;

        memset(&layout, 0, sizeof(layout));
        layout.width = 320;
        layout.height = 500;
        layout.scale_milli = 1000;
        layout.size_class = TORIRS_PANEL_SIZE_MEDIUM;
        layout.visible = true;
        layout.game_visible = true;
        layout.selection_generation = 1;
        dispatch_panel_layout(&layout);
    }
}

/** The plugin's own screen, which is what the rail entry opens. */
static void
panel_build(void)
{
    panel_build_view(TORIRS_PANEL_VIEW_PAGE);
}

/** Advance the clock and run one logic tick, rebuilding the page if the tick
 *  asked for one -- which is what the client's next selected frame does. */
static void
tick(uint64_t advance_ms)
{
    struct ToriRS_TickEvent ev;

    memset(&ev, 0, sizeof(ev));
    g_client.now_ms += advance_ms;
    dispatch_logic_tick(&ev);
    if( g_client.rebuild_wanted )
        panel_build();
}

/**
 * Click the box for `row` in the skill strip.
 *
 * The strip is ONE control -- 25 skills would not fit in the panel's 48-widget
 * budget as seven controls each -- so a selection is a coordinate, and the row
 * pitch is the cache's own 48+2. @see the plugin's file comment.
 */
#define TEST_BOX_PITCH 50
static void
press_box(int row);

static void
press(char const* id, int action, int value)
{
    struct ToriRS_PanelActionEvent ev;

    memset(&ev, 0, sizeof(ev));
    ev.id = id;
    ev.action = action;
    ev.value = value;
    ev.text = "";
    ev.selection_generation = 1;
    dispatch_panel_action(&ev);
    if( g_client.rebuild_wanted )
        panel_build();
}

static void
press_box(int row)
{
    struct ToriRS_PanelActionEvent ev;

    memset(&ev, 0, sizeof(ev));
    ev.id = "boxes";
    ev.action = TORIRS_PANEL_ACTION_ACTIVATE;
    ev.value = -1;
    ev.text = "";
    ev.x = 10;
    /* The first pitch is the session overview; skill row zero begins below it. */
    ev.y = (row + 1) * TEST_BOX_PITCH + 10;
    ev.selection_generation = 1;
    dispatch_panel_action(&ev);
    if( g_client.rebuild_wanted )
        panel_build();
}

/* ---- the loot tracker's band strip ---------------------------------------
 *
 * The loot page is one drawing well too, but its bands are VARIABLE height --
 * an expanded source carries its item grid -- so a test names a y rather than
 * a row, and the header band is the top 33 of each. @see the plugin's
 * LT_HEAD_H block.
 */
#define TEST_HEAD_H 33
/** The totals band the strip opens with, which every band sits below. */
#define TEST_TOTALS_H 44

static void
press_strip(int y)
{
    struct ToriRS_PanelActionEvent ev;

    memset(&ev, 0, sizeof(ev));
    ev.id = "strip";
    ev.action = TORIRS_PANEL_ACTION_ACTIVATE;
    ev.value = -1;
    ev.text = "";
    ev.x = 10;
    ev.y = y;
    ev.selection_generation = 1;
    dispatch_panel_action(&ev);
    if( g_client.rebuild_wanted )
        panel_build();
}

/** Is there a band strip with anything in it? A strip with only its totals
 *  band and the empty note is the "nothing recorded" state. */
static int
has_loot(void)
{
    struct FakeWidget const* w = fake_widget_find("strip");
    return w && w->height > TEST_TOTALS_H + TEST_HEAD_H;
}

/** The source whose detail block is open, or NULL. */
static char const*
detail_source(void)
{
    struct FakeWidget const* w = fake_widget_find("sec_detail");
    return w ? w->label : NULL;
}

/** How many boxes the strip was built for -- its height states it. */
static int
box_count(void)
{
    struct FakeWidget const* w = fake_widget_find("boxes");
    /*
     * The SKILL boxes, which is one fewer than the strip holds: the overview
     * box is always drawn -- it is the session's own answer and it has one
     * whether or not a skill has been trained -- so the strip is the list plus
     * that one. @see xt_draw_overview.
     */
    return w && w->height >= TEST_BOX_PITCH ? w->height / TEST_BOX_PITCH - 1 : 0;
}

/** The skill whose detail block is open, or NULL. */
static char const*
detail_skill(void)
{
    struct FakeWidget const* w = fake_widget_find("sec_detail");
    return w ? w->label : NULL;
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
    if( g_plugin_state ) dispatch_stop();
    memset(&g_client, 0, sizeof(g_client));
    memset(&g_store, 0, sizeof(g_store));
    g_store.revision = 1;
    g_lane_game = TORIRS_GAME_OLDSCHOOL;
    g_client.now_ms = 100000;
    g_client.logged_in = true;
    g_client.me.true_x = 3200;
    g_client.me.true_z = 3200;
    g_client.me.level = 0;
    for( int i = 0; i < FAKE_SKILLS; i++ )
    {
        g_client.level[i] = 1;
        /* The ordinary case is a logged-in client whose stats have arrived; the
         * one test about the moment before that clears this itself. */
        g_client.xp_stated[i] = true;
    }
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
    plugin_prepare(&TORIRS_PLUGIN_XP_TRACKER);
    dispatch_start();
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
        box_count() == 0,
        "the first sight of a stat table seeds rather than reporting a gain, so no "
        "skill has a box until it has been trained (got %d)",
        box_count());
    TEST_ASSERT(
        fake_widget_find("boxes") != NULL,
        "the strip is still declared -- its overview box states the empty session");
}

/*
 * A stat table nobody has stated yet is not a reading of zero.
 *
 * The client seeds RS_PlayerStats with what a FRESH ACCOUNT has, so that its
 * CS1 value scripts evaluate before any session exists -- which means the
 * pre-login table is not empty, it is somebody else's. stat_xp answers "no
 * reading" for a skill the server has not stated, and a tracker that seeded
 * from one anyway takes the login burst, where the whole account arrives at
 * once, as one enormous gain. That is what the panel showed: a session that
 * snapped to the character's total the moment it appeared.
 */
static void
test_xp_untransmitted_table_is_not_a_reading(void)
{
    client_reset();
    for( int i = 0; i < FAKE_SKILLS; i++ )
        g_client.xp_stated[i] = false;
    xp_start();
    tick(20);
    tick(1000);
    TEST_ASSERT(
        box_count() == 0,
        "a table the server has not stated trains nothing (got %d)", box_count());

    /* The login burst: a levelled account, every skill at once. */
    for( int i = 0; i < FAKE_SKILLS; i++ )
        g_client.xp_stated[i] = true;
    g_client.xp[SKILL_WOODCUTTING] = 13034431;
    g_client.level[SKILL_WOODCUTTING] = 99;
    tick(1000);
    TEST_ASSERT(
        box_count() == 0,
        "and arriving is a SEED, not thirteen million xp of gain (got %d)",
        box_count());

    /* Only what is cut afterwards belongs to the session. */
    g_client.xp[SKILL_WOODCUTTING] += 100;
    tick(1000);
    TEST_ASSERT(box_count() == 1, "the first real gain opens the box");
    press_box(0);
    TEST_ASSERT(
        row_text("d_gained") && strcmp(row_text("d_gained"), "100") == 0,
        "and the session is that gain alone (got %s)",
        row_text("d_gained") ? row_text("d_gained") : "(none)");
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

    TEST_ASSERT(box_count() == 1, "a trained skill gets a box (got %d)", box_count());
    TEST_ASSERT(!fake_widget_find("empty"), "and the empty note is gone");

    press_box(-1);
    TEST_ASSERT(!detail_skill(), "the session overview is not a disguised skill button");
    press_box(0);
    TEST_ASSERT(
        row_text("d_gained") && strcmp(row_text("d_gained"), "100") == 0,
        "and the box reads the gain back (got %s)",
        row_text("d_gained") ? row_text("d_gained") : "(none)");
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
    /* The rate is the box's and the detail block's -- the strip has no room
     * to state it, so the detail block is where it can be read at all. */
    press_box(0);
    TEST_ASSERT(
        row_text("d_hr") && strcmp(row_text("d_hr"), "36,000") == 0,
        "a minute of 10xp/second reads as 36,000/hr (got '%s')",
        row_text("d_hr") ? row_text("d_hr") : "(none)");

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
        row_text("d_hr") && strcmp(row_text("d_hr"), "3,272") == 0,
        "ten idle minutes dilute the rate over the longer span (got '%s')",
        row_text("d_hr") ? row_text("d_hr") : "(none)");

    /*
     * PAUSING is what stops the clock, and this is the assertion that says so:
     * ten further idle minutes past a pause move the number not at all.
     */
    press("d_pause", TORIRS_PANEL_ACTION_ACTIVATE, -1);
    for( int i = 0; i < 600; i++ )
        tick(1000);
    press("d_pause", TORIRS_PANEL_ACTION_ACTIVATE, -1);
    TEST_ASSERT(
        row_text("d_hr") && strcmp(row_text("d_hr"), "3,272") == 0,
        "but a paused skill's clock does not run (got '%s')",
        row_text("d_hr") ? row_text("d_hr") : "(none)");
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
    press_box(0);

    TEST_ASSERT(
        detail_skill() && strcmp(detail_skill(), "Woodcutting") == 0,
        "clicking a box opens that skill's detail (got '%s')",
        detail_skill() ? detail_skill() : "(none)");
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

    press_box(0);
    TEST_ASSERT(!detail_skill(), "and clicking it again closes it");
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
    press_box(0);
    TEST_ASSERT(
        row_text("d_actleft") && strcmp(row_text("d_actleft"), "\xe2\x80\x94") == 0,
        "nine actions is not a mean, so the row declines to answer (got '%s')",
        row_text("d_actleft") ? row_text("d_actleft") : "(none)");
}

static void
test_xp_virtual_progress_continues_past_99(void)
{
    client_reset();
    /* The protocol's base level is capped at 99, but this XP is virtual level
     * 120. The CS2 tracker continues toward level 121 at 115,126,838 XP. */
    g_client.level[SKILL_WOODCUTTING] = 99;
    g_client.xp[SKILL_WOODCUTTING] = 113479638;
    fake_config_set_raw("hide_maxed", "0");
    xp_start();
    tick(20);
    g_client.xp[SKILL_WOODCUTTING] += 100;
    tick(1000);
    press_box(0);

    TEST_ASSERT(
        row_text("d_left") && strcmp(row_text("d_left"), "1,647,100") == 0,
        "a base-level 99 continues from virtual 120 toward 121 (got '%s')",
        row_text("d_left") ? row_text("d_left") : "(none)");
    TEST_ASSERT(
        row_text("d_ttl") && strcmp(row_text("d_ttl"), "4:34:31") == 0,
        "and its time-to-level uses that virtual threshold (got '%s')",
        row_text("d_ttl") ? row_text("d_ttl") : "(none)");
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

    TEST_ASSERT(
        box_count() == 1, "hide_maxed drops the 99's box and keeps the other (got %d)",
        box_count());
    press_box(0);
    TEST_ASSERT(
        detail_skill() && strcmp(detail_skill(), "Attack") == 0,
        "and the one left is the skill that is not maxed (got '%s')",
        detail_skill() ? detail_skill() : "(none)");
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

    /* The reference's own per-skill Pause, on the box it acts on. */
    press_box(0);
    TEST_ASSERT(
        row_text("d_pause") && strcmp(row_text("d_pause"), "Pause") == 0,
        "a running skill offers Pause (got '%s')",
        row_text("d_pause") ? row_text("d_pause") : "(none)");

    press("d_pause", TORIRS_PANEL_ACTION_ACTIVATE, -1);
    TEST_ASSERT(
        row_text("d_pause") && strcmp(row_text("d_pause"), "Unpause") == 0,
        "pressing it pauses the skill and offers the way back (got '%s')",
        row_text("d_pause") ? row_text("d_pause") : "(none)");
    /*
     * A pause stops the CLOCK, not the record: the xp you earned is still
     * earned, so the record keeps it and the per-skill rate keeps its last
     * measurement. That the clock stops is pinned by
     * test_xp_rate_is_over_training_time, which is the assertion that can
     * actually see it; what belongs here is that pausing does not quietly
     * throw the session away.
     */
    TEST_ASSERT(
        row_text("d_gained") && strcmp(row_text("d_gained"), "600") == 0,
        "and pausing keeps the xp already earned (got %s)",
        row_text("d_gained") ? row_text("d_gained") : "(none)");

    press("d_pause", TORIRS_PANEL_ACTION_ACTIVATE, -1);
    TEST_ASSERT(
        row_text("d_pause") && strcmp(row_text("d_pause"), "Pause") == 0,
        "and unpauses again");
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
    press_box(0);
    TEST_ASSERT(
        row_text("d_pause") && strcmp(row_text("d_pause"), "Pause") == 0,
        "a skill just trained is not paused");

    /* Two minutes of nothing. The timer is about xp NOT arriving, which no xp
     * event could announce -- so it lives on the per-second half. */
    for( int i = 0; i < 121; i++ )
        tick(1000);
    TEST_ASSERT(
        row_text("d_pause") && strcmp(row_text("d_pause"), "Unpause") == 0,
        "two idle minutes pause it (got '%s')",
        row_text("d_pause") ? row_text("d_pause") : "(none)");

    g_client.xp[SKILL_WOODCUTTING] += 10;
    tick(1000);
    TEST_ASSERT(
        row_text("d_pause") && strcmp(row_text("d_pause"), "Pause") == 0,
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
    press_box(0);
    TEST_ASSERT(
        row_text("d_gained") && strcmp(row_text("d_gained"), "600") == 0,
        "logging out KEEPS the session -- a hop is not a new one (got %s)",
        row_text("d_gained") ? row_text("d_gained") : "(none)");
    TEST_ASSERT(
        row_text("d_pause") && strcmp(row_text("d_pause"), "Unpause") == 0,
        "and pauses every skill when pause_on_logout is set (got '%s')",
        row_text("d_pause") ? row_text("d_pause") : "(none)");
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
    TEST_ASSERT(box_count() == 1, "trained, so it has a box");

    /*
     * Per SKILL, through the box's own detail block, which is where the reset
     * lives now: the page carries no "Reset all" row, because the tracker this
     * is a port of has no such control -- its resets are ops on a row.
     */
    press_box(0);
    press("d_reset", TORIRS_PANEL_ACTION_ACTIVATE, -1);
    TEST_ASSERT(
        box_count() == 0,
        "resetting the skill zeroes the session and takes its box with it (got %d)",
        box_count());

    /* And it re-seeds rather than counting the current xp as a gain. */
    tick(1000);
    TEST_ASSERT(
        box_count() == 0,
        "the reset re-seeds from the client's xp rather than banking it as a gain "
        "(got %d)",
        box_count());
}

static void
test_xp_offline_gains_are_not_the_session(void)
{
    client_reset();
    fake_config_set_raw("save_state", "1");
    g_client.xp[SKILL_WOODCUTTING] = 1000;
    plugin_prepare(&TORIRS_PLUGIN_XP_TRACKER);
    dispatch_start();
    panel_build();
    tick(20);
    g_client.xp[SKILL_WOODCUTTING] = 1600;
    tick(1000);
    dispatch_stop();
    TEST_ASSERT(g_client.asset_present, "stopping writes the session out");

    /*
     * Back in, a million xp later, earned somewhere this client was not
     * watching. It is not this session's, so the reconciliation must move the
     * start rather than report it -- otherwise the panel opens claiming a
     * million xp in no time at all, which is the reference's own "offline
     * gains" case.
     */
    g_client.xp[SKILL_WOODCUTTING] = 1001600;
    plugin_prepare(&TORIRS_PLUGIN_XP_TRACKER);
    dispatch_start();
    panel_build();
    tick(20);
    press_box(0);
    TEST_ASSERT(
        row_text("d_gained") && strcmp(row_text("d_gained"), "600") == 0,
        "the saved session comes back and the offline million does not (got %s)",
        row_text("d_gained") ? row_text("d_gained") : "(none)");
}

/* ====================================================================== */
/* Loot tracker                                                            */
/* ====================================================================== */

/** Let the tick pull the store into the page. */
static void
settle(void)
{
    tick(1000);
}

static void
loot_start(void)
{
    fake_config_set_raw("price_source", "Cache value");
    fake_config_set_raw("kill_chat_message", "0");
    fake_config_set_raw("chat_value_threshold", "0");
    fake_config_set_raw("ignored_items", "");
    fake_config_set_raw("ignored_sources", "");
    plugin_prepare(&TORIRS_PLUGIN_LOOT_TRACKER);
    dispatch_start();
    panel_build();
}

static struct ToriRS_NpcSnapshot
dying_npc(char const* name, int x)
{
    struct ToriRS_NpcSnapshot npc;
    memset(&npc, 0, sizeof(npc));
    npc.npc_id = 1;
    npc.base_npc_id = 1;
    snprintf(npc.name, sizeof(npc.name), "%s", name);
    npc.size = 1;
    npc.true_x = x;
    npc.true_z = 3200;
    npc.level = 0;
    npc.health_ratio = 0;
    npc.health_scale = 30;
    return npc;
}

static struct ToriRS_GroundItemSnapshot
drop_at(int obj, int count, int cost, char const* name, int x)
{
    struct ToriRS_GroundItemSnapshot item;
    memset(&item, 0, sizeof(item));
    item.obj_id = obj;
    item.count = count;
    item.cost = cost;
    snprintf(item.name, sizeof(item.name), "%s", name);
    item.tile_x = x;
    item.tile_z = 3200;
    item.level = 0;
    return item;
}

static void
test_loot_rs289_inference_and_osrs_dedup(void)
{
    struct ToriRS_NpcSnapshot goblin;
    struct ToriRS_NpcSnapshot imp;
    struct ToriRS_GroundItemSnapshot coins;

    /* rs289lc has no LOOT_ADD producer. A dying NPC plus a drop on its
     * footprint becomes one source, while a confirmed zero-drop death still
     * increments its own source. */
    client_reset();
    g_lane_game = TORIRS_GAME_RS2;
    loot_start();
    goblin = dying_npc("Goblin", 3200);
    imp = dying_npc("Imp", 3201);
    coins = drop_at(995, 12, 1, "Coins", 3200);
    dispatch_npc_despawn(&goblin);
    dispatch_npc_despawn(&imp);
    dispatch_item_spawn(&coins);
    tick(1201);
    TEST_ASSERT(has_loot(), "rs289 infers loot without a native LootStore producer");
    /* Candidates expire newest-first. The expanded zero-drop Imp owns the
     * native 56px header + framed "No loot to display." body; Goblin follows
     * it, and this lands four pixels into that next header. */
    press_strip(TEST_TOTALS_H + 56 + 4);
    TEST_ASSERT(
        detail_source() && strcmp(detail_source(), "Goblin") == 0 &&
            row_text("d_kills") && strcmp(row_text("d_kills"), "1") == 0 &&
            row_text("d_value") && strcmp(row_text("d_value"), "12") == 0,
        "the inferred rs289 source carries one kill and its ground-item value (%s/%s/%s)",
        detail_source() ? detail_source() : "-",
        row_text("d_kills") ? row_text("d_kills") : "-",
        row_text("d_value") ? row_text("d_value") : "-");
    press("d_clear", TORIRS_PANEL_ACTION_ACTIVATE, -1);
    TEST_ASSERT(
        has_loot() && !detail_source(),
        "Clear data removes the inferred Goblin locally and retains the zero-drop Imp");

    /* osrs239 receives the same world events, but its LOOT_ADD-backed store is
     * authoritative. The fallback callbacks must not add a second copy. */
    client_reset();
    g_lane_game = TORIRS_GAME_OLDSCHOOL;
    loot_start();
    loot_add("Goblin", 995, 12, 1, 1);
    dispatch_npc_despawn(&goblin);
    dispatch_item_spawn(&coins);
    tick(1201);
    press_strip(TEST_TOTALS_H + 4);
    TEST_ASSERT(
        row_text("d_kills") && strcmp(row_text("d_kills"), "1") == 0 &&
            row_text("d_value") && strcmp(row_text("d_value"), "12") == 0,
        "osrs239 reads one authoritative store record, never store plus inference");
}

/*
 * A kill in the client's store becomes a band.
 *
 * The store is the CLIENT's record -- the server's kill hook feeds it and the
 * game's own tracker reads it -- so a case seeds the store rather than
 * staging a despawn and hoping the plugin correlates it.
 */
static void
test_loot_kill_becomes_a_record(void)
{
    client_reset();
    loot_start();

    loot_add("Goblin", 526, 1, 100, 1);
    settle();

    TEST_ASSERT(has_loot(), "a recorded kill is a band");
    press_strip(TEST_TOTALS_H + 4);
    TEST_ASSERT(
        detail_source() && strcmp(detail_source(), "Goblin") == 0,
        "named after the kill (got '%s')",
        detail_source() ? detail_source() : "(none)");
    TEST_ASSERT(
        row_text("d_kills") && strcmp(row_text("d_kills"), "1") == 0,
        "one kill (got '%s')", row_text("d_kills") ? row_text("d_kills") : "(none)");
    TEST_ASSERT(
        row_text("d_value") && strcmp(row_text("d_value"), "100") == 0,
        "worth what the store priced it at (got '%s')",
        row_text("d_value") ? row_text("d_value") : "(none)");
}

/*
 * A multi-item drop is ONE kill.
 *
 * The store bumps its count per distinct event id, which is the difference
 * between a kill count and a drop count -- and the number the game's own
 * "Name x N" shows.
 */
static void
test_loot_multi_item_drop_is_one_kill(void)
{
    client_reset();
    loot_start();

    loot_add("Goblin", 526, 1, 100, 1);
    loot_add("Goblin", 995, 25, 1, 1);
    settle();
    press_strip(TEST_TOTALS_H + 4);

    TEST_ASSERT(
        row_text("d_kills") && strcmp(row_text("d_kills"), "1") == 0,
        "three items off one death is one kill (got '%s')",
        row_text("d_kills") ? row_text("d_kills") : "(none)");
    TEST_ASSERT(
        row_text("d_value") && strcmp(row_text("d_value"), "125") == 0,
        "and every row counts towards its value (got '%s')",
        row_text("d_value") ? row_text("d_value") : "(none)");
}

static void
test_loot_two_kills_merge_and_sum(void)
{
    client_reset();
    loot_start();

    loot_add("Goblin", 526, 1, 100, 1);
    loot_add("Goblin", 526, 1, 100, 2);
    settle();
    press_strip(TEST_TOTALS_H + 4);

    TEST_ASSERT(
        row_text("d_kills") && strcmp(row_text("d_kills"), "2") == 0,
        "two goblins is two kills on one band (got '%s')",
        row_text("d_kills") ? row_text("d_kills") : "(none)");
    TEST_ASSERT(
        row_text("d_value") && strcmp(row_text("d_value"), "200") == 0,
        "and the quantities sum (got '%s')",
        row_text("d_value") ? row_text("d_value") : "(none)");
    TEST_ASSERT(
        row_text("d_per_kill") && strcmp(row_text("d_per_kill"), "100") == 0,
        "with a value per kill (got '%s')",
        row_text("d_per_kill") ? row_text("d_per_kill") : "(none)");
}

static void
test_loot_high_alchemy_price(void)
{
    client_reset();
    loot_start();
    fake_config_set_raw("price_source", "High alchemy");

    loot_add("Goblin", 1319, 1, 100000, 1);
    settle();
    press_strip(TEST_TOTALS_H + 4);

    /* Three fifths, which is the game's own formula and not a rounding of it. */
    TEST_ASSERT(
        row_text("d_value") && strcmp(row_text("d_value"), "60,000") == 0,
        "high alchemy is three fifths of the recorded value (got '%s')",
        row_text("d_value") ? row_text("d_value") : "(none)");
}

static void
test_loot_ignored_source(void)
{
    client_reset();
    loot_start();
    fake_config_set_raw("ignored_sources", " goblin ");

    loot_add("Goblin", 526, 1, 100, 1);
    settle();
    TEST_ASSERT(
        !has_loot(),
        "an ignore entry is matched trimmed and without case, so the band is gone");
}

static void
test_loot_ignore_button(void)
{
    client_reset();
    loot_start();

    loot_add("Goblin", 526, 1, 100, 1);
    settle();
    TEST_ASSERT(has_loot(), "recorded");

    /* The CS2 header's third op, on the band it acts on. */
    press_strip(TEST_TOTALS_H + 4);
    press("d_ignore", TORIRS_PANEL_ACTION_ACTIVATE, -1);
    settle();
    TEST_ASSERT(!has_loot(), "Ignore drops the band");
    TEST_ASSERT(
        strstr(fake_cfg_str(FAKE_CONTEXT, "ignored_sources"), "Goblin") != NULL,
        "and writes it to the list a person can also type into (got '%s')",
        fake_cfg_str(FAKE_CONTEXT, "ignored_sources"));

    /* And it STAYS ignored, which is the point of writing it there: the store
     * still holds the kill, so anything less would bring it back next tick. */
    loot_add("Goblin", 526, 1, 100, 2);
    settle();
    TEST_ASSERT(!has_loot(), "so the store's next kill is filtered too");
}

static void
test_loot_clear_and_stable_detail_identity(void)
{
    int builds;

    client_reset();
    loot_start();
    loot_add("Goblin", 526, 1, 100, 1);
    loot_add("Guard", 995, 2, 1, 2);
    settle();
    press_strip(TEST_TOTALS_H + 4);
    TEST_ASSERT(
        detail_source() && strcmp(detail_source(), "Goblin") == 0,
        "the first band selects Goblin before a store reorder");

    builds = g_client.builds;
    {
        unsigned char tmp[sizeof(g_store.source[0])];
        memcpy(tmp, &g_store.source[0], sizeof(tmp));
        memcpy(&g_store.source[0], &g_store.source[1], sizeof(tmp));
        memcpy(&g_store.source[1], tmp, sizeof(tmp));
    }
    g_store.revision++;
    settle();
    TEST_ASSERT(
        g_client.builds == builds + 1 && detail_source() &&
            strcmp(detail_source(), "Goblin") == 0,
        "a same-count reorder rebuilds the custom identity but keeps the selected source");

    press("d_clear", TORIRS_PANEL_ACTION_ACTIVATE, -1);
    TEST_ASSERT(
        g_store.count == 1 && strcmp(g_store.source[0].name, "Guard") == 0,
        "Clear data targets the built source id, not its former array index");
}

/*
 * The header's first op: a band is a header alone until it is opened.
 *
 * script2907 offers Collapse/Expand and script3042 only lays cells out under
 * an expanded one, so the strip's height is the assertion -- a collapsed band
 * is exactly the header and its gap.
 */
static void
test_loot_expand_collapse(void)
{
    int collapsed;
    int expanded;

    client_reset();
    loot_start();

    loot_add("Goblin", 526, 1, 100, 1);
    settle();
    /*
     * A band arrives OPEN, the way the game's own tracker draws one: the drops
     * under a name are what the panel was opened to see. So the gesture under
     * test is closing it and opening it again, not the other way round.
     */
    expanded = fake_widget_find("strip")->height;

    press_strip(TEST_TOTALS_H + 4);
    collapsed = fake_widget_find("strip")->height;
    TEST_ASSERT(
        collapsed < expanded,
        "closing a band gives its drops' room back (%d -> %d)", expanded, collapsed);

    press_strip(TEST_TOTALS_H + 4);
    TEST_ASSERT(
        fake_widget_find("strip")->height == expanded,
        "and opening it again makes room for them");
}

static void
test_loot_attention(void)
{
    struct ToriRS_GameEvent ev;

    client_reset();
    loot_start();

    loot_add("Goblin", 1319, 1, 100000, 1);
    settle();

    memset(&ev, 0, sizeof(ev));
    ev.kind = "valuable_drop";
    snprintf(ev.subject, sizeof(ev.subject), "Rune 2h sword");
    ev.value = 100000;
    dispatch_game_event(&ev);
    TEST_ASSERT(g_client.attention, "a valuable drop asks the player to look");

    press_strip(TEST_TOTALS_H + 4);
    TEST_ASSERT(!g_client.attention, "and looking clears it");
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
/*
 * A list that grew is a different CUSTOM input identity.
 *
 * A band arriving changes the y-to-source mapping. Rebuilding gives the well
 * a new semantic serial, so a delayed click on the prior bitmap is rejected
 * instead of being delivered to the source that moved underneath it.
 */
static void
test_loot_growth_rebuilds_for_identity(void)
{
    int builds;
    int height;
    int height_sets;
    int redraws;

    client_reset();
    loot_start();
    loot_add("Goblin", 1319, 1, 100, 1);
    settle();
    builds = g_client.builds;
    height = fake_widget_find("strip")->height;
    height_sets = g_client.exact_height_sets;
    redraws = g_client.redraws;
    TEST_ASSERT(builds > 0, "the page was declared once to begin with");

    loot_add("Cerberus", 1319, 1, 500, 2);
    settle();
    loot_add("Guard", 1319, 2, 50, 3);
    settle();
    loot_add("Imp", 1319, 1, 10, 4);
    settle();

    TEST_ASSERT(
        fake_widget_find("strip")->height > height,
        "three more sources make the strip taller (%d -> %d)", height,
        fake_widget_find("strip")->height);
    TEST_ASSERT(
        g_client.builds == builds + 3,
        "each source re-declares the custom input identity once (%d rebuilds)",
        g_client.builds - builds);
    TEST_ASSERT(
        g_client.exact_height_sets > height_sets && g_client.redraws > redraws,
        "each rebuilt bitmap also publishes its exact height and redraw");
}

static void
test_loot_unchanged_revision_is_o1(void)
{
    int const idle_ticks = 4;
    int visits;
    int height_sets;
    int redraws;

    client_reset();
    loot_start();
    visits = g_client.loot_source_visits;
    height_sets = g_client.exact_height_sets;
    redraws = g_client.redraws;
    for( int i = 0; i < idle_ticks; i++ ) settle();
    TEST_ASSERT(
        g_client.loot_source_visits == visits,
        "an unchanged loot revision performs no source/row snapshot walk");
    TEST_ASSERT(
        g_client.exact_height_sets == height_sets && g_client.redraws == redraws,
        "an unchanged loot revision emits no retained panel mutations");
}

/** A new visual row replaces the custom widget identity exactly once. */
static void
test_xp_growth_rebuilds_for_topology(void)
{
    int builds;
    int height;

    client_reset();
    g_client.xp[SKILL_WOODCUTTING] = 50000;
    g_client.xp[SKILL_ATTACK] = 50000;
    xp_start();
    tick(20);
    g_client.xp[SKILL_WOODCUTTING] += 100;
    tick(1000);
    builds = g_client.builds;
    height = fake_widget_find("boxes")->height;
    TEST_ASSERT(box_count() == 1, "one skill trained, one box");

    g_client.xp[SKILL_ATTACK] += 100;
    tick(1000);

    TEST_ASSERT(box_count() == 2, "a second skill trained gets a second box");
    TEST_ASSERT(
        fake_widget_find("boxes")->height > height,
        "which makes the strip taller (%d -> %d)", height,
        fake_widget_find("boxes")->height);
    TEST_ASSERT(
        g_client.builds == builds + 1,
        "and re-declares once to fence clicks from the old bitmap (%d rebuilds)",
        g_client.builds - builds);
}

static void
test_hidden_page_does_no_work(void)
{
    struct ToriRS_PanelLayoutEvent hidden;
    int visits;

    client_reset();
    loot_start();

    memset(&hidden, 0, sizeof(hidden));
    hidden.selection_generation = 1;
    hidden.visible = false;
    dispatch_panel_layout(&hidden);

    visits = g_client.loot_source_visits;
    loot_add("Goblin", 526, 1, 100, 1);
    settle();
    TEST_ASSERT(!has_loot(), "a hidden page is not rebuilt");
    TEST_ASSERT(
        g_client.loot_source_visits == visits,
        "a hidden page leaves even a changed store unscanned");

    /* The STORE still holds it -- only the drawing stopped -- so showing the
     * page again states everything that happened meanwhile. */
    hidden.visible = true;
    dispatch_panel_layout(&hidden);
    settle();
    TEST_ASSERT(has_loot(), "and showing it again catches up");
}

static void
test_settings_face_is_the_generated_form(void)
{
    client_reset();
    loot_start();

    loot_add("Goblin", 526, 1, 100, 1);
    settle();
    TEST_ASSERT(has_loot(), "the PAGE face carries the records");

    panel_build_view(TORIRS_PANEL_VIEW_SETTINGS);
    TEST_ASSERT(
        g_client.widget_count == 0,
        "the SETTINGS face declares nothing, leaving the generated form (got %d)",
        g_client.widget_count);

    /* And going back is a rebuild, not a resurrection: the model was cleared
     * for the other face, so the page has to state itself again. */
    panel_build();
    TEST_ASSERT(has_loot(), "and the page comes back whole");

    client_reset();
    xp_start();
    tick(20);
    panel_build_view(TORIRS_PANEL_VIEW_SETTINGS);
    TEST_ASSERT(
        g_client.widget_count == 0, "the xp tracker answers the same way (got %d)",
        g_client.widget_count);
}

int
main(void)
{
    api_init();

    test_xp_first_reading_seeds();
    test_xp_untransmitted_table_is_not_a_reading();
    test_xp_gain_makes_a_row();
    test_xp_rate_is_over_training_time();
    test_xp_detail_and_time_to_level();
    test_xp_actions_left_unknown_until_ten();
    test_xp_virtual_progress_continues_past_99();
    test_xp_hide_maxed();
    test_xp_pause();
    test_xp_auto_pause();
    test_xp_logout_pauses_and_keeps_state();
    test_xp_reset();
    test_xp_offline_gains_are_not_the_session();

    test_loot_kill_becomes_a_record();
    test_loot_rs289_inference_and_osrs_dedup();
    test_loot_multi_item_drop_is_one_kill();
    test_loot_two_kills_merge_and_sum();
    test_loot_high_alchemy_price();
    test_loot_ignored_source();
    test_loot_ignore_button();
    test_loot_clear_and_stable_detail_identity();
    test_loot_expand_collapse();
    test_loot_attention();

    test_settings_face_is_the_generated_form();
    test_loot_growth_rebuilds_for_identity();
    test_loot_unchanged_revision_is_o1();
    test_xp_growth_rebuilds_for_topology();
    test_hidden_page_does_no_work();

    if( g_plugin_state ) dispatch_stop();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
