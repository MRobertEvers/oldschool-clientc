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
static int
app_view_split_catch_up(struct App* app);

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
                &app->frame_view->world_camera,
                app->frame_view->world_camera_pos.x,
                app->frame_view->world_camera_pos.y,
                app->frame_view->world_camera_pos.z);
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

/* ------------------------------------------------------------ render skip
 *
 * Owner request 2026-09-30: a run that only needs the logic -- a frame-locked
 * quest test, whose speed is bounded by how fast the software rasteriser
 * draws 765x503 -- can stop drawing, and screenshots must still work.
 *
 * What is skipped is App_Render (world paint + 3D + the 2D raster) and the
 * present. NOT skipped, because App_RunOnce does it before the host is asked
 * to draw: the logic tick, input, the network, the plugins, the UI layout and
 * the emit walk (app->emit, and from it app->world_emit_desc, the viewport
 * rectangle every projection and the world gate read). So hit testing, the
 * camera and every projection stay exactly current.
 *
 * THE GOAL IS AN IDENTICAL RUN: the same virtual timeline, frame for frame,
 * as with skip off -- so every roll, wander and race lands the same way and
 * a test green with skip off is green with skip on. A first version cost one
 * frame per screenshot and per pick read, and five quests (hazeelcult,
 * idesofmilk, ikov, junglepotion, rovingelves) went red on the shifted
 * timeline, skip off green on the same binary. So nothing here may move a
 * frame: render-time state is drawn LATE, never waited for.
 *
 * What the draw itself produces, and so what goes stale on a skipped frame:
 *
 *   1. app->frame_view->world_pickset and the hover tile -- App_PickFinish, from the pick
 *      the rasteriser runs at the pointer as each model projects. Read by the
 *      right-click menu build, the left-click default action, the mouseover
 *      text, and api.drive.pick_holds / pick_point.
 *      - A driver READ catches up first (App_RenderSkipCatchUp): the skipped
 *        frame is drawn then, from the plugin pump, when the app still holds
 *        exactly that frame's emit list, scene, camera and pointer.
 *      - A pushed click or move owes the CURRENT frame a draw
 *        (App_RenderSkipRequestDraw): the push drains next frame and is
 *        resolved there against the set this frame stamps.
 *      - A camera write that lands at once (DrivePointer_Camera,
 *        App_SetCameraPose) catches up FIRST, so the late draw sees the
 *        camera that frame had.
 *   2. Which world-entity views (sailing hulls) are drawn in full -- decided
 *      while painting (app_wev_decide_flatten), and read by the actor
 *      projection, the pick classifier and a deck's dynamic population.
 *      Covered by drawing every frame while a live hull stands inside the
 *      loaded scene (App_RenderSkipHullsInScene).
 *   3. A model's bounds cylinder -- posed by the renderer, read by the
 *      overlay heights the NEXT emit draws (health bars, headicons, overhead
 *      text), and the mouseover text the next frame builds from (1). Only a
 *      picture shows either: a screenshot request catches up (app_plugin_
 *      screenshot), so the captured frame is laid out from a drawn one.
 *   4. CAM_SHAKE's jitter draws from rand() while painting, which cs2vm2's
 *      random opcode shares. Not covered: a skipped frame during a camera
 *      shake leaves the C library's sequence where it was.
 *   5. The mouseover entries app_hover_text_update publishes every frame are
 *      built from the last DRAWN frame's pickset, and the cache's cursor
 *      tooltip script (~mouseover_tooltip) reads them a frame later. Not
 *      covered (catching up every frame the pointer rests on the world would
 *      be drawing every frame): a picture can carry a tooltip naming what the
 *      pointer hovered at the last drawn frame. Measured over the 87-quest
 *      suite: the ledgers are identical, 63% of shots byte-identical, and
 *      96% of the rest differ only in that tooltip box.
 *   6. A model whose bind pose was never captured accumulates its animation
 *      once per draw (toridraw_scene.c's own comment: "the model does not
 *      animate, it accumulates"), so its picture depends on how many frames
 *      were drawn. Pre-existing; skipping draws only makes it show
 *      (druid's suits of armour).
 *
 * And one rule over all of them: skip only removes draws. A frame the host
 * would not have drawn (App_RunOnce withheld its commit) is not drawn for any
 * of the reasons above either, and is never caught up -- its emit list may
 * point into tree components freed since (a widget model's render cache:
 * forcing such a frame crashed conformance in ToriRS_FrameNextCommand,
 * SIGBUS on the freed cache's release pointer, after a ::goto).
 */

void
App_RenderSkipSet(
    struct App* app,
    int enabled)
{
    assert(app);
    /* A watched client with a script attached presents every frame: the
     * watcher is looking at it. Skip stays off until the script detaches. */
    if( enabled && app->view_split.attached )
    {
        TORIRS_LOG("render-skip: refused while a script's view is attached (the watcher's frames are presented)\n");
        return;
    }
    if( enabled && !app->render_skip.enabled )
    {
        app->render_skip.fresh = 1;
        app->render_skip.catch_up_ok = 0;
    }
    app->render_skip.enabled = enabled ? 1 : 0;
}

int
App_RenderSkipEnabled(struct App const* app)
{
    assert(app);
    return app->render_skip.enabled;
}

void
App_RenderSkipRequestDraw(
    struct App* app,
    int frames)
{
    assert(app);
    assert(frames > 0);
    /* Split (App_ViewSplit): every caller is a driver push -- the frame it
     * owes is the RUNNER's, drawn offscreen before the presented one. */
    if( app->view_split.attached )
    {
        if( app->view_split.runner_draw_owed < frames )
            app->view_split.runner_draw_owed = frames;
        return;
    }
    if( app->render_skip.draw_frames < frames )
        app->render_skip.draw_frames = frames;
}

int
App_RenderSkipCatchUp(struct App* app)
{
    int const width = UITREE_LAYOUT_ROOT_W;
    int const height = UITREE_LAYOUT_ROOT_H;

    assert(app);
    /* Split: a driver read wants the RUNNER's pickset, which the presented
     * frame never fills; draw the runner's view late, exactly as a skipped
     * frame is drawn late below. */
    if( app->view_split.attached )
        return app_view_split_catch_up(app);
    if( !app->render_skip.enabled || app->render_skip.fresh )
        return 1;
    if( !app->render_skip.catch_up_ok )
        return 0;
    if( !app->render_skip.scratch )
    {
        app->render_skip.scratch = malloc((size_t)width * (size_t)height * sizeof(int));
        assert(app->render_skip.scratch);
    }
    /* The skipped frame, drawn now: the pick arms from the pointer it had
     * (world_mouse_* are rewritten later in this App_RunOnce, not before the
     * pump), exactly as the on-time draw would have armed it. */
    App_Render(app, app->render_skip.scratch, width, height);
    app->render_skip.fresh = 1;
    app->render_skip.catch_up_ok = 0;
    app->render_skip.frames_caught_up++;
    return 1;
}

void
app_render_skip_init(struct App* app)
{
    char const* skip = getenv("TORIRS_RENDER_SKIP");

    assert(app);
    /* tools/quest_gate/run.py sets it for every quest run (1, or 0 under
     * --render-every-frame); `::renderskip` and api.drive.render_skip switch
     * it afterwards. */
    App_RenderSkipSet(app, skip && skip[0] == '1');
}

/* What render skip saved, for a run that wants to know: client.log's last
 * word on a quest run, and the speed table's frame count. A REPORT, not
 * narration: it prints only when someone asked -- the knob is set (run.py
 * sets it either way) or skip was switched on. Then the scratch buffer goes. */
void
app_render_skip_shutdown(struct App* app)
{
    assert(app);
    if( getenv("TORIRS_RENDER_SKIP") || app->render_skip.enabled )
        TORIRS_REPORT(
            "render-skip: %llu frame(s) drawn, %llu not drawn, %llu of those drawn late "
            "for a read (skip %s at exit)\n",
            (unsigned long long)app->render_skip.frames_drawn,
            (unsigned long long)app->render_skip.frames_skipped,
            (unsigned long long)app->render_skip.frames_caught_up,
            app->render_skip.enabled ? "on" : "off");
    free(app->render_skip.scratch);
    app->render_skip.scratch = NULL;
}

/* Item 2 above: the live hulls standing inside the loaded scene. One outside
 * it is never painted, picked or projected onto the viewport, and the client
 * keeps a hull it has sailed away from live (conformance: the sailing rows'
 * hull is still in the registry after a ::tele home and after a relog), so
 * "any hull live" would draw every frame for the rest of such a run. A
 * nested view's transform is deck-local and lands "inside" -- the safe
 * direction. WORLDVIEW_MAX slots, never the tree: a constant per frame. */
int
App_RenderSkipHullsInScene(struct App* app)
{
    int count = 0;
    int x0;
    int z0;
    int span;

    assert(app);
    if( !app->world )
        return 0;
    x0 = app->world->_base_tile_x * 128;
    z0 = app->world->_base_tile_z * 128;
    span = app->world->_scene_size * 128;
    for( int id = 1; id < WORLDVIEW_MAX; id++ )
    {
        struct Wev const* wev;

        if( !Wevs_IsLive(&app->wevs, id) )
            continue;
        wev = Wevs_Get(&app->wevs, id);
        if( wev->x >= x0 && wev->x < x0 + span && wev->z >= z0 && wev->z < z0 + span )
            count++;
    }
    return count;
}

int
App_RenderSkipFrame(
    struct App* app,
    int redraw,
    int forced)
{
    int draw = redraw;

    assert(app);
    if( app->render_skip.enabled )
    {
        int capture_pending = 0;
        int must;

        for( int i = 0; i < APP_PLUGIN_SCREENSHOTS_MAX; i++ )
            capture_pending |= app->plugin_screenshots[i].in_use;
        must = forced || capture_pending || app->render_skip.draw_frames > 0 ||
               App_RenderSkipHullsInScene(app) > 0;
        /* Only ever TAKES a draw away (the banner's last rule). */
        draw = redraw && must;
        if( draw )
        {
            app->render_skip.fresh = 1;
            app->render_skip.catch_up_ok = 0;
        }
        else if( redraw )
        {
            /* Committed and skipped: the state goes stale, and this frame's
             * emit list -- the current one until the next commit -- can
             * still be drawn late. */
            app->render_skip.fresh = 0;
            app->render_skip.catch_up_ok = 1;
        }
        else
        {
            /* Not committed: with skip off nothing is drawn either, so a
             * fresh state stays fresh; a stale one can no longer be caught
             * up -- the tree may have moved under the old emit list. */
            app->render_skip.catch_up_ok = 0;
        }
    }
    if( draw )
        app->render_skip.frames_drawn++;
    else
        app->render_skip.frames_skipped++;
    /* A request is for frames DRAWN, not frames passed: one owed across an
     * uncommitted stretch is still owed when the stretch ends. */
    if( draw && app->render_skip.draw_frames > 0 )
        app->render_skip.draw_frames--;
    return draw;
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

    /* The offscreen runner frame is not a drawn frame: the plugins' frame
     * count, the pacer and every "a frame was drawn" reader see only the
     * presented ones (the split's contract). */
    if( !app->view_split.offscreen )
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
    if( app_world_drawable(app) && app->frame_view->world_mouse_in_viewport )
        ToriPlatform_Renderer_Soft3D_SetPick(
            app->soft, app->frame_view->world_mouse_x, app->frame_view->world_mouse_y);

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


/* ------------------------------------------------------------ runner split
 *
 * THE RUNNER CAMERA SPLIT (struct App_ViewSplit; the work order is
 * docs/minigames/raid_loop/CAMERA_TRIAGE_seam2.md, runner_view_split).
 *
 * While a script runs in a client that presents, views[0] is the
 * AutomationRunner's and views[1] the PlayerClient's. The presented frame is
 * drawn through views[1] on whatever renderer is active and picks at the
 * physical pointer. The runner's pickset and photographs come from an
 * OFFSCREEN frame drawn through views[0] on the software lane into a buffer
 * nobody presents -- the render-skip catch-up below with the view as the
 * parameter -- either before the presented frame (a driver push owed it, the
 * way a skipped frame is owed a draw) or late, from the plugin pump, when a
 * driver read finds the runner's set a frame old.
 *
 * THE OFFSCREEN FRAME'S CONTRACT, hazard by hazard (what was already safe in
 * App_RenderSkipCatchUp / app_capture_fallback_render, what this adds):
 *
 *   - Frame end / load-event drain. Soft3D's RenderFrame always ends with
 *     ToriRS_FrameEnd -> ToriDraw_SceneFrameEnd, which empties the scene's
 *     event queue and frees the poses held for the GPU replay. The catch-up
 *     was safe only because it ran on the software lane with the queue
 *     already spent (main.c's skipped-frame SceneFrameEnd); the capture
 *     re-render was NOT safe on a GPU lane (it drained events the lane had
 *     not read). Changed: the offscreen frame sets the held poses and the
 *     queue's count aside and puts them back afterwards, so the presented
 *     frame -- on any lane -- finds them exactly as App_RunOnce left them;
 *     whatever the offscreen draw itself queued is dropped.
 *   - Animation. Advance is logic's (World_Cycle), never the draw's: already
 *     safe. Accumulation: the soft frame poses with the scene's pose cache
 *     (reuse), so a second draw of the same frame re-poses nothing on the
 *     software lane: already safe there. A GPU lane that re-poses on its own
 *     (GL3) can still re-apply a pose to a model with no captured bind pose
 *     on a frame that also drew offscreen -- the pre-existing druid-armour
 *     class (app_render.c item 6), not made worse on the software lane.
 *   - Painter buffer. Changed: the offscreen frame paints into its own
 *     (runner_painter_buffer); the shared painter's per-paint state (roof
 *     mask, cull span, occluders, flatten decision) is left as the runner
 *     wrote it until the next presented paint rewrites it -- which is why the
 *     owed draw runs BEFORE the presented one.
 *   - rand(). CAM_SHAKE rolled at every paint (app_world_paint). Changed: the
 *     roll is taken once per loop iteration (cam_script.shake_roll) and every paint
 *     of that iteration reuses it; one view, one paint: the same rand() calls
 *     in the same order as before.
 *   - Paint-limit / frame counts. The paint limit is a debug setting the
 *     camera keys step, never the draw: already safe. App_NoteFrameDrawn
 *     (the plugins' frame count) is skipped for the offscreen frame: changed.
 *   - The pick. Armed at the frame view's pointer: the offscreen frame's view
 *     is the runner's, so it arms at the runner's pointer (a shot disarms it,
 *     as the capture re-render always did).
 */

static int
app_view_split_world_tick(struct App const* app)
{
    assert(app);
    return app->world ? (int)(app->world->cycle / APP_SERVER_TICK_LOGIC_CYCLES) : 0;
}

int
App_ViewAttachPlayerClient(
    struct App* app,
    char const** out_reason)
{
    struct App_ViewSplit* split;
    char const* reason = NULL;

    assert(app);
    split = &app->view_split;
    if( split->attached )
    {
        if( out_reason )
            *out_reason = "already attached";
        return 1;
    }
    if( !split->presentable )
        reason = "this client presents nothing (no window, or a headless run): one view, nothing created";
    else if( app->render_skip.enabled )
        reason = "render skip is on: nothing is presented, one view, nothing created";
    else if( split->lane_refusal )
        reason = split->lane_refusal;
    if( reason )
    {
        if( out_reason )
            *out_reason = reason;
        return 0;
    }

    /* views[1] is a COPY of views[0]: the watcher's picture does not jump
     * when the script starts. Its menu is its own and starts closed. */
    app->views[APP_VIEW_PLAYER_CLIENT] = app->views[APP_VIEW_RUNNER];
    split->player_client_menu = *app->views[APP_VIEW_RUNNER].minimenu;
    UIMinimenu_Hide(&split->player_client_menu);
    app->views[APP_VIEW_PLAYER_CLIENT].minimenu = &split->player_client_menu;
    split->menu_bound_to_player_client = 0;
    app->view_split.view_count = 2;
    app->frame_view = &app->views[APP_VIEW_RUNNER];
    /* The runner turns its camera through its verbs only: no key or middle
     * button latch carries over to it. */
    app->views[APP_VIEW_RUNNER].cam_key_left = 0;
    app->views[APP_VIEW_RUNNER].cam_key_right = 0;
    app->views[APP_VIEW_RUNNER].cam_key_up = 0;
    app->views[APP_VIEW_RUNNER].cam_key_down = 0;
    app->views[APP_VIEW_RUNNER].cam_mmb_active = 0;

    split->runner_bus = malloc(sizeof(*split->runner_bus));
    assert(split->runner_bus);
    CmdBus_Init(split->runner_bus);
    split->runner_painter_buffer = painter_buffer_new();
    assert(split->runner_painter_buffer);
    split->runner_pixels = NULL;
    split->runner_pixels_count = 0;

    LibToriRS_Input_Init(&split->physical_input, 0);
    split->physical_x = app->views[APP_VIEW_RUNNER].world_mouse_x;
    split->physical_y = app->views[APP_VIEW_RUNNER].world_mouse_y;
    split->physical_input.curr.mouse_x = split->physical_x;
    split->physical_input.curr.mouse_y = split->physical_y;
    split->physical_input.prev = split->physical_input.curr;
    split->physical_moved_last = 0;
    split->runner_x = split->physical_x;
    split->runner_y = split->physical_y;
    split->runner_pointer_valid = 1;
    split->runner_buttons_held = 0;
    split->runner_events_this_frame = 0;
    split->physical_frame = 0;
    split->held_count = 0;
    /* Owner decision 1: OFF when a script starts. */
    split->interact = 0;
    split->runner_fresh = 0;
    split->runner_catch_up_ok = 0;
    split->runner_draw_owed = 0;
    split->offscreen = 0;
    split->attached = 1;
    split->attaches++;
    TORIRS_REPORT(
        "view-split: attached (AutomationRunner = views[0], PlayerClient = views[1]); interact off\n");
    if( out_reason )
        *out_reason = "attached";
    return 1;
}

void
App_ViewDetachPlayerClient(struct App* app)
{
    struct App_ViewSplit* split;
    struct UIMinimenu* storage;

    assert(app);
    split = &app->view_split;
    if( !split->attached )
        return;
    assert(!split->menu_bound_to_player_client);
    assert(!split->offscreen);

    /* views[0] becomes the watcher's: what is on screen stays on screen, and
     * nothing the runner did is carried over. Its menu storage stays
     * interact.minimenu, now holding the watcher's menu. */
    storage = app->views[APP_VIEW_RUNNER].minimenu;
    assert(storage == &app->interact.minimenu);
    app->views[APP_VIEW_RUNNER] = app->views[APP_VIEW_PLAYER_CLIENT];
    app->views[APP_VIEW_RUNNER].minimenu = storage;
    *storage = split->player_client_menu;
    memset(&app->views[APP_VIEW_PLAYER_CLIENT], 0, sizeof(app->views[APP_VIEW_PLAYER_CLIENT]));
    app->view_split.view_count = 1;
    app->frame_view = &app->views[APP_VIEW_RUNNER];

    free(split->runner_bus);
    split->runner_bus = NULL;
    free(split->runner_painter_buffer->commands);
    free(split->runner_painter_buffer);
    split->runner_painter_buffer = NULL;
    free(split->runner_pixels);
    split->runner_pixels = NULL;
    split->runner_pixels_count = 0;
    split->held_count = 0;
    split->interact = 0;
    split->physical_frame = 0;
    split->runner_draw_owed = 0;
    split->attached = 0;
    TORIRS_REPORT(
        "view-split: detached after %llu offscreen frame(s) (%llu for reads, %llu for shots), "
        "%llu presented; physical %llu delivered, %llu dropped, %llu held\n",
        (unsigned long long)split->offscreen_frames,
        (unsigned long long)split->offscreen_frames_for_reads,
        (unsigned long long)split->offscreen_frames_for_shots,
        (unsigned long long)split->presented_frames,
        (unsigned long long)split->physical_delivered,
        (unsigned long long)split->physical_dropped,
        (unsigned long long)split->physical_held);
}

void
App_ViewSetInteract(
    struct App* app,
    int interact)
{
    assert(app);
    if( !app->view_split.attached )
        return;
    if( (interact ? 1 : 0) != app->view_split.interact )
        TORIRS_REPORT(
            "view-split: interact %s at tick %d\n",
            interact ? "ON (the watcher's clicks and keys act on the game)" : "OFF",
            app_view_split_world_tick(app));
    app->view_split.interact = interact ? 1 : 0;
    /* Whatever waited for a gesture was the watcher's intent while it could
     * act; switched off, it is dropped. */
    if( !interact )
        app->view_split.held_count = 0;
}

int
App_ViewWatcherActions(
    struct App const* app,
    uint32_t after_serial,
    struct App_ViewSplitWatcherAction* out,
    int capacity)
{
    int written = 0;
    uint32_t newest;
    uint32_t first;

    assert(app);
    assert(out);
    assert(capacity > 0);
    newest = app->view_split.watcher_serial;
    if( newest <= after_serial )
        return 0;
    first = after_serial + 1;
    if( newest - first >= APP_VIEW_SPLIT_WATCHER_MAX )
        first = newest - APP_VIEW_SPLIT_WATCHER_MAX + 1;
    for( uint32_t serial = first; serial <= newest && written < capacity; serial++ )
        out[written++] = app->view_split.watcher[serial % APP_VIEW_SPLIT_WATCHER_MAX];
    return written;
}

static void
app_view_split_watcher_note(
    struct App* app,
    char const* what,
    int x,
    int y,
    int detail)
{
    struct App_ViewSplit* split = &app->view_split;
    struct App_ViewSplitWatcherAction* slot;

    split->watcher_serial++;
    slot = &split->watcher[split->watcher_serial % APP_VIEW_SPLIT_WATCHER_MAX];
    slot->serial = split->watcher_serial;
    slot->tick = app_view_split_world_tick(app);
    slot->what = what;
    slot->x = x;
    slot->y = y;
    slot->detail = detail;
}

static int
app_view_split_command_is_input(uint32_t type)
{
    return type == TORIRS_CMD_INPUT_KEY_DOWN || type == TORIRS_CMD_INPUT_KEY_UP ||
           type == TORIRS_CMD_INPUT_KEY_EVENT || type == TORIRS_CMD_INPUT_OSRS_KEY ||
           type == TORIRS_CMD_INPUT_MOUSE_DOWN || type == TORIRS_CMD_INPUT_MOUSE_UP ||
           type == TORIRS_CMD_INPUT_MOUSE_MOVE || type == TORIRS_CMD_INPUT_MOUSE_WHEEL ||
           type == TORIRS_CMD_INPUT_CLEAR_KEYS || type == TORIRS_CMD_INPUT_MOUSE_LEAVE;
}

/* The runner is between a press and its release, or between a right press
 * and the row it will choose: a physical press must not land inside that. */
static int
app_view_split_runner_busy(struct App* app)
{
    struct App_ViewSplit const* split = &app->view_split;

    return split->runner_events_this_frame || split->runner_buttons_held ||
           App_RunnerView(app)->minimenu->visible;
}

/* Deliver one physical command to the game input (it acts through the
 * PlayerClient view this frame); a watcher action while Interact is on is
 * noted for the driver's `watcher.*` rows. */
static void
app_view_split_deliver(
    struct App* app,
    struct ToriRS_CmdHeader const* header,
    uint8_t const* payload,
    struct LibToriRS_Input* game_input,
    int chrome)
{
    struct App_ViewSplit* split = &app->view_split;

    (void)ToriRS_Input_ApplyCmd(game_input, header, payload);
    split->physical_frame = 1;
    split->physical_delivered++;
    if( chrome || !split->interact )
        return;
    if( header->type == TORIRS_CMD_INPUT_MOUSE_DOWN && header->length >= sizeof(struct ToriRS_CmdMouseButton) )
    {
        struct ToriRS_CmdMouseButton button;
        memcpy(&button, payload, sizeof(button));
        app_view_split_watcher_note(
            app,
            button.button == TORIRSM_RIGHT ? "right_click" : "click",
            button.x,
            button.y,
            button.button);
    }
    else if( header->type == TORIRS_CMD_INPUT_KEY_DOWN && header->length >= sizeof(struct ToriRS_CmdKey) )
    {
        struct ToriRS_CmdKey key;
        memcpy(&key, payload, sizeof(key));
        app_view_split_watcher_note(app, "key", split->physical_x, split->physical_y, (int)key.keycode);
    }
    else if( header->type == TORIRS_CMD_INPUT_MOUSE_WHEEL && header->length >= sizeof(struct ToriRS_CmdMouseWheel) )
    {
        struct ToriRS_CmdMouseWheel wheel;
        memcpy(&wheel, payload, sizeof(wheel));
        app_view_split_watcher_note(app, "wheel", split->physical_x, split->physical_y, (int)wheel.wheel_y);
    }
}

int
App_ViewSplitPhysicalCommand(
    struct App* app,
    struct ToriRS_CmdHeader const* header,
    uint8_t const* payload,
    struct LibToriRS_Input* game_input)
{
    struct App_ViewSplit* split;
    int pointer_kind;
    int chrome;
    int is_move;

    assert(app);
    assert(header);
    assert(payload);
    assert(game_input);
    split = &app->view_split;
    assert(split->attached);
    if( !app_view_split_command_is_input(header->type) )
        return 0;

    /* Every physical event steers the PlayerClient view's own input: its
     * camera keys, middle button, wheel and pointer. */
    (void)ToriRS_Input_ApplyCmd(&split->physical_input, header, payload);
    pointer_kind = header->type == TORIRS_CMD_INPUT_MOUSE_DOWN || header->type == TORIRS_CMD_INPUT_MOUSE_UP ||
                   header->type == TORIRS_CMD_INPUT_MOUSE_MOVE;
    is_move = header->type == TORIRS_CMD_INPUT_MOUSE_MOVE || header->type == TORIRS_CMD_INPUT_MOUSE_LEAVE;
    if( pointer_kind )
    {
        split->physical_x = split->physical_input.curr.mouse_x;
        split->physical_y = split->physical_input.curr.mouse_y;
        split->physical_moved_last = 1;
    }

    /* Who may act on the game: the plugin chrome always (the Scripts panel,
     * its Stop and Interact buttons), the game only while Interact is on. */
    if( header->type == TORIRS_CMD_INPUT_KEY_DOWN || header->type == TORIRS_CMD_INPUT_KEY_UP ||
        header->type == TORIRS_CMD_INPUT_KEY_EVENT || header->type == TORIRS_CMD_INPUT_OSRS_KEY ||
        header->type == TORIRS_CMD_INPUT_CLEAR_KEYS )
        chrome = app_chrome_holds_keyboard(app);
    else
        chrome = app_chrome_wants_pointer(app, split->physical_x, split->physical_y);
    if( !chrome && !split->interact )
    {
        /* A spectator: orbit, zoom and hover-inspect came through
         * physical_input above; the game never sees the event. */
        if( !is_move )
            split->physical_dropped++;
        return 1;
    }
    if( app_view_split_runner_busy(app) )
    {
        /* Never interleaved with the runner's gesture: a move is simply not
         * delivered (the physical pointer is already latched above), a press,
         * release, key or wheel waits for the gesture to end. */
        if( is_move )
            return 1;
        if( split->held_count < APP_VIEW_SPLIT_HELD_MAX && header->length <= APP_VIEW_SPLIT_HELD_PAYLOAD )
        {
            struct App_ViewSplitHeld* held = &split->held[split->held_count++];
            held->type = header->type;
            held->length = header->length;
            memcpy(held->payload, payload, header->length);
            split->physical_held++;
        }
        else
            split->physical_dropped++;
        return 1;
    }
    app_view_split_deliver(app, header, payload, game_input, chrome);
    return 1;
}

/* Start of the drain: whether the runner speaks this frame (its bus is
 * drained after the physical one, so this is read first), and the physical
 * commands that waited, when the runner's gesture is over. */
void
app_view_split_drain_begin(
    struct App* app,
    struct LibToriRS_Input* game_input)
{
    struct App_ViewSplit* split = &app->view_split;
    int replayed = 0;

    assert(split->attached);
    split->runner_events_this_frame = CmdRing_IsEmpty(&split->runner_bus->ring) ? 0 : 1;
    split->physical_frame = 0;
    LibToriRS_Input_Begin(&split->physical_input, game_input->curr.time);
    if( split->held_count == 0 || app_view_split_runner_busy(app) )
        return;
    for( int i = 0; i < split->held_count; i++ )
    {
        struct ToriRS_CmdHeader header;
        header.type = split->held[i].type;
        header.length = split->held[i].length;
        app_view_split_deliver(app, &header, split->held[i].payload, game_input, 0);
        replayed++;
    }
    split->held_count = 0;
    (void)replayed;
}

/* One runner command, seen as the drain hands it to the game input: the
 * runner's own pointer and gesture are tracked from its OWN events, never
 * from the game input's pointer (a delivered physical move may have moved
 * that). */
void
app_view_split_note_runner_command(
    struct App* app,
    struct ToriRS_CmdHeader const* header,
    uint8_t const* payload)
{
    struct App_ViewSplit* split = &app->view_split;

    assert(split->attached);
    if( (header->type == TORIRS_CMD_INPUT_MOUSE_DOWN || header->type == TORIRS_CMD_INPUT_MOUSE_UP) &&
        header->length >= sizeof(struct ToriRS_CmdMouseButton) )
    {
        struct ToriRS_CmdMouseButton button;
        memcpy(&button, payload, sizeof(button));
        split->runner_x = button.x;
        split->runner_y = button.y;
        split->runner_pointer_valid = 1;
        split->physical_moved_last = 0;
        if( button.button < 31 )
        {
            if( header->type == TORIRS_CMD_INPUT_MOUSE_DOWN )
                split->runner_buttons_held |= 1 << button.button;
            else
                split->runner_buttons_held &= ~(1 << button.button);
        }
    }
    else if( header->type == TORIRS_CMD_INPUT_MOUSE_MOVE && header->length >= sizeof(struct ToriRS_CmdMouseMove) )
    {
        struct ToriRS_CmdMouseMove move;
        memcpy(&move, payload, sizeof(move));
        split->runner_x = move.x;
        split->runner_y = move.y;
        split->runner_pointer_valid = 1;
        split->physical_moved_last = 0;
    }
}

void
app_view_split_drain_end(struct App* app)
{
    assert(app->view_split.attached);
    LibToriRS_Input_End(&app->view_split.physical_input);
}

void
App_ViewSplitDrawRunner(
    struct App* app,
    int* pixels,
    int pick)
{
    struct App_ViewSplit* split;
    struct App_WorldView* runner;
    struct App_WorldView* saved_view;
    struct PaintersBuffer* saved_buffer;
    struct ToriDraw_Scene* scene;
    struct ToriDraw_ScenePendingPose* saved_poses;
    int saved_pose_count;
    int saved_pose_cap;
    int saved_event_count;
    int saved_in_viewport;
    int const width = UITREE_LAYOUT_ROOT_W;
    int const height = UITREE_LAYOUT_ROOT_H;
    int* target = pixels;

    assert(app);
    split = &app->view_split;
    assert(split->attached);
    assert(!split->offscreen);
    assert(app->scene);
    if( !target )
    {
        if( split->runner_pixels_count < width * height )
        {
            free(split->runner_pixels);
            split->runner_pixels = malloc((size_t)width * (size_t)height * sizeof(int));
            assert(split->runner_pixels);
            split->runner_pixels_count = width * height;
        }
        target = split->runner_pixels;
    }
    runner = App_RunnerView(app);
    scene = app->scene;

    saved_view = app->frame_view;
    saved_buffer = app->painter_buffer;
    saved_in_viewport = runner->world_mouse_in_viewport;
    /* What the presented frame owes its lane (held poses, queued load events)
     * is set aside: the offscreen frame's ToriRS_FrameEnd must not spend it. */
    saved_poses = scene->pending_poses;
    saved_pose_count = scene->pending_pose_count;
    saved_pose_cap = scene->pending_pose_cap;
    saved_event_count = scene->event_queue.count;
    scene->pending_poses = NULL;
    scene->pending_pose_count = 0;
    scene->pending_pose_cap = 0;

    app->frame_view = runner;
    if( !pick )
        runner->world_mouse_in_viewport = 0;
    app->painter_buffer = split->runner_painter_buffer;
    split->offscreen = 1;
    App_Render(app, target, width, height);
    split->offscreen = 0;

    /* Its FrameEnd freed what it held itself; the array is ours. */
    assert(scene->pending_pose_count == 0);
    free(scene->pending_poses);
    scene->pending_poses = saved_poses;
    scene->pending_pose_count = saved_pose_count;
    scene->pending_pose_cap = saved_pose_cap;
    scene->event_queue.count = saved_event_count;
    app->painter_buffer = saved_buffer;
    runner->world_mouse_in_viewport = saved_in_viewport;
    app->frame_view = saved_view;
    split->offscreen_frames++;
}

static int
app_view_split_catch_up(struct App* app)
{
    struct App_ViewSplit* split = &app->view_split;

    if( split->offscreen || split->runner_fresh )
        return 1;
    if( !split->runner_catch_up_ok )
        return 0;
    App_ViewSplitDrawRunner(app, NULL, 1);
    split->runner_fresh = 1;
    split->runner_catch_up_ok = 0;
    split->offscreen_frames_for_reads++;
    return 1;
}

void
App_ViewSplitBeforePresent(
    struct App* app,
    int committed)
{
    struct App_ViewSplit* split;

    assert(app);
    split = &app->view_split;
    assert(split->attached);
    if( !committed )
    {
        /* As render skip: a frame that committed nothing leaves a fresh set
         * fresh, and a stale one can no longer be drawn late. */
        split->runner_catch_up_ok = 0;
        return;
    }
    if( split->runner_draw_owed > 0 )
    {
        /* BEFORE the presented frame, so every piece of shared painter state
         * is left as the presented frame writes it. */
        App_ViewSplitDrawRunner(app, NULL, 1);
        split->runner_draw_owed--;
        split->runner_fresh = 1;
        split->runner_catch_up_ok = 0;
        return;
    }
    split->runner_fresh = 0;
    split->runner_catch_up_ok = 1;
}

void
App_ViewSplitBindPlayerClient(
    struct App* app,
    int bind)
{
    struct App_ViewSplit* split;
    struct UIMinimenu swap;

    assert(app);
    split = &app->view_split;
    assert(split->attached);
    assert((bind ? 1 : 0) != split->menu_bound_to_player_client);
    /* The UI interaction step drives interact.minimenu; for a watcher's
     * gesture that storage must hold the WATCHER's menu, so the two menus'
     * contents and the views' pointers swap together and back. */
    swap = app->interact.minimenu;
    app->interact.minimenu = split->player_client_menu;
    split->player_client_menu = swap;
    if( bind )
    {
        app->views[APP_VIEW_RUNNER].minimenu = &split->player_client_menu;
        app->views[APP_VIEW_PLAYER_CLIENT].minimenu = &app->interact.minimenu;
        app->frame_view = &app->views[APP_VIEW_PLAYER_CLIENT];
    }
    else
    {
        app->views[APP_VIEW_RUNNER].minimenu = &app->interact.minimenu;
        app->views[APP_VIEW_PLAYER_CLIENT].minimenu = &split->player_client_menu;
        app->frame_view = &app->views[APP_VIEW_RUNNER];
    }
    split->menu_bound_to_player_client = bind ? 1 : 0;
}

static void
app_view_split_latch_one(
    struct App* app,
    struct App_WorldView* view,
    int x,
    int y,
    int absent)
{
    view->pointer_absent = absent;
    view->world_mouse_in_viewport = !absent && app_world_mouse_gate(app, x, y);
    view->world_mouse_x = x;
    view->world_mouse_y = y;
    if( !view->world_mouse_in_viewport )
    {
        view->world_hover_tile_x = -1;
        view->world_hover_tile_z = -1;
        view->world_hover_view = 0;
        World_PickSetReset(&view->world_pickset);
    }
}

void
app_view_split_latch_pointers(struct App* app)
{
    struct App_ViewSplit* split;
    struct App_WorldView* saved;
    struct App_WorldView* owner;

    assert(app);
    split = &app->view_split;
    assert(split->attached);
    app_view_split_latch_one(
        app, &app->views[APP_VIEW_RUNNER], split->runner_x, split->runner_y, !split->runner_pointer_valid);
    app_view_split_latch_one(
        app,
        &app->views[APP_VIEW_PLAYER_CLIENT],
        split->physical_x,
        split->physical_y,
        split->physical_input.mouse_pointer_absent);
    /* Hover follows whichever pointer moved last (the DESIGN's INPUT rule). */
    owner = split->physical_moved_last ? &app->views[APP_VIEW_PLAYER_CLIENT] : &app->views[APP_VIEW_RUNNER];
    saved = app->frame_view;
    app->frame_view = owner;
    app_hover_text_update(app, owner->world_mouse_x, owner->world_mouse_y);
    app->frame_view = saved;
}
