#include "plugin/torirs_plugin_v2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_CLIENT_SETTINGS;

static int checks;
static int failures;
#define CHECK(c, m) do { checks++; if( !(c) ) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (m)); } } while( 0 )

#define CS_FRAME_ROWS_MAX 33

struct Fake
{
    struct ToriRS_FrameOfferInfo offers[5];
    int offer_count;
    struct ToriRS_FrameSelection selection;
    int requests;
    int invalidates;
    int option_sets;
    int text_sets;
    int selects;
    char selected_id[TORIRS_PLUGIN_FRAME_ID_MAX];
    char option_value[CS_FRAME_ROWS_MAX][TORIRS_PLUGIN_FRAME_ID_MAX];
    char option_label[CS_FRAME_ROWS_MAX][TORIRS_UI_LABEL_MAX];
    int option_count;
    char selected_value[TORIRS_PLUGIN_FRAME_ID_MAX];
    char detail[192];
};
static struct Fake fake;

static void fake_log(struct ToriRS_ApiV2* api, char const* format, ...)
{ (void)api; (void)format; }
static int fake_offer_next(
    struct ToriRS_ApiV2* api, int iterator, struct ToriRS_FrameOfferInfo* out)
{
    int const next = iterator + 1;
    (void)api;
    if( next < 0 || next >= fake.offer_count ) return -1;
    *out = fake.offers[next];
    return next;
}
static void fake_selection(
    struct ToriRS_ApiV2* api, struct ToriRS_FrameSelection* out)
{ (void)api; *out = fake.selection; }
static enum ToriRS_Result fake_select(struct ToriRS_ApiV2* api, char const* id)
{
    (void)api;
    fake.selects++;
    snprintf(fake.selected_id, sizeof(fake.selected_id), "%s", id);
    snprintf(fake.selection.requested_id, sizeof(fake.selection.requested_id), "%s", id);
    return TORIRS_RESULT_OK;
}
static enum ToriRS_Result fake_panel_request(
    struct ToriRS_ApiV2* api, struct ToriRS_PluginPanelDesc const* desc)
{ (void)api; (void)desc; fake.requests++; return TORIRS_RESULT_OK; }
static void fake_panel_invalidate(struct ToriRS_ApiV2* api)
{ (void)api; fake.invalidates++; }
static enum ToriRS_Result fake_panel_set_text(
    struct ToriRS_ApiV2* api, char const* id, char const* text)
{
    (void)api;
    fake.text_sets++;
    if( strcmp(id, "gameframe_detail") == 0 )
        snprintf(fake.detail, sizeof(fake.detail), "%s", text);
    return TORIRS_RESULT_OK;
}
static enum ToriRS_Result fake_panel_set_options(
    struct ToriRS_ApiV2* api,
    char const* id,
    char const* value,
    struct ToriRS_SelectOption const* options,
    int count)
{
    (void)api;
    fake.option_sets++;
    if( strcmp(id, "gameframe") != 0 ) return TORIRS_RESULT_NOT_FOUND;
    fake.option_count = count;
    snprintf(fake.selected_value, sizeof(fake.selected_value), "%s", value);
    for( int i = 0; i < count && i < CS_FRAME_ROWS_MAX; i++ )
    {
        snprintf(fake.option_value[i], sizeof(fake.option_value[i]), "%s", options[i].value);
        snprintf(fake.option_label[i], sizeof(fake.option_label[i]), "%s", options[i].label);
    }
    return TORIRS_RESULT_OK;
}
static bool fake_display_get(
    struct ToriRS_ApiV2* api, int setting, int* value, int* min, int* max)
{ (void)api; (void)setting; (void)value; (void)min; (void)max; return false; }

static void fake_heading(struct ToriRS_PanelBuilder* p, char const* t)
{ (void)p; (void)t; }
static void fake_paragraph(struct ToriRS_PanelBuilder* p, char const* t)
{ (void)p; (void)t; }
static void fake_toggle(struct ToriRS_PanelBuilder* p, char const* i, char const* l, bool v)
{ (void)p; (void)i; (void)l; (void)v; }
static void fake_button(struct ToriRS_PanelBuilder* p, char const* i, char const* l, bool e)
{ (void)p; (void)i; (void)l; (void)e; }
static void fake_custom(struct ToriRS_PanelBuilder* p, char const* i, int h)
{ (void)p; (void)i; (void)h; }
static void fake_key_value(
    struct ToriRS_PanelBuilder* p, char const* i, char const* l, char const* v)
{ (void)p; (void)i; (void)l; (void)v; }
static enum ToriRS_Result fake_node(
    struct ToriRS_PanelBuilder* p, struct ToriRS_PanelNode const* n)
{ (void)p; (void)n; return TORIRS_RESULT_OK; }
static void fake_label(struct ToriRS_PanelBuilder* p, char const* id, char const* text)
{
    (void)p;
    if( strcmp(id, "gameframe_detail") == 0 )
        snprintf(fake.detail, sizeof(fake.detail), "%s", text);
}
static void fake_select_builder(
    struct ToriRS_PanelBuilder* p,
    char const* id,
    char const* label,
    char const* value,
    struct ToriRS_SelectOption const* options,
    int count)
{
    (void)p; (void)label;
    if( strcmp(id, "gameframe") != 0 ) return;
    fake.option_count = count;
    snprintf(fake.selected_value, sizeof(fake.selected_value), "%s", value);
    for( int i = 0; i < count && i < CS_FRAME_ROWS_MAX; i++ )
    {
        snprintf(fake.option_value[i], sizeof(fake.option_value[i]), "%s", options[i].value);
        snprintf(fake.option_label[i], sizeof(fake.option_label[i]), "%s", options[i].label);
    }
}

static void offer(int row, char const* id, char const* title)
{
    memset(&fake.offers[row], 0, sizeof(fake.offers[row]));
    fake.offers[row].struct_size = sizeof(fake.offers[row]);
    snprintf(fake.offers[row].id, sizeof(fake.offers[row].id), "%s", id);
    snprintf(fake.offers[row].title, sizeof(fake.offers[row].title), "%s", title);
    fake.offers[row].available = true;
}

int main(void)
{
    struct ToriRS_ClientApiV2 client = {
        .struct_size = sizeof(client), .display_get = fake_display_get,
    };
    struct ToriRS_ApiV2 api = { 0 };
    struct ToriRS_PanelBuilder panel = {
        .struct_size = sizeof(panel), .heading = fake_heading,
        .paragraph = fake_paragraph, .toggle = fake_toggle,
        .select = fake_select_builder, .button = fake_button,
        .custom = fake_custom, .label = fake_label,
        .key_value = fake_key_value, .node = fake_node,
    };
    struct ToriRS_PluginEvPanelAction action = {
        .id = "gameframe", .action = TORIRS_PLUGIN_UI_PICK,
    };
    void* state;
    int invalidates;

    memset(&fake, 0, sizeof(fake));
    offer(0, "core/native", "Native");
    offer(1, "gameframe-layout/classic-fixed", "Classic|Fixed");
    offer(2, "gameframe-layout/modern-resizable", "Modern Resizable");
    offer(3, "mobile-gameframe/stone-drawer", "Stone Drawer");
    fake.offer_count = 4;
    fake.selection.struct_size = sizeof(fake.selection);
    snprintf(fake.selection.requested_id, sizeof(fake.selection.requested_id),
        "%s", "gameframe-layout/modern-resizable");
    snprintf(fake.selection.active_id, sizeof(fake.selection.active_id),
        "%s", "gameframe-layout/modern-resizable");
    fake.selection.status = TORIRS_FRAME_STATUS_ACTIVE;
    fake.selection.revision = 7;

    api.struct_size = sizeof(api);
    api.major_version = TORIRS_PLUGIN_API_V2_MAJOR;
    api.minor_version = TORIRS_PLUGIN_API_V2_MINOR;
    api.core.log = fake_log;
    api.frame.offer_next = fake_offer_next;
    api.frame.selection = fake_selection;
    api.frame.select = fake_select;
    api.panel.request = fake_panel_request;
    api.panel.invalidate = fake_panel_invalidate;
    api.panel.set_text = fake_panel_set_text;
    api.panel.set_options = fake_panel_set_options;
    api.client = &client;
    state = calloc(1, TORIRS_PLUGIN_CLIENT_SETTINGS.state_size);

    CHECK(TORIRS_PLUGIN_CLIENT_SETTINGS.flags & TORIRS_PLUGIN_V2_ESSENTIAL,
        "client settings is essential");
    TORIRS_PLUGIN_CLIENT_SETTINGS.callbacks.on_start(&api, state);
    CHECK(fake.requests == 1, "start registers one shared panel");
    TORIRS_PLUGIN_CLIENT_SETTINGS.callbacks.on_ui_build(&api, state, &panel, 0);
    CHECK(fake.option_count == 4, "Auto plus three plugin offers are shown");
    CHECK(strcmp(fake.option_value[0], "auto") == 0, "Auto has a stable value");
    CHECK(strcmp(fake.option_label[1], "Classic|Fixed") == 0,
        "structured labels preserve delimiter text");
    CHECK(strcmp(fake.selected_value, "gameframe-layout/modern-resizable") == 0,
        "the requested stable id is selected");
    CHECK(strcmp(fake.detail, "Active: Modern Resizable.") == 0,
        "the active frame is described by title");

    action.value = 99;
    action.text = "gameframe-layout/classic-fixed";
    TORIRS_PLUGIN_CLIENT_SETTINGS.callbacks.on_ui_action(&api, state, &action);
    CHECK(fake.selects == 1 &&
          strcmp(fake.selected_id, "gameframe-layout/classic-fixed") == 0,
        "the action uses its stable value, not a row number");
    action.text = "forged/frame";
    TORIRS_PLUGIN_CLIENT_SETTINGS.callbacks.on_ui_action(&api, state, &action);
    CHECK(fake.selects == 1, "an unknown stable value is refused");

    snprintf(fake.selection.requested_id, sizeof(fake.selection.requested_id),
        "%s", "mobile-gameframe/stone-drawer");
    snprintf(fake.selection.active_id, sizeof(fake.selection.active_id), "%s", "core/native");
    fake.selection.status = TORIRS_FRAME_STATUS_LOADING;
    snprintf(fake.selection.reason, sizeof(fake.selection.reason), "%s", "Starting provider.");
    fake.selection.revision++;
    invalidates = fake.invalidates;
    TORIRS_PLUGIN_CLIENT_SETTINGS.callbacks.on_frame_start(&api, state, NULL);
    CHECK(fake.invalidates == invalidates, "a resolver change does not rebuild the page");
    CHECK(fake.option_sets == 3 && fake.text_sets == 3,
        "frame actions and resolver changes patch their two retained rows");
    TORIRS_PLUGIN_CLIENT_SETTINGS.callbacks.on_ui_build(&api, state, &panel, 0);
    CHECK(strstr(fake.detail, "Loading Stone Drawer.") != NULL,
        "loading detail names the requested frame");
    CHECK(strstr(fake.detail, "Active for now: Native gameframe.") != NULL,
        "loading detail names the active frame");
    invalidates = fake.invalidates;
    TORIRS_PLUGIN_CLIENT_SETTINGS.callbacks.on_frame_start(&api, state, NULL);
    CHECK(fake.invalidates == invalidates, "an unchanged selection is retained");

    snprintf(fake.selection.requested_id, sizeof(fake.selection.requested_id),
        "%s", "removed-provider/favourite");
    fake.selection.status = TORIRS_FRAME_STATUS_FALLBACK;
    fake.selection.revision++;
    TORIRS_PLUGIN_CLIENT_SETTINGS.callbacks.on_frame_start(&api, state, NULL);
    TORIRS_PLUGIN_CLIENT_SETTINGS.callbacks.on_ui_build(&api, state, &panel, 0);
    CHECK(fake.option_count == 5, "an unavailable saved id remains visible");
    CHECK(strcmp(fake.option_value[4], "removed-provider/favourite") == 0 &&
          strcmp(fake.selected_value, "removed-provider/favourite") == 0,
        "the unavailable row retains and selects the exact saved id");

    free(state);
    printf("client_settings_test: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
