/*
 * Compile-only public-contract test for torirs_plugin_v2.h.
 *
 * This intentionally has no host. It is a small third-party plugin translation
 * unit whose job is to stop the documented source shape drifting: modules are
 * embedded, callbacks receive per-instance state, and frame/draw builders are
 * scoped parameters rather than saved surface handles.
 */

#include "plugin/torirs_plugin_v2.h"

#include <stddef.h>

#define STRUCT_SIZE_IS_FIRST(type)                                                        \
    _Static_assert(offsetof(struct type, struct_size) == 0, #type " must start with size")
#define RESERVED_IS_TAIL(type)                                                           \
    _Static_assert(                                                                      \
        offsetof(struct type, reserved_v2) + sizeof(((struct type*)0)->reserved_v2) ==   \
            sizeof(struct type),                                                         \
        #type " must keep its reserved ABI storage at the tail")
#define MODULE_LAYOUT_IS_FROZEN(type, first_operation, operation_count)                  \
    _Static_assert(                                                                      \
        offsetof(struct type, first_operation) == (sizeof(void*) == 8 ? 8u : 4u),        \
        #type " header layout changed");                                                 \
    _Static_assert(                                                                      \
        offsetof(struct type, reserved_v2) ==                                            \
            offsetof(struct type, first_operation) +                                    \
                (operation_count)*sizeof(((struct type*)0)->first_operation),            \
        #type " must consume reserved slots instead of growing");                       \
    RESERVED_IS_TAIL(type)
#define API_MODULES_ARE_ADJACENT(left, right)                                            \
    _Static_assert(                                                                      \
        offsetof(struct ToriRS_ApiV2, right) ==                                          \
            offsetof(struct ToriRS_ApiV2, left) +                                       \
                sizeof(((struct ToriRS_ApiV2*)0)->left),                                 \
        "ApiV2 embedded module order/stride changed")

STRUCT_SIZE_IS_FIRST(ToriRS_ApiV2);
STRUCT_SIZE_IS_FIRST(ToriRS_CoreApiV2);
STRUCT_SIZE_IS_FIRST(ToriRS_ConfigApiV2);
STRUCT_SIZE_IS_FIRST(ToriRS_WorldApiV2);
STRUCT_SIZE_IS_FIRST(ToriRS_InputApiV2);
STRUCT_SIZE_IS_FIRST(ToriRS_UiApiV2);
STRUCT_SIZE_IS_FIRST(ToriRS_PlacementApiV2);
STRUCT_SIZE_IS_FIRST(ToriRS_FrameApiV2);
STRUCT_SIZE_IS_FIRST(ToriRS_DrawApiV2);
STRUCT_SIZE_IS_FIRST(ToriRS_AssetsApiV2);
STRUCT_SIZE_IS_FIRST(ToriRS_SceneApiV2);
STRUCT_SIZE_IS_FIRST(ToriRS_PanelApiV2);
STRUCT_SIZE_IS_FIRST(ToriRS_CacheApiV2);
STRUCT_SIZE_IS_FIRST(ToriRS_ClientApiV2);
STRUCT_SIZE_IS_FIRST(ToriRS_GameApiV2);
STRUCT_SIZE_IS_FIRST(ToriRS_DrawBuilder);
STRUCT_SIZE_IS_FIRST(ToriRS_DrawContext);
STRUCT_SIZE_IS_FIRST(ToriRS_FrameBuilder);
STRUCT_SIZE_IS_FIRST(ToriRS_PanelBuilder);
STRUCT_SIZE_IS_FIRST(ToriRS_PanelNode);
STRUCT_SIZE_IS_FIRST(ToriRS_PluginCallbacks);
STRUCT_SIZE_IS_FIRST(ToriRS_PluginDefV2);
MODULE_LAYOUT_IS_FROZEN(ToriRS_CoreApiV2, log, 8);
MODULE_LAYOUT_IS_FROZEN(ToriRS_ConfigApiV2, has, 6);
MODULE_LAYOUT_IS_FROZEN(ToriRS_WorldApiV2, local_player, 6);
MODULE_LAYOUT_IS_FROZEN(ToriRS_InputApiV2, key_held, 6);
MODULE_LAYOUT_IS_FROZEN(ToriRS_UiApiV2, ref, 7);
MODULE_LAYOUT_IS_FROZEN(ToriRS_PlacementApiV2, revision, 8);
MODULE_LAYOUT_IS_FROZEN(ToriRS_FrameApiV2, offer_next, 5);
MODULE_LAYOUT_IS_FROZEN(ToriRS_DrawApiV2, project, 4);
MODULE_LAYOUT_IS_FROZEN(ToriRS_AssetsApiV2, request, 12);
MODULE_LAYOUT_IS_FROZEN(ToriRS_SceneApiV2, mesh_create, 16);
MODULE_LAYOUT_IS_FROZEN(ToriRS_PanelApiV2, request, 8);
MODULE_LAYOUT_IS_FROZEN(ToriRS_CacheApiV2, frame_root, 9);
RESERVED_IS_TAIL(ToriRS_ClientApiV2);
RESERVED_IS_TAIL(ToriRS_GameApiV2);
RESERVED_IS_TAIL(ToriRS_SelectOption);
RESERVED_IS_TAIL(ToriRS_UiNode);
RESERVED_IS_TAIL(ToriRS_UiContribution);
RESERVED_IS_TAIL(ToriRS_FrameOffer);
RESERVED_IS_TAIL(ToriRS_PanelNode);
RESERVED_IS_TAIL(ToriRS_ApiV2);
_Static_assert(
    offsetof(struct ToriRS_SelectOption, reserved_v2) == (sizeof(void*) == 8 ? 40u : 20u),
    "SelectOption frozen array stride changed");
_Static_assert(
    offsetof(struct ToriRS_UiNode, reserved_v2) == (sizeof(void*) == 8 ? 192u : 140u),
    "UiNode frozen embedded stride changed");
_Static_assert(
    offsetof(struct ToriRS_UiContribution, reserved_v2) == (sizeof(void*) == 8 ? 280u : 188u),
    "UiContribution frozen array stride changed");
_Static_assert(
    offsetof(struct ToriRS_FrameOffer, reserved_v2) == (sizeof(void*) == 8 ? 64u : 40u),
    "FrameOffer frozen array stride changed");
_Static_assert(
    offsetof(struct ToriRS_PluginDefV2, callbacks) >
        offsetof(struct ToriRS_PluginDefV2, draw_order),
    "the extensible callback table must remain the definition's final field");
_Static_assert(
    offsetof(struct ToriRS_PluginDefV2, callbacks) == (sizeof(void*) == 8 ? 80u : 44u),
    "the stable callback-table offset changed");
_Static_assert(offsetof(struct ToriRS_ApiV2, major_version) == 4, "ApiV2 major offset is frozen");
_Static_assert(offsetof(struct ToriRS_ApiV2, minor_version) == 8, "ApiV2 minor offset is frozen");
_Static_assert(
    offsetof(struct ToriRS_ApiV2, instance) == (sizeof(void*) == 8 ? 16u : 12u),
    "ApiV2 instance offset is frozen");
_Static_assert(
    offsetof(struct ToriRS_ApiV2, core) ==
        offsetof(struct ToriRS_ApiV2, instance) + sizeof(void*),
    "ApiV2 module prefix is frozen");
API_MODULES_ARE_ADJACENT(core, config);
API_MODULES_ARE_ADJACENT(config, world);
API_MODULES_ARE_ADJACENT(world, input);
API_MODULES_ARE_ADJACENT(input, ui);
API_MODULES_ARE_ADJACENT(ui, placement);
API_MODULES_ARE_ADJACENT(placement, frame);
API_MODULES_ARE_ADJACENT(frame, draw);
API_MODULES_ARE_ADJACENT(draw, assets);
API_MODULES_ARE_ADJACENT(assets, scene);
API_MODULES_ARE_ADJACENT(scene, panel);
API_MODULES_ARE_ADJACENT(panel, cache);

struct ExampleState
{
    struct ToriRS_UiNodeRef report;
    int starts;
};

static void
example_start(struct ToriRS_ApiV2* api, void* plugin_state)
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
    struct ToriRS_ApiV2* api,
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
    struct ToriRS_ApiV2* api,
    void* plugin_state,
    struct ToriRS_DrawBuilder* draw)
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
        .type = TORIRS_PLUGIN_CFG_BOOL,
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
    struct ToriRS_ApiV2* api,
    void* plugin_state,
    struct ToriRS_UiNodeRef node,
    struct ToriRS_DrawBuilder* draw)
{
    (void)api;
    (void)plugin_state;
    (void)node;
    draw->text(draw, 0, 0, "Report", 0xffffffu);
}

static enum ToriRS_CallbackResult
example_ui_node_action(
    struct ToriRS_ApiV2* api,
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

static struct ToriRS_PluginDefV2 const EXAMPLE_PLUGIN = {
    .struct_size = sizeof(struct ToriRS_PluginDefV2),
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
    _Static_assert(TORIRS_PLUGIN_API_V2_MAJOR == 2u, "this is the v2 contract");
    _Static_assert(
        sizeof(((struct ToriRS_ApiV2*)0)->ui) == sizeof(struct ToriRS_UiApiV2),
        "ui is embedded in the API");
    _Static_assert(
        sizeof(((struct ToriRS_ApiV2*)0)->placement) ==
            sizeof(struct ToriRS_PlacementApiV2),
        "placement is embedded in the API");
    return EXAMPLE_PLUGIN.struct_size == sizeof(EXAMPLE_PLUGIN) ? 0 : 1;
}
