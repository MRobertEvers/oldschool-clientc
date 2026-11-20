/*
 * Loading a world: the load task, the minimap bake, and the post-load wiring.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "mem/torirs_mem.h"
#include "mem/torimem_toridraw.h"
#include "app/app_internal.h"
#include "engine/world_builder/world_rebuild_job.h"
#include "boot_telemetry.h"
#include "platform/platform_web_telemetry.h"

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

/* Bind app-owned overlay models to their revision-configured nodes. No node
 * means that overlay does not exist for the revision; there is intentionally
 * no C fallback that changes the shape of the UITree. */
void
app_bind_configured_overlays(struct App* app)
{
    int minimenu_font_id = -1;

    app->hover_text.font_id = -1;
    UITREE_SCAN_METER(app->tree);
    for( uint32_t i = 0; i < app->tree->component_count; i++ )
    {
        struct UITreeComponent const* node = &app->tree->components[i];
        if( node->freed )
            continue;
        if( node->type == UIELEM_BUILTIN_MINIMENU )
            minimenu_font_id = node->u.minimenu.font_id;
        else if( node->type == UIELEM_BUILTIN_HOVERTEXT )
            app->hover_text.font_id = node->u.hovertext.font_id;
    }
    /* Every view's menu draws in the tree's face. */
    for( int view = 0; view < app->world_views.split.view_count; view++ )
        app->world_views.views[view].minimenu->font_id = minimenu_font_id;
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
        TORIDRAW_FREE(argb);
        return;
    }

    sprites = TORIDRAW_MALLOC(sizeof(*sprites)); /* SceneSpriteAdd frees it */
    assert(sprites);
    sprites[0] = sprite;

    ToriDraw_SceneSpriteAdd(app->scene, UITREE_SCENE_WORLD_MAP_SPRITE_ID, sprites, 1);
    MinimapView_SetMap(&app->minimap, UITREE_SCENE_WORLD_MAP_SPRITE_ID, pixel_w, pixel_h, level);
#if defined(TORIRS_HAVE_GLES2)
    {
        /* The GLES2 renderer keeps a GPU copy of the pixels behind the
         * rotated-masked minimap and has no event that says they changed --
         * this is the one place they do. Declared here rather than through
         * its header because this is the one call site in this file. */
        void ToriPlatformAndroid_Renderer_GLES2_RotmaskSourceChanged(void);
        ToriPlatformAndroid_Renderer_GLES2_RotmaskSourceChanged();
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

    if( !app->world || !app->world->load_complete || !MinimapView_HasMap(&app->minimap) )
        return;
    local = app_local_player(app);
    if( !local )
        return;
    if( app_minimap_level(app, local) == MinimapView_MapLevel(&app->minimap) &&
        baked_minimap_seq == app->world->minimap_seq )
        return;
    baked_minimap_seq = app->world->minimap_seq;
    app_rebuild_world_map(app, app_minimap_level(app, local));
    RedrawRequest_Raise(&app->redraw_request);
}

/* Task_WorldLoad on_done trampoline: adapts the void* hook to App_WorldLoadFinish. */
/*
 * A LOCAL LOAD ON THE BACKGROUND QUEUE.
 *
 * The load was one Task_WorldLoad on the parallel queue, and its last step
 * -- the build, every scene element of the squares, then the finish -- ran
 * 10 to 11 ms in one resume (the editor's reload, measured by the frame
 * contract's compute check). CPU work belongs on the background queue, so
 * the load is split the way the background rebuild splits it
 * (app_world_job.c): the GATHER runs there as a job, fanned out there and
 * given the floor and the slack, and the BUILD and FINISH are the frame's,
 * at the point the parallel pump used to finish the load (just after
 * App_PumpAsync). A load begun while another is still gathering replaces
 * it: the earlier gather is cancelled, its loaders run on into the cache.
 * The rebuild of the root view is listed in the pending index from the
 * begin to the finish (App_ViewRebuildBegin), so a parallel task waits for
 * it by name.
 */
struct AppWorldLocalLoad
{
    struct ToriRS_TaskJob job;
    /* The gather, while it has not ended (the job counts it). */
    struct ToriRS_Task* gather;
    int pending;
    int chunk_count;
    int chunks[WORLD_REBUILD_JOB_MAX_CHUNKS * 2];
    struct RS_ParallelTask rebuild;
};

static struct AppWorldLocalLoad*
app_world_local_load(struct App* app)
{
    assert(app);
    if( !app->world_local )
    {
        app->world_local = ToriMem_Calloc(ToriMem_Process(), 1, sizeof(*app->world_local));
        assert(app->world_local);
    }
    return app->world_local;
}

/*
 * THE ONE PLACE A LOCAL LOAD ENDS: `built` (the frame built and finished it)
 * or cancelled. Unlisted as the root view's rebuild; built, the view's built
 * trigger fires; and the job ends as an owner.
 */
static void
app_world_local_load_end(
    struct App* app,
    int built)
{
    struct AppWorldLocalLoad* ll = app->world_local;

    assert(ll);
    App_ViewRebuildEnd(app, &ll->rebuild, built);
    ToriRS_TaskJob_End(&ll->job, built);
}

/* Stop a gather still out (its loaders outlive it and end into the cache). */
static void
app_world_local_load_cancel(struct App* app)
{
    struct AppWorldLocalLoad* ll = app->world_local;

    assert(ll);
    if( !ll->pending )
        return;
    if( !ToriRS_TaskJob_Done(&ll->job) )
        TaskWorldLoad_Cancel(app->background_runner.queue, ll->gather);
    assert(ToriRS_TaskJob_Done(&ll->job));
    ll->gather = NULL;
    ll->pending = 0;
    app_world_local_load_end(app, 0);
}

static void
app_world_local_load_start(
    struct App* app,
    int const* chunks_xz,
    int chunk_count)
{
    struct AppWorldLocalLoad* ll = app_world_local_load(app);

    assert(chunks_xz);
    assert(chunk_count > 0);
    assert(chunk_count <= WORLD_REBUILD_JOB_MAX_CHUNKS);
    if( ll->pending )
    {
        TORIRS_LOG("world load: a local load still gathering is replaced by a newer one\n");
        app_world_local_load_cancel(app);
    }
    memcpy(ll->chunks, chunks_xz, sizeof(int) * 2 * (size_t)chunk_count);
    ll->chunk_count = chunk_count;
    ll->gather = CreateTask_WorldGather(
        app->provider,
        app->world_builder,
        app->background_runner.queue,
        ll->chunks,
        ll->chunk_count,
        -1,
        -1,
        104,
        NULL,
        NULL);
    ll->pending = 1;
    App_ViewRebuildBegin(app, &ll->rebuild, WORLDVIEW_ROOT, ToriRS_TaskJobOwner(&ll->job, "the local world load"));
    /* No deadline of its own: nothing waits on a local load but the frame
     * that shows it, and the background queue gets its floor every frame. */
    ToriRS_TaskJob_Start(&ll->job, &app->background_runner, ll->gather, 0);
}

void
app_world_local_load_poll(struct App* app)
{
    struct AppWorldLocalLoad* ll;

    assert(app);
    ll = app->world_local;
    if( !ll || !ll->pending || !ToriRS_TaskJob_Done(&ll->job) )
        return;
    ll->gather = NULL;
    ll->pending = 0;
    /* The build and the finish, in the order Task_WorldLoad ran them. */
    WorldLoad_Build(app->world_builder, ll->chunks, ll->chunk_count, -1, -1, 104, NULL);
    App_WorldLoadFinish(app);
    app_world_local_load_end(app, 1);
}

void
app_world_local_load_free(struct App* app)
{
    assert(app);
    if( !app->world_local )
        return;
    app_world_local_load_cancel(app);
    ToriMem_Free(ToriMem_Process(), app->world_local);
    app->world_local = NULL;
}

/* Start a local load for a chunk list; never blocks. Its gather runs on the
 * background queue, and App_WorldLoadFinish runs in the frame that finds it
 * ended (app_world_local_load_poll). Reused by the reload hotkey and the
 * first-load trigger; assets already cached make a reload near-instant.
 * chunks == NULL -> the configured/default map. The REBUILD_NORMAL packet
 * task queues its own load (it awaits it) rather than calling here. */
void
app_world_load_begin(
    struct App* app,
    int const* chunks_xz,
    int chunk_pair_count)
{
    int chunks[APP_WORLD_MAP_SQUARE_MAX * 2] = { 50, 50 };

    /* This load replaces the scene a background rebuild was gathering for;
     * that rebuild is moot (and its commit would build over this one). */
    App_WorldJobCancel(app, "offline load");

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
     * A scene is one grid over the bounding box of its squares, so two squares
     * a world apart are a grid nobody can allocate ("single allocation over
     * 4 GB", 2026-11-11: m50_50 with a parked square at m90_240). The editor's
     * opens fit their list first (app_map_editor_world_begin); this is the
     * last line for a list that came from anywhere else -- TORIRS_WORLD_MAP,
     * a rebuild queue. Refused in a sentence, down to the first square.
     */
    {
        int min_x = chunks_xz[0];
        int max_x = chunks_xz[0];
        int min_z = chunks_xz[1];
        int max_z = chunks_xz[1];

        for( int i = 1; i < chunk_pair_count; i++ )
        {
            min_x = chunks_xz[i * 2] < min_x ? chunks_xz[i * 2] : min_x;
            max_x = chunks_xz[i * 2] > max_x ? chunks_xz[i * 2] : max_x;
            min_z = chunks_xz[i * 2 + 1] < min_z ? chunks_xz[i * 2 + 1] : min_z;
            max_z = chunks_xz[i * 2 + 1] > max_z ? chunks_xz[i * 2 + 1] : max_z;
        }
        if( max_x - min_x >= APP_WORLD_MAP_SQUARE_MAX || max_z - min_z >= APP_WORLD_MAP_SQUARE_MAX )
        {
            TORIRS_LOG(
                "world load: squares m%d_%d .. m%d_%d span %d x %d, more than one scene holds (%d a side): "
                "loading m%d_%d alone\n",
                min_x, min_z, max_x, max_z, max_x - min_x + 1, max_z - min_z + 1, APP_WORLD_MAP_SQUARE_MAX,
                chunks_xz[0], chunks_xz[1]);
            chunk_pair_count = 1;
        }
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
    if( app->world && WorldLoadState_Active(&app->world_load) )
    {
        /* Every attached view holds its own camera across the reload. */
        for( int view = 0; view < app->world_views.split.view_count; view++ )
            WorldCameraHold_Capture(
                &app->world_views.views[view].cam_hold,
                app->world->_base_tile_x,
                app->world->_base_tile_z,
                app->world_views.views[view].world_camera_pos.x,
                app->world_views.views[view].world_camera_pos.y,
                app->world_views.views[view].world_camera_pos.z,
                app->world_views.views[view].world_camera.pitch,
                app->world_views.views[view].world_camera.yaw);
    }

    WorldLoadState_BeginLocal(&app->world_load);
    ToriRS_BootTelemetry_Mark("world_load");
    /* Ahead of the drain, so what it removes goes with this scene. */
    SideApps_WorldLoad(app->side_apps, SIDE_APP_WORLD_LOAD_BEGIN, app_side_app_host(app), NULL, 0);
    App_WorldDrainEntityRemoved(app);

    /* The side-apps' copy of the squares, before the load runs: an editor
     * boot seeds the provider from the content tree it edits, and
     * Task_WorldLoad skips a square the provider already holds
     * (app_mapeditor_app.c). */
    SideApps_WorldLoad(app->side_apps, SIDE_APP_WORLD_LOAD_SQUARES, app_side_app_host(app), chunks_xz, chunk_pair_count);

    app_world_local_load_start(app, chunks_xz, chunk_pair_count);
    RedrawRequest_Raise(&app->redraw_request);
}

/* Post-load wiring, split from the old synchronous app_world_load: height
 * fn, texture requests, minimap bake, and the server ack when the load was
 * REBUILD_NORMAL-driven. Camera placement is only for offline/hotkey loads —
 * a server-driven rebuild shifts the existing camera (deob field3239 -= dx<<7)
 * instead of resetting to scene-centre top-down. */
void
App_WorldLoadFinish(struct App* app)
{
    /* TORIRS_REBUILD_TIMING: the finish's stages, one line. */
    double tf[8] = { 0 };
    int const timing = WorldBuilder_TimingOn();
    if( timing )
        tf[0] = WorldBuilder_TimingNowMs();
    WorldLoadState_Land(&app->world_load);
    ToriRS_BootTelemetry_Mark("world_loaded");
    ToriPlatformWeb_Telemetry(
        "world_load_finish",
        "{\"complete\":%d,\"server_driven\":%d}",
        app->world->load_complete ? 1 : 0,
        WorldLoadState_ServerDriven(&app->world_load) ? 1 : 0);

    if( app->world->load_complete )
    {
        int server_driven = WorldLoadState_ServerDriven(&app->world_load);

        WorldLoadState_Complete(&app->world_load);
        /* The absolute tile origin just moved, which is the one thing a plugin
         * holding saved tiles has to hear about: every scene-local number it
         * might have cached is renumbered by a rebuild. Raised after
         * WorldLoadState_Complete so a handler that queries the world finds it live. */
        PluginHost_WorldLoaded(app->plugins, app->world->_base_tile_x, app->world->_base_tile_z);
        /* AFTER the event, so an object a handler placed in response to the
         * rebuild is materialised by its own set_position rather than being
         * swept up by a pass that already ran. */
        app_plugin_objects_rebuild(app);
        if( timing )
            tf[1] = WorldBuilder_TimingNowMs();
        /* The finish every rebuilt view runs (a deck's too): every loc in the
         * new scene gets its LOC_ADD trigger, ground items still waiting for
         * a model take new placeholders against this generation, and --
         * server-driven only, an offline or editor load builds from text that
         * already holds its edits -- every loc a zone packet changed, landed
         * or still pending, is put back on the scene built from the map.
         * After the plugin seam because a trigger script can create overlays,
         * and the object rebuild sweeps anything placed before it. */
        if( timing )
            tf[2] = WorldBuilder_TimingNowMs();
        App_WorldViewLoadFinish(app, WORLDVIEW_ROOT, server_driven);
        if( timing )
            tf[3] = WorldBuilder_TimingNowMs();
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
            {
            struct App_WorldView* const saved_view = app->world_views.frame;
            /* Every attached view is placed the same way. */
            for( int view = 0; view < app->world_views.split.view_count; view++ )
            {
            app->world_views.frame = &app->world_views.views[view];
            restored = WorldCameraHold_Restore(
                &app->world_views.frame->cam_hold,
                app->world->_base_tile_x,
                app->world->_base_tile_z,
                app->world->_scene_size,
                &app->world_views.frame->world_camera_pos.x,
                &app->world_views.frame->world_camera_pos.y,
                &app->world_views.frame->world_camera_pos.z,
                &app->world_views.frame->world_camera.pitch,
                &app->world_views.frame->world_camera.yaw);

            if( !restored )
            {
                /* Offline/hotkey load: place the camera at scene centre. */
                app->world_views.frame->world_camera_pos.x = app->world->_scene_size / 2 * 128 + 64;
                app->world_views.frame->world_camera_pos.z = app->world->_scene_size / 2 * 128 + 64;
                app->world_views.frame->world_camera_pos.y = -2000;
                app->world_views.frame->world_camera.pitch = 450;
                app->world_views.frame->world_camera.yaw = 0;
            }
            {
                char const* cam = getenv("TORIRS_WORLD_CAM");
                int cx, cy, cz, cpitch, cyaw;
                if( cam && sscanf(cam, "%d,%d,%d,%d,%d", &cx, &cy, &cz, &cpitch, &cyaw) == 5 )
                {
                    app->world_views.frame->world_camera_pos.x = cx;
                    app->world_views.frame->world_camera_pos.y = cy;
                    app->world_views.frame->world_camera_pos.z = cz;
                    app->world_views.frame->world_camera.pitch = cpitch;
                    app->world_views.frame->world_camera.yaw = cyaw;
                }
            }
            }
            app->world_views.frame = saved_view;
            }
        }
        /* World scenery models reference textures; the bridge scan walks the
         * scene elements the rebuild just created. */
        if( timing )
            tf[4] = WorldBuilder_TimingNowMs();
        app_sync_textures(app);
        if( timing )
            tf[5] = WorldBuilder_TimingNowMs();
        /* Decouple the BFS window from the resident scene size: rsmod floods a
         * fixed 128x128 box around the mover. LostCity leaves this at 0
         * (whole map). */
        if( ToriRS_Features_Resolved(&app->features) )
        {
            for( int i = 0; i < COLLISION_LEVELS; i++ )
            {
                if( app->world->collision_maps[i] )
                    collision_map_set_route_window(
                        app->world->collision_maps[i], app->features.route_window_tiles);
            }
        }
        {
            struct WorldEntity_Player* local = app_local_player(app);
            app_rebuild_world_map(app, app_minimap_level(app, local));
        }
        if( timing )
        {
            tf[6] = WorldBuilder_TimingNowMs();
            TORIRS_REPORT("rebuild_timing: load_finish plugins=%.2fms triggers=%.2fms loc_changes=%.2fms "
                          "seq_cam=%.2fms textures=%.2fms minimap=%.2fms\n",
                tf[1] - tf[0], tf[2] - tf[1], tf[3] - tf[2], tf[4] - tf[3], tf[5] - tf[4],
                tf[6] - tf[5]);
        }

        if( server_driven )
        {
            WorldLoadState_Acknowledged(&app->world_load);
            App_SendMapBuildComplete(app);
        }
    }
    else
    {
        WorldLoadState_Fail(&app->world_load);
        TORIRS_LOG("app: world load incomplete\n");
    }
    RedrawRequest_Raise(&app->redraw_request);
    app_rebuild_frames_finished(app);
    app_world_digest_loaded(app);
}
