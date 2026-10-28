/*
 * Canvas size, window mode, the interface scale, the plugin layout pass, and
 * the text-input handoff.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"
#include "perf_audit.h"

/* Most hooks a single canvas change dispatches. The gameframe registers one
 * onResize per open interface root (script 901 does it for the toplevel; panels
 * that lay themselves out register their own), so the real count is single
 * digits — this is a "something is looping" bound, not a budget. */
#define APP_RESIZE_HOOK_MAX 256

/*
 * The Display side panel's client-layout row, as a child index of
 * `[iface:settings_side]`.
 *
 * The CHILD stays in C, the way every app_iface_com caller's does: which
 * component of that panel carries the layout dropdown is a fact about the
 * panel's own layout and travels with it (`display_dynamic_setting_1_buttons`
 * is the 41st section of interfaces/settings_side.if). Which id the INTERFACE
 * has is the revision's, and that half is the profile's.
 */
#define APP_SETTINGS_SIDE_LAYOUT_ROW 40

/* Private to this unit, declared up front so definition order is free. */
static void
app_dispatch_resize_hook_ids(
    struct App* app,
    int const* ids,
    int count);

/* Dispatch a queue selected by the completed trigger=true layout pass. Ids,
 * rather than component-array indices, survive cc_create/cc_delete reallocating
 * the tree while a listener runs. */
static void
app_dispatch_resize_hook_ids(
    struct App* app,
    int const* ids,
    int count)
{
    assert(app);
    assert(ids);
    assert(count >= 0);

    for( int i = 0; i < count; i++ )
    {
        int32_t idx = UITree_FindByComponentId(app->tree, ids[i]);
        if( idx < 0 )
            continue;
        RS_CS2_DispatchHook(
            &app->host,
            &app->runner,
            ids[i],
            &UITree_Hooks(&app->tree->components[idx])->on_resize);
    }
}

/* The canvas, exactly as asked. @see App_SetCanvasSize for the clamped entry. */
static int
app_set_canvas_size_exact(
    struct App* app,
    int width,
    int height)
{
    struct UITreeResizeHookSnapshot resize_before[APP_RESIZE_HOOK_MAX];
    int changed_ids[APP_RESIZE_HOOK_MAX];
    int resize_before_count = 0;
    int changed_count = 0;

    assert(app);
    assert(width > 0);
    assert(height > 0);

    /* All three copies are tested, not just the layout one: they are set
     * together here and nowhere else, so disagreement means somebody wrote one
     * of them directly and this is where that gets repaired. */
    if( width == UITREE_LAYOUT_ROOT_W && height == UITREE_LAYOUT_ROOT_H &&
        app->host.viewport_w == width && app->host.viewport_h == height )
        return 0;

    if( app->tree && app->tree->component_count > 0 )
    {
        /* Keep the cached pre-pass dimensions even when another mutation has
         * already invalidated layout. method3791 snapshots its old computed
         * fields immediately before recomputing; normalising pending changes
         * against the old canvas here would incorrectly erase a real resize. */
        resize_before_count =
            UITree_SnapshotResizeHooks(app->tree, resize_before, APP_RESIZE_HOOK_MAX);
        if( app->tree->resize_hooks.count > APP_RESIZE_HOOK_MAX )
            TORIRS_ERR("resize: more than %d onResize hooks; truncating\n", APP_RESIZE_HOOK_MAX);
    }

    UITree_LayoutSetRootSize(width, height);
    app->host.viewport_w = width;
    app->host.viewport_h = height;

    if( app->tree && app->tree->component_count > 0 )
    {
        /* Resolve BEFORE dispatching: the listeners read their own box back
         * through if_getwidth/if_getheight (toplevel_resize's very first
         * statements), so they have to see the new size, not the old one. */
        UITree_LayoutInvalidate(app->tree);
        UITree_LayoutResolve(app->tree, 0, 0, width, height);
        /* Physical canvas resize is method6192's trigger=true path. Build the
         * whole changed-size queue before its first listener runs: callbacks
         * can mutate the tree, but cannot retroactively change events already
         * queued by the completed layout pass. */
        changed_count = UITree_CollectResizedHookIds(
            app->tree, resize_before, resize_before_count, 1, changed_ids, APP_RESIZE_HOOK_MAX);
        app_dispatch_resize_hook_ids(app, changed_ids, changed_count);
        /* And again after: the listeners are all if_setsize/if_setposition. */
        UITree_LayoutInvalidate(app->tree);
        UITree_LayoutResolve(app->tree, 0, 0, width, height);
    }

    app->need_redraw = 1;
    /* TORIRS_REPORT, not TORIRS_LOG: the shipping lane compiles -DNDEBUG,
     * which strips TORIRS_LOG -- and this is the one line that says whether a
     * resize arrived at all, in the build every report comes from. */
    if( getenv("TORIRS_RESIZE_DEBUG") )
        TORIRS_REPORT("canvas: %dx%d\n", width, height);
    return 1;
}

int
App_SetCanvasSize(
    struct App* app,
    int width,
    int height)
{
    assert(app);

    /*
     * The floor is the FRAME's, and a plugin layout brings its own.
     *
     * APP_CANVAS_MIN_W/H is a fact about a revconfig gameframe -- its children
     * are insets off 765x503 and a smaller canvas gives them zero-sized
     * viewports -- so it is the right floor for exactly as long as that frame
     * is the one on screen. While a plugin arranges the frame it is not, and
     * clamping a phone-shaped layout up to a desktop canvas is how a mobile
     * frame ends up letterboxed inside the size it was written to avoid.
     *
     * And the frame's floor is not the CANVAS's: the lane docks its popout
     * strip inside the canvas, so the canvas has to hold the frame AND the
     * strip. @see App_CanvasFloorWidth.
     */
    {
        int const min_w = App_CanvasFloorWidth(app);
        int const min_h = App_CanvasFloorHeight(app);

        if( width < min_w )
            width = min_w;
        if( height < min_h )
            height = min_h;
    }

    return app_set_canvas_size_exact(app, width, height);
}

/*
 * The two canvas-floor measurements. Both walk the component array and read
 * nothing else, so they live on UITree; these are the App spellings the rest
 * of the file and the plugin layout already call.
 */
int
App_MeasureRightChromeStripWidth(struct App const* app)
{
    assert(app);
    return UITree_MeasureRightChromeStripWidth(app->tree);
}

int
App_FixedCanvasWidth(struct App const* app)
{
    assert(app);
    return APP_CANVAS_MIN_W + App_MeasureRightChromeStripWidth(app);
}

static int
app_fixed_window_percent(struct App const* app)
{
    struct ClientScaleSettings settings;

    assert(app);
    App_ClientScaleSettings(app, &settings);
    return ClientScale_RoundPercent(&settings, RS_CS2Host_UiScalePercent(&app->host));
}

int
App_FixedWindowWidth(struct App const* app)
{
    assert(app);
    return App_FixedCanvasWidth(app) * app_fixed_window_percent(app) / 100;
}

int
App_FixedWindowHeight(struct App const* app)
{
    assert(app);
    return APP_CANVAS_MIN_H * app_fixed_window_percent(app) / 100;
}

int
App_CanvasFloorWidth(struct App const* app)
{
    assert(app);
    /* The strip is CARVED OUT of the canvas -- the resizable toplevels lay
     * their own children out beside it (interface 728's rail is 42 columns of
     * the canvas on both of them), so a canvas of exactly the frame's floor
     * leaves the frame 42 short of it. */
    return APP_CANVAS_MIN_W + App_MeasureRightChromeStripWidth(app);
}

int
App_CanvasFloorHeight(struct App const* app)
{
    assert(app);
    (void)app;
    /* A phone too: Jagex's app sizes its canvas to 503 rows (a 1290-pixel
     * iPhone landscape draws 2.56 device pixels to the canvas pixel), and the
     * mobile toplevel's scripts are written against that height -- below 503
     * toplevel_side_customise (919) pins the side panel to the bottom corner
     * and the main modal has no room beside it. The strip is full-height by
     * definition, so there is nothing to add on this axis. */
    return APP_CANVAS_MIN_H;
}

int
App_SyncFixedChromeInset(struct App* app)
{
    int want_w;

    assert(app);
    if( App_WindowMode(app) != CS2VM_WINDOW_MODE_FIXED )
        return 0;
    want_w = App_FixedCanvasWidth(app);
    if( want_w == UITREE_LAYOUT_ROOT_W && APP_CANVAS_MIN_H == UITREE_LAYOUT_ROOT_H )
        return 0;
    return App_SetCanvasSize(app, want_w, APP_CANVAS_MIN_H);
}

int
App_ResizableWindowFloor(
    struct App const* app,
    int* out_w,
    int* out_h)
{
    struct ClientScaleSettings settings;
    int frame_w;
    int frame_h;
    int percent;

    assert(app);
    assert(out_w);
    assert(out_h);
    if( App_WindowMode(app) != CS2VM_WINDOW_MODE_RESIZABLE )
        return 0;
    /* The frame at the CHOSEN interface scale, in the unit HighDPI counts it
     * in -- drawable pixels, or window points. At 100% it was "the window
     * holds the frame at 1:1", and any larger scale then asked for a buffer
     * smaller than the frame: 200% on a Retina display cut the login screen
     * to its top-left quarter. The shell caps this at the display, and
     * ClientScale_LowerToFloor covers what the display cannot hold. The
     * pixel limit caps it: @see ClientScale_WindowFloorPercent. */
    App_ClientScaleSettings(app, &settings);
    frame_w = App_CanvasFloorWidth(app);
    frame_h = App_CanvasFloorHeight(app);
    percent = ClientScale_WindowFloorPercent(
        &settings, RS_CS2Host_UiScalePercent(&app->host), frame_w, frame_h);
    *out_w = (int)((long long)frame_w * percent / 100);
    *out_h = (int)((long long)frame_h * percent / 100);
    return 1;
}

void
App_ClientScaleSettings(
    struct App const* app,
    struct ClientScaleSettings* out)
{
    int output_filter;

    assert(app);
    assert(out);
    /* The store clamps every one of these on the way in, so each is already a
     * legal enum value here. */
    out->fit = (enum ClientScaleFit)RS_CS2Host_GetOption(
        &app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_CLIENT_FIT);
    /* A phone's canvas IS the screen at the floor's scale (app_mobile_fit_floor),
     * so there is nothing for a stretch mode to decide: integer whole-multiples
     * of a 2.56 scale showed the frame at 2x, 78% of an iPhone with a black
     * band all round, and stretch could only distort a rounding pixel. */
    if( CS2VM2_OnMobile() )
        out->fit = CLIENT_SCALE_FIT_KEEP_ASPECT;
    out->max_pixel_width = RS_CS2Host_GetOption(
        &app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_MAX_PIXEL_WIDTH);
    out->max_pixel_height = RS_CS2Host_GetOption(
        &app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_MAX_PIXEL_HEIGHT);
    output_filter = RS_CS2Host_GetOption(
        &app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_OUTPUT_FILTER);
    out->output_filter = output_filter == RS_CS2_OUTPUT_FILTER_SAME_AS_INTERFACE
                             ? (enum ClientScaleFilter)RS_CS2Host_UiScaleMode(&app->host)
                             : (enum ClientScaleFilter)(output_filter - 1);
    out->high_dpi = App_HighDpiMode(app);
    out->density_percent = App_DisplayDensityPercent(app);
    /* Stretch mode NONE's size. A fixed window is sized in points by its
     * percent (App_FixedWindowWidth), so the frame unstretched is that
     * percent of the display density. A resizable layout is the window
     * divided by its percent, so undoing the division is the window; before
     * the first layout, 100% of the HighDPI unit. */
    if( App_WindowMode(app) == CS2VM_WINDOW_MODE_FIXED )
        out->unstretched_percent =
            (int)((long long)ClientScale_RoundPercent(out, RS_CS2Host_UiScalePercent(&app->host)) *
                  App_DisplayDensityPercent(app) / 100);
    else if( app->client_scale.layout.percent > 0 )
        out->unstretched_percent = app->client_scale.layout.percent;
    else
        out->unstretched_percent = ClientScale_LayoutDensityPercent(out);
}

enum ClientScaleHighDpi
App_HighDpiMode(struct App const* app)
{
    int const chosen =
        RS_CS2Host_GetOption(&app->host, RS_CS2_OPTION_DEVICE, RS_CS2_DEVICEOPTION_HIGH_DPI);

    assert(app);
    if( chosen == RS_CS2_HIGH_DPI_AUTO )
        return app->client_scale.high_dpi_auto;
    return (enum ClientScaleHighDpi)(chosen - RS_CS2_HIGH_DPI_DEVICE_PIXELS);
}

int
App_DisplayDensityPercent(struct App const* app)
{
    assert(app);
    /* 0 until a platform reports one: a headless run has no display, and 1x
     * is what its canvas has always been. */
    return app->client_scale.density_percent > 0 ? app->client_scale.density_percent : 100;
}

void
App_SetDisplayDensity(
    struct App* app,
    int density_percent)
{
    assert(app);
    assert(density_percent > 0);
    if( app->client_scale.density_percent == density_percent )
        return;
    app->client_scale.density_percent = density_percent;
    if( getenv("TORIRS_RESIZE_DEBUG") )
        TORIRS_REPORT("resize: display density %d%%\n", density_percent);
    /* The window moved to a display of another density: a HighDPI mode that
     * reads it now lays out at a different percent. */
    app->host.client_scale_dirty = true;
}

void
App_SetHighDpiAuto(
    struct App* app,
    enum ClientScaleHighDpi mode)
{
    assert(app);
    assert(mode >= 0);
    assert(mode < CLIENT_SCALE_HIGH_DPI_COUNT);
    if( app->client_scale.high_dpi_auto == mode )
        return;
    app->client_scale.high_dpi_auto = mode;
    app->host.client_scale_dirty = true;
}

/*
 * A phone's canvas is the frame's floor, exactly: as many rows as the 503 the
 * mobile toplevel is written against, at whatever scale fills the screen with
 * them -- which is what Jagex's app draws (a 1290-pixel iPhone landscape is
 * 1092x503 at 2.56 device pixels to the canvas pixel). Raised as well as
 * lowered: the cache's own settings ask a phone for 400%, but only after
 * login, and at the title screen's 100% of device pixels the 765x503 title sat
 * in a corner of a 2796x1290 canvas.
 */
static void
app_mobile_fit_floor(
    struct ClientScaleSettings const* settings,
    int floor_w,
    int floor_h,
    int window_w,
    int window_h,
    struct ClientScaleLayout* layout)
{
    int fit;
    int fit_h;

    assert(settings);
    assert(layout);
    assert(floor_w > 0);
    assert(floor_h > 0);
    if( !CS2VM2_OnMobile() )
        return;
    fit = (int)((long long)window_w * 100 / floor_w);
    fit_h = (int)((long long)window_h * 100 / floor_h);
    if( fit_h < fit )
        fit = fit_h;
    if( fit < 1 )
        fit = 1;
    layout->lowered_to_fit = fit < layout->percent;
    layout->percent = fit;
    layout->shown_percent =
        (int)((long long)fit * 100 / ClientScale_LayoutDensityPercent(settings));
    layout->w = (int)((long long)window_w * 100 / fit);
    layout->h = (int)((long long)window_h * 100 / fit);
    if( layout->w < floor_w )
        layout->w = floor_w;
    if( layout->h < floor_h )
        layout->h = floor_h;
}

int
App_ApplyWindowLayout(
    struct App* app,
    int window_w,
    int window_h)
{
    struct ClientScaleSettings settings;

    assert(app);
    assert(window_w > 0);
    assert(window_h > 0);
    app->client_scale.window_w = window_w;
    app->client_scale.window_h = window_h;
    App_ClientScaleSettings(app, &settings);
    ClientScale_WindowLayout(
        &settings,
        RS_CS2Host_UiScalePercent(&app->host),
        window_w,
        window_h,
        &app->client_scale.layout);
    ClientScale_LowerToFloor(
        &settings,
        App_CanvasFloorWidth(app),
        App_CanvasFloorHeight(app),
        window_w,
        window_h,
        &app->client_scale.layout);
    app_mobile_fit_floor(
        &settings,
        App_CanvasFloorWidth(app),
        App_CanvasFloorHeight(app),
        window_w,
        window_h,
        &app->client_scale.layout);
    if( getenv("TORIRS_RESIZE_DEBUG") )
        TORIRS_REPORT(
            "resize: chose %d%% hidpi %d density %d limit %dx%d; window %dx%d -> buffer %dx%d at "
            "%d%% (frame floor %dx%d)\n",
            RS_CS2Host_UiScalePercent(&app->host), (int)settings.high_dpi, settings.density_percent,
            settings.max_pixel_width, settings.max_pixel_height, window_w, window_h,
            app->client_scale.layout.w, app->client_scale.layout.h, app->client_scale.layout.percent,
            App_CanvasFloorWidth(app), App_CanvasFloorHeight(app));
    /* Exact: the floor is already in the layout, lowered evenly rather than
     * clamped per axis. @see platform/client_scale.h rule 5. */
    return app_set_canvas_size_exact(
        app, app->client_scale.layout.w, app->client_scale.layout.h);
}

int
App_SetSafeArea(
    struct App* app,
    struct ToriRS_CmdSafeArea const* area)
{
    struct UITreeResizeHookSnapshot hooks[APP_RESIZE_HOOK_MAX];
    int ids[APP_RESIZE_HOOK_MAX];
    int count = 0;
    int* px;

    assert(app);
    assert(area);
    px = app->host.safe_area_px;
    if( px[0] == area->left && px[1] == area->top && px[2] == area->right &&
        px[3] == area->bottom && app->host.safe_area_window_w == area->width &&
        app->host.safe_area_window_h == area->height )
        return 0;
    px[0] = area->left;
    px[1] = area->top;
    px[2] = area->right;
    px[3] = area->bottom;
    app->host.safe_area_window_w = area->width;
    app->host.safe_area_window_h = area->height;
    if( getenv("TORIRS_RESIZE_DEBUG") )
        TORIRS_REPORT(
            "safe area: %d,%d,%d,%d of %dx%d\n", area->left, area->top, area->right,
            area->bottom, area->width, area->height);

    /* No box changed size, so the trigger=true rule would select nobody; but
     * every listener that read SAFEAREA_GET* laid itself out against the old
     * answer. Run them all once, as a resize runs the ones it resized. */
    if( app->tree && app->tree->component_count > 0 )
    {
        count = UITree_SnapshotResizeHooks(app->tree, hooks, APP_RESIZE_HOOK_MAX);
        for( int i = 0; i < count; i++ )
            ids[i] = hooks[i].component_id;
        app_dispatch_resize_hook_ids(app, ids, count);
        UITree_LayoutInvalidate(app->tree);
        UITree_LayoutResolve(
            app->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    }
    app->need_redraw = 1;
    return 1;
}

int
App_SyncClientScale(struct App* app)
{
    assert(app);
    /* Fixed mode applies the scale to the WINDOW instead
     * (App_FixedWindowWidth/Height), and consumes this flag itself. Taking it
     * here too would let whichever of the two runs first in a frame eat the
     * change before the other one sees it. */
    if( App_WindowMode(app) != CS2VM_WINDOW_MODE_RESIZABLE )
        return 0;
    if( !app->host.client_scale_dirty )
        return 0;
    /* Nothing has told us how big the window is yet — a boot-time restore from
     * preferences lands here before the shell's first resize. Keep the flag:
     * the change is real, it just has nothing to divide yet. */
    if( app->client_scale.window_w <= 0 || app->client_scale.window_h <= 0 )
        return 0;
    app->host.client_scale_dirty = false;
    return App_ApplyWindowLayout(app, app->client_scale.window_w, app->client_scale.window_h);
}

void
App_SetClientScalePresent(
    struct App* app,
    struct ClientScalePresent const* present)
{
    assert(app);
    assert(present);
    app->client_scale.present = *present;
    app->client_scale.present_known = true;
    /* Fixed mode never runs App_ApplyWindowLayout, so its layout is stated
     * here: the pinned canvas, and the percent the window was sized by. */
    if( App_WindowMode(app) == CS2VM_WINDOW_MODE_FIXED )
    {
        struct ClientScaleSettings settings;
        int const chosen = RS_CS2Host_UiScalePercent(&app->host);

        App_ClientScaleSettings(app, &settings);
        app->client_scale.layout.w = UITREE_LAYOUT_ROOT_W;
        app->client_scale.layout.h = UITREE_LAYOUT_ROOT_H;
        app->client_scale.layout.percent = ClientScale_RoundPercent(&settings, chosen);
        app->client_scale.layout.shown_percent = app->client_scale.layout.percent;
        app->client_scale.layout.rounded_by_integer = app->client_scale.layout.percent != chosen;
        app->client_scale.layout.raised_by_limit = 0;
        app->client_scale.layout.lowered_to_fit = 0;
    }
    app->client_scale.shown_percent = app->client_scale.layout.shown_percent;
}

struct AppClientScale const*
App_ClientScale(struct App const* app)
{
    assert(app);
    return &app->client_scale;
}

int
App_WindowMode(struct App const* app)
{
    assert(app);
    return app->host.window_mode;
}

void
App_SetBootWindowMode(
    struct App* app,
    int mode)
{
    assert(app);
    if( mode != CS2VM_WINDOW_MODE_FIXED && mode != CS2VM_WINDOW_MODE_RESIZABLE )
        return;
    app->host.window_mode = mode;
    app->host.default_window_mode = mode;
    /*
     * Record it on the App's OWN config too, because that is where the boot
     * statement is looked for later.
     *
     * `app->cfg` is a COPY taken by App_Init, so a caller that settles the mode
     * after App_Init -- which the shell must, since the CS1 lane's mode is
     * derived from the resolved ui logic -- writes only its own local struct,
     * and the App is left believing nobody stated a mode. The preferences
     * restore (`app->cfg.window_mode` in Task_AppBoot) then hands the saved
     * default the lane instead, and a fixed-only 2004 frame ends up in a
     * resizable window it has no layout for: a 765x503 island in a grey field.
     *
     * Deliberately NOT window_mode_dirty: the shell is the caller and applies
     * the platform side directly. Raising it here would make the boot config
     * indistinguishable from a clientscript's SETWINDOWMODE on the next drain.
     */
    app->cfg.window_mode = mode;
}

void
App_SyncNativeWindowMode(struct App* app)
{
    int want;

    assert(app);
    /*
     * The lane's own frame decides the window mode: the lane's default on CS2,
     * where the toplevel lays itself out to the canvas it is handed.
     *
     * On a CS1 lane the native frame does not HAVE a resizable mode. Its
     * gameframe is a baked 765x503 layout wrapped in one `fixed_shell`
     * rs_layer (revconfig `*_dat1_ui.ini`) that clips every surface under it,
     * and there is no CS2 setwindowmode and no relayout hook on that lane to
     * make it anything else -- so a bigger canvas leaves the 2004 frame
     * anchored in the top-left corner and the rest of the window flat grey.
     * So the answer on CS1 is FIXED whatever the default says, and the default
     * -- the command line, the manifest, or the saved preference, whose
     * out-of-the-box value is resizable -- only gets a say on CS2.
     */
    want = App_UiLogic(app) == APP_UI_LOGIC_CS1 ? CS2VM_WINDOW_MODE_FIXED
                                                : app->host.default_window_mode;
    if( app->host.window_mode == want )
        return;
    app->host.window_mode = want;
    app->host.window_mode_dirty = true;
}

void
App_PluginLayoutTick(struct App* app)
{
    assert(app);

    PA_INC(layout_tick_calls);
    if( !app->plugins )
        return;
    /* Once, when the host has started: the lane's window mode is restated a
     * single time and from then on belongs to CS2 and the player. */
    if( !app->native_window_mode_synced && PluginHost_Started(app->plugins) )
    {
        App_SyncNativeWindowMode(app);
        app->native_window_mode_synced = 1;
    }
    /* And the way into the plugins, which is the PLATFORM's to draw: no plugin
     * and no CS2 interface carries a plugin button. Asked once, when the host
     * has started and there is something to show; a touch client gets the
     * edge tab, a desk the sidebar. */
    if( !app->plugin_chrome_requested && app->platform_bus && PluginHost_Started(app->plugins) )
    {
        (void)CmdBus_PushPluginChrome(
            app->platform_bus,
            app->touch_ui ? TORIRS_PLUGIN_CHROME_EDGE_TAB : TORIRS_PLUGIN_CHROME_SIDEBAR);
        app->plugin_chrome_requested = 1;
    }
    /*
     * Name the cache gameframe's regions before plugins query them. The emit
     * fence's rebind reads these stamps, and the tree keeps the binder so the
     * fence can re-run it after a rebuild.
     */
    if( app->app_state == APP_STATE_READY && app->tree && app->tree->root_index >= 0 )
    {
        UITree_FrameSetBinder(app->tree, app_plugin_frame_bind, app);
        UITree_FrameBind(app->tree);
    }
    bool const widgets_ready =
        app->app_state == APP_STATE_READY && app->tree && app->tree->root_index >= 0;
    PluginHost_WidgetsChanged(
        app->plugins,
        widgets_ready ? app->tree->instance_id : 0,
        widgets_ready ? app->tree->generation : 0);
    /* Unconditional, and that is the point: a hide, a move, a re-skin or a
     * retype bumps no tree generation, so the bindings pass above cannot see
     * them. Every bound watch is stamped here and told when its widget moved
     * underneath it. */
    PluginHost_WidgetStates(app->plugins);
}

/**
 * Does the client have somewhere for typed characters to go right now?
 *
 * Three sources, and they are the three places this client accepts text: the
 * login form's two fields, the chat line, and a plugin that asked for input.
 *
 * This exists for the soft keyboard. On a desktop the answer is not needed --
 * a physical keyboard is always there, and SDL's text-input mode only governs
 * whether TEXTINPUT events arrive, which the shell turns on once at boot and
 * never turns off. On a touch device it is the whole question: there is no
 * keyboard unless one is raised, and raising it at the wrong time covers half
 * the screen with something the user cannot type into.
 */
int
app_wants_text_input(struct App const* app)
{
    assert(app);

    /* The login form, and only while it is the screen being shown: `focus`
     * keeps its last value across a screen change, so testing it alone would
     * raise the keyboard over the main menu. */
    if( (app->screen == APP_SCREEN_TITLE || app->screen == APP_SCREEN_CONNECTING) &&
        app->title.screen == RS_TITLE_LOGIN_FORM && app->title.focus >= 0 &&
        app->title.focus < RS_TITLE_FIELD_COUNT )
        return 1;

    if( app->chat_input_active )
        return 1;

    /* The mobile scripts' own request. @see App::vm_keyboard_open. */
    if( app->vm_keyboard_open )
        return 1;

    /* A plugin asked for it (torirs_plugin_bridge.u.c). Kept last so the
     * client's own fields win when both are true. */
    return app->text_input_on ? 1 : 0;
}

int
App_TakeOpenUrl(struct App* app, char* out, int cap)
{
    assert(app);
    assert(out);
    assert(cap > 0);
    if( !app->host.client.pending_open_url[0] )
        return 0;
    snprintf(out, (size_t)cap, "%s", app->host.client.pending_open_url);
    app->host.client.pending_open_url[0] = '\0';
    return 1;
}

int
App_TakeTextInputChange(
    struct App* app,
    int* out_on)
{
    int wanted;

    assert(app);

    /*
     * Poll rather than wait for a writer, because the two client-side sources
     * are plain state that many code paths change -- clicking a field, pressing
     * Escape, submitting the form, a script closing the chat. Making each of
     * those remember to set a dirty flag would be a rule to keep, and the one
     * that forgot would leave the keyboard up over a screen with no field.
     */
    wanted = app_wants_text_input(app);
    if( wanted != app->text_input_effective )
    {
        app->text_input_effective = wanted;
        app->text_input_dirty = 1;
    }

    if( !app->text_input_dirty )
        return 0;
    app->text_input_dirty = 0;
    if( out_on )
        *out_on = app->text_input_effective;
    return 1;
}

int
App_TakeWindowModeChange(
    struct App* app,
    int* out_mode)
{
    assert(app);
    if( !app->host.window_mode_dirty )
        return 0;
    app->host.window_mode_dirty = false;
    if( out_mode )
        *out_mode = app->host.window_mode;
    return 1;
}

/**
 * Drain a Display-panel layout choice (0/1/2) raised by settings_client_mode.
 * Same split as App_TakeWindowModeChange: the App owns the flag; the shell
 * sends WINDOW_STATUS.
 */
int
App_TakeClientLayoutChange(
    struct App* app,
    int* out_mode)
{
    assert(app);
    if( !app->host.client_layout_dirty )
        return 0;
    app->host.client_layout_dirty = false;
    if( out_mode )
        *out_mode = app->host.client_layout_mode;
    return 1;
}

int
App_FrameCapFps(struct App const* app)
{
    struct RevConfigFrameItem const* frame;

    assert(app);
    frame = &app->revconfig_profile.frame;
    if( frame->cap_source == REVCONFIG_FRAME_CAP_CS2 )
    {
        /* The cache's Limit Framerate row, when the player has picked one;
         * the profile's own number until then. */
        int const chosen = RS_CS2Host_FrameRateCapFps(&app->host);
        if( chosen > 0 )
            return chosen;
    }
    return frame->cap_fps > 0 ? frame->cap_fps : 0;
}

int
App_FrameSettled(struct App const* app)
{
    assert(app);
    return !app->runner_had_work && !app->runner.frame_settle_pending &&
           !app->exec_runner_had_work && !app->server_tick_open &&
           RS_ClientScriptQueue_Count(&app->pending_clientscripts) == 0 && app->host.triggeroplocal_count == 0 &&
           !app->host.close_modal_requested && app->host.resume_pausebutton_component_id == -1 &&
           app->host.social_send_count == 0;
}
