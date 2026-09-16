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

int
App_SetCanvasSize(
    struct App* app,
    int width,
    int height)
{
    struct UITreeResizeHookSnapshot resize_before[APP_RESIZE_HOOK_MAX];
    int changed_ids[APP_RESIZE_HOOK_MAX];
    int resize_before_count = 0;
    int changed_count = 0;

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
        int min_w = App_CanvasFloorWidth(app);
        int min_h = APP_CANVAS_MIN_H;
        int plugin_min_w = 0;

        /* Height only: the width floor already carries the frame's own and the
         * strip's, and the strip is full-height by definition, so there is
         * nothing to add on this axis. */
        App_PluginLayoutMinSize(app, &plugin_min_w, &min_h);
        if( width < min_w )
            width = min_w;
        if( height < min_h )
            height = min_h;
    }

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
App_MeasureLaneFrameCoreWidth(struct App const* app)
{
    assert(app);
    return UITree_MeasureLaneFrameCoreWidth(app->tree);
}

int
App_FixedCanvasWidth(struct App const* app)
{
    assert(app);
    return APP_CANVAS_MIN_W + App_MeasureRightChromeStripWidth(app);
}

int
App_CanvasFloorWidth(struct App const* app)
{
    int frame_min_w = APP_CANVAS_MIN_W;
    int frame_min_h = APP_CANVAS_MIN_H;

    assert(app);
    App_PluginLayoutMinSize(app, &frame_min_w, &frame_min_h);
    /* And never below the LANE's own frame, which a plugin layout arranges
     * over rather than replaces: its widgets are still laid out by the lane's
     * toplevel, and a toplevel handed less than it can use spills them out of
     * the area the plugin frame was given. @see App_MeasureLaneFrameCoreWidth. */
    {
        int const lane_core_w = App_MeasureLaneFrameCoreWidth(app);
        if( frame_min_w < lane_core_w )
            frame_min_w = lane_core_w;
    }
    /* The strip is CARVED OUT of the canvas -- the resizable toplevels lay
     * their own children out beside it (interface 728's rail is 42 columns of
     * the canvas on both of them, and a plugin layout gets the same canvas
     * less the same rail as its FRAME_BUILD area), so a canvas of exactly the
     * frame's floor leaves the frame 42 short of it. */
    return frame_min_w + App_MeasureRightChromeStripWidth(app);
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
App_SyncResizableCanvasFloor(struct App* app)
{
    int want_w;

    assert(app);
    if( App_WindowMode(app) != CS2VM_WINDOW_MODE_RESIZABLE )
        return 0;
    want_w = App_CanvasFloorWidth(app);
    /* Raise only. The canvas a resizable window follows is the window's, and
     * the window is the authority whenever it is big enough; shrinking back
     * belongs to the next TORIRS_CMD_WINDOW_RESIZE, which carries the size the
     * window actually is and is clamped by this same floor on the way in. */
    if( UITREE_LAYOUT_ROOT_W >= want_w )
        return 0;
    return App_SetCanvasSize(app, want_w, UITREE_LAYOUT_ROOT_H);
}

/* One window axis through the interface scale. Rounds down, so 100% is exact
 * and every other scale errs towards a slightly larger element rather than a
 * canvas that overruns the window it is stretched into. */
int
app_ui_scaled_axis(
    struct App const* app,
    int window_px)
{
    int const percent = RS_CS2Host_UiScalePercent(&app->host);

    assert(app);
    assert(percent >= RS_CS2_UI_SCALE_MIN);
    if( window_px <= 0 )
        return window_px;
    return window_px * 100 / percent;
}

int
App_SyncUiScale(struct App* app)
{
    assert(app);
    if( !app->host.ui_scale_dirty )
        return 0;
    /* Nothing has told us how big the window is yet — a boot-time restore from
     * preferences lands here before the shell's first resize. Keep the flag:
     * the scale is real, it just has nothing to divide yet. */
    if( app->window_w <= 0 || app->window_h <= 0 )
        return 0;
    app->host.ui_scale_dirty = false;
    return App_SetCanvasSize(
        app, app_ui_scaled_axis(app, app->window_w), app_ui_scaled_axis(app, app->window_h));
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
App_SyncPluginLayoutCanvas(struct App* app)
{
    int want;

    assert(app);
    /*
     * A RELEASE does not force resizable back on.
     *
     * The lane had a window mode before any plugin frame was committed -- a dat1
     * world is fixed and says so in its manifest -- and a released layout
     * should hand that back rather than leave the client in whatever mode the
     * plugin wanted. A committed frame states its mode; native restates the
     * lane's default.
     *
     * Except that on a CS1 lane the native frame does not HAVE a resizable
     * mode to restate. Its gameframe is a baked 765x503 layout wrapped in one
     * `fixed_shell` rs_layer (revconfig `*_dat1_ui.ini`) that clips every
     * surface under it, and there is no CS2 setwindowmode and no relayout hook
     * on that lane to make it anything else -- so a bigger canvas leaves the
     * 2004 frame anchored in the top-left corner and the rest of the window
     * flat grey. That is not the lane's default being honoured, it is a mode
     * the frame on screen cannot be in.
     *
     * So the native answer on CS1 is FIXED whatever the default says, and the
     * default -- the command line, the manifest, or the saved preference,
     * whose out-of-the-box value is resizable -- only gets a say on CS2, where
     * the toplevel really does lay itself out to the canvas it is handed.
     *
     * The escape hatch is a PLUGIN frame that declares
     * TORIRS_FRAME_CANVAS_WINDOW (gameframe-layout's Modern Resizable, the
     * Stone Drawer): it takes the other branch, so selecting one still makes
     * this lane resizable, and releasing it comes back here and pins fixed
     * again.
     */
    want = !app->plugin_frame_active
               ? (App_UiLogic(app) == APP_UI_LOGIC_CS1 ? CS2VM_WINDOW_MODE_FIXED
                                                       : app->host.default_window_mode)
           : app->plugin_layout_canvas == TORIRS_FRAME_CANVAS_FIXED ? CS2VM_WINDOW_MODE_FIXED
                                                                    : CS2VM_WINDOW_MODE_RESIZABLE;
    if( app->host.window_mode == want )
        return;
    /*
     * Re-asserted every frame the committed plugin frame stands, not only when it changes.
     *
     * The window mode has other writers: a clientscript's setwindowmode, and
     * on login the saved Display-panel layout being applied. Either of those
     * lands AFTER frame selection, and a frame that only spoke once loses to them
     * silently -- the plugin goes on laying its frame out at the canvas it
     * asked for while the client uses the window's, which puts a 765x503 frame
     * in the corner of a 1440x900 canvas with the lane's own chrome spread
     * around it. Two gameframes at once, and neither wrong from where it is
     * standing.
     *
     * Whoever holds the frame decides how big the canvas is. Restating it is
     * how that stays true for longer than one frame.
     */
    app->host.window_mode = want;
    app->host.window_mode_dirty = true;
}

int
App_PluginLayoutFixedSize(
    struct App const* app,
    int* out_w,
    int* out_h)
{
    assert(app);
    if( !app->plugin_frame_active )
        return 0;
    if( app->plugin_layout_canvas != TORIRS_FRAME_CANVAS_FIXED )
        return 0;
    if( app->plugin_layout_fixed_w <= 0 || app->plugin_layout_fixed_h <= 0 )
        return 0;
    if( out_w )
        *out_w = app->plugin_layout_fixed_w;
    if( out_h )
        *out_h = app->plugin_layout_fixed_h;
    return 1;
}

int
App_PluginLayoutMinSize(
    struct App const* app,
    int* out_w,
    int* out_h)
{
    assert(app);
    if( !app->plugin_frame_active )
        return 0;
    if( app->plugin_layout_canvas != TORIRS_FRAME_CANVAS_WINDOW )
        return 0;
    /* A claim that named no minimum gets the client's, rather than a floor of
     * zero: "I did not say" and "any size at all" are different statements, and
     * only one of them should be able to produce a 1x1 canvas. */
    if( app->plugin_layout_fixed_w <= 0 || app->plugin_layout_fixed_h <= 0 )
        return 0;
    if( out_w )
        *out_w = app->plugin_layout_fixed_w;
    if( out_h )
        *out_h = app->plugin_layout_fixed_h;
    return 1;
}

void
App_PluginLayoutTick(struct App* app)
{
    int frame_candidate;

    assert(app);

    PA_INC(layout_tick_calls);
    if( !app->plugins )
        return;
    /*
     * Name the cache gameframe's regions before providers query them. Frame
     * provision and the emit-fence rebind read these stamps. The
     * tree keeps the binder so the fence can re-run it after a rebuild.
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
    frame_candidate = PluginHost_FrameNeedsLayout(app->plugins) ? 1 : 0;
    if( !app->plugin_frame_active )
    {
        /* A plugin frame that ended while the tree was up: give the chrome back once,
         * then stop paying for the check. UITree_FrameRelease is idempotent,
         * and `plugin_layout_dirty` is what makes this happen exactly once. */
        if( app->plugin_layout_dirty && app->tree )
        {
            UITree_FrameRelease(app->tree);
            UITree_EnsureLayout(app->tree);
            app->plugin_layout_dirty = 0;
        }
        /* A candidate build is independent of committed ownership. Native is
         * deliberately still live here; fall through to the safe layout fence
         * only for the one attempt the host requested. */
        frame_candidate = PluginHost_FrameNeedsLayout(app->plugins) ? 1 : 0;
        if( frame_candidate )
            goto candidate_layout;
        /* app_plugin_frame_activate already restored the lane's default on the
         * selection transition. Do not restate it on every native-frame tick:
         * ordinary CS2/user window-mode changes own this state again now. */
        return;
    }
candidate_layout:
    if( !app->tree || app->tree->root_index < 0 )
        return;
    /*
     * Nothing is declared against a frame that is still being built.
     *
     * The gameframe bakes across many frames -- the root interface, its packs,
     * the scripts that rearrange them -- and a declaration made partway
     * through finds none of the roles and none of the chrome, then stands for
     * ever because it believes it succeeded. What that looks like is a client
     * drawing the plugin's stones UNDER its own untouched gameframe, which is
     * the one outcome worse than either frame alone.
     *
     * Marked dirty rather than merely skipped, so the first READY frame
     * declares even if nothing else changed.
     */
    if( app->app_state != APP_STATE_READY )
    {
        app->plugin_layout_dirty = 1;
        return;
    }

    /*
     * And nothing is declared against a tree that is not a gameframe at all.
     *
     * A committed plugin frame belongs to the game screen, not the title tree.
     * Its provider may remain running across logout, but the frame must not be
     * taken (UITree_FrameProvide) while the title tree bakes.
     *
     * A provider correctly declines to build outside the game, but taking the
     * frame here would still be destructive: the chrome collection hides every
     * root-group decoration it finds, and against the login screen that reads
     * as the login screen falling apart -- the plate and most of the background
     * gone, two strips of brazier left standing.
     *
     * Marked dirty rather than merely skipped, so the provider is asked again
     * on the first READY frame of the next session instead of inheriting
     * whatever the last one left behind.
     */
    if( app->screen != APP_SCREEN_GAME )
    {
        app->plugin_layout_dirty = 1;
        return;
    }

    /* Restate the selected offer's canvas policy. It has other
     * writers, and the last one to speak wins. */
    if( app->plugin_frame_active )
        App_SyncPluginLayoutCanvas(app);

    /*
     * Re-declare only when the last answer stopped being true.
     *
     * The cases are the canvas resizing, the gameframe being rebuilt (which
     * bumps the tree generation and drops the whole table with it), the boot
     * finishing, and the committed selection itself. Re-declaring every frame would work and
     * would cost a whole-tree walk per frame to collect the chrome again -- on
     * a rev-239 toplevel that is thousands of nodes for an answer that has not
     * changed since the window was last dragged.
     */
    /*
     * A generation move is only a reason when it moved a ROLE. The fence's
     * reassert already re-collects the chrome on every generation change;
     * what a fresh frame build adds is the provider re-placing its surfaces and the
     * answers it reads back ("does this frame have tab 7"), which change only
     * when a role's node did. On an OldSchool lane a cache timer recreates
     * its overlay nodes every logic tick, so without this gate the whole
     * layout was re-declared at frame rate. @see UITree_FrameSlotsStale.
     */
    if( !app->plugin_layout_dirty && UITree_FrameActive(app->tree) &&
        app->plugin_layout_generation != app->tree->generation &&
        app->plugin_layout_w == UITREE_LAYOUT_ROOT_W &&
        app->plugin_layout_h == UITREE_LAYOUT_ROOT_H && !UITree_FrameSlotsStale(app->tree) )
        app->plugin_layout_generation = app->tree->generation;

    if( frame_candidate || app->plugin_layout_dirty || !UITree_FrameActive(app->tree) ||
        app->plugin_layout_generation != app->tree->generation ||
        app->plugin_layout_w != UITREE_LAYOUT_ROOT_W ||
        app->plugin_layout_h != UITREE_LAYOUT_ROOT_H )
    {
        { uint64_t const pa_h0 = PerfAudit_Now();
        PluginHost_Layout(app->plugins, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        PA_ADD(plugin_host_layout_ns, PerfAudit_Now() - pa_h0); }
        /* A provider may change selection from inside on_gameframe. The host
         * correctly abandons that transaction (its selection epoch moved),
         * which leaves dirty set and the effective frame released. Give the
         * replacement selection one bounded, sequential attempt now so native
         * chrome cannot leak into interaction between this tick and the final
         * publication tick. Resolve first because the replacement handler may
         * read widgets while laying out. A provider that keeps changing
         * selection cannot spin us: it gets at most this one retry. */
        if( app->plugin_frame_active && app->plugin_layout_dirty )
        {
            UITree_EnsureLayout(app->tree);
            PluginHost_Layout(app->plugins, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        }
        /* FrameProvide installs the chrome suppression and role binding and
         * invalidates the resolved boxes. Publish that before interaction
         * consumes them; a provider may also change frame selection from
         * inside its callback, and frame_activate drops the old effective
         * frame immediately. */
        UITree_EnsureLayout(app->tree);
    }
    /* UITree_EmitWalk keeps a final generation fence as well. It no longer
     * rewrites CS2 geometry: it only rebinds the standing semantic declaration
     * if topology somehow changed after this settled pass. */
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

/*
 * Which of the lane's four top-level roots is on screen.
 *
 * Off the LIVE root and not off `client_layout_mode`, because the two answer
 * different questions and only this one answers the asker's. The mode is what
 * the Display panel last chose; the root is what the server actually mounted,
 * and between a choice and the remount that follows it -- three server ticks,
 * and every one of them a frame somebody draws -- they disagree. A gameframe
 * arranges over the root, so it must be told about the root.
 *
 * The four ids are the profile's ([iface:toplevel_fixed] and its siblings): a
 * revision that declares none of them, or a lane whose frame is revconfig
 * builtins, has no such thing and says so with -1.
 */
int
app_native_layout_mode(struct App const* app)
{
    static char const* const roots[] = {
        "toplevel_fixed",
        "toplevel_resizable_classic",
        "toplevel_resizable_modern",
        "toplevel_mobile",
    };
    int root;

    assert(app);
    if( App_UiLogic(app) != APP_UI_LOGIC_CS2 )
        return -1;
    root = app->host.top_interface_id;
    if( root <= 0 )
        return -1;
    for( int layout = 0; layout < (int)(sizeof(roots) / sizeof(roots[0])); layout++ )
    {
        if( RevConfigRefs_Get(&app->revconfig_refs, "iface", roots[layout]) == root )
            return layout;
    }
    return -1;
}

/*
 * Ask the lane to put the client in one of its native top-level chromes.
 *
 * ## Why this is a packet and not a field
 *
 * The client does not own which toplevel is up. The SERVER opens it
 * (`if_opentop` out of `~gameframe_apply_mode`), and everything mounted into
 * it -- the panels, the arming for every one of their ops, the overlay
 * containers -- is mounted by the server against that root. A client that
 * opened a different toplevel by itself would be looking at a frame its own
 * server did not know about, which is a worse frame than the one being
 * escaped.
 *
 * So the request is the one a player makes: the Display panel's layout row,
 * `settings_side:display_dynamic_setting_1_buttons`, whose sub-id is the
 * choice plus one. `[proc,settings_side_login]` arms subs 1..3 of it with op 1
 * at login and leaves it armed for the session, which is what makes this a
 * legitimate press rather than an invented one -- the server asked to be told
 * about exactly this, and this client checks that it did (the arming table
 * below) before saying anything.
 *
 * And it has to be that row, not WINDOW_STATUS. Revision 239's WINDOW_STATUS
 * carries the window CLASS -- 1 fixed, 2 resizable -- so Classic and Modern
 * are the same packet, and a client that only sent it could never move between
 * them. The row's op is the golden client's own answer to that, and the server
 * content says so in as many words (settings_side.rs2, the note above the
 * if_setevents).
 *
 * @param layout enum AppNativeLayout, and one of the three the Display row
 *               offers -- MOBILE is not selectable (see the enum).
 * @return true when the lane has been asked, INCLUDING the case where it is
 *         already wearing that chrome; false when this lane cannot be asked,
 *         with the reason logged once.
 */
bool
app_native_layout_select(
    struct App* app,
    int layout)
{
    int row;
    int sub;
    int live;

    assert(app);
    assert(layout >= APP_NATIVE_LAYOUT_FIXED);
    assert(layout <= APP_NATIVE_LAYOUT_RESIZABLE_MODERN);

    if( App_UiLogic(app) != APP_UI_LOGIC_CS2 )
        return false;
    /* No session, nothing to ask: the toplevel is the server's to open, and an
     * offline lane has no server. */
    if( !app->net || app->net->state != TORIRS_NET_GAME )
        return false;

    live = app_native_layout_mode(app);
    if( live == layout )
    {
        /* Arrived. Drop the outstanding request rather than leaving it to
         * suppress the NEXT one, which may be a move straight back. */
        app->host.client_layout_wanted = -1;
        return true;
    }
    /*
     * The phone's root is not a Display choice.
     *
     * 601 is opened because the login's client type said phone, and the row
     * offers no way to ask for it or to leave it. Pressing the row here would
     * take a phone off the only chrome its cache lays out for a phone.
     */
    if( live == APP_NATIVE_LAYOUT_MOBILE )
    {
        static int said;
        if( !said++ )
            TORIRS_LOG("native_layout: the mobile toplevel is not one of the Display "
                       "layouts; the lane stays on it\n");
        return false;
    }

    row = app_iface_com(app, "settings_side", APP_SETTINGS_SIDE_LAYOUT_ROW);
    if( row < 0 )
    {
        static int said;
        if( !said++ )
            TORIRS_LOG("native_layout: no [iface:settings_side] in this profile; "
                       "nothing can ask this lane for a top-level chrome\n");
        return false;
    }
    sub = layout + 1;
    /* The server's own arming, and the whole licence for the packet below. A
     * lane whose content never armed the row is one where this press would be
     * a message nobody asked for. */
    if( (UIIfEventTable_At(&app->if_events, row, sub) & (1u << 1)) == 0 )
    {
        static int said;
        if( !said++ )
            TORIRS_LOG("native_layout: this server has not armed the Display layout "
                       "row (com=%d sub=%d); no chrome can be asked for\n", row, sub);
        return false;
    }

    /* One packet per request, not one per frame: the asker is a frame
     * provider's layout pass. @see APP_NATIVE_LAYOUT_RETRY_CYCLES. */
    if( app->host.client_layout_wanted == layout &&
        app->logic_cycle - app->host.client_layout_wanted_cycle <
            APP_NATIVE_LAYOUT_RETRY_CYCLES )
        return true;
    app->host.client_layout_wanted = layout;
    app->host.client_layout_wanted_cycle = app->logic_cycle;
    APP_NET_SEND(
        app,
        net_out_if_button_op(
            app->net->rev, app->net->random_out, _nsbuf, sizeof(_nsbuf), 1, row, sub));
    TORIRS_LOG("native_layout: asked for %d (Display row com=%d sub=%d)\n", layout, row, sub);
    return true;
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
