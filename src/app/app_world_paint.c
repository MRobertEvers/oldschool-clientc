/*
 * Painting the world: the roof check, the painter cull, and the paint itself.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static int
app_world_roof_check(struct App* app);
static void
app_update_painter_cull(
    struct App* app,
    int cam_sx,
    int cam_sz);

/* Reference roofCheck (Client.ts 4713): walk the camera->player tile line;
 * if any stepped tile (endpoints included) carries the remove-roof land flag
 * (0x4), cut drawing down to the player's level, else draw all 4. Only armed
 * at low pitch (< 310) — the high look-down orbit clears roofs anyway.
 * Scripted cams use the simpler roofCheck2 height test. */
static int
app_world_roof_check(struct App* app)
{
    struct WorldEntity_Player* player = app_local_player(app);
    struct World* world = app->world;
    int top = 3;
    int level;

    if( !player || !world || !world->tile_flags )
        return 3;
    level = player->grid_position.level;

    /* Aboard, the player's coordinates are deck-local — the raster walk below
     * would march toward a tile near the scene corner and read roof flags
     * that are not over anyone. The deob flips to sub-view roof rules while
     * aboard; a hull on open water has no roofs to remove, so show all. */
    if( app->aboard_view != WORLDVIEW_ROOT )
        return 3;

    /*
     * "Hide roofs" — game option 1, and the first thing both of the reference's
     * roof checks test (`if (!prefs.isHidingRoofs())` guards the whole selective
     * walk in each; when it is set they return the player's level outright).
     * The Display panel's toggle and the reference's ::toggleroof cheat write
     * this same setting, whose two messages say what the two states are:
     * "Roofs are now all hidden" against "Roofs will only be removed
     * selectively".
     */
    if( RS_CS2Host_GetOption(&app->host, RS_CS2_OPTION_GAME, RS_CS2_GAMEOPTION_HIDE_ROOFS) )
        return level;

    if( app->cam_script.scripted )
    {
        int cam_tx = app->world_camera_pos.x >> 7;
        int cam_tz = app->world_camera_pos.z >> 7;
        int ground_y =
            app_world_height(app, app->world_camera_pos.x, app->world_camera_pos.z, level);
        if( ground_y - app->world_camera_pos.y >= 800 ||
            (World_TileFlagGet(world, cam_tx, cam_tz, level) & 0x4) == 0 )
            return 3;
        return level;
    }

    /* Below this pitch the camera is low enough to be looking THROUGH the
     * scene rather than down onto it, so the sightline decides. Above it,
     * only the tile the player is standing on does. */
    if( app->world_camera.pitch < 310 )
        top = World_RoofLevelAlongLine(
            world,
            level,
            app->world_camera_pos.x >> 7,
            app->world_camera_pos.z >> 7,
            (int)player->draw_position.x >> 7,
            (int)player->draw_position.z >> 7);

    if( World_TileFlagGet(
            world, (int)player->draw_position.x >> 7, (int)player->draw_position.z >> 7, level) &
        0x4 )
        top = level;
    return top;
}

/* Update painter frustum cull for the current camera. Default path builds a
 * per-frame analytic span from live eye height (zoom-aware). TORIRS_PAINTER_CULL=baked
 * restores the old CPU-baked table. TORIRS_PAINTER_NOCULL=1 disables both. */
static void
app_update_painter_cull(
    struct App* app,
    int cam_sx,
    int cam_sz)
{
    struct World* world;
    struct Painter* painter;
    char const* nocull;
    char const* cull_mode;
    int follow_cam;
    int vw;
    int vh;
    int near_z;
    int far_z;
    int radius;
    int center_sx;
    int center_sz;
    int eye_height;
    int level;
    int anchor_x;
    int anchor_z;
    struct PaintersCullSpan span;
    struct PaintersCullSpanParams params;

    assert(app);
    world = app->world;
    if( !world || !world->painter )
        return;
    painter = world->painter;
    {
        char const* dd = torirs_env_draw_distance();
        int v = ToriRS_Features_PainterDrawDistance(app->features);
        int from_env;
        if( dd && dd[0] != '\0' && sscanf(dd, "%d", &from_env) == 1 )
            v = from_env;
        painter_set_draw_distance(painter, v);
    }
    radius = painter_get_draw_distance(painter);

    nocull = torirs_env_painter_nocull();
    if( nocull && nocull[0] != '\0' && nocull[0] != '0' )
    {
        painter_set_cullspan(painter, NULL);
        if( !world->cullmap || !world->cullmap->all_visible )
        {
            if( world->cullmap )
                painters_cullmap_free(world->cullmap);
            world->cullmap = painters_cullmap_new_nocull();
            painter_set_cullmap(painter, world->cullmap);
        }
        painter_set_draw_center(painter, -1, -1);
        return;
    }

    if( !app->world_view_valid )
        return;
    vw = app->world_emit_desc.w;
    vh = app->world_emit_desc.h;
    if( vw < 1 || vh < 1 )
        return;

    /* The painter's draw box is centred on the EYE tile — the official does
     * this unconditionally (class112.method4111 derives the window from
     * field1755/field1765, the camera tile). Centring on the orbit anchor
     * instead put nine extra z rows behind the camera in the box and dropped
     * nine near ones, which is §9.7(b)'s share of the Inferno wedge
     * (docs/ORANGE_WEDGE.md, promoted per §11.7).
     * TORIRS_WEDGE_DRAWCENTER=orbit restores the old behaviour for A/B. */
    follow_cam = 0;
    {
        char const* dc = torirs_env_wedge_drawcenter();
        if( dc && strcmp(dc, "orbit") == 0 )
            follow_cam = app->net && !app->cam_script.scripted;
    }
    if( follow_cam )
    {
        /* (int) then >>7, as the reference does: field2354 is `(int) field917`
         * and the camera tile is that mirror shifted (client.java:9373). */
        center_sx = (int)app->orbit.anchor_x >> 7;
        center_sz = (int)app->orbit.anchor_z >> 7;
        anchor_x = (int)app->orbit.anchor_x;
        anchor_z = (int)app->orbit.anchor_z;
        painter_set_draw_center(painter, center_sx, center_sz);
    }
    else
    {
        center_sx = cam_sx;
        center_sz = cam_sz;
        anchor_x = app->world_camera_pos.x;
        anchor_z = app->world_camera_pos.z;
        painter_set_draw_center(painter, -1, -1);
    }

    level = 0;
    {
        struct WorldEntity_Player* player = World_PlayerGetByServerPid(world, world->local_pid);
        if( player )
            level = player->grid_position.level;
    }
    eye_height = app_world_height(app, anchor_x, anchor_z, level) - app->world_camera_pos.y;

    near_z = app->world_camera.near_plane_z;
    if( near_z < 1 )
        near_z = 50;
    /* Far clip = drawDistance * 210 (deob class243.method4457). Covers the
     * radius box diagonal (radius * 128 * sqrt(2)) at both 25 and 90. */
    far_z = radius * OCCLUDER_FAR_CLIP_PER_TILE;

    cull_mode = torirs_env_painter_cull();
    if( cull_mode && strcmp(cull_mode, "baked") == 0 )
    {
        struct PaintersCullMap* cm = NULL;
        int slice_n;
        struct timespec t0;
        struct timespec t1;
        uint64_t bake_ns;

        painter_set_cullspan(painter, NULL);

        /* Debounce viewport resize: the CPU bake is multi-hundred-ms. */
        if( app->painter_cullmap_bake_w > 0 && app->painter_cullmap_bake_h > 0 )
        {
            int dw = vw - app->painter_cullmap_bake_w;
            int dh = vh - app->painter_cullmap_bake_h;
            if( dw < 0 )
                dw = -dw;
            if( dh < 0 )
                dh = -dh;
            if( dw < 8 && dh < 8 )
                return;
        }

        {
            struct ToriDrawTrigTables tables = {
                .sin = ToriDraw_GetSinTable(),
                .cos = ToriDraw_GetCosTable(),
                .tan = ToriDraw_GetTanTable(),
            };
            struct ToriDrawTrigFns trig;
            ToriDraw_TrigFnsFromTables(&trig, &tables);

            clock_gettime(CLOCK_MONOTONIC, &t0);
            cm = painters_cullmap_build_toridraw(
                radius,
                near_z,
                vw,
                vh,
                toridraw_projection_cot16(
                    app->world_camera.projection_mode,
                    app->world_camera.projection_scale,
                    app->world_camera.fov_rpi2048),
                &trig);
            clock_gettime(CLOCK_MONOTONIC, &t1);
        }
        if( !cm )
            return;

        bake_ns =
            (uint64_t)(t1.tv_sec - t0.tv_sec) * 1000000000ull + (uint64_t)(t1.tv_nsec - t0.tv_nsec);

        slice_n = painters_cullmap_slice_visible_count(
            cm, app->world_camera.pitch, app->world_camera.yaw);
        if( slice_n <= 0 )
        {
            TORIRS_LOG(
                "painter_cullmap: bake empty for pitch=%d yaw=%d near=%d %dx%d "
                "(%.2f ms) — keeping nocull\n",
                app->world_camera.pitch,
                app->world_camera.yaw,
                near_z,
                vw,
                vh,
                (double)bake_ns / 1.0e6);
            painters_cullmap_free(cm);
            if( !world->cullmap || !world->cullmap->all_visible )
            {
                if( world->cullmap )
                    painters_cullmap_free(world->cullmap);
                world->cullmap = painters_cullmap_new_nocull();
                painter_set_cullmap(painter, world->cullmap);
            }
            app->painter_cullmap_bake_w = vw;
            app->painter_cullmap_bake_h = vh;
            return;
        }

        TORIRS_LOG(
            "painter_cullmap: baked radius=%d near=%d %dx%d slice_vis=%d in %.2f ms\n",
            radius,
            near_z,
            vw,
            vh,
            slice_n,
            (double)bake_ns / 1.0e6);

        if( world->cullmap )
            painters_cullmap_free(world->cullmap);
        world->cullmap = cm;
        painter_set_cullmap(painter, world->cullmap);
        app->painter_cullmap_bake_w = vw;
        app->painter_cullmap_bake_h = vh;
        return;
    }

    /* Default: analytic per-frame span. Keep a nocull cullmap installed so any
     * leftover bit-test path is a no-op. */
    if( !world->cullmap || !world->cullmap->all_visible )
    {
        if( world->cullmap )
            painters_cullmap_free(world->cullmap);
        world->cullmap = painters_cullmap_new_nocull();
        painter_set_cullmap(painter, world->cullmap);
    }

    params.pitch = app->world_camera.pitch;
    params.yaw = app->world_camera.yaw & 0x7ff;
    params.eye_height = eye_height;
    params.y_lo = PCULL_FRUSTUM_Y_START;
    params.y_hi = PCULL_FRUSTUM_Y_END;
    params.near_clip = near_z;
    params.far_clip = far_z;
    params.screen_width = vw;
    params.screen_height = vh;
    /* Same values the frame will be drawn with; the cull frustum must not
     * assume a different projection scale than the rasterizer uses. */
    params.projection_mode = app->world_camera.projection_mode;
    params.projection_scale = app->world_camera.projection_scale;
    params.fov_rpi2048 = app->world_camera.fov_rpi2048;
    /* Eye-relative row range covering the draw box around the orbit centre. */
    params.dz_min = (center_sz - radius) - cam_sz - 2;
    params.dz_max = (center_sz + radius) - cam_sz + 2;
    if( params.dz_min < -PAINTERS_CULLSPAN_MAX_DZ )
        params.dz_min = -PAINTERS_CULLSPAN_MAX_DZ;
    if( params.dz_max > PAINTERS_CULLSPAN_MAX_DZ )
        params.dz_max = PAINTERS_CULLSPAN_MAX_DZ;

    painters_cullspan_build(&span, &params);
    painter_set_cullspan(painter, &span);
    (void)center_sx;
}

void
app_world_paint(struct App* app)
{
    /* TORIRS_WEDGE_CAM=x,y,z,pitch,yaw — pin the eye so a draw-order capture can
     * be taken at the same camera as the instrumented official client. Pitch/yaw
     * are the C client's 2048-per-turn units (the official's 16384-per-turn value
     * divided by 8). Off unless the env var is set; when set it is re-applied every
     * frame *before* anything reads the camera, so the painter, the occluders and
     * the frame the renderer draws all agree. Ordering telemetry is worthless if
     * the two clients look from different places (the C settled eye is two tiles
     * farther in z than the official's, and the bucket traversal is centred on the
     * camera tile). */
    {
        static int resolved = 0;
        static int have = 0;
        static int have_path = 0;
        static int px, py, pz, ppitch, pyaw;
        static struct ToriRS_WedgeCameraPath path;
        if( !resolved )
        {
            char const* wc = getenv("TORIRS_WEDGE_CAM");
            char const* wp = getenv("TORIRS_WEDGE_CAM_PATH");
            resolved = 1;
            /* The path wins where a scene sets both: TORIRS_WEDGE_CAM is then
             * the still the route was authored from, and the route is the
             * thing being measured. */
            if( wp && wp[0] )
            {
                if( ToriRS_WedgeCameraPathParse(wp, &path) )
                    have_path = 1;
                else
                    /* Once, at resolve time, never per frame. Loud because the
                     * alternative is a mistyped route silently measuring a
                     * still camera and reporting a number that looks fine. */
                    TORIRS_ERR(
                        "TORIRS_WEDGE_CAM_PATH: cannot parse, camera not "
                        "moving: %s\n",
                        wp);
            }
            if( wc && sscanf(wc, "%d,%d,%d,%d,%d", &px, &py, &pz, &ppitch, &pyaw) == 5 )
                have = 1;
        }
        if( have_path )
        {
            struct ToriRS_WedgeCameraKey key;
            ToriRS_WedgeCameraPathEval(&path, g_torirs_frame_no, &key);
            app->world_camera_pos.x = key.x;
            app->world_camera_pos.y = key.y;
            app->world_camera_pos.z = key.z;
            app->world_camera.pitch = key.pitch;
            app->world_camera.yaw = key.yaw;
        }
        else if( have )
        {
            app->world_camera_pos.x = px;
            app->world_camera_pos.y = py;
            app->world_camera_pos.z = pz;
            app->world_camera.pitch = ppitch;
            app->world_camera.yaw = pyaw;
        }
    }

    /* >>7, not /128: the orbit eye can sit at negative coords past the scene
     * edge, and truncation toward zero would mis-seed the bucket flood-fill
     * origin by a tile. Clamp into the scene — the bucket's distance metric
     * and adjacency tests assume an in-bounds origin. */
    int cam_sx = app->world_camera_pos.x >> 7;
    int cam_sz = app->world_camera_pos.z >> 7;
    int cam_slevel = 0; /* painter_paint_bucket ignores it (iterates levels) */
    if( app->world )
    {
        int max_tile = app->world->_scene_size - 1;
        if( cam_sx < 0 )
            cam_sx = 0;
        if( cam_sx > max_tile )
            cam_sx = max_tile;
        if( cam_sz < 0 )
            cam_sz = 0;
        if( cam_sz > max_tile )
            cam_sz = max_tile;
    }
    /* The viewport component owns the level mask (RevConfig `levels=`); older
     * nodes leave it 0, which would draw nothing — treat that as all levels. */
    uint8_t level_mask = app->world_emit_desc.world_level_mask;
    if( cam_slevel < 0 )
        cam_slevel = 0;
    if( cam_slevel > 3 )
        cam_slevel = 3;
    if( !level_mask )
        level_mask = 0xF;
    /* Roof hiding: the per-frame camera->player roofCheck caps the top drawn
     * level; config (RevConfig levels=) can still restrict further. */
    level_mask &= (uint8_t)((1u << (app_world_roof_check(app) + 1)) - 1);
    /* The map editor's Vis row REPLACES the mask rather than narrowing it: the
     * point of the row is to see a plane the ordinary rules would hide, and an
     * AND could only ever take levels away. 0 is "all levels", the default,
     * and leaves everything above untouched. */
    uint8_t editor_vis_mask = 0;
    if( app->editor )
    {
        editor_vis_mask = Editor_PanelVisLevelMask(&app->editor_panel);
        if( editor_vis_mask )
            level_mask = editor_vis_mask;
    }
    /* CAM_SHAKE jitter (reference Client-TS 4448): each axis is a sine plus a
     * random spread, and all five compound. It displaces the camera for this
     * frame's draw only — the base position is restored below, or the y axis
     * would ratchet the eye away a little more every frame. */
    int shake_x = app->world_camera_pos.x;
    int shake_y = app->world_camera_pos.y;
    int shake_z = app->world_camera_pos.z;
    int shake_pitch = app->world_camera.pitch;
    int shake_yaw = app->world_camera.yaw;
    for( int axis = 0; axis < 5; axis++ )
    {
        int spread, jitter;
        if( !app->cam_script.shake[axis] )
            continue;
        spread = app->cam_script.shake_jitter[axis];
        jitter = (int)((double)rand() / ((double)RAND_MAX + 1.0) * (spread * 2 + 1)) - spread;
        jitter += (int)(sin((double)app->cam_script.shake_cycle[axis] *
                            ((double)app->cam_script.shake_speed[axis] / 100.0)) *
                        app->cam_script.shake_amplitude[axis]);
        switch( axis )
        {
        case 0:
            app->world_camera_pos.x += jitter;
            cam_sx = app->world_camera_pos.x >> 7;
            break;
        case 1:
            app->world_camera_pos.y += jitter;
            break;
        case 2:
            app->world_camera_pos.z += jitter;
            cam_sz = app->world_camera_pos.z >> 7;
            break;
        case 3:
            app->world_camera.yaw = (app->world_camera.yaw + jitter) & 0x7ff;
            break;
        case 4:
            app->world_camera.pitch = app_world_clamp_pitch(app, app->world_camera.pitch + jitter);
            break;
        default:
            break;
        }
        app->cam_script.shake_cycle[axis]++;
    }
    if( app->world )
    {
        int max_tile = app->world->_scene_size - 1;
        if( cam_sx < 0 )
            cam_sx = 0;
        if( cam_sx > max_tile )
            cam_sx = max_tile;
        if( cam_sz < 0 )
            cam_sz = 0;
        if( cam_sz > max_tile )
            cam_sz = max_tile;
    }
    painter_set_camera_angles(app->world->painter, app->world_camera.pitch, app->world_camera.yaw);
    painter_set_level_mask(app->world->painter, level_mask);

    /* World entities (SAILING_PLAN C3): the eye is only final here — WEDGE_CAM
     * has been applied and the shake added, and both are undone right after the
     * paint — so this is the one place a boat-space camera can be derived.
     * Once per boat per frame, not once per descent. */
    app_wev_bind_view_cameras(
        app,
        app->world_camera.pitch,
        app->world_camera.yaw,
        app->world_camera_pos.x,
        app->world_camera_pos.y,
        app->world_camera_pos.z);

    app_update_painter_cull(app, cam_sx, cam_sz);

    /* Planar occluders: project shadows for this eye. TORIRS_OCCLUDERS=0
     * disables (mirrors TORIRS_PAINTER_NOCULL=1). Default is on. */
    {
        struct SceneOccluders* occ = painter_get_occluders(app->world->painter);
        const char* env_occ = torirs_env_occluders();
        int occ_off = env_occ && env_occ[0] == '0' && env_occ[1] == '\0';
        if( occ && !occ_off )
        {
            int top_level = app_world_roof_check(app);
            /* The occluder set is bucketed by the top level being drawn --
             * bucket N holds every surface spanning levels 0..N. The Vis row
             * replaced the painter's level mask above, so the bucket has to
             * follow it: left at the roof-check level, the floors and roofs of
             * the storeys the row just hid keep casting their shadows and cull
             * the plane the user asked to look at. Highest set bit of the mask
             * is the top level it draws (solo included -- bucket N is the only
             * one whose surfaces can be in front of level N's geometry). */
            if( editor_vis_mask )
            {
                top_level = 0;
                while( (editor_vis_mask >> (top_level + 1)) != 0 )
                    top_level++;
            }
            /* Eye for depth/spread stays raw (deob cameraX/Y/Z); only the
             * camera tile used by the footprint gate is scene-clamped inside
             * select_for_camera so it lines up with the painter's cam_sx/sz. */
            scene_occluders_select_for_camera(
                occ,
                app->world_camera_pos.x,
                app->world_camera_pos.y,
                app->world_camera_pos.z,
                top_level,
                painter_get_draw_distance(app->world->painter),
                painter_get_cullspan(app->world->painter),
                NULL,
                app->world_camera.pitch,
                app->world_camera.yaw);
            if( torirs_env_occluders_debug() )
            {
                static int s_logged;
                if( !s_logged )
                {
                    int n_wall = 0;
                    int n_floor = 0;
                    int i;
                    s_logged = 1;
                    for( i = 0; i < occ->level_occluder_count[top_level]; i++ )
                    {
                        uint8_t p = occ->level_occluders[top_level][i].plane;
                        if( p == OCCLUDER_PLANE_CONSTANT_Y )
                            n_floor++;
                        else
                            n_wall++;
                    }
                    TORIRS_LOG(
                        "occluders: top=%d built_wall=%d built_floor=%d active=%d "
                        "eye=(%d,%d,%d) cam_tile=(%d,%d)\n",
                        top_level,
                        n_wall,
                        n_floor,
                        occ->active_count,
                        app->world_camera_pos.x,
                        app->world_camera_pos.y,
                        app->world_camera_pos.z,
                        occ->camera_sx,
                        occ->camera_sz);
                }
            }
        }
        else if( occ && occ_off )
        {
            occ->active_count = 0;
        }
    }

    /* Draw-order telemetry (TORIRS_WEDGELOG): hand the painter the eye and world
     * viewport it is about to paint with, for the log header. No-op otherwise. */
    PAINTER_DBG_WEDGE_SET_EYE(
        app->world_camera_pos.x,
        app->world_camera_pos.y,
        app->world_camera_pos.z,
        app->world_view_valid ? app->world_emit_desc.w : 0,
        app->world_view_valid ? app->world_emit_desc.h : 0);

    if( app->world_render_mode == TORIRS_WORLD_DEPTH )
        painter_collect_visible_depth(
            app->world->painter, app->painter_buffer, cam_sx, cam_sz, cam_slevel);
    /* TORIRS_PAINTER_W3D=1 runs the reference cascade (painter_paint_world3d)
     * in the live client instead of the distance-bucket drain. A draw-order
     * bug is either in the traversal or in the geometry it orders, and this is
     * what separates the two: same scene, same frame, the other painter. Pair
     * it with TORIRS_PIXOWNER to name what changed hands, or
     * TORIRS_PAINTER_ALT=1 + TORIRS_BMP_SERIES for a same-frame image pair. */
    else if(
        g_torirs_painter_force == 1 || (g_torirs_painter_force == 0 && torirs_env_painter_w3d()) )
        painter_paint_world3d(app->world->painter, app->painter_buffer, cam_sx, cam_sz, cam_slevel);
    else
        painter_paint_bucket(app->world->painter, app->painter_buffer, cam_sx, cam_sz, cam_slevel);

    if( app->world_render_mode != TORIRS_WORLD_DEPTH )
        app_wev_order_parent_ground(app);

    app->world_camera_pos.x = shake_x;
    app->world_camera_pos.y = shake_y;
    app->world_camera_pos.z = shake_z;
    app->world_camera.pitch = shake_pitch;
    app->world_camera.yaw = shake_yaw;

    /* TORIRS_PAINT_DEBUG: what the painter actually emitted this frame, by kind.
     * Scene elements existing is not the same as being painted — the bucket
     * flood-fill, the level mask and the cull map each drop work silently. */
    if( torirs_env_paint_debug() )
    {
        int by_kind[16] = { 0 };
        for( int i = 0; i < app->painter_buffer->command_count; i++ )
            by_kind[app->painter_buffer->commands[i]._bf_kind & 0xF]++;
        TORIRS_LOG(
            "paint: cam=%d,%d campos=(%d,%d,%d) pitch=%d yaw=%d level_mask=0x%x roof=%d "
            "commands=%d kinds:",
            cam_sx,
            cam_sz,
            (int)app->world_camera_pos.x,
            (int)app->world_camera_pos.y,
            (int)app->world_camera_pos.z,
            app->world_camera.pitch,
            app->world_camera.yaw,
            level_mask,
            app_world_roof_check(app),
            app->painter_buffer->command_count);
        for( int k = 0; k < 16; k++ )
            if( by_kind[k] )
                TORIRS_LOG(" %d:%d", k, by_kind[k]);
        TORIRS_LOG("\n");
    }

    /* TORIRS_TILETABLE=x0,x1,z0,z1: one row per (tile, cache level) joining
     * everything that decides when a ground mesh reaches the screen —
     *
     *   flags     the raw floor settings byte (VIS_BELOW 0x08 is the one that
     *             moves a mesh onto another level's pass)
     *   elem      the terrain element id, or -1 when the tile has no geometry
     *             at all; a -1 row can carry any flag and still draw nothing
     *   set       PaintersTile::terrain_levels, the meshes this level emits
     *   order     the command-buffer index the mesh landed at, or "-" when it
     *             was never emitted
     *
     * The point is the join: flags alone say what *should* happen, the emit
     * order alone says what did, and only the two side by side say whether a
     * tile that looks wrong on screen is mis-flagged, mis-ordered, or simply
     * has no mesh to move. Prints once, after the paint that produced it. */
    {
        static int done = 0;
        static int paints = 0;
        const char* env = torirs_env_tiletable();
        /* Wait for the paint the caller means. The table is only interesting
         * after a teleport into an instance, and printing on the first paint
         * silently describes wherever the player logged in — the same trap that
         * made TORIRS_HPROF report Lumbridge's relief for the arena. */
        const char* at = torirs_env_tiletable_at();
        int want_paint = at ? atoi(at) : 600;
        int x0, x1, z0, z1;
        paints++;
        if( env && !done && paints >= want_paint &&
            sscanf(env, "%d,%d,%d,%d", &x0, &x1, &z0, &z1) == 4 )
        {
            done = 1;
            TORIRS_LOG("tile     lvl flags elem  set order\n");
            for( int z = z0; z <= z1; z++ )
                for( int x = x0; x <= x1; x++ )
                    for( int lv = 0; lv < WORLD_MAP_TERRAIN_LEVELS; lv++ )
                    {
                        int order = -1;
                        /* Count, do not stop at the first: "the mesh is drawn
                         * once, from the level that owns it" is only provable
                         * by showing there is no second emission. A search that
                         * breaks on the first hit reports a double-draw and a
                         * single draw identically. */
                        int order_count = 0;
                        for( int i = 0; i < app->painter_buffer->command_count; i++ )
                        {
                            struct PaintersElementCommand* c = &app->painter_buffer->commands[i];
                            if( c->_bf_kind != PNTR_CMD_TERRAIN )
                                continue;
                            if( (int)c->_terrain._bf_terrain_x != x ||
                                (int)c->_terrain._bf_terrain_z != z ||
                                (int)c->_terrain._bf_terrain_y != lv )
                                continue;
                            if( order < 0 )
                                order = i;
                            order_count++;
                        }
                        TORIRS_LOG(
                            "%3d,%-3d  L%d  0x%02x %5d 0x%x ",
                            x,
                            z,
                            lv,
                            (unsigned)World_TileFlagGet(app->world, x, z, lv),
                            World_TerrainElementAt(app->world, x, z, lv),
                            painter_tile_get_terrain_levels(app->world->painter, x, z, lv));
                        if( order < 0 )
                            TORIRS_LOG("-\n");
                        else if( order_count > 1 )
                            TORIRS_LOG("%d  DRAWN %dx\n", order, order_count);
                        else
                            TORIRS_LOG("%d\n", order);
                    }
            /* The other half of the comparison. A terrain order is only
             * meaningful against the scenery it is supposed to be behind, and
             * both have to be read off the SAME buffer — the render-command
             * sequence TORIRS_DRAW_ORDER prints is a filtered renumbering, so
             * the two cannot be lined up across tools. */
            TORIRS_LOG("locs overlapping the rect (same numbering)\n");
            for( int i = 0; i < app->painter_buffer->command_count; i++ )
            {
                struct PaintersElementCommand* c = &app->painter_buffer->commands[i];
                struct WorldEntity_Scenery* sc;
                if( c->_bf_kind != PNTR_CMD_ELEMENT )
                    continue;
                sc = World_SceneryGetByElementId(app->world, painter_command_element_id(c));
                if( !sc )
                    continue;
                if( sc->grid_position.x < x0 - 8 || sc->grid_position.x > x1 + 8 ||
                    sc->grid_position.z < z0 - 8 || sc->grid_position.z > z1 + 8 )
                    continue;
                TORIRS_LOG(
                    "  order %5d loc=%-6d slot=%d,%d L%d size=%dx%d\n",
                    i,
                    sc->loc_id,
                    sc->grid_position.x,
                    sc->grid_position.z,
                    sc->grid_position.level,
                    sc->debug.draw_size_x,
                    sc->debug.draw_size_z);
            }
        }
    }
}
