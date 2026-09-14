/*
 * The world viewport: the wedge scale, the retained world rectangle, and the messages drawn over it.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static int
app_world_fov_override(void);
static int
app_wedge_scale_mode(void);
static void
app_apply_wedge_scale(struct App* app);
static void
app_draw_viewport_message(
    struct App* app,
    int* pixels,
    int width,
    int height,
    char const* line1,
    char const* line2_nullable,
    int fill_black);

/*
 * World projection scale — recomputed per layout, the reference behaviour
 * (docs/ORANGE_WEDGE.md §4/§11/§12, promoted to the default per §11.7).
 *
 * The reference client recomputes the world projection scale from the world
 * viewport HEIGHT on every layout (class159.method5357:
 * scale = viewportHeight * zoom / 334, zoom interpolated between the two
 * VIEWPORT_SETFOV endpoints over height-334 in [0,100]). Leaving it at the
 * compile-time projection_scale = 512 is what drew the Inferno 2.68x magnified —
 * at a 503-high world viewport the reference lands on 191/192.
 *
 * ON by default. TORIRS_WEDGE_SCALE values:
 *   (unset) | 1 | auto        recompute per class159.method5357 (default)
 *   0 | off                   legacy constant scale 512, for A/B comparison
 *   <n>                       force the linear scale to n (n >= 8), for bisection
 *   TORIRS_WEDGE_ZOOM=<n>,<f>   override the decoded SETFOV endpoints (auto mode)
 *   TORIRS_WEDGE_FOV_DEBUG=1    log the SETFOV decode and the resulting scale
 *
 * The scale reaches the kernels exactly, through ToriDraw_Camera.projection_scale.
 *
 * TORIRS_WORLD_FOV=<n> drives the camera's OTHER knob instead: it switches the
 * world camera to TORIDRAW_PROJECTION_MODE_FOV and sets fov_rpi2048 to n (units of
 * 2*pi/2048, 512 = the default). Both spellings are configurable; the angle
 * cannot express most integer scales, so it is the wrong tool for matching a
 * reference projection and the right one for a free camera. It wins over
 * TORIRS_WEDGE_SCALE when both are set, since it is the more explicit request.
 */
static int
app_world_fov_override(void)
{
    static int cached = -2;
    if( cached == -2 )
        cached = ToriRS_EnvFovOverride(
            getenv("TORIRS_WORLD_FOV"),
            TORIDRAW_PROJECTION_FOV_MIN,
            TORIDRAW_PROJECTION_FOV_MAX);
    return cached;
}

/* 0 = auto (recompute — the default), -1 = forced off (legacy constant 512),
 * >0 = forced linear scale. */
static int
app_wedge_scale_mode(void)
{
    static int cached = -2;
    if( cached == -2 )
        cached = ToriRS_EnvScaleMode(getenv("TORIRS_WEDGE_SCALE"));
    return cached;
}

static void
app_apply_wedge_scale(struct App* app)
{
    int near_zoom = app->host.viewport_zoom_near;
    int far_zoom = app->host.viewport_zoom_far;
    struct ToriRS_ViewportProjection projection;

    /* TORIRS_WEDGE_ZOOM=<near>,<far> overrides the cache's SETFOV endpoints,
     * which is how the band itself gets bisected. */
    {
        char const* zoom_spec = torirs_env_wedge_zoom();
        int spec_near;
        int spec_far;

        if( zoom_spec && sscanf(zoom_spec, "%d,%d", &spec_near, &spec_far) == 2 )
        {
            near_zoom = spec_near;
            far_zoom = spec_far;
        }
    }

    projection = ToriRS_ProjectionForViewport(
        app_wedge_scale_mode(),
        app_world_fov_override(),
        app->revconfig_profile.camera.viewport_zoom,
        app->world_view_valid,
        app->world_emit_desc.h,
        near_zoom,
        far_zoom);

    switch( projection.action )
    {
    case TORIRS_VIEWPORT_PROJECTION_KEEP:
        return;

    case TORIRS_VIEWPORT_PROJECTION_FOV:
        app->world_camera.projection_mode = TORIDRAW_PROJECTION_MODE_FOV;
        app->world_camera.fov_rpi2048 = projection.fov_rpi2048;
        if( torirs_env_wedge_fov_debug() )
        {
            static int logged = 0;
            if( !logged )
            {
                logged = 1;
                TORIRS_LOG(
                    "wedge: fov mode fov_rpi2048=%d -> realised scale %d\n",
                    projection.fov_rpi2048,
                    toridraw_projection_scale_from_cot16(
                        toridraw_projection_cot16_from_fov(projection.fov_rpi2048)));
            }
        }
        return;

    case TORIRS_VIEWPORT_PROJECTION_SCALE:
        /* Exact: the kernels multiply by projection_scale directly. */
        app->world_camera.projection_mode = TORIDRAW_PROJECTION_MODE_SCALE;
        app->world_camera.projection_scale = projection.scale;
        if( torirs_env_wedge_fov_debug() )
        {
            static int last = -1;
            if( projection.scale != last )
            {
                last = projection.scale;
                TORIRS_ERR(
                    "wedge: vp_h=%d zoom(near=%d far=%d)=%d -> scale=%d realised=%d\n",
                    app->world_emit_desc.h,
                    projection.near_zoom,
                    projection.far_zoom,
                    projection.zoom,
                    projection.scale,
                    toridraw_projection_scale_from_cot16(toridraw_projection_cot16(
                        app->world_camera.projection_mode,
                        app->world_camera.projection_scale,
                        app->world_camera.fov_rpi2048)));
            }
        }
        return;
    }
}

void
app_update_world_viewport(struct App* app)
{
    app_debug_log_position(app);
    app_debug_log_bridges(app);
    app_debug_height_profile(app);
    app_debug_tile_project(app);
    app_debug_tile_flags(app);
    app->world_view_valid = 0;
    MinimapView_Invalidate(&app->minimap);
    if( torirs_env_world_view_debug() )
    {
        int kinds[24] = { 0 };
        for( int i = 0; i < app->emit.count; i++ )
            if( app->emit.cmds[i].kind >= 0 && app->emit.cmds[i].kind < 24 )
                kinds[app->emit.cmds[i].kind]++;
        TORIRS_LOG(
            "worldview: node_index=%d emit_count=%d kinds:",
            App_WorldNodeIndex(app),
            app->emit.count);
        for( int k = 0; k < 24; k++ )
            if( kinds[k] )
                TORIRS_LOG(" %d:%d", k, kinds[k]);
        int wx = 0, wy = 0, ww = 0, wh = 0, widx = -1;
        for( int i = 0; i < app->emit.count; i++ )
            if( app->emit.cmds[i].kind == UITREE_EMIT_WORLD )
            {
                wx = app->emit.cmds[i].x;
                wy = app->emit.cmds[i].y;
                ww = app->emit.cmds[i].w;
                wh = app->emit.cmds[i].h;
                widx = i;
            }
        TORIRS_LOG(" WORLDRECT=%d,%d %dx%d idx=%d\n", wx, wy, ww, wh, widx);
        /* Anything drawn after the world that covers a meaningful slice of it. */
        for( int i = widx + 1; i < app->emit.count; i++ )
        {
            struct UITreeEmitDesc* d = &app->emit.cmds[i];
            int ox = d->x > wx ? d->x : wx, oy = d->y > wy ? d->y : wy;
            int ex = (d->x + d->w) < (wx + ww) ? (d->x + d->w) : (wx + ww);
            int ey = (d->y + d->h) < (wy + wh) ? (d->y + d->h) : (wy + wh);
            int area = (ex - ox) > 0 && (ey - oy) > 0 ? (ex - ox) * (ey - oy) : 0;
            if( area > (ww * wh) / 20 )
                TORIRS_LOG(
                    "  occluder idx=%d kind=%d comp=%d node=%d rect=%d,%d %dx%d trans=%d "
                    "overlap=%d%%\n",
                    i,
                    d->kind,
                    d->component_id,
                    d->node_index,
                    d->x,
                    d->y,
                    d->w,
                    d->h,
                    d->trans,
                    area * 100 / (ww * wh));
        }
    }
    for( int i = 0; i < app->emit.count; i++ )
    {
        if( app->emit.cmds[i].kind == UITREE_EMIT_WORLD )
        {
            app->world_emit_desc = app->emit.cmds[i];
            app->world_view_valid = 1;
        }
        else if( app->emit.cmds[i].kind == UITREE_EMIT_MINIMAP )
        {
            MinimapView_Publish(
                &app->minimap,
                app->emit.cmds[i].x,
                app->emit.cmds[i].y,
                app->emit.cmds[i].w,
                app->emit.cmds[i].h,
                app->emit.cmds[i].rotation_r2pi2048);
        }
    }
    /* Recomputes the projection scale from the world viewport height, the
     * reference behaviour (TORIRS_WEDGE_SCALE=off reverts to constant 512). */
    app_apply_wedge_scale(app);
}

int32_t
App_WorldNodeIndex(struct App const* app)
{
    assert(app);
    assert(app->tree);
    return app->tree->world_index;
}

int
app_world_drawable(struct App* app)
{
    /* world_view_valid == a WORLD desc survived the last emit walk, so a hidden
     * or absent viewport component costs nothing: no paint, no 3D, no pick.
     * During a server-driven rebuild (deob gameState 25 / Client-TS sceneState
     * 1) suppress the world so mid-load frames are not a frozen wrong scene —
     * App_Render draws the "Loading - please wait." overlay instead. */
    return app->world_view_valid && app->world && app->world->load_complete &&
           app->world->painter && app->painter_buffer &&
           !(app->world_load_server_driven && app->world_load_inflight);
}

/* deob method5761 / Client-TS REBUILD_NORMAL: black fill of the game area plus
 * centred "Loading - please wait." while maps rebuild. */
static void
app_draw_viewport_message(
    struct App* app,
    int* pixels,
    int width,
    int height,
    char const* line1,
    char const* line2_nullable,
    int fill_black)
{
    struct ToriDraw_Font* font;
    struct ToriDraw_ViewPort vp;
    int font_cache_id;
    int scene_id;
    int vx, vy, vw, vh;
    int cx, cy;

    assert(app);
    assert(pixels);
    if( width <= 0 || height <= 0 || !line1 )
        return;

    if( app->world_view_valid )
    {
        vx = app->world_emit_desc.x;
        vy = app->world_emit_desc.y;
        vw = app->world_emit_desc.w;
        vh = app->world_emit_desc.h;
    }
    else
    {
        vx = 0;
        vy = 0;
        vw = width;
        vh = height;
    }
    if( vw <= 0 || vh <= 0 )
        return;

    /*
     * Whether the world underneath survives is the caller's call, and the two
     * callers differ. A scene rebuild blacks it out because the scene it was
     * drawn from is gone (deob method5761). A lost connection does not: the
     * reference paints its two lines straight onto the retained viewport, and
     * the last frame the session produced is exactly what a player wants to
     * still be looking at while it comes back.
     */
    if( fill_black )
    {
        for( int y = vy; y < vy + vh; y++ )
        {
            if( y < 0 || y >= height )
                continue;
            for( int x = vx; x < vx + vw; x++ )
            {
                if( x < 0 || x >= width )
                    continue;
                pixels[y * width + x] = 0x000000;
            }
        }
    }

    font_cache_id = app_font_cache_id(app, APP_FONT_P12);
    scene_id = font_cache_id >= 0 ? UITreeSceneBridge_EnsureFont(&app->bridge, font_cache_id) : -1;
    if( scene_id < 0 )
    {
        if( font_cache_id >= 0 )
        {
            struct ToriRS_Task* task = CreateTask_FontLoad(app->provider, font_cache_id);
            if( task )
                ToriRS_TaskQueue_Add(app->runner.queue, task);
        }
        scene_id = app_minimenu_font_scene_id(app);
    }
    if( scene_id < 0 )
        return;
    font = ToriDraw_SceneFontGet(app->scene, scene_id);
    if( !font )
        return;

    vp.width = width;
    vp.height = height;
    vp.stride = width;
    vp.x_center = width / 2;
    vp.y_center = height / 2;
    vp.clip_left = vx < 0 ? 0 : vx;
    vp.clip_top = vy < 0 ? 0 : vy;
    vp.clip_right = (vx + vw > width) ? width : (vx + vw);
    vp.clip_bottom = (vy + vh > height) ? height : (vy + vh);

    cx = vx + vw / 2;
    cy = vy + vh / 2;
    /* Two lines straddle the centre by the reference's own 15px step
     * (143/158 against a 503-tall viewport); one line sits on it. */
    if( line2_nullable )
        cy -= 8;
    /* Shadow then white — matches deob black+white centreString pair. */
    (void)ToriDraw2D_DrawString(font, &vp, cx + 1, cy + 1, line1, 0x000000, true, false, pixels);
    (void)ToriDraw2D_DrawString(font, &vp, cx, cy, line1, 0xffffff, true, false, pixels);
    if( line2_nullable )
    {
        (void)ToriDraw2D_DrawString(
            font, &vp, cx + 1, cy + 16, line2_nullable, 0x000000, true, false, pixels);
        (void)ToriDraw2D_DrawString(
            font, &vp, cx, cy + 15, line2_nullable, 0xffffff, true, false, pixels);
    }
}

/* deob method5761 / Client-TS REBUILD_NORMAL: while the scene rebuilds, the
 * game area shows "Loading - please wait." instead of the world. */
void
app_draw_rebuild_loading_overlay(
    struct App* app,
    int* pixels,
    int width,
    int height)
{
    app_draw_viewport_message(
        app, pixels, width, height, "Loading - please wait.", NULL, /* fill_black */ 1);
}

/*
 * The reference's lost-connection notice (Client-TS `lostCon`, Client.ts:2739;
 * deob gameState 40, client.java:8542), over the retained viewport.
 *
 * Two lines, because they answer two different questions: what happened, and
 * whether the player has to do anything about it. Once the attempts are spent
 * the second line stops promising a reconnect that is no longer coming.
 */
void
app_draw_connection_lost_overlay(
    struct App* app,
    int* pixels,
    int width,
    int height)
{
    app_draw_viewport_message(
        app,
        pixels,
        width,
        height,
        "Connection lost",
        NetLinkWatch_GaveUp(&app->net_link) ? "Unable to reestablish - please reload"
                                  : "Please wait - attempting to reestablish",
        /* fill_black */ 0);
}
