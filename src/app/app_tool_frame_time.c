/*
 * The frame-time panel: a side-app (sideapp/side_app.h).
 *
 * One minimenu-styled ToriRSChrome panel (src/ui/README_DEBUG_OVERLAY.md) in
 * the developer chrome, holding the frame time averaged over the last
 * FRAME_TIME_RING_SAMPLES frames. A key bound to `debug_overlay` (CTRL held)
 * shows and hides it. The node that draws the developer chrome is declared by
 * the manifest (`type=debug_overlay`, docs/debug_overlay.md §2) and answered
 * through UITREE_HOST_GET_DEBUG_OVERLAY.
 *
 * A hidden panel builds no primitives, so the panel costs nothing until the
 * toggle key turns it on. The samples are not this tool's: the frame clock
 * keeps them every frame (App_NoteFrameTime, perf/frame_clock.h) -- the
 * average is a property of the client, not of whether anyone is looking at
 * it, and a readout that starts at "--" for ten frames after every toggle
 * would be useless for exactly the stutter it is there to catch.
 *
 * Built first in the developer chrome (app_debug_overlay_init's
 * SideApps_BuildChrome, ahead of the loc editor's panel). The key is
 * delivered and the readout refreshed in the dev chrome's tick, before the
 * BOOTING early-out so the key still latches during a boot, and before the
 * chrome is built so a changed readout reaches the same frame's display list;
 * the key with CTRL, not one the map editor claimed this frame, and not while
 * a text input has focus.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "mem/torirs_mem.h"
#include "app/app_internal.h"

struct AppToolFrameTime
{
    /** The panel and its one row in the developer chrome, 0 until the
     *  chrome's panels are built. */
    int panel;
    int frame_row;
    /** Whether the panel is shown. */
    int visible;
};

/* Private to this unit, declared up front so definition order is free. */
static void
app_tool_frame_time_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args);
static void
app_tool_frame_time_frame(
    void* self,
    struct SideAppHost* host,
    enum SideAppPhase phase,
    struct LibToriRS_Input* input);
static void
app_tool_frame_time_build_chrome(
    void* self,
    struct SideAppHost* host);
static void
app_tool_frame_time_free(void* self);

/* Sizes the panel. The title is the widest string the panel will ever hold, so
 * content sizing (fixed_w 0) settles on one width and the panel never resizes
 * as the digits change under it. */
static char const k_app_tool_frame_time_title[] = "Frame time (10-frame avg)";

enum
{
    APP_TOOL_FRAME_TIME_TOGGLE
};

static struct SideAppAction const k_app_tool_frame_time_actions[] = {
    [APP_TOOL_FRAME_TIME_TOGGLE] = { "debug_overlay", SIDE_APP_PHASE_EARLY_CHROME, SIDE_APP_GATE_CTRL,
                                     SIDE_APP_KEY_PRESS, 0 },
};

static struct SideAppHooks const k_app_tool_frame_time_hooks = {
    .key = app_tool_frame_time_key,
    .free = app_tool_frame_time_free,
    .frame = app_tool_frame_time_frame,
    .build_chrome = app_tool_frame_time_build_chrome,
};

void
app_tool_frame_time_register(struct App* app)
{
    struct AppToolFrameTime* tool;

    assert(app);
    assert(app->side_apps);
    tool = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*tool));
    SideApps_Register(
        app->side_apps,
        "frame_time",
        tool,
        &k_app_tool_frame_time_hooks,
        k_app_tool_frame_time_actions,
        (int)(sizeof(k_app_tool_frame_time_actions) / sizeof(k_app_tool_frame_time_actions[0])));
}

/* The panel, hidden, in the developer chrome. */
static void
app_tool_frame_time_build_chrome(
    void* self,
    struct SideAppHost* host)
{
    struct AppToolFrameTime* tool = self;
    struct App* app = app_from_side_app_host(host);

    assert(tool);
    tool->visible = 0;
    tool->panel = ToriRSChrome_PanelAdd(
        &app->dev_overlay->ui, TORIRS_CHROME_PANEL_MENU, 8, 8, 0, k_app_tool_frame_time_title);
    tool->frame_row = ToriRSChrome_MenuItem(&app->dev_overlay->ui, tool->panel, "--");
    ToriRSChrome_PanelSetVisible(&app->dev_overlay->ui, tool->panel, 0);
}

/* The toggle key: show or hide the panel. */
static void
app_tool_frame_time_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args)
{
    struct AppToolFrameTime* tool = self;
    struct App* app = app_from_side_app_host(host);

    (void)action;
    (void)args;
    assert(tool);
    assert(action == APP_TOOL_FRAME_TIME_TOGGLE);
    assert(args);
    tool->visible = !tool->visible;
    ToriRSChrome_PanelSetVisible(&app->dev_overlay->ui, tool->panel, tool->visible);
}

/* The readout, refreshed in the dev chrome's tick while the panel is shown. */
static void
app_tool_frame_time_frame(
    void* self,
    struct SideAppHost* host,
    enum SideAppPhase phase,
    struct LibToriRS_Input* input)
{
    struct AppToolFrameTime* tool = self;
    struct App* app = app_from_side_app_host(host);

    (void)input;
    assert(tool);
    if( phase != SIDE_APP_PHASE_EARLY_CHROME )
        return;
    if( tool->visible )
    {
        uint32_t const mean_us = FrameTimeRing_MeanUs(&app->frame_clock.frame_times);
        char text[TORIRS_CHROME_INPUT_MAX];

        /* `mean_us` is 0 both for "no samples yet" and for a run of sub-10us
         * frames, and the readout says "--" for either: there is nothing
         * useful to print. */
        DevOverlay_FrameTimeText(mean_us, text, sizeof(text));
        /* Compare-then-set: an unchanged readout dirties nothing, so a steady
         * client rebuilds no display list and requests no redraw. */
        ToriRSChrome_SetText(&app->dev_overlay->ui, tool->frame_row, text);
    }
}

static void
app_tool_frame_time_free(void* self)
{
    ToriMem_Free(ToriMem_Process(), self);
}
