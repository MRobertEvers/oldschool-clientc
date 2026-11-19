/*
 * The painter-command stepper: a side-app (sideapp/side_app.h).
 *
 * The v0 client's debug (docs/ORANGE_WEDGE.md): `paint_toggle` flips the cap
 * on the painter stream (unlimited <-> 0), `paint_more` / `paint_less` step
 * it +-1 and `paint_more_100` / `paint_less_100` +-100. The raster then draws
 * exactly the first N painter commands, which is how a draw-order artefact is
 * walked to the command that paints it. The cap itself is the renderer's
 * (ToriRS_Frame_PaintLimitGet / _Set, render/torirs_frame.c); this tool only
 * steers it, and owns the one value it keeps: the cap it last logged.
 *
 * The steppers repeat while HELD, like the W/A/S/D camera keys and unlike the
 * one-shot debug keys: a scene is ~1700 commands, so finding the one that
 * paints a pixel by tapping J is not a thing anyone will do. Only the toggle
 * is edge-triggered -- held, it would flip every frame.
 *
 * Delivered at the camera-keys point of the frame (app_world_camera_keys,
 * once per view the keys steer), under that point's gates: the world loaded
 * and its viewport live, the catalog's model view not focused, and no text
 * input focused unless the map editor owns the camera; the keys with CTRL,
 * not one the map editor claimed this frame. Last at that point
 * (app_tools.c's order): the keys of one delivery build one new cap, and the
 * keys_done hook applies it. Before those gates the probe hook explains,
 * under TORIRS_KEY_DEBUG, a paint key that went down.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "mem/torirs_mem.h"
#include "app/app_internal.h"

struct AppToolPaintStepper
{
    /* One log line per gesture, not per frame: while a stepper is held the
     * value changes every frame, and a hundred lines a second buries the
     * number you are trying to read. Printed when the keys settle. Seeded
     * with the starting cap on the first delivery, so an untouched client
     * says nothing; INT_MIN until then. */
    int logged;
    /* The delivery in progress: open from this delivery's first paint key
     * (or its keys_done) to its keys_done. */
    int open;
    int limit;
    int next;
    int stepping;
    int toggled;
};

/* Private to this unit, declared up front so definition order is free. */
static void
app_tool_paint_stepper_begin(struct AppToolPaintStepper* tool);
static void
app_tool_paint_stepper_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args);
static void
app_tool_paint_stepper_keys_done(
    void* self,
    struct SideAppHost* host,
    enum SideAppPhase phase);
static void
app_tool_paint_stepper_keys_probe(
    void* self,
    struct SideAppHost* host,
    enum SideAppPhase phase,
    int any_down);
static void
app_tool_paint_stepper_free(void* self);

enum
{
    APP_TOOL_PAINT_TOGGLE,
    APP_TOOL_PAINT_MORE,
    APP_TOOL_PAINT_LESS,
    APP_TOOL_PAINT_MORE_100,
    APP_TOOL_PAINT_LESS_100
};

static struct SideAppAction const k_app_tool_paint_stepper_actions[] = {
    [APP_TOOL_PAINT_TOGGLE] = { "paint_toggle", SIDE_APP_PHASE_CAMERA_KEYS, SIDE_APP_GATE_CTRL,
                                SIDE_APP_KEY_PRESS, 0 },
    [APP_TOOL_PAINT_MORE] = { "paint_more", SIDE_APP_PHASE_CAMERA_KEYS, SIDE_APP_GATE_CTRL,
                              SIDE_APP_KEY_HELD, 0 },
    [APP_TOOL_PAINT_LESS] = { "paint_less", SIDE_APP_PHASE_CAMERA_KEYS, SIDE_APP_GATE_CTRL,
                              SIDE_APP_KEY_HELD, 0 },
    [APP_TOOL_PAINT_MORE_100] = { "paint_more_100", SIDE_APP_PHASE_CAMERA_KEYS, SIDE_APP_GATE_CTRL,
                                  SIDE_APP_KEY_HELD, 0 },
    [APP_TOOL_PAINT_LESS_100] = { "paint_less_100", SIDE_APP_PHASE_CAMERA_KEYS, SIDE_APP_GATE_CTRL,
                                  SIDE_APP_KEY_HELD, 0 },
};

static struct SideAppHooks const k_app_tool_paint_stepper_hooks = {
    .key = app_tool_paint_stepper_key,
    .free = app_tool_paint_stepper_free,
    .keys_done = app_tool_paint_stepper_keys_done,
    .keys_probe = app_tool_paint_stepper_keys_probe,
};

void
app_tool_paint_stepper_register(struct App* app)
{
    struct AppToolPaintStepper* tool;

    assert(app);
    assert(app->side_apps);
    tool = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*tool));
    tool->logged = INT_MIN;
    SideApps_Register(
        app->side_apps,
        "paint_stepper",
        tool,
        &k_app_tool_paint_stepper_hooks,
        k_app_tool_paint_stepper_actions,
        (int)(sizeof(k_app_tool_paint_stepper_actions) / sizeof(k_app_tool_paint_stepper_actions[0])));
}

/* Open the delivery: read the cap the keys step from. */
static void
app_tool_paint_stepper_begin(struct AppToolPaintStepper* tool)
{
    if( tool->open )
        return;
    tool->open = 1;
    tool->limit = ToriRS_Frame_PaintLimitGet();
    tool->next = tool->limit;
    tool->stepping = 0;
    tool->toggled = 0;
    if( tool->logged == INT_MIN )
        tool->logged = tool->limit;
}

/* A paint key fired: the toggle on its press, a stepper while it is held.
 * Each builds on what the keys before it in this delivery made. */
static void
app_tool_paint_stepper_key(
    void* self,
    struct SideAppHost* host,
    int action,
    char const* args)
{
    struct AppToolPaintStepper* tool = self;

    (void)host;
    (void)args;
    assert(tool);
    assert(host);
    assert(args);
    app_tool_paint_stepper_begin(tool);
    if( action == APP_TOOL_PAINT_TOGGLE )
    {
        tool->next = tool->limit < 0 ? 0 : -1;
        tool->toggled = 1;
        return;
    }
    if( action == APP_TOOL_PAINT_MORE )
        tool->next = (tool->next < 0 ? 0 : tool->next) + 1;
    else if( action == APP_TOOL_PAINT_LESS )
        tool->next = (tool->next < 0 ? 0 : tool->next) - 1;
    else if( action == APP_TOOL_PAINT_MORE_100 )
        tool->next = (tool->next < 0 ? 0 : tool->next) + 100;
    else
    {
        assert(action == APP_TOOL_PAINT_LESS_100);
        tool->next = (tool->next < 0 ? 0 : tool->next) - 100;
    }
    tool->stepping = 1;
}

/* Every delivery, a key or none: apply the new cap, and log it once the keys
 * settle. */
static void
app_tool_paint_stepper_keys_done(
    void* self,
    struct SideAppHost* host,
    enum SideAppPhase phase)
{
    struct AppToolPaintStepper* tool = self;
    struct App* app = app_from_side_app_host(host);
    int next;

    (void)phase;
    assert(tool);
    assert(phase == SIDE_APP_PHASE_CAMERA_KEYS);
    app_tool_paint_stepper_begin(tool);
    tool->open = 0;
    next = tool->next;
    if( next != tool->limit )
    {
        if( next < -1 )
            next = -1;
        ToriRS_Frame_PaintLimitSet(next);
        RedrawRequest_Raise(&app->redraw_request);
    }
    if( (tool->toggled || !tool->stepping) && ToriRS_Frame_PaintLimitGet() != tool->logged )
    {
        tool->logged = ToriRS_Frame_PaintLimitGet();
        TORIRS_LOG("paintlimit: %d\n", tool->logged);
    }
}

/* TORIRS_KEY_DEBUG: why a world/debug key did nothing. Every gate of the
 * camera keys silently swallows the whole key set, and "the hotkey is broken"
 * and "the hotkey never ran" look identical from outside. Keyed off the raw
 * key state rather than key_event_count — that counter only fills when a
 * component carries an onKey hook, so it is 0 for exactly the debug keys this
 * is meant to explain. */
static void
app_tool_paint_stepper_keys_probe(
    void* self,
    struct SideAppHost* host,
    enum SideAppPhase phase,
    int any_down)
{
    struct App* app = app_from_side_app_host(host);

    (void)self;
    (void)phase;
    assert(self);
    assert(phase == SIDE_APP_PHASE_CAMERA_KEYS);
    if( torirs_env_key_debug() && any_down )
        TORIRS_LOG(
            "camera_keys: paint-cap key seen; world_active=%d view_valid=%d "
            "chat=%d/%d/%d iface_input=%d\n",
            WorldLoadState_Active(&app->world_load),
            app->world_views.emit_valid,
            UIChatLine_InputActive(&app->chat_line),
            app->chat.social_input_open,
            app->chat.dialog_input_open,
            app_iface_text_input_focused(app));
}

static void
app_tool_paint_stepper_free(void* self)
{
    ToriMem_Free(ToriMem_Process(), self);
}
