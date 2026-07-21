#include "app.h"

#include "cs2vm2/cs2vm2.h"
#include "engine/dat1/dat1_buildcache.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/player_appearance.h"
#include "engine/toridraw_model_from_torirs.h"
#include "engine/uitree_cmd_render.h"
#include "engine/world_builder/task_world_load.h"
#include "engine/world_builder/world_builder.h"
#include "game/rs_cs2_dispatch.h"
#include "game/rs_minimenu_build.h"
#include "game/rs_worldmap.h"
#include "game/rs_minimenu_cross.h"
#include "game/task_cs1_run.h"
#include "painters/painters.h"
#include "platform/platform_sdl2_renderer_soft3d.h"
#include "render/torirs_frame.h"
#include "render/torirs_pick.h"
#include "toridraw.h"
#include "ui/uitree_layout.h"
#include "world/world.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    APP_LOGIC_TICK_MS = 20,
    APP_MAX_CATCHUP_TICKS = 5,
    /* Mouseover text origin inside the viewport. The reference container puts
     * its text child at (0,0); the classic client drew the same line at
     * (4, 15) — one padded cell in, with the baseline a line down. Ours is a
     * text box, so the baseline offset comes from the font ascent. */
    APP_HOVERTEXT_INSET_X = 4,
    APP_HOVERTEXT_INSET_Y = 2,
};

static int
app_host_request(
    void* user,
    struct UITreeHostRequest* req)
{
    struct App* app = (struct App*)user;
    struct InvSlot slot;

    assert(req);
    assert(app);

    switch( req->kind )
    {
    case UITREE_HOST_GET_SCROLLBAR_SCENE:
        return UITreeSceneBridge_ScrollbarSceneId(&app->bridge);
    case UITREE_HOST_GET_STATIC_SPRITE_SCENE:
        return UITreeSceneBridge_StaticSpriteSceneId(
            &app->bridge, (enum StaticSpriteSlot)req->u.static_sprite.slot);
    case UITREE_HOST_GET_CROSS_ACTIVE:
        return UICross_IsActive(&app->cross) ? 1 : 0;
    case UITREE_HOST_GET_CROSS_ATLAS_FRAME:
        return UICross_AtlasFrame(&app->cross);
    case UITREE_HOST_GET_CROSS_POSITION:
        if( req->u.get_cross_position.out_x )
            *req->u.get_cross_position.out_x = app->cross.x;
        if( req->u.get_cross_position.out_y )
            *req->u.get_cross_position.out_y = app->cross.y;
        return 1;
    case UITREE_HOST_GET_MINIMENU_VISIBLE:
        return app->interact.minimenu.visible ? 1 : 0;
    case UITREE_HOST_GET_MINIMENU_STATE:
        assert(req->u.get_minimenu_state.out);
        *req->u.get_minimenu_state.out = &app->interact.minimenu;
        return 1;
    case UITREE_HOST_GET_HOVERTEXT_STATE:
        assert(req->u.get_hovertext_state.out);
        *req->u.get_hovertext_state.out = &app->hover_text;
        return 1;
    case UITREE_HOST_MEASURE_TEXT:
    {
        struct ToriDraw_Font* font = ToriDraw_SceneFontGet(app->scene, req->u.measure_text.font_id);
        if( !font || !req->u.measure_text.text )
            return 0;
        return ToriDraw2D_MeasureString(font, req->u.measure_text.text);
    }
    /* Compass/minimap rotation, in the 0..2047 units the rotated sprite blit
     * takes. Normalized because ToriDraw_Sin/Cos assert that range. */
    case UITREE_HOST_GET_CAMERA_YAW:
        return ToriDraw_NormalizeAngle(app->world_camera.yaw);
    /* Minimap: the baked world map plus the camera's pivot inside it. The
     * widget box is fixed, so the map scrolls by moving this source anchor. */
    case UITREE_HOST_GET_MINIMAP_STATE:
        if( app->world_map_scene_id <= 0 || !app->world || !app->world->minimap )
            return -1;
        minimap_compute_camera_src_anchor(
            app->world_camera_pos.x,
            app->world_camera_pos.z,
            app->world_map_w,
            app->world_map_h,
            app->world->minimap->width,
            app->world->minimap->height,
            req->u.get_minimap_state.out_src_anchor_x,
            req->u.get_minimap_state.out_src_anchor_y);
        return app->world_map_scene_id;
    case UITREE_HOST_GET_INV_SOURCE_SLOT:
        assert(req->u.get_inv_source_slot.out);
        if( !InvManager_GetSlot(
                &app->invs,
                req->u.get_inv_source_slot.source_id,
                req->u.get_inv_source_slot.slot,
                &slot) )
            return 0;
        req->u.get_inv_source_slot.out->obj_id = slot.obj_id;
        req->u.get_inv_source_slot.out->obj_count = slot.obj_count;
        req->u.get_inv_source_slot.out->scene_id = slot.scene_id;
        req->u.get_inv_source_slot.out->atlas_index = slot.atlas_index;
        return 1;
    /* CS1 answers come from the per-tick evaluation cached on each node, so
     * drawing never runs the VM and never has to handle a mid-frame yield. */
    case UITREE_HOST_IS_ACTIVE:
        if( !req->u.is_active.component )
            return 0;
        return req->u.is_active.component->cs1_active ? 1 : 0;
    case UITREE_HOST_EVAL_TEXT_PLACEHOLDER:
        if( !req->u.eval_text_placeholder.component ||
            req->u.eval_text_placeholder.script_idx < 0 ||
            req->u.eval_text_placeholder.script_idx >= UITREE_CS1_VALUE_MAX )
            return 0;
        return req->u.eval_text_placeholder.component
            ->cs1_values[req->u.eval_text_placeholder.script_idx];
    default:
        return 0;
    }
}

/* Demo content until real state sync exists: seed the worn/backpack/bank
 * containers so item-bearing interfaces have something to show. */
static void
seed_inv_defaults(struct InvManager* invs)
{
    static int const k_worn_items[] = { 1153, 1007, 1725, 1333, 1115, 1201,
                                        1189, 1063, 1067, 2564, 882 };
    static int const k_backpack_items[] = { 1333 };
    /* Bank contents so interface 12 renders its item grid + tab row. */
    static int const k_bank_items[] = { 995,  1333, 1153, 1007, 1725, 1115, 1201, 1189, 1063,
                                        1067, 2564, 882,  4151, 1305, 1319, 1215, 1231, 1147,
                                        1163, 1079, 1093, 861,  1163, 1704, 2550, 6585, 1725,
                                        3105, 1387, 1275, 1291, 4587, 1215, 1333, 995,  1038 };

    assert(invs);
    assert(InvManager_ResolveSource(invs, INV_MANAGER_SOURCE_NAME_WORN) >= 0);
    assert(InvManager_ResolveSource(invs, INV_MANAGER_SOURCE_NAME_BACKPACK) >= 0);

    assert(InvManager_ApplyFull(
        invs,
        INV_MANAGER_CONTAINER_WORN,
        k_worn_items,
        NULL,
        (int)(sizeof(k_worn_items) / sizeof(k_worn_items[0]))));
    assert(InvManager_ApplyFull(
        invs,
        INV_MANAGER_CONTAINER_BACKPACK,
        k_backpack_items,
        NULL,
        (int)(sizeof(k_backpack_items) / sizeof(k_backpack_items[0]))));
    assert(InvManager_EnsureContainer(invs, INV_MANAGER_CONTAINER_BANK, 800, "bank") >= 0);
    assert(InvManager_ApplyFull(
        invs,
        INV_MANAGER_CONTAINER_BANK,
        k_bank_items,
        NULL,
        (int)(sizeof(k_bank_items) / sizeof(k_bank_items[0]))));
}

/* Run one CS1 evaluation pass over the tree. Drains synchronously like
 * app_sync_textures: the pass is short, and any pack load it needs is serviced
 * inside the task before it retries the component. Returns nonzero when a
 * cached result changed, so the caller redraws. */
static int
app_run_cs1_eval(struct App* app)
{
    struct ToriRS_Task* task = CreateTask_CS1Eval(&app->cs1_host);
    if( !task )
        return 0;

    app->cs1_host.eval_dirty = false;
    ToriRS_TaskQueue_Add(app->runner.queue, task);
    TaskRunner_Drain(&app->runner);

    return app->cs1_host.eval_dirty ? 1 : 0;
}

/* Scene models reference textures by face id, but the ToriDraw texture map
 * starts empty (reference: textures load on demand and faces skip-render
 * until they land). Collect ids the scene is missing, load them through the
 * async pipeline, and publish into the scene. Ids that fail stay marked in
 * the bridge and are never re-requested. */
static void
app_sync_textures(struct App* app)
{
    int ids[256];
    int id_count;
    int queued = 0;

    id_count = UITreeSceneBridge_CollectMissingTextures(&app->bridge, ids, 256);
    if( id_count == 0 )
        return;

    for( int i = 0; i < id_count; i++ )
    {
        struct ToriRS_Task* task = CreateTask_TextureLoad(app->provider, ids[i]);
        if( task )
        {
            ToriRS_TaskQueue_Add(app->runner.queue, task);
            queued++;
        }
    }
    if( queued )
        TaskRunner_Drain(&app->runner);

    if( UITreeSceneBridge_PublishTextures(&app->bridge, ids, id_count) )
        app->need_redraw = 1;

    if( getenv("TORIRS_TEX_DEBUG") )
    {
        for( int i = 0; i < id_count; i++ )
        {
            struct ToriDraw_Texture* scene_tex =
                ToriDraw_TextureMapGet(&ToriDraw_SceneTexState(app->scene)->texture_map, ids[i]);
            int nonzero = 0;
            int total = 0;
            if( scene_tex && scene_tex->texels )
            {
                total = scene_tex->width * scene_tex->height;
                for( int t = 0; t < total; t++ )
                    if( scene_tex->texels[t] != 0 )
                        nonzero++;
            }
            fprintf(
                stderr,
                "tex_sync: id=%d provider=%d scene=%d failed=%d opaque=%d texels=%d/%d\n",
                ids[i],
                CacheProvider_TextureHas(app->provider, ids[i]) ? 1 : 0,
                scene_tex != NULL ? 1 : 0,
                (ids[i] >= 0 && ids[i] < 256) ? (int)app->bridge.texture_failed[ids[i]] : -1,
                scene_tex ? (int)scene_tex->opaque : -1,
                nonzero,
                total);
        }
    }
}

void
App_Init(
    struct App* app,
    struct AppConfig const* cfg)
{
    assert(app);
    assert(cfg);
    memset(app, 0, sizeof(*app));
    app->cfg = *cfg;

    ToriDraw_Init();

    /* Phase 1: task runtime + disk. The runner owns the async pipeline every
     * other phase loads through. */
    app->runner.io = ToriRS_IO_New();
    app->runner.queue = ToriRS_TaskQueue_New();
    app->runner.px = PlatformX_IO_New();
    assert(app->runner.px != NULL);

    if( cfg->cache_kind == APP_CACHE_DAT1 )
    {
        app->dat1_disk = RSCache_Dat1DiskNewFromDirectory(cfg->cache_dir);
        if( !app->dat1_disk )
            fprintf(
                stderr,
                "app: no dat1 cache at %s (expected main_file_cache.dat; pass --dat2 for a "
                "js5 cache)\n",
                cfg->cache_dir);
        assert(app->dat1_disk != NULL);
        PlatformX_IO_InitDat1Disk(app->runner.px, app->dat1_disk);
        /* No xtea step: dat1 archives are not encrypted. */
    }
    else
    {
        app->dat2_disk = RSCache_Dat2DiskNewFromDirectory(cfg->cache_dir);
        if( !app->dat2_disk )
            fprintf(
                stderr,
                "app: no dat2 cache at %s (expected main_file_cache.dat2; pass --dat1 for a "
                "317-era cache)\n",
                cfg->cache_dir);
        assert(app->dat2_disk != NULL);
        /* Map archives are xtea-encrypted; keys load into the rscache global
         * table the disk layer consults on archive fetch. */
        {
            char xtea_path[1024];
            snprintf(xtea_path, sizeof(xtea_path), "%s/xteas.json", cfg->cache_dir);
            if( RSCache_XteaConfigLoadKeys(xtea_path) <= 0 )
                fprintf(stderr, "app: no xtea keys at %s (world maps may fail)\n", xtea_path);
        }
        PlatformX_IO_InitDat2Disk(app->runner.px, app->dat2_disk);
    }
    PlatformX_IO_InitConfigPath(app->runner.px, cfg->config_dir);
    PlatformX_IO_InitScriptPath(app->runner.px, cfg->script_dir);

    /* Phase 2: asset pipeline (provider is a view over the build cache). */
    if( cfg->cache_kind == APP_CACHE_DAT1 )
    {
        app->dat1_bc = dat1_buildcache_new();
        app->provider = dat1_buildcache_as_provider(app->dat1_bc);
    }
    else
    {
        app->dat2_bc = dat2_buildcache_new();
        app->provider = dat2_buildcache_as_provider(app->dat2_bc);
    }

    /* Phase 3: renderer scene + id bridge (bridge needs scene + provider). */
    app->scene = ToriDraw_SceneNew(0);
    assert(app->scene);
    UITreeSceneBridge_Init(&app->bridge, app->scene, app->provider);

    /* Phase 4: game state (host needs tree + provider + invs + varps, then
     * the bridge for icon rasterization). */
    app->tree = UITree_New(256);
    assert(app->tree);
    InvManager_Init(&app->invs);
    VarPManager_Init(&app->varps);

    /* Phase 4b: world sim + builder. The World is a pure simulation that
     * references scene elements/assets by integer id; the builder keeps it in
     * sync with the shared scene from cache data. */
    app->world = World_New();
    assert(app->world);
    app->world_builder = WorldBuilder_New(app->world, app->provider, app->scene, &app->varps);
    assert(app->world_builder);
    app->painter_buffer = painter_buffer_new();
    assert(app->painter_buffer);
    /* v1 GameRunescape camera defaults; repositioned on world load complete. */
    app->world_camera.fov_rpi2048 = 512;
    app->world_camera.near_plane_z = 50;
    app->world_camera.pitch = 148;
    app->world_camera_pos.z = -800;
    app->world_hover_tile_x = -1;
    app->world_hover_tile_z = -1;
    app->world_hover_tile_level = 0;
    app->world_map_scene_id = -1;
    app->proj_src_tile_x = -1;
    app->proj_src_tile_z = -1;
    app->proj_src_tile_level = 0;

    seed_inv_defaults(&app->invs);
    RS_CS2Host_Init(&app->host, app->tree, app->provider, &app->invs, &app->varps);
    RS_CS2Host_SetBridge(&app->host, &app->bridge);
    RS_PlayerStats_Init(&app->stats);
    RS_CS1Host_Init(&app->cs1_host, app->tree, app->provider, &app->invs, &app->varps, &app->stats);

    /* Phase 5: frame state. */
    UITree_EmitBufferInit(&app->emit);
    UITree_HostInit(&app->ui_host);
    app->ui_host.user = app;
    app->ui_host.request = app_host_request;
    UIInteraction_Init(&app->interact);
    UIHoverText_Reset(&app->hover_text);
    app->hover_com_id = -1;
    app->clicked_com_id = -1;
    app->need_redraw = 1;
}

void
App_Shutdown(struct App* app)
{
    assert(app);
    UITree_EmitBufferFree(&app->emit);
    RS_CS2Host_Free(&app->host);
    if( app->painter_buffer )
    {
        free(app->painter_buffer->commands);
        free(app->painter_buffer);
    }
    WorldBuilder_Free(app->world_builder);
    World_Free(app->world);
    VarPManager_Free(&app->varps);
    InvManager_Free(&app->invs);
    UITree_Free(app->tree);
    if( app->builder_active )
        UITreeBuilder_Free(&app->builder);
    UITreeSceneBridge_Free(&app->bridge);
    ToriDraw_SceneFree(app->scene);
    /* Only the pair matching cfg.cache_kind was ever created; both frees assert
     * on NULL, so the unused side must not be handed to them. */
    if( app->dat2_bc )
        dat2_buildcache_free(app->dat2_bc);
    if( app->dat1_bc )
        dat1_buildcache_free(app->dat1_bc);
    PlatformX_IO_Free(app->runner.px);
    if( app->dat2_disk )
        RSCache_Dat2DiskFree(app->dat2_disk);
    if( app->dat1_disk )
        RSCache_Dat1DiskFree(app->dat1_disk);
    ToriRS_TaskQueue_Free(app->runner.queue);
    ToriRS_IO_Free(app->runner.io);
}

/* Scene font id for the minimenu (reference uses bold-12; dat2 fonts-table
 * archive 496 in this cache era, e.g. bank title font). Dat1 has no fonts
 * table: its fonts live in the title jagfile and are pinned at cache-font
 * slots 0-3 by RevConfig, where b12 is slot 2. Falls back to any text node's
 * already-resolved scene font when b12 cannot load. */
static int
app_minimenu_font_scene_id(struct App* app)
{
    enum
    {
        APP_FONT_B12_CACHE_ID = 496,
        APP_FONT_B12_DAT1_SLOT = 2,
    };
    int font_cache_id = app->cfg.cache_kind == APP_CACHE_DAT1 ? APP_FONT_B12_DAT1_SLOT
                                                              : APP_FONT_B12_CACHE_ID;
    int scene_id = UITreeSceneBridge_EnsureFont(&app->bridge, font_cache_id);
    if( scene_id <= 0 )
    {
        struct ToriRS_Task* task = CreateTask_FontLoad(app->provider, font_cache_id);
        if( task )
        {
            ToriRS_TaskQueue_Add(app->runner.queue, task);
            TaskRunner_Drain(&app->runner);
            scene_id = UITreeSceneBridge_EnsureFont(&app->bridge, font_cache_id);
        }
    }
    if( scene_id <= 0 )
    {
        for( uint32_t i = 0; i < app->tree->component_count; i++ )
        {
            struct UITreeComponent const* node = &app->tree->components[i];
            if( !node->freed && node->type == UIELEM_RS_TEXT && node->u.rs_text.font_id > 0 )
            {
                scene_id = node->u.rs_text.font_id;
                break;
            }
        }
    }
    return scene_id;
}

/* Overlay chrome the interface pack does not provide: the click cross and the
 * minimenu popup live as late root siblings (drawn/hit-tested above the
 * interface) and stay invisible until the host reports them active. */
static void
app_push_builtin_overlay_nodes(struct App* app)
{
    struct UITreeNodeSpec spec;
    int font_id = app_minimenu_font_scene_id(app);

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_BUILTIN_CROSS;
    spec.component_id = APP_COM_ID_CROSS;
    spec.width = 16;
    spec.height = 16;
    assert(UITree_Push(app->tree, -1, &spec) >= 0);

    /* Pushed before the minimenu so the popup draws over the hover line — the
     * two are never both up (4726 returns early on minimenu_isopen), but the
     * ordering keeps that invariant from mattering. */
    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_BUILTIN_HOVERTEXT;
    spec.component_id = APP_COM_ID_HOVERTEXT;
    spec.u.hovertext.font_id = font_id;
    assert(UITree_Push(app->tree, -1, &spec) >= 0);

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_BUILTIN_MINIMENU;
    spec.component_id = APP_COM_ID_MINIMENU;
    spec.u.minimenu.font_id = font_id;
    assert(UITree_Push(app->tree, -1, &spec) >= 0);

    app->interact.minimenu.font_id = font_id;
    app->hover_text.font_id = font_id;
}

/* World_HeightFn: projectiles/movers track terrain height (world units). */
static int
app_world_height(
    void* userdata,
    int world_x,
    int world_z,
    int level)
{
    struct App* app = (struct App*)userdata;
    if( !app->world || !app->world->heightmap )
        return 0;
    return heightmap_get_interpolated(app->world->heightmap, world_x, world_z, level);
}

/* Bake the loaded world's minimap tiles into a single scene sprite the minimap
 * widget blits from (v1 GameRunescape_RebuildWorldMap). SceneSpriteAdd frees any
 * previous entry, so the reload hotkey just overwrites in place. */
static void
app_rebuild_world_map(struct App* app)
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

    argb = minimap_bake_argb(app->world->minimap, &pixel_w, &pixel_h);
    if( !argb )
        return;

    sprite = ToriDraw_SpriteNewFromArgbOwned(argb, pixel_w, pixel_h);
    if( !sprite )
    {
        free(argb);
        return;
    }

    sprites = malloc(sizeof(*sprites));
    if( !sprites )
    {
        ToriDraw_SpriteFree(sprite);
        return;
    }
    sprites[0] = sprite;

    ToriDraw_SceneSpriteAdd(app->scene, UITREE_SCENE_WORLD_MAP_SPRITE_ID, sprites, 1);
    app->world_map_scene_id = UITREE_SCENE_WORLD_MAP_SPRITE_ID;
    app->world_map_w = pixel_w;
    app->world_map_h = pixel_h;
}

/* Queue Task_WorldLoad for the configured chunk list and drain it. Reused by
 * the reload hotkey; assets already cached make a reload near-instant. */
static void
app_world_load(struct App* app)
{
    int chunks[2] = { 50, 50 };
    struct ToriRS_Task* task;

    {
        char const* env = getenv("TORIRS_WORLD_MAP");
        if( env && sscanf(env, "%d,%d", &chunks[0], &chunks[1]) != 2 )
        {
            chunks[0] = 50;
            chunks[1] = 50;
        }
    }

    app->world_load_attempted = 1;
    task = CreateTask_WorldLoad(app->provider, app->world_builder, chunks, 1);
    ToriRS_TaskQueue_Add(app->runner.queue, task);
    TaskRunner_Drain(&app->runner);

    if( app->world->load_complete )
    {
        app->world_active = 1;
        World_SetHeightFn(app->world, app_world_height, app);
        /* v1 scene-reset camera: scene center, above ground, looking down. */
        app->world_camera_pos.x = app->world->_scene_size / 2 * 128 + 64;
        app->world_camera_pos.z = app->world->_scene_size / 2 * 128 + 64;
        app->world_camera_pos.y = -2000;
        app->world_camera.pitch = 450;
        app->world_camera.yaw = 0;
        /* TORIRS_WORLD_CAM=x,y,z,pitch,yaw: place the camera explicitly (scene coords, y negative
         * above ground). The startup camera looks near-straight down, where same-tile locs barely
         * overlap, so the headless BMP path cannot otherwise reproduce an oblique view — the sim
         * harness only injects letters/digits and pitch is bound to the arrow keys. */
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
        /* World scenery models reference textures; the bridge scan walks the
         * scene elements the rebuild just created. */
        app_sync_textures(app);
        app_rebuild_world_map(app);
    }
    else
    {
        fprintf(stderr, "app: world load incomplete (map %d,%d)\n", chunks[0], chunks[1]);
    }
    app->need_redraw = 1;
}

/* Cache the WORLD node's emit desc: the mouse gate rect and the viewport the
 * frame emitter draws with (pick/render parity comes from sharing it). Also the
 * "is a world on screen this frame" flag every world subsystem gates on. Uses
 * the previous frame's emit buffer — the world box only changes on relayout. */
static void
app_update_world_viewport(struct App* app)
{
    app->world_view_valid = 0;
    for( int i = 0; i < app->emit.count; i++ )
    {
        if( app->emit.cmds[i].kind == UITREE_EMIT_WORLD )
        {
            app->world_emit_desc = app->emit.cmds[i];
            app->world_view_valid = 1;
            return;
        }
    }
}

int32_t
App_WorldNodeIndex(struct App const* app)
{
    assert(app);
    for( uint32_t i = 0; i < app->tree->component_count; i++ )
    {
        struct UITreeComponent const* node = &app->tree->components[i];
        if( !node->freed && node->type == UIELEM_BUILTIN_WORLD )
            return (int32_t)i;
    }
    return -1;
}

void
App_OpenRootInterface(
    struct App* app,
    int interface_id)
{
    assert(app);
    memset(&app->open_stats, 0, sizeof(app->open_stats));

    if( getenv("TORIRS_CS2_TRACE") )
        g_cs2_trace_mode = 2;

    if( app->cfg.revconfig_ui_ini && app->cfg.revconfig_ui_ini[0] )
    {
        /* RevConfig build: the INI names the whole gameframe (chrome widgets
         * plus the cache interface packs mounted under them), so it replaces
         * the interface open rather than wrapping it. The CS2 host is passed
         * only for dat2 — dat1 interface packs carry IF1 scripts, which the
         * CS1 host evaluates on the tick instead. */
        UITreeBuilder_InitEx(
            &app->builder,
            app->provider,
            app->tree,
            &app->invs,
            app->cfg.cache_kind == APP_CACHE_DAT1 ? NULL : &app->host,
            app->cfg.revconfig_ui_ini,
            app->cfg.revconfig_cache_ini);
        /* Bake remaps sprite/font ids to scene ids so the tree renders directly. */
        app->builder.bridge = &app->bridge;
        app->builder_active = 1;

        ToriRS_TaskQueue_Add(app->runner.queue, CreateTask_UITreeBuild(&app->builder));
        TaskRunner_Drain(&app->runner);

        printf(
            "RevConfigBuild done: ui=%s tree_components=%u sprites=%d fonts=%d onloads=%d\n",
            app->cfg.revconfig_ui_ini,
            app->tree->component_count,
            app->builder.sprite_count,
            app->builder.font_count,
            app->builder.onload_count);
    }
    else
    {
        /* Open the requested interface directly as the tree root (TS parity:
         * WidgetManager.setRootInterface(groupId) — any group can be the root;
         * no hardcoded 161 chrome required). */
        struct ToriRS_Task* root_task = CreateTask_InterfaceOpen(
            app->provider,
            app->tree,
            &app->host,
            &app->invs,
            &app->bridge,
            interface_id,
            &app->open_stats);
        assert(root_task != NULL);
        ToriRS_TaskQueue_Add(app->runner.queue, root_task);
        TaskRunner_Drain(&app->runner);

        printf(
            "InterfaceOpen done: iface=%d pack_components=%d tree_components=%u onloads=%d "
            "inv_hooks=%d var_hooks=%d mounts=%d\n",
            app->open_stats.interface_id,
            app->open_stats.pack_component_count,
            app->tree->component_count,
            app->open_stats.onload_count,
            app->host.inv_transmit_hook_count,
            app->host.var_transmit_hook_count,
            app->tree->interface_parent_count);
    }

    if( getenv("TORIRS_ANIM_DEBUG") )
    {
        for( uint32_t i = 0; i < app->tree->component_count; i++ )
        {
            struct UITreeComponent const* node = &app->tree->components[i];
            if( node->freed || node->type != UIELEM_RS_MODEL )
                continue;
            fprintf(
                stderr,
                "anim_debug: com=0x%x model=%d seq=%d\n",
                node->component_id,
                node->u.rs_model.gamecache_model_id,
                node->u.rs_model.anim_seq_id);
        }
    }

    app_push_builtin_overlay_nodes(app);

    /* Load model-widget sequences and apply the first frame before the
     * initial render; App_RunOnce advances frames each tick. */
    UITreeAnim_RequestMissing(
        app->tree, app->scene, app->provider, app->runner.queue, &app->seq_loads);
    TaskRunner_Drain(&app->runner);
    UITreeAnim_Advance(app->tree, app->scene, 0);

    app_sync_textures(app);

    /* No viewport component in the opened interface -> no map at all. Trees
     * that grow one later (a mounted interface) load lazily in App_RunOnce. */
    if( App_WorldNodeIndex(app) >= 0 )
        app_world_load(app);

    app->emit.count = 0;
    UITree_EmitWalk(app->tree, &app->ui_host, &app->emit, -1);
    /* Prime the viewport cache so the first App_Render can draw the world
     * without waiting for a App_RunOnce pass to latch it. */
    app_update_world_viewport(app);
    app->need_redraw = 1;
}

/* One 20ms client tick: clock, widget timers, animation loads + advance. */
static int
app_logic_tick(struct App* app)
{
    int redraw = 0;

    RS_CS2Host_Tick(&app->host);

    /* World map panning and element flashing advance on the client tick, the
     * same clock the map's own onTimer scripts run on. */
    if( RS_WorldMap_Cycle(app->host.worldmap) )
        redraw = 1;

    /* onTimer fires once per client tick for every component with a timer
     * hook (reference processWidgetTimers). Component ids are snapshotted
     * first because a hook can CC_CREATE children and realloc the array. */
    {
        int timer_ids[256];
        int timer_n = 0;
        for( uint32_t ti = 0; ti < app->tree->component_count && timer_n < 256; ti++ )
        {
            struct UITreeComponent const* tc = &app->tree->components[ti];
            if( tc->component_id >= 0 && tc->runtime_hooks.on_timer.script_id > 0 )
                timer_ids[timer_n++] = tc->component_id;
        }
        for( int i = 0; i < timer_n; i++ )
        {
            int32_t idx = UITree_FindByComponentId(app->tree, timer_ids[i]);
            if( idx < 0 )
                continue;
            RS_CS2_DispatchHook(
                &app->host,
                &app->runner,
                timer_ids[i],
                &app->tree->components[idx].runtime_hooks.on_timer);
            redraw = 1;
        }
    }

    /* Widgets-loaded transmit traversal, once per tick (TS processWidgetTransmits).
     * Early-outs unless a widget was unhidden this tick; per-hook serial gating
     * means already-fired hooks run nothing. */
    RS_CS2_PumpTransmits(&app->host, &app->runner);

    /* CS1 (IF1) value scripts drive active state and %N text. The reference
     * re-evaluates them at draw time; here a task does it once per tick so the
     * VM's asset yields can be serviced asynchronously, and the emit pass just
     * reads the cached results. */
    if( app_run_cs1_eval(app) )
        redraw = 1;

    /* TORIRS_STATS=1: periodic growth diagnostics — component_count must stay
     * flat under the CC_DELETEALL/CC_CREATE rebuild pattern (reclamation). */
    {
        static int stats_enabled = -1;
        static int stats_tick = 0;
        if( stats_enabled < 0 )
            stats_enabled = getenv("TORIRS_STATS") != NULL;
        if( stats_enabled && ++stats_tick % 250 == 0 )
            fprintf(
                stderr,
                "torirs_stats: tick=%d components=%u free_head=%d inv_hooks=%d var_hooks=%d\n",
                stats_tick,
                app->tree->component_count,
                app->tree->free_head,
                app->host.inv_transmit_hook_count,
                app->host.var_transmit_hook_count);
    }

    {
        static int anim_dbg = -1;
        static int anim_dbg_tick = 0;
        if( anim_dbg < 0 )
            anim_dbg = getenv("TORIRS_ANIM_DEBUG") != NULL;
        if( anim_dbg && ++anim_dbg_tick % 25 == 0 )
        {
            for( uint32_t i = 0; i < app->tree->component_count; i++ )
            {
                struct UITreeComponent const* node = &app->tree->components[i];
                if( node->freed || node->type != UIELEM_RS_MODEL )
                    continue;
                if( node->u.rs_model.anim_seq_id < 0 )
                    continue;
                fprintf(
                    stderr,
                    "anim_tick t=%d com=0x%x seq=%d frame=%d\n",
                    anim_dbg_tick,
                    node->component_id,
                    node->u.rs_model.anim_seq_id,
                    node->u.rs_model.anim_frame);
            }
        }
    }

    /* Animations: request missing sequences (async), apply what's loaded.
     * In-flight sequences render at rest pose until they land. */
    UITreeAnim_RequestMissing(
        app->tree, app->scene, app->provider, app->runner.queue, &app->seq_loads);
    TaskRunner_Drain(&app->runner);
    if( UITreeAnim_Advance(app->tree, app->scene, 1) )
        redraw = 1;

    /* CS2 hooks this tick may have ensured new textured models. */
    app_sync_textures(app);

    if( UICross_IsActive(&app->cross) )
    {
        UICross_Tick(&app->cross, APP_LOGIC_TICK_MS);
        redraw = 1;
    }

    return redraw;
}

/* Movers (players/npcs) and in-flight projectiles push their sim positions
 * into the scene elements the frame emitter draws (v1 synced projectiles;
 * movers were spawn-time only there because nothing pathed them). */
static void
app_world_sync_positions(struct App* app)
{
    struct World* world = app->world;
    struct World_EntityPool* pool;

    pool = &world->entities.player;
    for( int pi = World_EntityPoolHead(pool); pi != WORLD_ENTITY_NIL;
         pi = World_EntityPoolNext(pool, pi) )
    {
        struct WorldEntity_Player* player = World_EntityPoolGet(pool, pi);
        if( !player || player->element_id < 0 )
            continue;
        int wx = (int)player->draw_position.x;
        int wz = (int)player->draw_position.z;
        int wy = app_world_height(app, wx, wz, player->grid_position.level);
        ToriDraw_SceneElementSetPosition(
            app->scene, player->element_id, wx, wy, wz, player->orientation.yaw);
    }

    pool = &world->entities.npc;
    for( int ni = World_EntityPoolHead(pool); ni != WORLD_ENTITY_NIL;
         ni = World_EntityPoolNext(pool, ni) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, ni);
        if( !npc || npc->element_id < 0 )
            continue;
        int wx = (int)npc->draw_position.x;
        int wz = (int)npc->draw_position.z;
        int wy = app_world_height(app, wx, wz, npc->grid_position.level);
        ToriDraw_SceneElementSetPosition(
            app->scene, npc->element_id, wx, wy, wz, npc->orientation.yaw);
    }

    pool = &world->entities.projectile;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Projectile* proj = World_EntityPoolGet(pool, i);
        if( !proj || proj->element_id < 0 || !proj->launched )
            continue;
        ToriDraw_SceneElementSetPositionPitchYaw(
            app->scene,
            proj->element_id,
            (int)proj->x,
            (int)proj->y,
            (int)proj->z,
            proj->orientation.pitch,
            proj->orientation.yaw);
    }
}

/* One client tick of scene-element animation frames. UITreeAnim only advances
 * UI model widgets; world scene elements (scenery + entities) advance here
 * (v1 GameRunescape_TickAnimations, both classic and skeletal branches). */
static void
app_world_tick_animations(struct App* app)
{
    int slot_count = ToriDraw_SceneElementSlotCount(app->scene);
    for( int element_id = 0; element_id < slot_count; element_id++ )
    {
        struct ToriDraw_SceneElement* element;

        if( !ToriDraw_SceneElementIsLive(app->scene, element_id) )
            continue;
        element = ToriDraw_SceneElementGet(app->scene, element_id);
        if( !element || element->anim_seq_id == -1 )
            continue;

        if( element->is_skeletal )
        {
            const struct ToriDraw_SkeletalAnim* skeletal = element->skeletal_animation;
            int play_frames;
            if( !skeletal || skeletal->frame_count <= 0 )
                continue;
            play_frames = element->skeletal_play_frames;
            if( play_frames <= 0 || play_frames > skeletal->frame_count )
                play_frames = skeletal->frame_count;
            element->anim_cycle++;
            if( element->anim_cycle >= 1 )
            {
                element->anim_frame = (element->anim_frame + 1) % play_frames;
                element->anim_cycle = 0;
            }
        }
        else
        {
            const struct ToriDraw_Animation* anim = element->animation;
            if( !anim || anim->frame_count <= 0 || !anim->frames )
                continue;
            element->anim_cycle++;
            if( element->anim_cycle >= anim->frames[element->anim_frame].delay )
            {
                element->anim_frame = (element->anim_frame + 1) % anim->frame_count;
                element->anim_cycle = 0;
            }
        }
    }
}

enum
{
    APP_CAMERA_MOVEMENT_SPEED = 70, /* v1 RUNESCAPE_CAMERA_MOVEMENT_SPEED */
    APP_CAMERA_ROTATION_SPEED = 10,
};

static int
app_world_drawable(struct App* app)
{
    /* world_view_valid == a WORLD desc survived the last emit walk, so a hidden
     * or absent viewport component costs nothing: no paint, no 3D, no pick. */
    return app->world_view_valid && app->world && app->world->load_complete &&
           app->world->painter && app->painter_buffer;
}

/* Fill the painter buffer for the current camera. Called by App_Render once
 * per frame (painter_paint_bucket resets command_count, so repainting is
 * safe). */
static void
app_world_paint(struct App* app)
{
    int cam_sx = app->world_camera_pos.x / 128;
    int cam_sz = app->world_camera_pos.z / 128;
    int cam_slevel = app->world_camera_pos.y / 240;
    /* The viewport component owns the level mask (RevConfig `levels=`); older
     * nodes leave it 0, which would draw nothing — treat that as all levels. */
    uint8_t level_mask = app->world_emit_desc.world_level_mask;
    if( cam_slevel < 0 )
        cam_slevel = 0;
    if( cam_slevel > 3 )
        cam_slevel = 3;
    if( !level_mask )
        level_mask = 0xF;
    painter_set_camera_angles(app->world->painter, app->world_camera.pitch, app->world_camera.yaw);
    painter_set_level_mask(app->world->painter, level_mask);

    painter_paint_bucket(app->world->painter, app->painter_buffer, cam_sx, cam_sz, cam_slevel);
}

/* "Only hittest the world if the mouse is over the world element": inside the
 * world emit clip rect with no interactive UI hovered on top (the world node
 * itself is pass-through, so hover_com_id < 0 over bare world). */
static int
app_world_mouse_gate(
    struct App* app,
    int mouse_x,
    int mouse_y,
    int hover_com_id)
{
    struct UITreeEmitClip const* clip;

    if( !app->world_active || !app->world_view_valid || hover_com_id >= 0 )
        return 0;
    clip = &app->world_emit_desc.clip;
    return mouse_x >= clip->x && mouse_x < clip->x + clip->w && mouse_y >= clip->y &&
           mouse_y < clip->y + clip->h;
}

/* Classify the raw hits the render pass collected into the app pickset +
 * hover tile. Runs after ToriRS_Soft3D_RenderFrame when the pick was armed. */
static void
app_world_pick_finish(
    struct App* app,
    struct ToriRS_PickHits const* hits)
{
    struct ToriRS_PickResult result;

    ToriRS_PickHitsClassify(app->world, hits, &app->world_pickset, &result);
    if( result.hover_tile_valid )
    {
        app->world_hover_tile_x = result.hover_tile_x;
        app->world_hover_tile_z = result.hover_tile_z;
        app->world_hover_tile_level = result.hover_tile_level;
    }
    else
    {
        app->world_hover_tile_x = -1;
        app->world_hover_tile_z = -1;
    }

    if( getenv("TORIRS_WORLD_PICK_DEBUG") )
    {
        fprintf(
            stderr,
            "world_pick: mouse=%d,%d count=%d hover_tile=%d,%d,%d\n",
            app->world_mouse_x,
            app->world_mouse_y,
            app->world_pickset.count,
            result.hover_tile_valid ? result.hover_tile_x : -1,
            result.hover_tile_valid ? result.hover_tile_z : -1,
            result.hover_tile_valid ? result.hover_tile_level : -1);
        for( int i = 0; i < app->world_pickset.count; i++ )
        {
            /* Loc id/name/footprint turn an element id into something you can look up in the
             * cache — the difference between "element 4345 draws late" and "the plinth is a
             * separate 1x1 loc one tile nearer than the statue". */
            struct WorldEntity_Scenery* scenery =
                World_SceneryGetByElementId(app->world, app->world_pickset.items[i].element_id);
            fprintf(
                stderr,
                "world_pick:  [%d] element=%d type=%d tile=%d,%d,%d loc=%d size=%dx%d origin=%d,%d,%d '%s'\n",
                i,
                app->world_pickset.items[i].element_id,
                (int)app->world_pickset.items[i].type,
                app->world_pickset.items[i].tile_x,
                app->world_pickset.items[i].tile_z,
                app->world_pickset.items[i].tile_level,
                scenery ? scenery->loc_id : -1,
                scenery ? scenery->size_x : -1,
                scenery ? scenery->size_z : -1,
                scenery ? scenery->grid_position.x : -1,
                scenery ? scenery->grid_position.z : -1,
                scenery ? scenery->grid_position.level : -1,
                scenery ? scenery->name : "");
        }
    }
}

static void
app_camera_move_forward(
    struct App* app,
    int amount)
{
    int direction_x = ToriDraw_Sin(app->world_camera.yaw);
    int direction_z = ToriDraw_Cos(app->world_camera.yaw);
    app->world_camera_pos.x -= (direction_x * amount) >> 16;
    app->world_camera_pos.z += (direction_z * amount) >> 16;
}

static void
app_camera_move_left(
    struct App* app,
    int amount)
{
    int direction_x = ToriDraw_Cos(app->world_camera.yaw);
    int direction_z = ToriDraw_Sin(app->world_camera.yaw);
    app->world_camera_pos.x += (direction_x * amount) >> 16;
    app->world_camera_pos.z += (direction_z * amount) >> 16;
}

/* World camera keys (v1 mapping): W/S forward/back, A/D strafe, R/F up/down,
 * arrows yaw/pitch. Suppressed while any visible onKey target wanted the
 * keyboard this frame, so typing in the UI never flies the camera. */
static void
app_world_camera_keys(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out)
{
    const int move = APP_CAMERA_MOVEMENT_SPEED;
    const int rotate = APP_CAMERA_ROTATION_SPEED;

    /* No key_target gating: the reference broadcasts every key to onKey
     * scripts AND moves the camera in the same frame; there is no focused
     * text-input concept to defer to yet. The viewport still has to be on
     * screen — with no world drawn these keys belong to the interface. */
    if( !app->world_active || !app->world_view_valid )
        return;
    (void)out;

    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_W) )
        app_camera_move_forward(app, move);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_S) )
        app_camera_move_forward(app, -move);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_A) )
        app_camera_move_left(app, -move);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_D) )
        app_camera_move_left(app, move);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_R) )
        app->world_camera_pos.y -= move;
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_F) )
        app->world_camera_pos.y += move;
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_LEFT) )
        app->world_camera.yaw = ToriDraw_AddAngle(app->world_camera.yaw, rotate);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_RIGHT) )
        app->world_camera.yaw = ToriDraw_AddAngle(app->world_camera.yaw, -rotate);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_UP) )
        app->world_camera.pitch = ToriDraw_AddAngle(app->world_camera.pitch, rotate);
    if( LibToriRS_Input_IsKeyHeld(input, TORIRSK_DOWN) )
        app->world_camera.pitch = ToriDraw_AddAngle(app->world_camera.pitch, -rotate);

    /* M: reload the world through the task system (assets cached -> fast;
     * rebuild clears world scene elements incl. spawned entities). */
    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_M) && App_WorldNodeIndex(app) >= 0 )
        app_world_load(app);
}

/* Classic human animation set (players; INTERFACE_PLAYER_IDLE_SEQ parity). */
enum
{
    APP_PLAYER_SEQ_READY = 808,
    APP_PLAYER_SEQ_WALK = 819,
    APP_PLAYER_SEQ_WALK_B = 820,
    APP_PLAYER_SEQ_WALK_L = 821,
    APP_PLAYER_SEQ_WALK_R = 822,
    APP_PLAYER_SEQ_TURN = 823,
    APP_PLAYER_SEQ_RUN = 824,
};

/* Wrap a freshly built (owned) model in a new dynamic scene element. The
 * element owns the model from here (SceneElementRemove frees it), which is
 * why spawns copy registry models instead of sharing handles. */
static int
app_world_scene_element_create(
    struct App* app,
    struct ToriDraw_Model* model,
    int world_x,
    int world_y,
    int world_z)
{
    struct ToriDraw_ModelHandle hnd;
    int element_id = ToriDraw_SceneElementAdd(app->scene);

    if( element_id < 0 )
    {
        ToriDraw_ModelFree(model);
        return -1;
    }
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = model;
    ToriDraw_SceneElementSetModel(app->scene, element_id, hnd);
    ToriDraw_SceneElementSetPosition(app->scene, element_id, world_x, world_y, world_z, 0);
    {
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(app->scene, element_id);
        if( el )
            el->dynamic = true;
    }
    return element_id;
}

/* Load a sequence through the task system (no-op when cached) and attach it. */
static void
app_world_apply_seq(
    struct App* app,
    int element_id,
    int seq_id)
{
    struct ToriRS_Task* task;

    if( seq_id < 0 )
        return;
    task = CreateTask_SequenceLoad(app->provider, app->scene, seq_id);
    if( task )
    {
        ToriRS_TaskQueue_Add(app->runner.queue, task);
        TaskRunner_Drain(&app->runner);
    }
    if( ToriDraw_SceneAnimationHas(app->scene, seq_id) )
    {
        /* Bind the resolved animation onto the element — the tick loop and
         * frame emitter read element->animation, which SetAnimationSeq alone
         * leaves NULL. Skip the empty sentinel (failed / maya-only seqs). */
        struct ToriDraw_Animation* anim = ToriDraw_SceneAnimationGet(app->scene, seq_id);
        if( anim && anim->frame_count > 0 && anim->frames && anim->base )
        {
            ToriDraw_SceneElementSetAnimationSeq(app->scene, element_id, seq_id);
            ToriDraw_SceneElementSetAnimation(app->scene, element_id, anim, true);
        }
    }
}

/* Config-driven color/texture swaps for a built model (npc/loc style). NULL
 * where the caller has none. */
struct AppModelRecolorSpec
{
    const int* recolors_from;
    const int* recolors_to;
    int recolor_count;
    const int* retextures_from;
    const int* retextures_to;
    int retexture_count;
};

/* Load (via tasks) + convert + merge + recolor + light one drawable model from
 * cache model ids. Returns an owned model or NULL. */
static struct ToriDraw_Model*
app_world_build_model(
    struct App* app,
    const int* model_ids,
    int count,
    const struct AppModelRecolorSpec* recolors)
{
    struct ToriDraw_Model* parts[16];
    struct ToriDraw_Model* model = NULL;
    int part_count = 0;
    int queued = 0;

    for( int i = 0; i < count; i++ )
    {
        struct ToriRS_Task* task = CreateTask_ModelLoad(app->provider, model_ids[i]);
        if( task )
        {
            ToriRS_TaskQueue_Add(app->runner.queue, task);
            queued++;
        }
    }
    if( queued )
        TaskRunner_Drain(&app->runner);

    for( int i = 0; i < count && part_count < 16; i++ )
    {
        struct ToriRS_Model* rs = CacheProvider_ModelGet(app->provider, model_ids[i]);
        struct ToriDraw_Model* part = rs ? ToriDraw_ModelFromToriRS(rs) : NULL;
        if( part )
            parts[part_count++] = part;
    }
    if( part_count == 0 )
        return NULL;

    if( part_count > 1 )
    {
        model = ToriDraw_ModelMerge(parts, part_count);
        for( int i = 0; i < part_count; i++ )
            ToriDraw_ModelFree(parts[i]);
    }
    else
        model = parts[0];
    if( !model )
        return NULL;

    /* Recolor before lighting: lighting bakes face colors into per-vertex
     * shaded colors, so a swap afterwards would be a no-op (same order as
     * scenery apply_transforms and the obj icon path in the scene bridge). */
    if( recolors )
    {
        for( int i = 0; i < recolors->recolor_count; i++ )
            ToriDraw_ModelRecolor(model, recolors->recolors_from[i], recolors->recolors_to[i]);
        for( int i = 0; i < recolors->retexture_count; i++ )
            ToriDraw_ModelRetexture(model, recolors->retextures_from[i], recolors->retextures_to[i]);
    }

    {
        struct ToriDraw_ModelHandle hnd;
        memset(&hnd, 0, sizeof(hnd));
        hnd.kind = TORIDRAWMK_MODEL;
        hnd.u.model.model = model;
        ToriDraw_LightModelDefaultPreScaled(hnd, 0, 0);
    }
    ToriDraw_ModelSetBoundsCylinder(model);
    ToriDraw_ModelCaptureOriginalVertices(model);
    return model;
}

/* Hotkey 9: default player model on the hovered tile. */
static void
app_world_spawn_player(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    int scene_model_id;
    struct ToriDraw_ModelHandle reg;
    struct ToriDraw_Model* copy;
    int world_x = tile_x * 128 + 64;
    int world_z = tile_z * 128 + 64;
    int world_y;
    int element_id;

    {
        struct ToriRS_Task* task = CreateTask_PlayerAppearanceLoad(app->provider);
        if( task )
        {
            ToriRS_TaskQueue_Add(app->runner.queue, task);
            TaskRunner_Drain(&app->runner);
        }
    }
    scene_model_id = UITreeSceneBridge_EnsurePlayerModel(&app->bridge);
    if( scene_model_id <= 0 )
    {
        fprintf(stderr, "spawn_player: player model unavailable\n");
        return;
    }
    reg = ToriDraw_SceneModelGet(app->scene, scene_model_id);
    if( reg.kind != TORIDRAWMK_MODEL || !reg.u.model.model )
        return;
    copy = ToriDraw_ModelCopy(reg.u.model.model);
    if( !copy )
        return;
    ToriDraw_ModelSetBoundsCylinder(copy);
    ToriDraw_ModelCaptureOriginalVertices(copy);

    world_y = app_world_height(app, world_x, world_z, level);
    element_id = app_world_scene_element_create(app, copy, world_x, world_y, world_z);
    if( element_id < 0 )
        return;
    app_world_apply_seq(app, element_id, APP_PLAYER_SEQ_READY);

    {
        struct WorldEntityFacet_IdleAnimations idle = {
            .readyanim = APP_PLAYER_SEQ_READY,
            .walkanim = APP_PLAYER_SEQ_WALK,
            .turnanim = APP_PLAYER_SEQ_TURN,
            .runanim = APP_PLAYER_SEQ_RUN,
            .walkanim_b = APP_PLAYER_SEQ_WALK_B,
            .walkanim_r = APP_PLAYER_SEQ_WALK_R,
            .walkanim_l = APP_PLAYER_SEQ_WALK_L,
        };
        World_PlayerSpawn(app->world, element_id, level, tile_x, tile_z, idle);
    }
    fprintf(
        stderr,
        "spawn_player: element=%d tile=%d,%d level=%d\n",
        element_id,
        tile_x,
        tile_z,
        level);
    app_sync_textures(app);
    app->need_redraw = 1;
}

/* Hotkey 8: npc on the hovered tile (TORIRS_SPAWN_NPC=<id> override). */
static void
app_world_spawn_npc(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    int npc_id = 3106; /* OSRS-era "Man" */
    struct ToriRS_Npctype* npctype;
    struct ToriDraw_Model* model;
    int size;
    int world_x, world_z, world_y;
    int element_id;
    int idx;

    {
        char const* env = getenv("TORIRS_SPAWN_NPC");
        if( env )
            npc_id = (int)strtol(env, NULL, 0);
    }
    {
        struct ToriRS_Task* task = CreateTask_NpcLoad(app->provider, npc_id);
        if( task )
        {
            ToriRS_TaskQueue_Add(app->runner.queue, task);
            TaskRunner_Drain(&app->runner);
        }
    }
    npctype = CacheProvider_NpctypeGet(app->provider, npc_id);
    if( !npctype || npctype->models_count <= 0 )
    {
        fprintf(stderr, "spawn_npc: npc %d unavailable\n", npc_id);
        return;
    }

    {
        struct AppModelRecolorSpec recolors = {
            .recolors_from = npctype->recolors_from,
            .recolors_to = npctype->recolors_to,
            .recolor_count = npctype->recolor_count,
            .retextures_from = npctype->retextures_from,
            .retextures_to = npctype->retextures_to,
            .retexture_count = npctype->retexture_count,
        };
        model = app_world_build_model(app, npctype->models, npctype->models_count, &recolors);
    }
    if( !model )
    {
        fprintf(stderr, "spawn_npc: npc %d models failed to load\n", npc_id);
        return;
    }

    size = npctype->size > 0 ? npctype->size : 1;
    world_x = tile_x * 128 + size * 64;
    world_z = tile_z * 128 + size * 64;
    world_y = app_world_height(app, world_x, world_z, level);
    element_id = app_world_scene_element_create(app, model, world_x, world_y, world_z);
    if( element_id < 0 )
        return;

    {
        struct WorldEntityFacet_IdleAnimations idle = {
            .readyanim = -1,
            .walkanim = -1,
            .turnanim = -1,
            .runanim = -1,
            .walkanim_b = -1,
            .walkanim_r = -1,
            .walkanim_l = -1,
        };
        idx = World_NpcSpawn(app->world, element_id, npc_id, level, tile_x, tile_z, size, idle);
    }
    /* Spawn does not carry menu data; the minimenu rows read it off the
     * entity, so copy name/actions/level from the config here. */
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(&app->world->entities.npc, idx);
        if( npc )
        {
            npc->combat_level = npctype->combat_level;
            snprintf(npc->name, sizeof(npc->name), "%s", npctype->name);
            for( int i = 0; i < 5; i++ )
                snprintf(
                    npc->actions[i].name, sizeof(npc->actions[i].name), "%s", npctype->actions[i]);
        }
    }
    fprintf(
        stderr,
        "spawn_npc: npc=%d element=%d tile=%d,%d level=%d size=%d recolors=%d retextures=%d\n",
        npc_id,
        element_id,
        tile_x,
        tile_z,
        level,
        size,
        npctype->recolor_count,
        npctype->retexture_count);
    app_sync_textures(app);
    app->need_redraw = 1;
}

/* Hotkey 0, two-press latch: first press marks the hovered tile as source,
 * second launches source -> hovered (same-tile press clears the latch).
 * Arc math lives in World_ProjectileSetTarget/Move (TS reference parity). */
static void
app_world_spawn_projectile(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    int model_id = 3081; /* v0 spawn-test model fallback */
    int model_ids[1];
    struct ToriDraw_Model* model;
    int src_x, src_z, dst_x, dst_z, src_y;
    int range, t2;
    int element_id;

    if( app->proj_src_tile_x < 0 )
    {
        app->proj_src_tile_x = tile_x;
        app->proj_src_tile_z = tile_z;
        app->proj_src_tile_level = level;
        fprintf(stderr, "spawn_projectile: source latched at %d,%d\n", tile_x, tile_z);
        return;
    }
    if( app->proj_src_tile_x == tile_x && app->proj_src_tile_z == tile_z )
    {
        app->proj_src_tile_x = -1;
        app->proj_src_tile_z = -1;
        fprintf(stderr, "spawn_projectile: latch cleared\n");
        return;
    }

    {
        char const* env = getenv("TORIRS_SPAWN_PROJ_MODEL");
        if( env )
            model_id = (int)strtol(env, NULL, 0);
    }
    model_ids[0] = model_id;
    model = app_world_build_model(app, model_ids, 1, NULL);
    if( !model )
    {
        fprintf(stderr, "spawn_projectile: model %d failed to load\n", model_id);
        return;
    }

    src_x = app->proj_src_tile_x * 128 + 64;
    src_z = app->proj_src_tile_z * 128 + 64;
    dst_x = tile_x * 128 + 64;
    dst_z = tile_z * 128 + 64;
    /* World y is negative-up: start slightly above the source ground. */
    src_y = app_world_height(app, src_x, src_z, app->proj_src_tile_level) - 160;

    range = abs(tile_x - app->proj_src_tile_x);
    if( abs(tile_z - app->proj_src_tile_z) > range )
        range = abs(tile_z - app->proj_src_tile_z);
    t2 = 60 + range * 5; /* ticks: base flight + per-tile stretch */

    element_id = app_world_scene_element_create(app, model, src_x, src_y, src_z);
    if( element_id < 0 )
        return;

    World_ProjectileSpawn(
        app->world,
        element_id,
        app->proj_src_tile_level,
        src_x,
        src_z,
        dst_x,
        dst_z,
        src_y,
        144, /* end height above target ground (36 * 4) */
        0,
        t2,
        15, /* launch slope (1/2048 circle units) */
        64);
    fprintf(
        stderr,
        "spawn_projectile: element=%d %d,%d -> %d,%d t2=%d\n",
        element_id,
        app->proj_src_tile_x,
        app->proj_src_tile_z,
        tile_x,
        tile_z,
        t2);
    app->proj_src_tile_x = -1;
    app->proj_src_tile_z = -1;
    app_sync_textures(app);
    app->need_redraw = 1;
}

/* Spawn test hotkeys (readme): 9 player, 8 npc, 0 projectile — all act on the
 * tile under the mouse, so they no-op when nothing is hovered. */
static void
app_world_hotkeys(
    struct App* app,
    struct LibToriRS_Input* input,
    struct UIInteractOut const* out)
{
    /* Spawn hotkeys gate on the hovered world tile, not on onKey targets —
     * under the real gameframe there is always some visible onKey component
     * and gating on it made every press suppress itself. */
    (void)out;
    if( !app->world_active || !app->world_view_valid )
        return;
    if( app->world_hover_tile_x < 0 || app->world_hover_tile_z < 0 )
        return;

    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_9) )
        app_world_spawn_player(
            app, app->world_hover_tile_x, app->world_hover_tile_z, app->world_hover_tile_level);
    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_8) )
        app_world_spawn_npc(
            app, app->world_hover_tile_x, app->world_hover_tile_z, app->world_hover_tile_level);
    if( LibToriRS_Input_IsKeyDown(input, TORIRSK_0) )
        app_world_spawn_projectile(
            app, app->world_hover_tile_x, app->world_hover_tile_z, app->world_hover_tile_level);
}

/* Per-frame world step: sim cycles, event drain (entity removals -> scene),
 * position sync, animation ticks. Runs every frame (cycles may be 0) so the
 * painter dynamic set stays fresh, and forces a redraw while active — but only
 * while a viewport is actually on screen; an unshown world does not tick. */
static void
app_world_frame(
    struct App* app,
    int cycles)
{
    struct World* world = app->world;

    if( !app->world_active || !app->world_view_valid || !world )
        return;

    World_Cycle(world, cycles);

    {
        int count = World_EventsCount(world);
        for( int i = 0; i < count; i++ )
        {
            const struct World_Event* ev = World_EventsPeek(world, i);
            if( ev->kind == WorldEventKind_EntityRemoved && ev->element_id >= 0 )
                ToriDraw_SceneElementRemove(app->scene, ev->element_id);
        }
        World_EventsClear(world);
    }

    app_world_sync_positions(app);

    for( int c = 0; c < cycles; c++ )
        app_world_tick_animations(app);

    /* Texture scroll (water/lava): dat2 texture defs carry direction/speed;
     * the map advances them per elapsed cycle (v1 runescape.c:3893). */
    if( cycles > 0 )
    {
        struct ToriDraw_TextureState* tex_state = ToriDraw_SceneTexState(app->scene);
        if( tex_state )
            ToriDraw_TextureMapAnimate(&tex_state->texture_map, cycles);
    }

    app->need_redraw = 1;
}

static int
app_measure_text_cb(
    void* user,
    int font_id,
    char const* text)
{
    struct App* app = (struct App*)user;
    struct ToriDraw_Font* font = ToriDraw_SceneFontGet(app->scene, font_id);
    if( !font || !text )
        return 0;
    return ToriDraw2D_MeasureString(font, text);
}

/*
 * Mouseover text, rebuilt every frame from a scratch menu at the pointer.
 *
 * This is the client half of what the reference gets from the cache: script
 * 4726 (re-armed each cycle by 4725) bails on minimenu_isopen, reads
 * minimenu_entry / _numops / _type, and has proc 4727 draw one line. Building
 * the same menu the right click would show is exactly what those opcodes
 * report, so both the line drawn here and the CS2 snapshot come from one pass.
 */
static void
app_hover_text_update(
    struct App* app,
    int mouse_x,
    int mouse_y,
    int hover_com_id)
{
    struct UIMinimenu scratch;
    char prev[UITREE_HOVERTEXT_LEN];
    bool const prev_visible = app->hover_text.visible;
    int click_in_world;

    snprintf(prev, sizeof(prev), "%s", app->hover_text.text);

    /* 4726's first gate: no hover line while the Choose Option popup is up. */
    if( app->interact.minimenu.visible || mouse_x < 0 || mouse_y < 0 ||
        mouse_x >= UITREE_LAYOUT_ROOT_W || mouse_y >= UITREE_LAYOUT_ROOT_H )
    {
        app->hover_text.visible = false;
        app->hover_text.text[0] = '\0';
    }
    else
    {
        click_in_world =
            app_world_mouse_gate(app, mouse_x, mouse_y, hover_com_id) && app_world_drawable(app);
        {
            struct RS_MinimenuBuildCtx mctx = {
                .tree = app->tree,
                .ui_host = &app->ui_host,
                .provider = app->provider,
                .runner = &app->runner,
                .invs = &app->invs,
                .chat = NULL,
                .world = app->world,
                /* Same rule the click paths use: world rows only when the
                 * pointer is over bare viewport. */
                .world_pickset = click_in_world ? &app->world_pickset : NULL,
                .click_in_world = click_in_world != 0,
            };
            UIMinimenu_Reset(&scratch);
            scratch.font_id = app->hover_text.font_id;
            RS_Minimenu_Build(&mctx, mouse_x, mouse_y, &scratch);
        }
        UIHoverText_Compose(&scratch, &app->hover_text);
    }

    /* Anchor at the world viewport's top-left (4726's container origin), or
     * the canvas when no viewport is on screen. */
    if( app->world_view_valid )
    {
        app->hover_text.x = app->world_emit_desc.clip.x + APP_HOVERTEXT_INSET_X;
        app->hover_text.y = app->world_emit_desc.clip.y + APP_HOVERTEXT_INSET_Y;
        app->hover_text.w = app->world_emit_desc.clip.w - APP_HOVERTEXT_INSET_X;
    }
    else
    {
        app->hover_text.x = APP_HOVERTEXT_INSET_X;
        app->hover_text.y = APP_HOVERTEXT_INSET_Y;
        app->hover_text.w = UITREE_LAYOUT_ROOT_W - APP_HOVERTEXT_INSET_X;
    }

    if( app->hover_text.visible != prev_visible || strcmp(app->hover_text.text, prev) != 0 )
        app->need_redraw = 1;
}

/* Build + show the minimenu for a right click (reference openMenu: width from
 * the widest row, centered on the click, clamped to the canvas). The tree
 * node stays unpositioned — emit and the interact gesture read the model. */
static void
app_minimenu_open(
    struct App* app,
    int click_x,
    int click_y,
    int click_in_world)
{
    struct RS_MinimenuBuildCtx mctx = {
        .tree = app->tree,
        .ui_host = &app->ui_host,
        .provider = app->provider,
        .runner = &app->runner,
        .invs = &app->invs,
        .chat = NULL,
        .world = app->world,
        .world_pickset = &app->world_pickset,
        .click_in_world = click_in_world != 0,
    };
    struct UIMinimenu* menu = &app->interact.minimenu;
    struct UIMinimenuLayout layout;
    int content_w = 0;
    int line_height = 0;

    RS_Minimenu_Build(&mctx, click_x, click_y, menu);

    {
        struct ToriDraw_Font* font = ToriDraw_SceneFontGet(app->scene, menu->font_id);
        if( font )
            line_height = font->line_height;
    }
    if( UIMinimenu_PrepareShow(menu, line_height, app_measure_text_cb, app, &layout, &content_w) )
    {
        UIMinimenu_ShowAt(
            menu, layout, content_w, click_x, click_y, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        app->need_redraw = 1;
    }
}

/* Execute one selected (or defaulted) menu row: cross feedback + hook
 * dispatch with the row's op index (v1 ui_click_use_minimenu_option). The
 * cross colour comes from the action alone (RS_Minimenu_CrossModeForAction,
 * reference doAction) and is decided once: an action the reference gives no
 * cross — UI buttons, inventory ops, Examine, Cancel — leaves a cross already
 * in flight running rather than clearing or recolouring it. Returns nonzero
 * when a CS2 hook was dispatched. */
static int
app_minimenu_use_option(
    struct App* app,
    int option_index,
    int click_x,
    int click_y)
{
    struct UIMinimenu* menu = &app->interact.minimenu;
    struct UIMinimenuOption opt;

    if( option_index < 0 || option_index >= menu->option_count )
        return 0;
    opt = menu->options[option_index];
    UIMinimenu_Hide(menu);
    app->need_redraw = 1;

    /* Reference doAction has no CANCEL branch at all: dismissing the menu is
     * not an interaction and must not disturb a running cross. */
    if( opt.action == REVCONFIG_MINIMENU_CANCEL )
        return 0;

    {
        enum UICrossMode cross_mode = RS_Minimenu_CrossModeForAction(opt.action);
        if( cross_mode != UI_CROSS_OFF )
            UICross_Show(&app->cross, cross_mode, click_x, click_y);
    }

    switch( opt.pick.kind )
    {
    case UI_MINIMENU_PICK_UI:
    case UI_MINIMENU_PICK_INV_SLOT:
    {
        int32_t idx = UITree_FindByComponentId(app->tree, opt.pick.id);
        struct UITreeRuntimeScriptHook const* hook;
        struct UITreeRuntimeScriptHook hook_copy;
        int hook_com_id = -1;

        if( idx < 0 )
            return 0;
        hook = UITree_ResolveClickHook(app->tree, idx, &hook_com_id);
        if( !hook || hook->script_id <= 0 )
        {
            /* IF1-style static buttons have no CS2 hook; the button engine
             * (APPLY_BUTTON_CLICK) is not implemented in src/main yet. */
            fprintf(
                stderr,
                "minimenu: no hook for com=0x%x action=%d op=%d\n",
                opt.pick.id,
                opt.action,
                opt.action_index);
            return 0;
        }
        hook_copy = *hook;
        RS_CS2_SetEventOp(&app->host, opt.action_index >= 0 ? opt.action_index + 1 : 1, 0);
        RS_CS2_SetEventMouse(&app->host, click_x, click_y);
        RS_CS2_DispatchHook(&app->host, &app->runner, hook_com_id, &hook_copy);
        RS_CS2_SetEventOp(&app->host, 1, 0);
        RS_CS2_PumpTransmits(&app->host, &app->runner);
        return 1;
    }
    case UI_MINIMENU_PICK_TERRAIN:
        /* Walk here: pathe the first spawned player (the closest thing to a
         * local player until state sync exists), else just the cross. */
        if( app->world )
        {
            struct World_EntityPool* pool = &app->world->entities.player;
            int head = World_EntityPoolHead(pool);
            if( head != WORLD_ENTITY_NIL )
                World_PlayerPathJump(
                    app->world, head, false, opt.pick.secondary_id, opt.pick.tertiary_id);
            else
                fprintf(
                    stderr,
                    "minimenu: walk %d,%d level=%d (no player)\n",
                    opt.pick.secondary_id,
                    opt.pick.tertiary_id,
                    opt.pick.quaternary_id);
        }
        return 0;
    case UI_MINIMENU_PICK_NPC:
        /* No interaction sim yet: face the walking player at it and log. */
        fprintf(
            stderr,
            "minimenu: opnpc%d npc_id=%d element=%d tile=%d,%d\n",
            opt.action_index + 1,
            opt.pick.secondary_id,
            opt.pick.id,
            opt.pick.tertiary_id,
            opt.pick.quaternary_id);
        return 0;
    case UI_MINIMENU_PICK_SCENERY:
        fprintf(
            stderr,
            "minimenu: oploc%d loc_id=%d element=%d tile=%d,%d\n",
            opt.action_index + 1,
            opt.pick.secondary_id,
            opt.pick.id,
            opt.pick.tertiary_id,
            opt.pick.quaternary_id);
        return 0;
    default:
        fprintf(stderr, "minimenu: unhandled pick kind %d\n", (int)opt.pick.kind);
        return 0;
    }
}

int
App_RunOnce(
    struct App* app,
    uint64_t now_ms,
    struct LibToriRS_Input* input)
{
    struct UIInteractOut out;
    int ran_cs2 = 0;

    assert(app);
    assert(input);

    /* Logic ticks at 20ms with bounded catch-up after a stall. */
    if( app->last_logic_ms == 0 )
        app->last_logic_ms = now_ms;
    {
        int ticks = (int)((now_ms - app->last_logic_ms) / APP_LOGIC_TICK_MS);
        if( ticks > 0 )
        {
            app->last_logic_ms += (uint64_t)ticks * APP_LOGIC_TICK_MS;
            if( ticks > APP_MAX_CATCHUP_TICKS )
                ticks = APP_MAX_CATCHUP_TICKS;
            for( int t = 0; t < ticks; t++ )
            {
                if( app_logic_tick(app) )
                {
                    app->need_redraw = 1;
                    ran_cs2 = 1;
                }
            }
        }
        else
        {
            ticks = 0;
        }

        /* World sim cycles == client 20ms ticks (v1 world_cycle cadence). */
        app_world_frame(app, ticks);
    }

    /* Per-frame interaction: returns intents; the app applies event context
     * and dispatches each hook through the game layer. */
    /* Publish this frame's key state before any hook runs, so KEYHELD and
     * KEYPRESSED answer about the frame the script is reacting to. */
    RS_CS2_SyncKeyState(&app->host, input);

    UITree_InteractFrame(&app->interact, app->tree, &app->ui_host, input, now_ms, &out);

    /* World hover: gate on the mouse being over the world element. The pick
     * itself runs inside App_Render (hittest right after each visible model
     * projects), so here we only latch the mouse point the next render picks
     * at; hover tile and pickset are the last rendered frame's (world frames
     * always mark need_redraw, so at most one frame stale). */
    app_update_world_viewport(app);
    /* A viewport that only appeared now (mounted interface, unhidden layer)
     * pulls the map in on first sight — the map is loaded iff the tree has a
     * world element, never eagerly. */
    if( app->world_view_valid && !app->world_load_attempted )
        app_world_load(app);
    app->world_mouse_in_viewport =
        app_world_mouse_gate(app, input->curr.mouse_x, input->curr.mouse_y, out.hover_com_id);
    app->world_mouse_x = input->curr.mouse_x;
    app->world_mouse_y = input->curr.mouse_y;
    if( !app->world_mouse_in_viewport )
    {
        app->world_hover_tile_x = -1;
        app->world_hover_tile_z = -1;
        World_PickSetReset(&app->world_pickset);
    }

    /* Mouseover text before any click handling: the reference recomputes it
     * every cycle from the same menu the click paths build. */
    app_hover_text_update(app, input->curr.mouse_x, input->curr.mouse_y, out.hover_com_id);

    /* Minimenu gesture results (see interact_minimenu): option selected on
     * mousedown -> dispatch; right press with no menu open -> build + show. */
    if( out.minimenu_select >= 0 )
    {
        if( app_minimenu_use_option(
                app, out.minimenu_select, input->curr.mouse_x, input->curr.mouse_y) )
            ran_cs2 = 1;
    }
    if( out.right_click )
    {
        /* The menu ctx reads app->world_pickset — the set the last rendered
         * frame hittested at the hover point (v1-style pickset-during-draw). */
        int click_in_world =
            app_world_mouse_gate(app, out.right_click_x, out.right_click_y, out.hover_com_id);
        if( !click_in_world || !app_world_drawable(app) )
            World_PickSetReset(&app->world_pickset);
        app_minimenu_open(app, out.right_click_x, out.right_click_y, click_in_world);
    }

    /* Left click executes the DEFAULT menu entry (reference
     * chooseDefaultMenuEntry): build the same menu the right click would show
     * and run its top normal-priority row, suppressing the legacy click
     * intent. Components with no menu rows keep the legacy hook path — for
     * them the scratch menu is Cancel-only and default_idx is -1. */
    if( out.clicked_com_id >= 0 && !out.minimenu_closed && out.minimenu_select < 0 )
    {
        struct RS_MinimenuBuildCtx mctx = {
            .tree = app->tree,
            .ui_host = &app->ui_host,
            .provider = app->provider,
            .runner = &app->runner,
            .invs = &app->invs,
            .chat = NULL,
            .world = app->world,
            .world_pickset = NULL, /* UI hit: mouse was over a component */
            .click_in_world = false,
        };
        struct UIMinimenu scratch;
        int default_idx;

        UIMinimenu_Reset(&scratch);
        scratch.font_id = app->interact.minimenu.font_id;
        RS_Minimenu_Build(&mctx, out.clicked_x, out.clicked_y, &scratch);
        default_idx = RS_Minimenu_DefaultOptionIndex(&scratch);
        if( default_idx >= 0 )
        {
            /* Steal the row set: use_option consumes interact.minimenu. */
            struct UIMinimenu saved = app->interact.minimenu;
            app->interact.minimenu = scratch;
            if( app_minimenu_use_option(app, default_idx, out.clicked_x, out.clicked_y) )
                ran_cs2 = 1;
            app->interact.minimenu = saved;

            /* Drop the legacy click intent (the only intent kind carrying
             * neither event-mouse nor drag context) so the hook does not run
             * twice; hover/wheel/hold intents pass through untouched. */
            {
                int kept = 0;
                for( int i = 0; i < out.intent_count; i++ )
                {
                    struct UIIntent const* intent = &out.intents[i];
                    if( !intent->has_event_mouse && !intent->has_drag_target &&
                        (intent->component_id == out.clicked_com_id || intent->component_id < 0) )
                        continue;
                    out.intents[kept++] = out.intents[i];
                }
                out.intent_count = kept;
            }
        }
    }

    /* Left click over bare world (no UI component hit): run the default menu
     * entry from world rows only — Walk here / nearest entity op (reference
     * chooseDefaultMenuEntry over the last rendered frame's pickset). */
    if( out.left_click_miss && !out.minimenu_closed && out.minimenu_select < 0 &&
        app_world_mouse_gate(app, out.left_click_miss_x, out.left_click_miss_y, out.hover_com_id) &&
        app_world_drawable(app) )
    {
        struct RS_MinimenuBuildCtx mctx = {
            .tree = app->tree,
            .ui_host = &app->ui_host,
            .provider = app->provider,
            .runner = &app->runner,
            .invs = &app->invs,
            .chat = NULL,
            .world = app->world,
            .world_pickset = &app->world_pickset,
            .click_in_world = true,
        };
        struct UIMinimenu scratch;
        int default_idx;

        UIMinimenu_Reset(&scratch);
        scratch.font_id = app->interact.minimenu.font_id;
        RS_Minimenu_Build(&mctx, out.left_click_miss_x, out.left_click_miss_y, &scratch);
        default_idx = RS_Minimenu_DefaultOptionIndex(&scratch);
        if( default_idx >= 0 )
        {
            struct UIMinimenu saved = app->interact.minimenu;
            app->interact.minimenu = scratch;
            if( app_minimenu_use_option(
                    app, default_idx, out.left_click_miss_x, out.left_click_miss_y) )
                ran_cs2 = 1;
            app->interact.minimenu = saved;
        }
    }

    app->hover_com_id = out.hover_com_id;
    if( out.clicked_com_id >= 0 )
        app->clicked_com_id = out.clicked_com_id;
    if( out.need_redraw )
        app->need_redraw = 1;

    /* Snapshot hooks by value before dispatching anything: intent->hook points
     * into tree->components[], and an earlier intent's script can CC_CREATE
     * (realloc) or CC_DELETEALL (reclaim/reuse the slot), dangling the pointer. */
    {
        struct UITreeRuntimeScriptHook hook_copies[UI_INTENT_MAX];
        for( int i = 0; i < out.intent_count; i++ )
            if( out.intents[i].hook )
                hook_copies[i] = *out.intents[i].hook;

        for( int i = 0; i < out.intent_count; i++ )
        {
            struct UIIntent const* intent = &out.intents[i];
            /* Set the op index explicitly per intent rather than relying on the
             * host default, so one intent's op cannot leak into the next.
             * Unset (0) means the primary left-click op, which is what every
             * mouse-driven dispatch reports; op-key matches carry their own. */
            RS_CS2_SetEventOp(&app->host, intent->op_index > 0 ? intent->op_index : 1, 0);
            if( intent->has_event_mouse )
                RS_CS2_SetEventMouse(&app->host, intent->event_mouse_x, intent->event_mouse_y);
            if( intent->has_drag_target )
                RS_CS2_SetEventDragTarget(&app->host, app->tree, intent->drag_target_id);
            RS_CS2_DispatchHook(
                &app->host,
                &app->runner,
                intent->component_id,
                intent->hook ? &hook_copies[i] : NULL);
            ran_cs2 = 1;
        }
    }

    /* Keyboard broadcast: every event this frame times every visible onKey
     * handler (reference OsrsClient key dispatch). Unlike the intent loop above
     * this re-resolves each component id immediately before dispatching it
     * rather than snapshotting hooks up front -- a broadcast runs many scripts
     * in one frame, and an earlier one can CC_CREATE (realloc components[]) or
     * CC_DELETEALL (reclaim the slot), so a target collected during the scan may
     * be gone by its turn. Same reasoning as the on_timer loop. */
    for( int e = 0; e < out.key_event_count; e++ )
    {
        for( int t = 0; t < out.key_target_count; t++ )
        {
            struct UIKeyTarget const* target = &out.key_targets[t];
            int32_t idx = UITree_FindByComponentId(app->tree, target->component_id);
            if( idx < 0 )
                continue;
            /* Re-check the hook too: the id may have been reclaimed and handed
             * to a different node since collection. */
            if( app->tree->components[idx].runtime_hooks.on_key.script_id <= 0 )
                continue;
            RS_CS2_SetEventMouse(
                &app->host, out.key_mouse_x - target->abs_x, out.key_mouse_y - target->abs_y);
            RS_CS2_SetEventKey(
                &app->host, out.key_events[e].key_typed, out.key_events[e].key_pressed);
            if( getenv("TORIRS_KEY_DEBUG") )
                fprintf(
                    stderr,
                    "key_dispatch: com=0x%08x script=%d typed=%d pressed=%d\n",
                    target->component_id,
                    app->tree->components[idx].runtime_hooks.on_key.script_id,
                    out.key_events[e].key_typed,
                    out.key_events[e].key_pressed);
            RS_CS2_DispatchHook(
                &app->host,
                &app->runner,
                target->component_id,
                &app->tree->components[idx].runtime_hooks.on_key);
            ran_cs2 = 1;
        }
    }

    /* Click handlers can unhide tabs; pump immediately so the freshly visible
     * widgets populate this frame instead of one tick later. Early-outs when
     * nothing was unhidden. */
    if( out.intent_count > 0 || out.key_target_count > 0 )
        RS_CS2_PumpTransmits(&app->host, &app->runner);

    app_world_camera_keys(app, input, &out);
    app_world_hotkeys(app, input, &out);

    if( ran_cs2 )
        UITree_LayoutResolve(app->tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

    if( app->need_redraw )
    {
        app->emit.count = 0;
        UITree_EmitWalk(app->tree, &app->ui_host, &app->emit, app->hover_com_id);
        app->need_redraw = 0;
        return 1;
    }
    return 0;
}

void
App_Render(
    struct App* app,
    int* pixels,
    int width,
    int height)
{
    struct ToriRS_Frame frame;
    struct ToriRS_Soft3D soft;

    assert(app);
    assert(pixels);

    ToriRS_FrameInit(&frame);
    ToriRS_FrameSetScene(&frame, app->scene);
    ToriRS_FrameSetCanvas(&frame, width, height);
    ToriRS_FrameSetEmitBuffer(&frame, &app->emit);

    /* World pass: paint the visibility-ordered command list for the current
     * camera and attach it so UITREE_EMIT_WORLD opens the 3D pass. */
    if( app_world_drawable(app) )
    {
        app_world_paint(app);
        ToriRS_FrameSetWorld(
            &frame,
            app->world,
            app->painter_buffer,
            &app->world_camera,
            app->world_camera_pos.x,
            app->world_camera_pos.y,
            app->world_camera_pos.z);
    }

    ToriRS_Soft3D_Init(&soft, app->scene, pixels, width, height);

    /* World hittest rides the render: each visible model is tested against
     * the mouse point right after it projects (the only window where the
     * scene scratch holds its projection), then the raw hits classify into
     * the pickset + hover tile the click/hotkey paths consume next frame. */
    if( app_world_drawable(app) && app->world_mouse_in_viewport )
        ToriRS_Soft3D_SetPick(&soft, app->world_mouse_x, app->world_mouse_y);

    ToriRS_Soft3D_RenderFrame(&soft, &frame);

    if( soft.pick_enabled )
        app_world_pick_finish(app, &soft.pick_hits);
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
