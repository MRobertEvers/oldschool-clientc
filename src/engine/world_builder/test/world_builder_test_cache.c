#include "test_harness.h"
#include <assert.h>

#include "asyncio.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_types.h"
#include "engine/world_builder/heightmap.h"
#include "engine/world_builder/world_builder.h"
#include "painters/painters.h"
#include "platform/platform_x_io.h"
#include "varp/varp_manager.h"
#include "world/world.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <bmp.h>
#include <rscache.h>
#include <toridraw.h>
#include <toridraw_scene.h>

#define DEFAULT_CACHE_DIR "/Users/matthewevers/Documents/git_repos/3draster/cache.jan2026"

#define MAP_X 50
#define MAP_Z 50

static const char*
profile_name_for_cache_dir(const char* cache_dir)
{
    const char* base = strrchr(cache_dir, '/');
    base = base ? base + 1 : cache_dir;

    if( strcmp(base, "cache.kronos") == 0 )
        return "kronos";
    if( strcmp(base, "cache.osrs184") == 0 )
        return "osrs184";
    if( strcmp(base, "cache.osrs230") == 0 )
        return "osrs230";
    if( strcmp(base, "cache.osrs239") == 0 )
        return "osrs239";
    if( strcmp(base, "cache.643") == 0 || strcmp(base, "cache.rs643") == 0 )
        return "643";
    return NULL;
}

#define FB_W 512
#define FB_H 384

/* -------- small sorted-array int set for uniqueness -------- */

struct IntSet
{
    int* items;
    int count;
    int cap;
};

static void
intset_init(struct IntSet* s)
{
    s->items = NULL;
    s->count = 0;
    s->cap = 0;
}

static void
intset_free(struct IntSet* s)
{
    free(s->items);
    s->items = NULL;
    s->count = 0;
    s->cap = 0;
}

/* Returns true if v was newly inserted. */
static bool
intset_add(
    struct IntSet* s,
    int v)
{
    int lo = 0;
    int hi = s->count;
    while( lo < hi )
    {
        int mid = (lo + hi) / 2;
        if( s->items[mid] < v )
            lo = mid + 1;
        else
            hi = mid;
    }
    if( lo < s->count && s->items[lo] == v )
        return false;

    if( s->count == s->cap )
    {
        s->cap = s->cap ? s->cap * 2 : 64;
        s->items = realloc(s->items, (size_t)s->cap * sizeof(int));
    }
    memmove(&s->items[lo + 1], &s->items[lo], (size_t)(s->count - lo) * sizeof(int));
    s->items[lo] = v;
    s->count++;
    return true;
}

/* -------- task runner -------- */

static void
run_task(
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_IO* io,
    struct PlatformX_IO* px,
    struct ToriRS_Task* task)
{
    assert(task);
    ToriRS_TaskQueue_Add(queue, task);
    while( ToriRS_TaskQueue_Run(queue, io) == TORIRS_ASYNCIO_STAT_YIELD )
        PlatformX_IO_Process(px, io);
}

/* -------- rendering -------- */

static int
count_nonzero(
    const int* pixels,
    int n)
{
    int nonzero = 0;
    for( int i = 0; i < n; i++ )
        if( (pixels[i] & 0x00FFFFFF) != 0 )
            nonzero++;
    return nonzero;
}

/* CacheProvider textures now include texels, but this test does not upload them
 * into ToriDraw_TextureMap. Strip face texture ids so soft-raster uses
 * flat/gouraud paths instead of asserting on a missing scene texture. */
static void
strip_scene_face_textures(struct ToriDraw_Scene* scene)
{
    int slot_count = ToriDraw_SceneElementSlotCount(scene);
    for( int element_id = 0; element_id < slot_count; element_id++ )
    {
        if( !ToriDraw_SceneElementIsLive(scene, element_id) )
            continue;
        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(scene, element_id);
        if( !el || el->model.kind != TORIDRAWMK_MODEL || !el->model.u.model.model )
            continue;
        struct ToriDraw_Model* model = el->model.u.model.model;
        if( !model->face_textures )
            continue;
        for( int f = 0; f < model->face_count; f++ )
            model->face_textures[f] = (faceint_t)-1;
    }
}

static int
render_scene(
    struct World* world,
    struct ToriDraw_Scene* scene,
    struct PaintersBuffer* pbuf,
    int* pixels,
    int cam_x,
    int cam_y,
    int cam_z,
    int pitch,
    int yaw,
    int* out_drawn)
{
    memset(pixels, 0, (size_t)FB_W * FB_H * sizeof(int));

    struct ToriDraw_Camera camera = {
        .proj_mode = TORIDRAW_PROJ_MODE_SCALE,
        .proj_scale = TORIDRAW_PROJ_SCALE_DEFAULT,
        .near_plane_z = 50,
        .pitch = pitch,
        .yaw = yaw,
        .roll = 0,
    };
    struct ToriDraw_ViewPort vp = {
        .width = FB_W,
        .height = FB_H,
        .stride = FB_W,
        .x_center = FB_W / 2,
        .y_center = FB_H / 2,
        .clip_left = 0,
        .clip_top = 0,
        .clip_right = FB_W,
        .clip_bottom = FB_H,
    };

    int drawn = 0;
    for( int i = 0; i < pbuf->command_count; i++ )
    {
        struct PaintersElementCommand* cmd = &pbuf->commands[i];
        int element_id = -1;

        if( cmd->_bf_kind == PNTR_CMD_ELEMENT )
        {
            element_id = (int)cmd->_entity._bf_entity;
        }
        else if( cmd->_bf_kind == PNTR_CMD_TERRAIN )
        {
            element_id = World_TerrainElementAt(
                world,
                (int)cmd->_terrain._bf_terrain_x,
                (int)cmd->_terrain._bf_terrain_z,
                (int)cmd->_terrain._bf_terrain_y);
        }

        if( element_id < 0 || !ToriDraw_SceneElementIsLive(scene, element_id) )
            continue;

        struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(scene, element_id);
        if( !el || el->model.kind == TORIDRAWMK_NONE )
            continue;

        struct ToriDraw_Position rel = el->world_position;
        rel.x -= cam_x;
        rel.y -= cam_y;
        rel.z -= cam_z;

        if( ToriDraw_RenderModel1Project(el->model, scene, &rel, &vp, &camera) !=
            TORIDRAW_CULL_VISIBLE )
            continue;
        if( ToriDraw_RenderModel2SortFaces(el->model, scene) <= 0 )
            continue;
        ToriDraw_RenderModel3Raster(scene, &vp, &camera, pixels, false);
        drawn++;
    }

    *out_drawn = drawn;
    return count_nonzero(pixels, FB_W * FB_H);
}

/* -------- config preload helpers -------- */

static void
collect_loc_models(
    struct ToriRS_Location* loc,
    struct IntSet* models)
{
    assert(loc);

    if( !loc->shapes )
    {
        int count = loc->lengths ? loc->lengths[0] : 0;
        for( int j = 0; j < count && loc->models && loc->models[0]; j++ )
            intset_add(models, loc->models[0][j]);
        return;
    }

    for( int i = 0; i < loc->shapes_and_model_count; i++ )
    {
        int count = loc->lengths ? loc->lengths[i] : 0;
        for( int j = 0; j < count && loc->models && loc->models[i]; j++ )
            intset_add(models, loc->models[i][j]);
    }
}

void
test_world_builder_cache_render(void)
{
    const char* cache_dir = getenv("CACHE_DIR");
    if( !cache_dir )
        cache_dir = DEFAULT_CACHE_DIR;

    struct stat st;
    if( stat(cache_dir, &st) != 0 || !S_ISDIR(st.st_mode) )
    {
        printf("SKIP: cache dir not found: %s\n", cache_dir);
        return;
    }

    struct ToriRS_IO* io = ToriRS_IO_New();
    struct ToriRS_TaskQueue* queue = ToriRS_TaskQueue_New();
    struct Dat2BuildCache* bc = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(bc);
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        printf("SKIP: could not open dat2 disk cache at %s\n", cache_dir);
        ToriRS_TaskQueue_Free(queue);
        ToriRS_IO_Free(io);
        dat2_buildcache_free(bc);
        return;
    }

    struct RSCache profile;
    const char* profile_name = profile_name_for_cache_dir(cache_dir);
    if( !profile_name || !RSCache_ProfileByName(profile_name, &profile) )
    {
        printf(
            "SKIP: cache dir %s has no labelled profile (set CACHE_DIR to cache.osrs230, etc.)\n",
            cache_dir);
        RSCache_Dat2DiskFree(disk);
        ToriRS_TaskQueue_Free(queue);
        ToriRS_IO_Free(io);
        dat2_buildcache_free(bc);
        return;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);
    /* The disk gets the profile, but the provider needs it too:
     * CacheProvider_Profile asserts the identity is stated, and the config
     * decoders go through it. Without this the test aborts on its first loc
     * decode — invisible while the cache dir is absent and the test skips. */
    CacheProvider_SetProfile(provider, &profile);

    if( RSCache_MapLocsEncrypted(&profile) )
    {
        char xtea_path[1024];
        snprintf(xtea_path, sizeof(xtea_path), "%s/xteas.json", cache_dir);
        int xtea_n = RSCache_XteaConfigLoadKeys(xtea_path);
        if( xtea_n <= 0 )
            printf("warning: no xtea keys loaded from %s\n", xtea_path);
        else
            printf("loaded %d xtea keys\n", xtea_n);
    }

    struct PlatformX_IO* px = PlatformX_IO_New();
    PlatformX_IO_InitDat2Disk(px, disk);

    ToriDraw_Init();
    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    struct World* world = World_New();
    struct VarPManager varp;
    VarPManager_Init(&varp);
    struct WorldBuilder* builder = WorldBuilder_New(world, provider, scene, &varp);

    int map_id = CacheProvider_MapId(MAP_X, MAP_Z);

    /* --- load the map square (terrain + scenery) --- */
    run_task(queue, io, px, CreateTask_MapTerrainLoad(provider, MAP_X, MAP_Z));
    run_task(queue, io, px, CreateTask_MapSceneryLoad(provider, MAP_X, MAP_Z));

    TEST_ASSERT(CacheProvider_MapTerrainHas(provider, map_id), "map terrain loaded");
    TEST_ASSERT(CacheProvider_MapSceneryHas(provider, map_id), "map scenery loaded");

    if( !CacheProvider_MapTerrainHas(provider, map_id) ||
        !CacheProvider_MapSceneryHas(provider, map_id) )
    {
        printf("map %d,%d not available (xtea/missing); aborting cache test cleanly\n", MAP_X, MAP_Z);
        goto cleanup;
    }

    /* --- preload terrain configs (underlays / overlays / textures) --- */
    struct IntSet underlays;
    struct IntSet overlays;
    struct IntSet textures;
    struct IntSet locs;
    struct IntSet models;
    intset_init(&underlays);
    intset_init(&overlays);
    intset_init(&textures);
    intset_init(&locs);
    intset_init(&models);

    struct ToriRS_MapTerrain* terrain = CacheProvider_MapTerrainGet(provider, map_id);
    if( terrain )
    {
        int n = TORIRS_MAP_TERRAIN_X * TORIRS_MAP_TERRAIN_Z * TORIRS_MAP_TERRAIN_LEVELS;
        for( int i = 0; i < n; i++ )
        {
            struct ToriRS_MapFloor* t = &terrain->tiles_xyz[i];
            if( t->underlay_id > 0 )
                intset_add(&underlays, t->underlay_id - 1);
            if( t->overlay_id > 0 )
                intset_add(&overlays, t->overlay_id - 1);
        }
    }

    for( int i = 0; i < underlays.count; i++ )
    {
        if( !CacheProvider_UnderlayHas(provider, underlays.items[i]) )
            run_task(queue, io, px, CreateTask_UnderlayLoad(provider, underlays.items[i]));
    }
    for( int i = 0; i < overlays.count; i++ )
    {
        if( !CacheProvider_FlotypeHas(provider, overlays.items[i]) )
            run_task(queue, io, px, CreateTask_FlotypeLoad(provider, overlays.items[i]));
    }
    /* textures referenced by loaded overlay flotypes */
    for( int i = 0; i < overlays.count; i++ )
    {
        struct ToriRS_Flotype* flo = CacheProvider_FlotypeGet(provider, overlays.items[i]);
        if( flo && flo->texture >= 0 )
            intset_add(&textures, flo->texture);
    }
    for( int i = 0; i < textures.count; i++ )
    {
        if( !CacheProvider_TextureHas(provider, textures.items[i]) )
            run_task(queue, io, px, CreateTask_TextureLoad(provider, textures.items[i]));
    }

    /* --- preload scenery locs, then their models --- */
    struct ToriRS_MapLocs* map_locs = CacheProvider_MapSceneryGet(provider, map_id);
    if( map_locs )
    {
        for( int i = 0; i < map_locs->locs_count; i++ )
            intset_add(&locs, map_locs->locs[i].loc_id);
    }

    for( int i = 0; i < locs.count; i++ )
    {
        if( !CacheProvider_LocationHas(provider, locs.items[i]) )
            run_task(queue, io, px, CreateTask_LocLoad(provider, locs.items[i]));
    }

    for( int i = 0; i < locs.count; i++ )
    {
        struct ToriRS_Location* loc = CacheProvider_LocationGet(provider, locs.items[i]);
        collect_loc_models(loc, &models);
    }

    for( int i = 0; i < models.count; i++ )
    {
        if( !CacheProvider_ModelHas(provider, models.items[i]) )
            run_task(queue, io, px, CreateTask_ModelLoad(provider, models.items[i]));
    }

    printf(
        "preloaded: %d underlays, %d overlays, %d textures, %d locs, %d models\n",
        underlays.count,
        overlays.count,
        textures.count,
        locs.count,
        models.count);

    /* --- rebuild the single chunk --- */
    {
        int chunks[] = { MAP_X, MAP_Z };
        WorldBuilder_RebuildChunklist(builder, chunks, 1);
    }
    World_SetLoadComplete(world, true);

    TEST_ASSERT(world->painter != NULL, "rebuild allocated painter");
    TEST_ASSERT(
        world->entities.terrain.active_count > 0 || world->entities.scenery.active_count > 0,
        "rebuild produced terrain or scenery elements");

    strip_scene_face_textures(scene);

    /* --- paint --- */
    struct PaintersBuffer* pbuf = painter_buffer_new();
    painter_set_camera_angles(world->painter, 128, 0);
    painter_set_level_mask(world->painter, 0xF);

    int cam_sx = world->_scene_size / 2;
    int cam_sz = world->_scene_size / 2;
    painter_paint_bucket(world->painter, pbuf, cam_sx, cam_sz, 0);
    TEST_ASSERT(pbuf->command_count > 0, "paint commands");

    /* --- raster: sweep a few camera setups, keep the best coverage --- */
    int* pixels = calloc((size_t)FB_W * FB_H, sizeof(int));

    int cam_x = cam_sx * 128 + 64;
    int cam_z = cam_sz * 128 + 64;
    int ground = 0;
    if( world->heightmap )
        ground = heightmap_get(world->heightmap, cam_sx, cam_sz, 0);

    const int pitches[] = { 128, 200, 256, 320, 384 };
    const int offsets[] = { 200, 400, 700, 1000, 1500 };

    int best_nonzero = 0;
    int best_drawn = 0;
    int best_pitch = pitches[0];
    int best_off = offsets[0];

    for( size_t pi = 0; pi < sizeof(pitches) / sizeof(pitches[0]); pi++ )
    {
        for( size_t oi = 0; oi < sizeof(offsets) / sizeof(offsets[0]); oi++ )
        {
            int cam_y = ground - offsets[oi];
            int drawn = 0;
            int nonzero = render_scene(
                world, scene, pbuf, pixels, cam_x, cam_y, cam_z, pitches[pi], 0, &drawn);
            if( nonzero > best_nonzero )
            {
                best_nonzero = nonzero;
                best_drawn = drawn;
                best_pitch = pitches[pi];
                best_off = offsets[oi];
            }
        }
    }

    /* final render with the best setup so the BMP matches the asserted coverage */
    int final_drawn = 0;
    int final_nonzero = render_scene(
        world, scene, pbuf, pixels, cam_x, ground - best_off, cam_z, best_pitch, 0, &final_drawn);

    TEST_ASSERT(final_drawn > 0 || final_nonzero > 100, "rendered something");
    TEST_ASSERT(final_nonzero > 100, "framebuffer not empty");

    mkdir("build", 0755);
    bmp_write_file("build/world_builder_cache_render.bmp", pixels, FB_W, FB_H);
    printf(
        "wrote build/world_builder_cache_render.bmp (%d cmds, %d drawn, %d px; pitch=%d off=%d)\n",
        pbuf->command_count,
        final_drawn,
        final_nonzero,
        best_pitch,
        best_off);
    (void)best_nonzero;
    (void)best_drawn;

    /* --- runtime loc change (zone LOC_DEL / LOC_ADD_CHANGE) regression ---
     * Pick a blocking straight wall from the built scene, delete it, and
     * re-add it: the pool entry and the collision flags on its tile must
     * round-trip exactly, and the respawn must be flagged for the per-frame
     * painter pass (runtime_spawn). Exercises WorldBuilder_ApplyLocChange
     * outside a build (transient maps freed, painter static set baked). */
    {
        struct World_EntityPool* spool = &world->entities.scenery;
        struct WorldEntity_Scenery wall = { 0 };
        int wall_found = 0;
        for( int i = World_EntityPoolHead(spool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(spool, i) )
        {
            struct WorldEntity_Scenery* sc = World_EntityPoolGet(spool, i);
            if( !sc || sc->grid_position.level != 0 )
                continue;
            if( sc->shape != RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE )
                continue;
            struct ToriRS_Location* cfg = CacheProvider_LocationGet(provider, sc->loc_id);
            /* map_scene_id == -1: mapscene walls draw a sprite, not a minimap
             * line, so they would skip the minimap round-trip asserted below. */
            if( cfg && cfg->blocks_walk && cfg->map_scene_id == -1 )
            {
                wall = *sc;
                wall_found = 1;
                break;
            }
        }
        if( wall_found )
        {
            struct CollisionMap* cmap = world->collision_maps[0];
            int wx = wall.grid_position.x;
            int wz = wall.grid_position.z;
            int tile_idx = collision_map_index_at(cmap, wx, wz);
            int flags_with = cmap->flags[tile_idx];
            int mm_with = world->minimap ? minimap_tile_wall(world->minimap, wx, wz, 0) : 0;
            unsigned mm_seq = world->minimap_seq;

            WorldBuilder_ApplyLocChange(builder, wx, wz, 0, -1, wall.shape, wall.angle);
            TEST_ASSERT(
                World_SceneryFindAt(world, wx, wz, 0, wall.shape) < 0,
                "LOC_DEL removed the wall pool entry");
            TEST_ASSERT(
                cmap->flags[tile_idx] != flags_with, "LOC_DEL cleared the wall collision");
            if( world->minimap )
            {
                TEST_ASSERT(
                    minimap_tile_wall(world->minimap, wx, wz, 0) != mm_with,
                    "LOC_DEL cleared the minimap wall line");
                TEST_ASSERT(
                    world->minimap_seq != mm_seq, "LOC_DEL bumped minimap_seq (rebake)");
            }

            WorldBuilder_ApplyLocChange(builder, wx, wz, 0, wall.loc_id, wall.shape, wall.angle);
            int re_idx = World_SceneryFindAt(world, wx, wz, 0, wall.shape);
            TEST_ASSERT(re_idx >= 0, "LOC_ADD_CHANGE re-spawned the wall");
            TEST_ASSERT(
                cmap->flags[tile_idx] == flags_with,
                "LOC_ADD_CHANGE restored the collision flags exactly");
            if( world->minimap )
                TEST_ASSERT(
                    minimap_tile_wall(world->minimap, wx, wz, 0) == mm_with,
                    "LOC_ADD_CHANGE restored the minimap wall line");
            {
                struct WorldEntity_Scenery* re = World_EntityPoolGet(spool, re_idx);
                TEST_ASSERT(re && re->runtime_spawn, "respawned loc flagged runtime_spawn");
                TEST_ASSERT(re && re->angle == wall.angle, "respawned loc kept the map angle");
                /* Wallside recorded for the per-frame painter re-registration:
                 * a straight wall re-adds as WALL_A on its build wallside, not
                 * as centre scenery. */
                TEST_ASSERT(
                    re && re->painter_wall_ab == WALL_A,
                    "respawned wall recorded painter WALL_A");

                /* The runtime spawn must be LIT: the sharelight accumulator is
                 * build-only, so scenery_register_sharelight lights the model
                 * synchronously — unlit face_colors_a stay all-zero (black). */
                if( re )
                {
                    struct ToriDraw_SceneElement* el =
                        ToriDraw_SceneElementGet(scene, re->element_id);
                    int lit = 0;
                    TEST_ASSERT(
                        el && el->model.kind == TORIDRAWMK_MODEL && el->model.u.model.model,
                        "respawned loc has a scene model");
                    if( el && el->model.kind == TORIDRAWMK_MODEL && el->model.u.model.model )
                    {
                        struct ToriDraw_Model* dm = el->model.u.model.model;
                        for( int f = 0; f < dm->face_count && !lit; f++ )
                            if( dm->face_colors_a && dm->face_colors_a[f] != 0 )
                                lit = 1;
                    }
                    TEST_ASSERT(lit, "respawned loc model is lit (face colours non-zero)");
                }
            }
        }
        else
            printf("no blocking straight wall on level 0; loc-change test skipped\n");
    }

    free(pixels);
    free(pbuf->commands);
    free(pbuf);

    intset_free(&underlays);
    intset_free(&overlays);
    intset_free(&textures);
    intset_free(&locs);
    intset_free(&models);

cleanup:
    WorldBuilder_Free(builder);
    World_Free(world);
    /* ToriDraw_SceneFree pulls in ToriDraw_FontFree, which is not part of the current
     * toridraw build; the scene is reclaimed at process exit. */
    VarPManager_Free(&varp);
    PlatformX_IO_Free(px);
    RSCache_Dat2DiskFree(disk);
    ToriRS_TaskQueue_Free(queue);
    ToriRS_IO_Free(io);
    dat2_buildcache_free(bc);
}

/* ------------------------------------------------------------------ */
/* Rebuild stutter bench (WB_BENCH=1)                                  */
/* ------------------------------------------------------------------ */

#include "engine/world_builder/task_world_load.h"

#include <time.h>

static double
bench_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/*
 * Times the live REBUILD_NORMAL path: one Task_WorldLoad (IO prefetch + the
 * synchronous WorldBuilder_RebuildCenterzone) over the 3x3 map squares around
 * Lumbridge, then warm re-runs of the rebuild alone — the warm number is the
 * per-teleport stutter with every asset already resident, which is the common
 * in-game case (walking across a zone boundary re-uses almost everything).
 * Run with TORIRS_REBUILD_TIMING=1 for the per-phase split.
 */
void
test_world_builder_bench(void)
{
    const char* cache_dir = getenv("CACHE_DIR");
    if( !cache_dir )
        cache_dir = DEFAULT_CACHE_DIR;

    struct stat st;
    if( stat(cache_dir, &st) != 0 || !S_ISDIR(st.st_mode) )
    {
        printf("SKIP: cache dir not found: %s\n", cache_dir);
        return;
    }

    struct ToriRS_IO* io = ToriRS_IO_New();
    struct ToriRS_TaskQueue* queue = ToriRS_TaskQueue_New();
    struct Dat2BuildCache* bc = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(bc);
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        printf("SKIP: could not open dat2 disk cache at %s\n", cache_dir);
        ToriRS_TaskQueue_Free(queue);
        ToriRS_IO_Free(io);
        dat2_buildcache_free(bc);
        return;
    }

    struct RSCache profile;
    const char* profile_name = profile_name_for_cache_dir(cache_dir);
    if( !profile_name || !RSCache_ProfileByName(profile_name, &profile) )
    {
        printf("SKIP: cache dir %s has no labelled profile\n", cache_dir);
        RSCache_Dat2DiskFree(disk);
        ToriRS_TaskQueue_Free(queue);
        ToriRS_IO_Free(io);
        dat2_buildcache_free(bc);
        return;
    }
    RSCache_Dat2DiskSetProfile(disk, &profile);
    CacheProvider_SetProfile(provider, &profile);

    if( RSCache_MapLocsEncrypted(&profile) )
    {
        char xtea_path[1024];
        snprintf(xtea_path, sizeof(xtea_path), "%s/xteas.json", cache_dir);
        RSCache_XteaConfigLoadKeys(xtea_path);
    }

    struct PlatformX_IO* px = PlatformX_IO_New();
    PlatformX_IO_InitDat2Disk(px, disk);

    ToriDraw_Init();
    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    struct World* world = World_New();
    struct VarPManager varp;
    VarPManager_Init(&varp);
    struct WorldBuilder* builder = WorldBuilder_New(world, provider, scene, &varp);

    /* Lumbridge: map square (50,50); the player's tile centre is
     * (50*64+32) -> zone 404. Prefetch the full 3x3 the classic 104x104
     * scene can touch. */
    int chunks[18];
    int count = 0;
    for( int mx = 49; mx <= 51; mx++ )
        for( int mz = 49; mz <= 51; mz++ )
        {
            chunks[count * 2] = mx;
            chunks[count * 2 + 1] = mz;
            count++;
        }
    int zone_x = (50 * 64 + 32) / 8;
    int zone_z = (50 * 64 + 32) / 8;

    double t0 = bench_now_ms();
    run_task(
        queue, io, px,
        CreateTask_WorldLoad(provider, builder, chunks, count, zone_x, zone_z, NULL, NULL, NULL));
    double t1 = bench_now_ms();
    printf("bench: cold WorldLoad (IO + rebuild) = %.1f ms\n", t1 - t0);

    int bench_iters = atoi(getenv("WB_BENCH")) > 1 ? atoi(getenv("WB_BENCH")) : 4;
    for( int iter = 0; iter < bench_iters; iter++ )
    {
        double w0 = bench_now_ms();
        WorldBuilder_RebuildCenterzone(builder, zone_x, zone_z, 104);
        double w1 = bench_now_ms();
        printf("bench: warm rebuild %d = %.1f ms\n", iter, w1 - w0);
    }

    printf(
        "bench: scene elements terrain=%d scenery=%d\n",
        world->entities.terrain.active_count,
        world->entities.scenery.active_count);

    /* Lighting checksum over every scenery model's lit face colours: any
     * change to the sharelight merge/apply pipeline must keep this stable. */
    {
        unsigned long long sum = 1469598103934665603ull;
        struct World_EntityPool* spool = &world->entities.scenery;
        for( int i = World_EntityPoolHead(spool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(spool, i) )
        {
            struct WorldEntity_Scenery* sc = World_EntityPoolGet(spool, i);
            if( !sc )
                continue;
            struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(scene, sc->element_id);
            if( !el || el->model.kind != TORIDRAWMK_MODEL || !el->model.u.model.model )
                continue;
            struct ToriDraw_Model* dm = el->model.u.model.model;
            for( int f = 0; f < dm->face_count; f++ )
            {
                unsigned long long v =
                    ((unsigned long long)(uint16_t)dm->face_colors_a[f] << 32) ^
                    ((unsigned long long)(uint16_t)dm->face_colors_b[f] << 16) ^
                    (unsigned long long)(uint16_t)dm->face_colors_c[f];
                if( dm->face_infos )
                    v ^= (unsigned long long)dm->face_infos[f] << 48;
                sum = (sum ^ v) * 1099511628211ull;
            }
        }
        printf("bench: lighting checksum = %llx\n", sum);
    }

    WorldBuilder_Free(builder);
    World_Free(world);
    VarPManager_Free(&varp);
    PlatformX_IO_Free(px);
    RSCache_Dat2DiskFree(disk);
    ToriRS_TaskQueue_Free(queue);
    ToriRS_IO_Free(io);
    dat2_buildcache_free(bc);
}
