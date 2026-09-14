/*
 * Loading a world: the load task, the minimap bake, and the post-load wiring.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

struct WevDeckBox;

/* The most squares one offline TORIRS_WORLD_MAP load may name. 16 is a 4x4
 * block, 256x256 tiles -- well past the 104x104 the live client keeps
 * resident, so the cap bounds the array without capping any scene worth
 * meshing. */
#define APP_WORLD_MAP_SQUARE_MAX 16

/* Private to this unit, declared up front so definition order is free. */
static void
app_bake_mapscenes(
    struct App* app,
    uint32_t* argb,
    int pw,
    int ph,
    int level);
static void
app_rebuild_world_map(
    struct App* app,
    int level);
static void
app_world_load_finish_cb(void* userdata);

/* Bind app-owned overlay models to their revision-configured nodes. No node
 * means that overlay does not exist for the revision; there is intentionally
 * no C fallback that changes the shape of the UITree. */
void
app_bind_configured_overlays(struct App* app)
{
    app->interact.minimenu.font_id = -1;
    app->hover_text.font_id = -1;
    for( uint32_t i = 0; i < app->tree->component_count; i++ )
    {
        struct UITreeComponent const* node = &app->tree->components[i];
        if( node->freed )
            continue;
        if( node->type == UIELEM_BUILTIN_MINIMENU )
            app->interact.minimenu.font_id = node->u.minimenu.font_id;
        else if( node->type == UIELEM_BUILTIN_HOVERTEXT )
            app->hover_text.font_id = node->u.hovertext.font_id;
    }
}

/* Reference drawDetail's mapscene pass: after the tile/wall bake, plot each loc
 * mapscene sprite gathered at scene build (world->mapscenes) for the level being
 * baked. The mapscene atlas lives in the scene, so the LOOKUP runs in app.c
 * rather than the leaf minimap layer; which icons belong on this level is
 * minimap_mapscene_draws_at_level's, next to the tile bake whose VisBelow
 * composition it has to agree with. */
static void
app_bake_mapscenes(
    struct App* app,
    uint32_t* argb,
    int pw,
    int ph,
    int level)
{
    struct World* world = app->world;
    int mapscene_scene;
    int count = 0;
    struct ToriDraw_Sprite** frames;
    int scene_size;
    uint8_t const* flags;

    if( !world || world->mapscene_count <= 0 || !world->minimap )
        return;
    mapscene_scene = UITreeSceneBridge_StaticSpriteSceneId(&app->bridge, STATIC_SPRITE_MAPSCENE);
    if( mapscene_scene <= 0 )
        return;
    frames = ToriDraw_SceneSpriteGet(app->scene, mapscene_scene, &count);
    if( !frames || count <= 0 )
        return;

    scene_size = world->_scene_size;
    flags = world->tile_flags;

    for( int i = 0; i < world->mapscene_count; i++ )
    {
        struct World_MapSceneIcon const* icon = &world->mapscenes[i];
        struct ToriDraw_Sprite* spr;

        if( icon->mapscene < 0 || icon->mapscene >= count )
            continue;
        if( !minimap_mapscene_draws_at_level(
                flags,
                scene_size,
                world->minimap->levels,
                level,
                icon->level,
                icon->x,
                icon->z) )
            continue;
        spr = frames[icon->mapscene];
        if( !spr || !spr->pixels_argb || spr->width <= 0 || spr->height <= 0 )
            continue;

        minimap_plot_mapscene(
            argb,
            pw,
            ph,
            spr->pixels_argb,
            spr->width,
            spr->height,
            spr->crop_x,
            spr->crop_y,
            icon->x,
            icon->z,
            world->minimap->height,
            icon->width,
            icon->length);
    }
}

/* Bake the loaded world's minimap tiles into a single scene sprite the minimap
 * widget blits from (v1 GameRunescape_RebuildWorldMap). SceneSpriteAdd frees any
 * previous entry, so the reload hotkey just overwrites in place.
 *
 * The bake is per level (reference minimapBuildBuffer(minusedlevel)), so it has
 * to be redone whenever the local player changes floor — app_world_map_poll. */
static void
app_rebuild_world_map(
    struct App* app,
    int level)
{
    int pixel_w = 0;
    int pixel_h = 0;
    uint32_t* argb;
    struct ToriDraw_Sprite* sprite;
    struct ToriDraw_Sprite** sprites;

    assert(app);
    assert(app->world);

    if( !app->world->minimap )
        return;

    argb =
        minimap_bake_argb(app->world->minimap, level, app->world->tile_flags, &pixel_w, &pixel_h);
    if( !argb )
        return;

    /* Reference drawDetail plots loc mapscene sprites (trees, rocks, altars, …)
     * into the same minimap image as the tiles/walls. */
    app_bake_mapscenes(app, argb, pixel_w, pixel_h, level);

    /* TORIRS_MINIMAP_BMP=path: the baked map straight to disk. The on-screen
     * minimap is a rotated, camera-anchored crop of this and needs a local
     * player to center on, so offline runs can only inspect the bake here. */
    if( getenv("TORIRS_MINIMAP_BMP") )
    {
        bmp_write_file(getenv("TORIRS_MINIMAP_BMP"), (int*)argb, pixel_w, pixel_h);
        TORIRS_LOG(
            "minimap: wrote %s (%dx%d level=%d)\n",
            getenv("TORIRS_MINIMAP_BMP"),
            pixel_w,
            pixel_h,
            level);
    }

    sprite = ToriDraw_SpriteNewFromArgbOwned(argb, pixel_w, pixel_h);
    if( !sprite )
    {
        free(argb);
        return;
    }

    sprites = malloc(sizeof(*sprites));
    assert(sprites);
    sprites[0] = sprite;

    ToriDraw_SceneSpriteAdd(app->scene, UITREE_SCENE_WORLD_MAP_SPRITE_ID, sprites, 1);
    app->world_map_scene_id = UITREE_SCENE_WORLD_MAP_SPRITE_ID;
    app->world_map_w = pixel_w;
    app->world_map_h = pixel_h;
    app->world_map_level = level;
#if defined(TORIRS_HAVE_GLES2)
    {
        /* The GLES2 renderer keeps a GPU copy of the pixels behind the
         * rotated-masked minimap and has no event that says they changed --
         * this is the one place they do. Declared here rather than through
         * its header because this is the one call site in this file. */
        void ToriRS_GLES2_RotmaskSourceChanged(void);
        ToriRS_GLES2_RotmaskSourceChanged();
    }
#endif
}

/* The level the minimap lives at: aboard, the rider's own level is a DECK
 * plane (the planking is authored at plane 1) while the minimap is the ROOT
 * world's — the deob renders it from the main world around the projected
 * position. Bake and cull with the hull's root level, or a plane-1 rider
 * gets the (empty) level-1 bake: a black map with floating icons. */
int
app_minimap_level(
    struct App* app,
    struct WorldEntity_Player const* local)
{
    (void)local;
    /* One authority for the effective root plane — see app_cinema_level. */
    return app_cinema_level(app);
}

/* Reference checkMinimap/minimapBuildBuffer trigger (Client.ts:5331): rebake
 * whenever the level the map was baked for stops matching the player's, or a
 * runtime loc change edited the wall/door bits (world->minimap_seq — an opened
 * door's red line has to move on the baked sprite). */
void
app_world_map_poll(struct App* app)
{
    static unsigned baked_minimap_seq = 0;
    struct WorldEntity_Player* local;

    if( !app->world || !app->world->load_complete || app->world_map_scene_id <= 0 )
        return;
    local = app_local_player(app);
    if( !local )
        return;
    if( app_minimap_level(app, local) == app->world_map_level &&
        baked_minimap_seq == app->world->minimap_seq )
        return;
    baked_minimap_seq = app->world->minimap_seq;
    app_rebuild_world_map(app, app_minimap_level(app, local));
    app->need_redraw = 1;
}

/* Task_WorldLoad on_done trampoline: adapts the void* hook to App_WorldLoadFinish. */
static void
app_world_load_finish_cb(void* userdata)
{
    App_WorldLoadFinish((struct App*)userdata);
}

/* Queue Task_WorldLoad for a chunk list; never blocks. App_WorldLoadFinish runs
 * as the task's on_done the moment the load lands (no polling). Reused by the
 * reload hotkey and the first-load trigger; assets already cached make a reload
 * near-instant. chunks == NULL -> the configured/default map. The REBUILD_NORMAL
 * packet task queues its own load (it awaits it) rather than calling here. */
void
app_world_load_begin(
    struct App* app,
    int const* chunks_xz,
    int chunk_pair_count)
{
    int chunks[APP_WORLD_MAP_SQUARE_MAX * 2] = { 50, 50 };
    struct ToriRS_Task* task;

    /* Same seam as CacheProvider_TrimDerivedCaches inside Task_WorldLoad:
     * previous scene's instance bases are no longer live. */
    TorirsModelInstCache_Clear(&app->model_inst_cache);

    if( !chunks_xz )
    {
        char const* env;
        int pair_count = 1;

        /*
         * Spawn square precedence: TORIRS_WORLD_MAP, then the manifest's `[cache:boot] spawn`,
         * then the client default of 50,50.
         *
         * The manifest layer matters because 50,50 is not universally loadable. A cache carries
         * XTEA keys only for the squares it was dumped with, and cache.643 has no key for
         * 50,50 (nor 49,49 / 50,49 / 51,49 / 51,50 — a hole right over Lumbridge). Terrain is
         * unencrypted, so an unkeyed square still renders ground and then **zero locs**, which
         * looks like a broken renderer rather than absent data.
         */
        if( app->cfg.spawn_x >= 0 && app->cfg.spawn_z >= 0 )
        {
            chunks[0] = app->cfg.spawn_x;
            chunks[1] = app->cfg.spawn_z;
        }
        env = getenv("TORIRS_WORLD_MAP");
        if( env )
        {
            /* Into scratch, not `chunks`: a list that turns out to be malformed
             * halfway through must not have already overwritten the manifest
             * square the message below is about to name as the fallback. */
            int parsed_chunks[APP_WORLD_MAP_SQUARE_MAX * 2];
            int parsed = ToriRS_EnvChunkList(env, parsed_chunks, APP_WORLD_MAP_SQUARE_MAX);
            if( parsed > 0 )
            {
                memcpy(chunks, parsed_chunks, sizeof(int) * 2 * (size_t)parsed);
                pair_count = parsed;
            }
            else
            {
                TORIRS_LOG(
                    "TORIRS_WORLD_MAP must be \"x,z\", or up to %d such squares "
                    "separated by ';', got '%s' - using %d,%d\n",
                    APP_WORLD_MAP_SQUARE_MAX,
                    env,
                    chunks[0],
                    chunks[1]);
            }
        }
        chunks_xz = chunks;
        chunk_pair_count = pair_count;
    }

    /*
     * Hold the camera across the reload.
     *
     * Every editor edit lands here through app_map_editor_drain's chunklist
     * rebuild, and without this each paint click snapped the eye back to the
     * scene centre -- the finish path places the camera for a FIRST look at a
     * scene, and a rebuild is not a first look. Absolute coordinates, so the
     * restore survives the scene window moving; see WorldCameraHold.
     */
    if( app->world && app->world_active )
    {
        WorldCameraHold_Capture(
            &app->cam_hold,
            app->world->_base_tile_x,
            app->world->_base_tile_z,
            app->world_camera_pos.x,
            app->world_camera_pos.y,
            app->world_camera_pos.z,
            app->world_camera.pitch,
            app->world_camera.yaw);
    }

    app->world_load_attempted = 1;
    app->world_load_inflight = 1;
    App_WorldDrainEntityRemoved(app);

    /*
     * Editor boots seed the provider from the content tree before the load
     * runs, so what gets meshed is the `.jm2`/`.jl2` text being edited rather
     * than the last bake. Task_WorldLoad skips a square the provider already
     * holds, so the text wins simply by being there first — the editor never
     * has to invalidate or race the cache path, and an unsaved edit is visible
     * without a bake.
     *
     * A square the content tree does not carry is left alone and loads from the
     * cache as usual, which is what lets an editor session sit at the edge of
     * authored content and still see the world around it.
     */
    if( app->editor )
    {
        for( int i = 0; i < chunk_pair_count; i++ )
            Editor_LoadSquare(app->editor, app->provider, chunks_xz[i * 2], chunks_xz[i * 2 + 1]);
    }

    task = CreateTask_WorldLoad(
        app->provider,
        app->world_builder,
        app->runner.queue,
        chunks_xz,
        chunk_pair_count,
        -1,
        -1,
        104,
        NULL,
        app_world_load_finish_cb,
        app);
    ToriRS_TaskQueue_Add(app->runner.queue, task);
    app->need_redraw = 1;
}

/* Post-load wiring, split from the old synchronous app_world_load: height
 * fn, texture requests, minimap bake, and the server ack when the load was
 * REBUILD_NORMAL-driven. Camera placement is only for offline/hotkey loads —
 * a server-driven rebuild shifts the existing camera (deob field3239 -= dx<<7)
 * instead of resetting to scene-centre top-down. */
void
App_WorldLoadFinish(struct App* app)
{
    app->world_load_inflight = 0;

    if( app->world->load_complete )
    {
        int server_driven = app->world_load_server_driven;

        app->world_active = 1;
        /* The absolute tile origin just moved, which is the one thing a plugin
         * holding saved tiles has to hear about: every scene-local number it
         * might have cached is renumbered by a rebuild. Raised after
         * world_active so a handler that queries the world finds it live. */
        PluginHost_WorldLoaded(app->plugins, app->world->_base_tile_x, app->world->_base_tile_z);
        /* AFTER the event, so an object a handler placed in response to the
         * rebuild is materialised by its own set_position rather than being
         * swept up by a pass that already ran. */
        app_plugin_objects_rebuild(app);
        /* Every loc in the new scene gets its LOC_ADD trigger. After the
         * plugin seam because a trigger script can create overlays, and the
         * object rebuild sweeps anything placed before it. */
        app_client_triggers_world_loaded(app);
        World_SetHeightFn(app->world, app_world_height, app);
        {
            struct World_SeqSource seq_source;
            /* Bound here rather than once at boot: the scene and the provider
             * are what the source reads, and this is the point at which the
             * world being handed the source has them. */
            WorldSeqSourceToriDraw_Bind(&app->seq_source, app->scene, app->provider);
            WorldSeqSourceToriDraw_Fill(&app->seq_source, &seq_source);
            World_SetSeqSource(app->world, &seq_source);
        }
        {
            struct World_AnimSoundSink anim_sound_sink = {
                .userdata = app,
                .frame = app_world_anim_frame_sound,
            };
            World_SetAnimSoundSink(app->world, &anim_sound_sink);
        }
        if( !server_driven )
        {
            int restored = 0;

            /* A held camera wins over the first-look placement, IF the new
             * scene contains it. Outside the scene (the square browser opened
             * somewhere distant) the hold is meaningless and the first-look
             * centre below is correct. */
            restored = WorldCameraHold_Restore(
                &app->cam_hold,
                app->world->_base_tile_x,
                app->world->_base_tile_z,
                app->world->_scene_size,
                &app->world_camera_pos.x,
                &app->world_camera_pos.y,
                &app->world_camera_pos.z,
                &app->world_camera.pitch,
                &app->world_camera.yaw);

            if( !restored )
            {
                /* Offline/hotkey load: place the camera at scene centre. */
                app->world_camera_pos.x = app->world->_scene_size / 2 * 128 + 64;
                app->world_camera_pos.z = app->world->_scene_size / 2 * 128 + 64;
                app->world_camera_pos.y = -2000;
                app->world_camera.pitch = 450;
                app->world_camera.yaw = 0;
            }
            {
                char const* cam = getenv("TORIRS_WORLD_CAM");
                int cx, cy, cz, cpitch, cyaw;
                if( cam && sscanf(cam, "%d,%d,%d,%d,%d", &cx, &cy, &cz, &cpitch, &cyaw) == 5 )
                {
                    app->world_camera_pos.x = cx;
                    app->world_camera_pos.y = cy;
                    app->world_camera_pos.z = cz;
                    app->world_camera.pitch = cpitch;
                    app->world_camera.yaw = cyaw;
                }
            }
        }
        /* World scenery models reference textures; the bridge scan walks the
         * scene elements the rebuild just created. */
        app_sync_textures(app);
        /* Decouple the BFS window from the resident scene size: rsmod floods a
         * fixed 128x128 box around the mover. LostCity leaves this at 0
         * (whole map). */
        if( app->features )
        {
            for( int i = 0; i < COLLISION_LEVELS; i++ )
            {
                if( app->world->collision_maps[i] )
                    collision_map_set_route_window(
                        app->world->collision_maps[i], app->features->route_window_tiles);
            }
        }
        {
            struct WorldEntity_Player* local = app_local_player(app);
            app_rebuild_world_map(app, app_minimap_level(app, local));
        }

        if( server_driven )
        {
            app->world_load_server_driven = 0;
            App_SendMapBuildComplete(app);
        }
    }
    else
    {
        app->world_load_server_driven = 0;
        TORIRS_LOG("app: world load incomplete\n");
    }
    app->need_redraw = 1;
}
