/*
 * Compile-only public-contract test for torirs_plugin_api.h.
 *
 * This intentionally has no host. It is a small third-party plugin translation
 * unit whose job is to stop the documented source shape drifting: modules are
 * embedded, callbacks receive per-instance state, and frame/draw builders are
 * scoped parameters rather than saved surface handles.
 */

#include "plugin/torirs_plugin_api.h"

#include <stddef.h>

/* No frozen major-2 offsets: all consumers are recompiled for major 3. */
_Static_assert(TORIRS_PLUGIN_API_MAJOR == 3, "current breaking API");

struct ExampleState
{
    struct ToriRS_UiNodeRef report;
    int starts;
};

static void
example_start(struct ToriRS_Api* api, void* plugin_state)
{
    struct ExampleState* state = plugin_state;
    struct ToriRS_UiNodeInfo info = {
        .struct_size = sizeof(info),
    };

    state->starts++;
    state->report = api->ui.ref(api, "frame.chat.button.report");
    if( api->ui.info(api, state->report, &info) && info.visible )
        (void)api->ui.invoke(api, state->report, "activate");
}

static enum ToriRS_FrameBuildResult
example_frame_build(
    struct ToriRS_Api* api,
    void* plugin_state,
    struct ToriRS_FrameBuilder* frame,
    struct ToriRS_FrameBuildContext const* context)
{
    struct ToriRS_Rect usable;
    struct ToriRS_FrameSurfaceOverlay compass_overlay = {
        .struct_size = sizeof(compass_overlay),
        .image = { .value = 1 },
        .x = 8,
        .y = 8,
        .alpha = 255,
    };
    struct ToriRS_FrameScrollbar scrollbar = {
        .struct_size = sizeof(scrollbar),
        .up = { 2 },
        .down = { 3 },
        .track = { 4 },
        .split_thumb = true,
        .thumb_top = { 5 },
        .thumb_middle = { 6 },
        .thumb_bottom = { 7 },
    };
    (void)plugin_state;

    if( !api->placement.primary(api, context->available, &usable) )
    {
        frame->reason(frame, "No usable frame area");
        return TORIRS_FRAME_UNSUPPORTED;
    }
    frame->surface(frame, TORIRS_SURFACE_VIEWPORT, usable);
    frame->surface(frame, TORIRS_SURFACE_COMPASS, (struct ToriRS_Rect){ 8, 8, 33, 33 });
    frame->surface(frame, TORIRS_SURFACE_ORBS, (struct ToriRS_Rect){ 44, 8, 40, 160 });
    frame->surface_overlay(frame, TORIRS_SURFACE_COMPASS, &compass_overlay);
    frame->scrollbar(frame, &scrollbar);
    return TORIRS_FRAME_READY;
}

static void
example_frame_draw(
    struct ToriRS_Api* api,
    void* plugin_state,
    struct ToriRS_Graphics* draw)
{
    struct ToriRS_Rect label;
    (void)plugin_state;

    if( api->placement.place(
            api,
            TORIRS_AREA_OVERLAY_SAFE,
            TORIRS_ANCHOR_TOP_RIGHT,
            120,
            24,
            8,
            &label) )
    {
        draw->text(draw, label.x, label.y, "Example", 0xffffffu);
    }
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
example_ui_node_draw(
    struct ToriRS_Api* api,
    void* plugin_state,
    struct ToriRS_UiNodeRef node,
    struct ToriRS_Graphics* draw)
{
    (void)api;
    (void)plugin_state;
    (void)node;
    draw->text(draw, 0, 0, "Report", 0xffffffu);
}

static enum ToriRS_CallbackResult
example_ui_node_action(
    struct ToriRS_Api* api,
    void* plugin_state,
    struct ToriRS_UiNodeRef node,
    char const* action)
{
    (void)api;
    (void)plugin_state;
    (void)node;
    return action ? TORIRS_CALLBACK_CONSUME : TORIRS_CALLBACK_CONTINUE;
}

static struct ToriRS_UiContribution const EXAMPLE_CONTRIBUTIONS[] = {
    {
        .struct_size = sizeof(struct ToriRS_UiContribution),
        .node = "frame.chat.button.report",
        .mode = TORIRS_UI_MODIFY,
        .facets = TORIRS_UI_FACET_APPEARANCE,
        .value = {
            .struct_size = sizeof(struct ToriRS_UiNode),
            .flags = TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED,
            .image = { .value = 0 },
            .clip = TORIRS_UI_CLIP_PARENT,
            .state_image_mask = 1u << TORIRS_UI_VISUAL_HOVER,
            .state_images = { [TORIRS_UI_VISUAL_HOVER] = { .value = 2 } },
            .label = "Report",
            .label_x = 4,
            .label_y = 2,
            .hit_rect_mode = TORIRS_UI_HIT_RECT_CUSTOM,
            .hit_rect = { 1, 1, 90, 20 },
            .action_count = 2,
            .actions = { "activate", "inspect" },
        },
    },
    { .struct_size = sizeof(struct ToriRS_UiContribution), .node = NULL },
};

static struct ToriRS_FrameOffer const EXAMPLE_FRAMES[] = {
    {
        .struct_size = sizeof(struct ToriRS_FrameOffer),
        .id = "example-fixed",
        .title = "Example Fixed",
        .canvas = TORIRS_FRAME_CANVAS_FIXED,
        .width = 765,
        .height = 503,
        .build = example_frame_build,
        .draw = example_frame_draw,
    },
    { .struct_size = sizeof(struct ToriRS_FrameOffer), .id = NULL },
};

static struct ToriRS_PluginDef const EXAMPLE_PLUGIN = {
    .struct_size = sizeof(struct ToriRS_PluginDef),
    .id = "v2-compile-example",
    .title = "V2 Compile Example",
    .version = "2.0.0",
    .state_size = sizeof(struct ExampleState),
    .config = &EXAMPLE_CONFIG,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = example_start,
        .on_ui_node_draw = example_ui_node_draw,
        .on_ui_node_action = example_ui_node_action,
    },
    .frames = EXAMPLE_FRAMES,
    .ui_contributions = EXAMPLE_CONTRIBUTIONS,
};

int
main(void)
{
    _Static_assert(TORIRS_PLUGIN_API_MAJOR == 3u, "this is the current contract");
    _Static_assert(
        sizeof(((struct ToriRS_Api*)0)->ui) == sizeof(struct ToriRS_UiApi),
        "ui is embedded in the API");
    _Static_assert(
        sizeof(((struct ToriRS_Api*)0)->placement) ==
            sizeof(struct ToriRS_PlacementApi),
        "placement is embedded in the API");
    return EXAMPLE_PLUGIN.struct_size == sizeof(EXAMPLE_PLUGIN) ? 0 : 1;
}
