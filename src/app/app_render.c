/*
 * Frame assembly and presentation: build, pick, damage rectangles, render, and
 * the BMP dump.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static int
app_damage_armed(void);
static int
app_damage_rects_armed(void);
static void
app_compute_damage(
    struct App* app,
    int width,
    int height);
static void
app_damage_report_dump(void);
static void
app_damage_note(
    struct App const* app,
    int width,
    int height);

void
App_SetWorldRenderMode(
    struct App* app,
    enum ToriRS_WorldRenderMode mode)
{
    if( app )
        app->world_render_mode = mode;
}

void
App_SetRendererAnimatesTextures(
    struct App* app,
    bool animates)
{
    assert(app);
    app->renderer_animates_textures = animates;
}

void
App_RefreshAfterTreeMutation(struct App* app)
{
    assert(app);
    UITree_LayoutResolve(app->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    app_request_cs1_eval(app);
    app->need_redraw = 1;
}

bool
App_IsBooting(
    struct App* app,
    int* out_progress)
{
    assert(app);
    if( out_progress )
        *out_progress = app->boot_progress;
    return app->app_state == APP_STATE_BOOTING;
}

int
App_BootHoldsLastFrame(struct App const* app)
{
    assert(app);
    /* Both halves, because the flag outlives nothing but its own bake: a bake
     * that has settled is READY and draws normally whatever the flag says. */
    return app->app_state == APP_STATE_BOOTING && app->boot_hold_last_frame;
}

bool
App_BuildFrame(
    struct App* app,
    struct ToriRS_Frame* frame,
    int width,
    int height)
{
    assert(app);
    assert(frame);

    if( app->app_state == APP_STATE_BOOTING )
        return false;

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_BUILD)
    {
        ToriRS_FrameInit(frame);
        ToriRS_FrameSetScene(frame, app->scene);
        ToriRS_FrameSetCanvas(frame, width, height);
        ToriRS_FrameSetEmitBuffer(frame, &app->emit);

        /* World pass: paint the visibility-ordered command list for the current
         * camera and attach it so UITREE_EMIT_WORLD opens the 3D pass. */
        if( app_world_drawable(app) )
        {
            TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_PAINT)
            {
                app_world_paint(app);
            }
            ToriRS_FrameSetWorld(
                frame,
                app->world,
                app->painter_buffer,
                &app->world_camera,
                app->world_camera_pos.x,
                app->world_camera_pos.y,
                app->world_camera_pos.z);
            /* Must follow SetWorld: it resets slot 0 to the root identity. */
            app_wev_bind_frame_xforms(app, frame);
            /* Must follow the paint: a tile marker that belongs in the scene
             * is placed by the command index it is drawn after, and that
             * index is a position in the list app_world_paint has just
             * written. Resolved here rather than where the marker was staged
             * because the staging happens during the overlay build, which is
             * a whole phase before this paint exists. */
            app_world_tile_marks_place(app);
            ToriRS_FrameSetWorldTileMarks(
                frame, app->world_tile_marks, app->world_tile_mark_count);
        }
    }
    return true;
}

void
App_PickFinish(
    struct App* app,
    struct ToriRS_PickHits const* hits)
{
    assert(app);
    assert(hits);
    app_world_pick_finish(app, hits);
}

/* Damage drawing is off unless asked for, so the A/B is a process restart and
 * every measurement below is against a binary that also contains the other
 * arm. Read once; off is one predicted branch. */
static int
app_damage_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = getenv("TORIRS_DAMAGE") ? 1 : 0;
    return armed;
}

/* Set for one frame by the damage report to dump the descs it unions. */
static int g_damage_trace;

/*
 * TORIRS_DAMAGE_RECTS=1: present the live rectangles separately instead of
 * their bounding box.
 *
 * Off, because it measured slower -- see App::damage. Kept switchable
 * rather than deleted so the arm that produced that number still exists.
 */
static int
app_damage_rects_armed(void)
{
    static int armed = -1;
    if( armed < 0 )
        armed = getenv("TORIRS_DAMAGE_RECTS") ? 1 : 0;
    return armed;
}

/*
 * Decide what this frame is allowed to leave unpresented. @see
 * App::damage.
 *
 * A single box by default, not a region list: the three live areas of an
 * in-world frame (viewport, minimap, the overlays inside them) are adjacent,
 * and BitBlt bills per call as well as per pixel.
 *
 * A desc contributes its box UNION its clip rather than their intersection.
 * Which of the two bounds the pixels it writes differs by kind, and the union
 * is the bound that is correct without knowing which -- an intersection would
 * be tighter and occasionally wrong, and being wrong here means stale pixels
 * that never repair.
 */
static void
app_compute_damage(
    struct App* app,
    int width,
    int height)
{
    assert(app);

    ToriRS_DamageRegionReset(&app->damage);
    if( !app_damage_armed() || !app->ui_retained_frame )
        return;

    for( int i = 0; i < app->emit.count; i++ )
    {
        struct UITreeEmitDesc const* d = &app->emit.cmds[i];
        int live = (d->kind == UITREE_EMIT_WORLD || d->kind == UITREE_EMIT_MINIMAP);

        /* Same predicate as the emitter's volatile count, and deliberately a
         * pointer test: a desc whose bytes are stable while a host buffer
         * behind it is refilled is exactly the thing a retained frame cannot
         * assume it has already drawn. */
        if( !live &&
            (d->minimap_dots || d->entity_overlays || d->worldmap_tiles || d->debug_prims) )
            live = 1;
        if( !live )
            continue;

        if( g_damage_trace )
            TORIRS_REPORT(
                "[damage] live kind=%d box=%d,%d %dx%d clip=%d,%d %dx%d\n",
                (int)d->kind,
                d->x,
                d->y,
                d->w,
                d->h,
                d->clip.x,
                d->clip.y,
                d->clip.w,
                d->clip.h);

        /* Box INTERSECT clip. A desc draws inside its own box and is then
         * further restricted by the enclosing scissor, so the pixels it can
         * touch are in both.
         *
         * The union of the two was tried first, on the theory that it was the
         * bound that stayed correct without knowing which one becomes the
         * raster scissor. It is correct and it is useless: WORLD and MINIMAP
         * both carry the root box as their clip, so the union is the whole
         * canvas and the damage rect never bounded anything -- measured at
         * 93% of frames retained and 0% of frames damaged, which is what a
         * bound that is always the screen looks like from the outside. */
        {
            int x0 = d->x > d->clip.x ? d->x : d->clip.x;
            int y0 = d->y > d->clip.y ? d->y : d->clip.y;
            int x1 = d->x + d->w;
            int y1 = d->y + d->h;
            int cx1 = d->clip.x + d->clip.w;
            int cy1 = d->clip.y + d->clip.h;

            if( cx1 < x1 )
                x1 = cx1;
            if( cy1 < y1 )
                y1 = cy1;
            ToriRS_DamageRegionAdd(&app->damage, x0, y0, x1 - x0, y1 - y0);
        }
    }
    g_damage_trace = 0;

    ToriRS_DamageRegionClamp(&app->damage, width, height, app_damage_rects_armed());
}

int
App_DamageRects(
    struct App const* app,
    struct ToriRS_DamageRect const** out_rects)
{
    assert(app);
    return ToriRS_DamageRegionRects(&app->damage, out_rects);
}

/*
 * TORIRS_DAMAGE_REPORT=1: how much of the canvas damage drawing actually
 * saved, printed once at exit.
 *
 * Here because "damage is on" and "damage is doing anything" are different
 * claims, and the frame rate cannot tell them apart: a gate that never fires
 * and a box that always covers the screen both read as no change. The two
 * numbers below separate them.
 */
static struct
{
    int64_t frames;
    int64_t retained;
    int64_t damaged;
    int64_t damaged_area;
    int64_t canvas_area;
} g_damage_stats;

static void
app_damage_report_dump(void)
{
    double area_pct;

    if( g_damage_stats.frames == 0 )
        return;
    area_pct = g_damage_stats.damaged > 0 ? 100.0 * (double)g_damage_stats.damaged_area /
                                                (double)g_damage_stats.canvas_area
                                          : 100.0;
    TORIRS_REPORT(
        "[damage] frames=%lld retained=%lld (%.1f%%) damaged=%lld (%.1f%%)\n",
        (long long)g_damage_stats.frames,
        (long long)g_damage_stats.retained,
        100.0 * (double)g_damage_stats.retained / (double)g_damage_stats.frames,
        (long long)g_damage_stats.damaged,
        100.0 * (double)g_damage_stats.damaged / (double)g_damage_stats.frames);
    TORIRS_REPORT("[damage] mean box on damaged frames: %.1f%% of canvas\n", area_pct);
}

static void
app_damage_note(
    struct App const* app,
    int width,
    int height)
{
    static int armed = -1;

    /* Periodic, not atexit: the measurement harness ends an arm with
     * taskkill /F, which runs no atexit handler, so an end-of-run dump is a
     * dump that never appears. */
    if( armed < 0 )
        armed = getenv("TORIRS_DAMAGE_REPORT") ? 1 : 0;
    if( !armed )
        return;

    g_damage_stats.frames++;
    if( app->ui_retained_frame )
        g_damage_stats.retained++;
    if( app->damage.valid )
    {
        g_damage_stats.damaged++;
        g_damage_stats.damaged_area += (int64_t)app->damage.w * app->damage.h;
        g_damage_stats.canvas_area += (int64_t)width * height;
    }
    if( g_damage_stats.frames % 600 == 0 )
    {
        app_damage_report_dump();
        TORIRS_REPORT(
            "[damage] box: %d,%d %dx%d valid=%d\n",
            app->damage.x,
            app->damage.y,
            app->damage.w,
            app->damage.h,
            app->damage.valid);
        /* Dump the contributing descs on the NEXT frame -- this one has
         * already unioned them. */
        g_damage_trace = 1;
    }
}

int
App_PresentDamage(
    struct App const* app,
    int* out_x,
    int* out_y,
    int* out_w,
    int* out_h)
{
    assert(app);
    assert(out_x);
    assert(out_y);
    assert(out_w);
    assert(out_h);
    return ToriRS_DamageRegionBox(&app->damage, out_x, out_y, out_w, out_h);
}

void
App_Render(
    struct App* app,
    int* pixels,
    int width,
    int height)
{
    struct ToriRS_Frame frame;

    assert(app);
    assert(pixels);
    assert(app->soft);

    App_NoteFrameDrawn(app);

    /* Pointed at the buffer before anything draws: the boot bar and the
     * viewport notices below write through its layer when the buffer is
     * scaled. */
    ToriPlatform_Renderer_Soft3D_Init(app->soft, app->scene, pixels, width, height);
    ToriPlatform_Renderer_Soft3D_SetLayout(app->soft, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    ToriPlatform_Renderer_Soft3D_SetInterfaceScaleMode(app->soft, RS_CS2Host_UiScaleMode(&app->host));

    if( !App_BuildFrame(app, &frame, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H) )
    {
        int* layer;
        int const layout_w = UITREE_LAYOUT_ROOT_W;
        int const layout_h = UITREE_LAYOUT_ROOT_H;
        /*
         * The startup progress bar. @see engine/boot_bar.h for why its
         * geometry is the one screen here that is not revconfig's.
         *
         * The deob has a second placement, 50 pixels BELOW centre, for when
         * the title screen is already up behind it. This client never needs
         * it: once the title tree is baked App_BuildFrame succeeds and the
         * panel's own bar takes over, so the only bar drawn here is the
         * centred one.
         */
        char const* caption;
        int caption_font_scene_id = -1;
        int percent = app->boot_progress;

        /*
         * What the boot task asked for, when it asked for anything.
         *
         * The render step does not decide what a load looks like: the task
         * that knows which stage it is at says so, and this obeys. That is
         * the whole point of the opt-in -- a task which never asks keeps the
         * old behaviour of settling silently, and one which does asks for a
         * specific picture rather than merely for a frame.
         *
         * A step that states no bar position (percent -1) leaves the bar
         * where the last stated one put it -- the profile's own contract
         * ("or -1 to leave the bar alone", rs_preload.h). Overriding with -1
         * would clamp to zero and walk the bar backwards.
         */
        if( app->runner.render.intent == TORIRS_RENDER_BOOT_BAR && app->runner.render.percent >= 0 )
            percent = app->runner.render.percent;

        /* Post-login (and any later quiet bake), the reference shows ONLY the
         * sentence: a black screen and "Loading - please wait.", no bar. The
         * bar belongs to the boot's own loading screen, which already ran to
         * 100 before the title. */
        if( App_BootTextOnly(app) )
        {
            for( int i = 0; i < width * height; i++ )
                pixels[i] = 0;
        }
        layer = ToriPlatform_Renderer_Soft3D_LayerBegin(app->soft, 0, 0, layout_w, layout_h);
        if( !App_BootTextOnly(app) )
            BootBar_Draw((uint32_t*)layer, layout_w, layout_h, percent);

        /*
         * The caption -- the same words and the same face the GPU lanes get
         * from App_BootBarCaption, so a boot does not read differently
         * depending on which renderer came up.
         *
         * Centred on the track and sitting on its baseline, where both
         * references put it, rather than in the middle of the canvas.
         */
        caption = App_BootBarCaption(app, &caption_font_scene_id);
        if( caption )
            app_boot_bar_caption(
                app,
                layer,
                layout_w,
                layout_h,
                BootBar_OriginX(layout_w) + BOOT_BAR_W / 2,
                BootBar_OriginY(layout_h) + BOOT_BAR_TEXT_BASELINE,
                caption,
                caption_font_scene_id);
        ToriPlatform_Renderer_Soft3D_LayerEnd(app->soft);
        return;
    }

    /* Must follow BuildFrame: the emit list it publishes is what says which
     * regions are live this frame. In layout pixels, as the emit list is. */
    app_compute_damage(app, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    app_damage_note(app, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    /* World hittest rides the render: each visible model is tested against
     * the mouse point right after it projects (the only window where the
     * scene scratch holds its projection), then the raw hits classify into
     * the pickset + hover tile the click/hotkey paths consume next frame. */
    if( app_world_drawable(app) && app->world_mouse_in_viewport )
        ToriPlatform_Renderer_Soft3D_SetPick(app->soft, app->world_mouse_x, app->world_mouse_y);

    TORIRS_PERF_SCOPE(TORIRS_PERF_STAGE_RENDER)
    {
        ToriPlatform_Renderer_Soft3D_RenderFrame(app->soft, &frame);
    }

    /* deob method5761 / Client-TS REBUILD_NORMAL: while the scene rebuilds,
     * the game area shows "Loading - please wait." instead of the world. */
    if( app->world_load_server_driven && app->world_load_inflight )
    {
        int* const layer = ToriPlatform_Renderer_Soft3D_LayerBegin(
            app->soft, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        app_draw_rebuild_loading_overlay(app, layer, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        ToriPlatform_Renderer_Soft3D_LayerEnd(app->soft);
    }

    /* And over the top of either: the session is gone. Last, so it is not the
     * thing a rebuild overlay covers — a reconnect drives a rebuild, and the
     * two would otherwise overlap with the wrong one winning. */
    if( NetLinkWatch_Lost(&app->net_link) )
    {
        int* const layer = ToriPlatform_Renderer_Soft3D_LayerBegin(
            app->soft, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        app_draw_connection_lost_overlay(app, layer, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        ToriPlatform_Renderer_Soft3D_LayerEnd(app->soft);
    }

    if( torirs_env_frame_debug() )
        TORIRS_LOG(
            "frame: draws element=%d terrain=%d dropped not_live=%d no_model=%d\n",
            frame.dbg_emit_element,
            frame.dbg_emit_terrain,
            frame.dbg_drop_not_live,
            frame.dbg_drop_no_model);

    if( app->soft->pick_enabled )
        App_PickFinish(app, &app->soft->pick_hits);
}

int
App_WriteBmp(
    struct App* app,
    char const* path,
    int width,
    int height)
{
    assert(app);
    return UITreeCmd_WriteBmp(app->scene, app->emit.cmds, app->emit.count, path, width, height);
}

void
App_SetPluginChromeExec(
    struct App* app,
    struct ToriRSChromeExec const* exec,
    int kind,
    int explicit_choice)
{
    assert(app);
    assert(exec);
    app->plugin_exec_pending = *exec;
    /* A newly installed presentation needs the complete retained rail even
     * when the registry itself did not change. */
    ToriRSChromeRailSync_Init(&app->plugin_rail);
    app->plugin_rail_has_layout = 0;
    app->plugin_exec_explicit = explicit_choice ? 1 : 0;
    /* Carried so a refusal can name what refused. Without it the fallback
     * message said "the 'buffer' executor would not start", which is both
     * impossible and unhelpful. */
    app->plugin_exec_kind = kind;
}

void
App_SetPluginNavMode(struct App* app, int mode)
{
    assert(app);
    assert(mode >= TORIRS_PLUGIN_NAV_AUTO);
    assert(mode <= TORIRS_PLUGIN_NAV_RAIL);
    app->plugin_nav.mode = mode;
}

