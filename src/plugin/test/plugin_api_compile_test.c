/*
 * Compile-only public-contract test for torirs_plugin_api.h.
 *
 * This intentionally has no host. It is a small third-party plugin translation
 * unit whose job is to stop the documented source shape drifting: modules are
 * embedded, callbacks receive per-instance state, a gameframe is PROVIDED
 * through on_gameframe and the widget API, and draw/panel builders are scoped
 * parameters rather than saved surface handles.
 */

#include "plugin/torirs_plugin_api.h"

#include <stddef.h>

/* No frozen major-2 offsets: all consumers are recompiled for major 3. */
_Static_assert(TORIRS_PLUGIN_API_MAJOR == 3, "current breaking API");

struct ExampleState
{
    struct ToriRS_WidgetRef report;
    int starts;
};

static void
example_report_bound(struct ToriRS_Api* api, void* user, struct ToriRS_WidgetEvent const* event)
{
    struct ExampleState* state = user;
    (void)api;
    if( event->type == TORIRS_WIDGET_BOUND )
        state->report = event->widget;
}

static void
example_start(struct ToriRS_Api* api, void* plugin_state)
{
    struct ExampleState* state = plugin_state;
    struct ToriRS_WidgetBounds bounds;

    state->starts++;
    (void)api->widgets.watch(api->widgets.context, "chat_report", example_report_bound, state);
    if( ToriRS_WidgetRefValid(state->report) &&
        api->widgets.bounds(api->widgets.context, state->report, &bounds) == TORIRS_CONTRACT_OK )
        (void)api->widgets.set_hidden(api->widgets.context, state->report, false);
}

/* An offer is served here: the frame is the provider's retained widget edits. */
static enum ToriRS_FrameBuildResult
example_gameframe(
    struct ToriRS_Api* api,
    void* plugin_state,
    struct ToriRS_GameframeEvent const* event)
{
    struct ToriRS_WidgetRef minimap;
    int chat_w = 0;
    int chat_h = 0;
    (void)plugin_state;

    if( !event->active )
        return TORIRS_FRAME_READY;
    if( api->widgets.find(api->widgets.context, "minimap", &minimap) != TORIRS_CONTRACT_OK )
        return TORIRS_FRAME_PENDING;
    (void)api->frame.surface_native_size(api, TORIRS_SURFACE_CHAT, &chat_w, &chat_h);
    (void)api->widgets.set_position(api->widgets.context, minimap, event->width - 200, 0);
    return TORIRS_FRAME_READY;
}

static void
example_draw_canvas(
    struct ToriRS_Api* api,
    void* plugin_state,
    struct ToriRS_Graphics* draw)
{
    struct ToriRS_DrawContext context = { .struct_size = sizeof(context) };
    (void)api;
    (void)plugin_state;
    if( draw->context(draw, &context) )
        draw->text(draw, context.bounds.x, context.bounds.y, "Example", 0xffffffu);
}

static struct ToriRS_ConfigItem const EXAMPLE_CONFIG_ITEMS[] = {
    {
        .key = "enabled",
        .type = TORIRS_CONFIG_BOOL,
        .label = "Enabled",
        .default_value = "1",
    },
    { 0 },
};

static struct ToriRS_ConfigSchema const EXAMPLE_CONFIG = {
    .struct_size = sizeof(struct ToriRS_ConfigSchema),
    .items = EXAMPLE_CONFIG_ITEMS,
};

static void
example_ui_build(
    struct ToriRS_Api* api,
    void* plugin_state,
    struct ToriRS_PanelBuilder* panel,
    int view)
{
    (void)api;
    (void)plugin_state;
    (void)view;
    panel->heading(panel, "Example");
}

static struct ToriRS_FrameOffer const EXAMPLE_FRAMES[] = {
    {
        .struct_size = sizeof(struct ToriRS_FrameOffer),
        .id = "example-fixed",
        .title = "Example Fixed",
        .canvas = TORIRS_FRAME_CANVAS_FIXED,
        .width = 765,
        .height = 503,
    },
    {
        .struct_size = sizeof(struct ToriRS_FrameOffer),
        .id = "example-window",
        .title = "Example Window",
        .canvas = TORIRS_FRAME_CANVAS_WINDOW,
        .min_width = 765,
        .min_height = 503,
    },
    { .struct_size = sizeof(struct ToriRS_FrameOffer), .id = NULL },
};

static struct ToriRS_PluginDef const EXAMPLE_PLUGIN = {
    .struct_size = sizeof(struct ToriRS_PluginDef),
    .id = "v2-compile-example",
    .title = "V2 Compile Example",
    .version = "3.0.0",
    .state_size = sizeof(struct ExampleState),
    .config = &EXAMPLE_CONFIG,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = example_start,
        .on_draw_canvas = example_draw_canvas,
        .on_ui_build = example_ui_build,
        .on_gameframe = example_gameframe,
    },
    .frames = EXAMPLE_FRAMES,
};

int
main(void)
{
    _Static_assert(TORIRS_PLUGIN_API_MAJOR == 3u, "this is the current contract");
    _Static_assert(
        sizeof(((struct ToriRS_Api*)0)->widgets) == sizeof(struct ToriRS_WidgetApi),
        "widgets is embedded in the API");
    _Static_assert(
        sizeof(((struct ToriRS_Api*)0)->frame) == sizeof(struct ToriRS_FrameApi),
        "frame is embedded in the API");
    _Static_assert(
        TORIRS_FRAME_OFFER_REQUIRED_SIZE <= sizeof(struct ToriRS_FrameOffer),
        "an offer is its id, title and canvas policy");
    return EXAMPLE_PLUGIN.struct_size == sizeof(EXAMPLE_PLUGIN) ? 0 : 1;
}
