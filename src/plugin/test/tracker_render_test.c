/*
 * Tracker panel-shape regression.
 *
 * The trackers used to compose their pages into PNG-like custom wells. This
 * test deliberately has no draw builder, image API, decoder, or raster helper:
 * it mounts each empty page through the public panel builder and verifies that
 * its summary is semantic UI and that no panel draw callback exists.
 */

#include "plugin/torirs_plugin_v2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_XP_TRACKER;
extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_LOOT_TRACKER;

static int g_checks;
static int g_failures;
static int g_customs;
static int g_actions;
static int g_key_values;
static int g_total_rate;
static int g_total_gained;
static int g_total_count;
static int g_total_value;

#define CHECK(condition, ...)                                                          \
    do                                                                                 \
    {                                                                                  \
        g_checks++;                                                                    \
        if( !(condition) )                                                             \
        {                                                                              \
            g_failures++;                                                              \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                               \
            printf(__VA_ARGS__);                                                       \
            printf("\n");                                                            \
        }                                                                              \
    } while( 0 )

static uint64_t
frame_ms(struct ToriRS_ApiV2* api)
{
    (void)api;
    return 1000;
}

static bool
config_bool(struct ToriRS_ApiV2* api, char const* key, bool* out)
{
    (void)api;
    (void)key;
    *out = false;
    return true;
}

static bool
config_int(struct ToriRS_ApiV2* api, char const* key, int* out)
{
    (void)api;
    (void)key;
    *out = 0;
    return true;
}

static bool
config_string(struct ToriRS_ApiV2* api, char const* key, char const** out)
{
    (void)api;
    (void)key;
    *out = "";
    return true;
}

static enum ToriRS_Result
panel_request(
    struct ToriRS_ApiV2* api,
    struct ToriRS_PanelDescriptor const* descriptor)
{
    (void)api;
    (void)descriptor;
    return TORIRS_RESULT_OK;
}

static void panel_invalidate(struct ToriRS_ApiV2* api) { (void)api; }
static void panel_attention(struct ToriRS_ApiV2* api, bool wanted)
{ (void)api; (void)wanted; }
static enum ToriRS_Result panel_set_text(
    struct ToriRS_ApiV2* api, char const* id, char const* text)
{ (void)api; (void)id; (void)text; return TORIRS_RESULT_OK; }
static enum ToriRS_Result panel_set_value(
    struct ToriRS_ApiV2* api, char const* id, int value)
{ (void)api; (void)id; (void)value; return TORIRS_RESULT_OK; }
static enum ToriRS_Result panel_set_height(
    struct ToriRS_ApiV2* api, char const* id, int height)
{ (void)api; (void)id; (void)height; return TORIRS_RESULT_OK; }
static void panel_redraw(struct ToriRS_ApiV2* api, char const* id)
{ (void)api; (void)id; }
static void asset_release(struct ToriRS_ApiV2* api, char const* name)
{ (void)api; (void)name; }

static int
loot_source_next(
    struct ToriRS_ApiV2* api,
    int iterator,
    struct ToriRS_LootSource* out)
{
    (void)api;
    (void)iterator;
    (void)out;
    return -1;
}

static int
loot_row_next(
    struct ToriRS_ApiV2* api,
    int source,
    int iterator,
    struct ToriRS_LootRow* out)
{
    (void)api;
    (void)source;
    (void)iterator;
    (void)out;
    return -1;
}

static uint64_t loot_revision(struct ToriRS_ApiV2* api)
{ (void)api; return 1; }

static void heading(struct ToriRS_PanelBuilder* panel, char const* text)
{ (void)panel; (void)text; }
static void paragraph(struct ToriRS_PanelBuilder* panel, char const* text)
{ (void)panel; (void)text; }
static void toggle(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    char const* label,
    bool value)
{ (void)panel; (void)id; (void)label; (void)value; }
static void select_row(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    char const* label,
    char const* value,
    struct ToriRS_SelectOption const* options,
    int option_count)
{
    (void)panel; (void)id; (void)label; (void)value;
    (void)options; (void)option_count;
}
static void button(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    char const* label,
    bool enabled)
{ (void)panel; (void)id; (void)label; (void)enabled; }
static void custom(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    int preferred_height)
{ (void)panel; (void)id; (void)preferred_height; g_customs++; }
static void label(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    char const* text)
{ (void)panel; (void)id; (void)text; }
static void key_value(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    char const* label_text,
    char const* value)
{
    (void)panel;
    (void)label_text;
    (void)value;
    g_key_values++;
    g_total_rate |= strcmp(id, "total_rate") == 0;
    g_total_gained |= strcmp(id, "total_gained") == 0;
    g_total_count |= strcmp(id, "total_count") == 0;
    g_total_value |= strcmp(id, "total_value") == 0;
}
static enum ToriRS_Result node(
    struct ToriRS_PanelBuilder* panel,
    struct ToriRS_PanelNode const* description)
{ (void)panel; (void)description; return TORIRS_RESULT_OK; }
static void action_row(
    struct ToriRS_PanelBuilder* panel,
    char const* id,
    char const* label_text,
    char const* text)
{ (void)panel; (void)id; (void)label_text; (void)text; g_actions++; }

static void
exercise(struct ToriRS_PluginDefV2 const* plugin, bool xp)
{
    struct ToriRS_ApiV2 api;
    struct ToriRS_GameApiV2 game;
    struct ToriRS_PanelBuilder panel;
    void* state = calloc(1, plugin->state_size);

    memset(&api, 0, sizeof(api));
    memset(&game, 0, sizeof(game));
    memset(&panel, 0, sizeof(panel));
    api.struct_size = sizeof(api);
    api.major_version = TORIRS_PLUGIN_API_V2_MAJOR;
    api.minor_version = TORIRS_PLUGIN_API_V2_MINOR;
    api.core.frame_ms = frame_ms;
    api.config.get_bool = config_bool;
    api.config.get_int = config_int;
    api.config.get_string = config_string;
    api.assets.release = asset_release;
    api.panel.request = panel_request;
    api.panel.invalidate = panel_invalidate;
    api.panel.attention = panel_attention;
    api.panel.set_text = panel_set_text;
    api.panel.set_value = panel_set_value;
    api.panel.set_height = panel_set_height;
    api.panel.redraw = panel_redraw;
    game.struct_size = sizeof(game);
    game.loot_source_next = loot_source_next;
    game.loot_row_next = loot_row_next;
    game.loot_revision = loot_revision;
    api.game = &game;

    panel.struct_size = sizeof(panel);
    panel.heading = heading;
    panel.paragraph = paragraph;
    panel.toggle = toggle;
    panel.select = select_row;
    panel.button = button;
    panel.custom = custom;
    panel.label = label;
    panel.key_value = key_value;
    panel.node = node;
    panel.action_row = action_row;

    g_customs = 0;
    g_actions = 0;
    g_key_values = 0;
    g_total_rate = 0;
    g_total_gained = 0;
    g_total_count = 0;
    g_total_value = 0;

    CHECK(state != NULL, "%s state allocated", plugin->title);
    if( !state )
        return;
    if( plugin->callbacks.on_start )
        plugin->callbacks.on_start(&api, state);
    plugin->callbacks.on_ui_build(
        &api, state, &panel, TORIRS_PANEL_VIEW_PAGE);

    CHECK(plugin->callbacks.on_ui_draw == NULL,
        "%s exposes no panel draw callback", plugin->title);
    CHECK(g_customs == 0, "%s declares no CUSTOM well", plugin->title);
    CHECK(g_actions == 0, "%s empty state has no action rows", plugin->title);
    CHECK(g_key_values == 2, "%s empty summary has two key/value rows", plugin->title);
    if( xp )
        CHECK(g_total_rate && g_total_gained,
            "XP summary uses total_rate and total_gained");
    else
        CHECK(g_total_count && g_total_value,
            "loot summary uses total_count and total_value");

    if( plugin->callbacks.on_stop )
        plugin->callbacks.on_stop(&api, state);
    free(state);
}

int
main(void)
{
    exercise(&TORIRS_PLUGIN_XP_TRACKER, true);
    exercise(&TORIRS_PLUGIN_LOOT_TRACKER, false);
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
