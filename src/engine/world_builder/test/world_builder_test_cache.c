#include "test_harness.h"

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
    if( !task )
        return;
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
        .fov_rpi2048 = 512,
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
    if( !loc )
        return;

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
    struct PlatformX_IO* px = PlatformX_IO_New();
    PlatformX_IO_InitDat2Disk(px, disk);

    ToriDraw_Init();
    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0);
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
