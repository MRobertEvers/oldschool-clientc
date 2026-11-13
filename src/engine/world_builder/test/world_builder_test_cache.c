#include "test_harness.h"
#include <assert.h>

#include "asyncio.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/torirs_types.h"
#include "engine/world_builder/collision_map.h"
#include "engine/world_builder/heightmap.h"
#include "engine/world_builder/minimap.h"
#include "engine/world_builder/world_builder.h"
#include "painters/painters.h"
#include "painters/scene_occluders.h"
#include "platform/platform_x_io.h"
#include "task_runner.h"
#include "varp/varp_manager.h"
#include "world/world.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* MinGW's mkdir takes no mode argument — the same portability seam
 * editor_host_local.c and rs2012_material_bake.c already carry. */
#ifdef _WIN32
#include <direct.h>
#define test_mkdir(path) _mkdir(path)
#else
#define test_mkdir(path) mkdir(path, 0755)
#endif

#include <bmp.h>
#include <rscache.h>
#include <toridraw.h>
#include <toridraw_scene.h>
#include <toridraw_element_id.h>

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
    struct ToriRS_IOBatch* io,
    struct PlatformX_IO* px,
    struct ToriRS_Task* task)
{
    /* The runner, not the bare queue: ToriRS_TaskQueue_Run steps only the
     * head, so a task that fans out children behind itself and waits on their
     * residency (Task_WorldLoad) would spin its wait budget without a child
     * ever running. TaskRunner_Step walks the whole queue and hands the reads
     * to the platform, which is what the client does per frame. BLOCKED is
     * the residency wait itself; stepping again is the next frame. */
    struct TaskRunner runner = {
        .queue = queue,
        .io = io,
        .px = px,
        /* The client's asset runner is parallel; serial steps the head only,
         * which is the same starvation one level up. */
        .parallel = 1,
    };
    enum TaskRunnerStat stat;

    assert(task);
    ToriRS_TaskQueue_Add(queue, task);
    do
    {
        stat = TaskRunner_Step(&runner);
    } while( stat != TASK_RUNNER_IDLE );
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
        if( !el || !ToriDraw_ModelKindIsFull(el->model.kind) || !el->model.u.model.model )
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
        .projection_mode = TORIDRAW_PROJECTION_MODE_SCALE,
        .projection_scale = TORIDRAW_PROJECTION_SCALE_DEFAULT,
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
            element_id = painter_command_element_id(cmd);
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

    struct ToriRS_IOBatch* io = ToriRS_IOBatch_New();
    struct ToriRS_TaskQueue* queue = ToriRS_TaskQueue_New();
    struct Dat2BuildCache* bc = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(bc);
    CacheProvider_SetAssetQueue(provider, queue);
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        printf("SKIP: could not open dat2 disk cache at %s\n", cache_dir);
        ToriRS_TaskQueue_Free(queue);
        ToriRS_IOBatch_Free(io);
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
        ToriRS_IOBatch_Free(io);
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

    test_mkdir("build");
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
                        el && ToriDraw_ModelKindIsFull(el->model.kind) && el->model.u.model.model,
                        "respawned loc has a scene model");
                    if( el && ToriDraw_ModelKindIsFull(el->model.kind) && el->model.u.model.model )
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
    painter_buffer_free(pbuf);

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
    ToriRS_IOBatch_Free(io);
    dat2_buildcache_free(bc);
}

/* ------------------------------------------------------------------ */
/* Rebuild stutter bench (WB_BENCH=1)                                  */
/* ------------------------------------------------------------------ */

#include "engine/world_builder/task_world_load.h"

#include <time.h>

#if defined(__APPLE__)
#include <malloc/malloc.h>
static size_t
heap_in_use(void)
{
    struct mstats ms = mstats();
    return ms.bytes_used;
}
#else
#include <malloc.h>
static size_t
heap_in_use(void)
{
    struct mallinfo mi = mallinfo();
    return (size_t)mi.uordblks;
}
#endif

static double
bench_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* FNV-1a over 64-bit words. */
static unsigned long long
wb_fold(
    unsigned long long h,
    unsigned long long v)
{
    return (h ^ v) * 1099511628211ull;
}

static unsigned long long
wb_fold_model(
    unsigned long long h,
    struct ToriDraw_Model const* dm)
{
    h = wb_fold(h, (unsigned long long)dm->vertex_count);
    h = wb_fold(h, (unsigned long long)dm->face_count);
    for( int v = 0; v < dm->vertex_count; v++ )
    {
        h = wb_fold(h, (unsigned long long)(uint32_t)dm->vertices_x[v]);
        h = wb_fold(h, (unsigned long long)(uint32_t)dm->vertices_y[v]);
        h = wb_fold(h, (unsigned long long)(uint32_t)dm->vertices_z[v]);
    }
    for( int f = 0; f < dm->face_count; f++ )
    {
        if( dm->face_colors_a )
            h = wb_fold(
                h,
                ((unsigned long long)(uint16_t)dm->face_colors_a[f] << 32) ^
                    ((unsigned long long)(uint16_t)dm->face_colors_b[f] << 16) ^
                    (unsigned long long)(uint16_t)dm->face_colors_c[f]);
        if( dm->face_infos )
            h = wb_fold(h, (unsigned long long)(uint32_t)dm->face_infos[f]);
    }
    return h;
}

/* The bench's lighting checksum: every scenery model's lit face colours. */
static unsigned long long
wb_lighting_checksum(
    struct World* world,
    struct ToriDraw_Scene* scene)
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
        if( !el || !ToriDraw_ModelKindIsFull(el->model.kind) || !el->model.u.model.model )
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
    return sum;
}

/*
 * Everything a bake leaves behind that the game reads: the window, heights,
 * collision on every level, the persisted tile flags, the scenery pool (ids,
 * shapes, angles, tiles and each model's geometry and lit colours), which
 * terrain tiles exist and their models, the painter's per-tile terrain sets,
 * the minimap tiles, mapfunction / mapscene icons, area sounds, occluder
 * planes and the hidden multiloc placements. Element ids are left out: they
 * are allocation order, which an aborted build before this one may change
 * without changing the scene.
 */
/* Sections of wb_state_checksum, so a mismatch names what differs. */
enum
{
    WB_SUM_WINDOW,
    WB_SUM_HEIGHTS,
    WB_SUM_COLLISION,
    WB_SUM_TILE_FLAGS,
    WB_SUM_SCENERY,
    WB_SUM_TERRAIN,
    WB_SUM_PAINTER,
    WB_SUM_MINIMAP,
    WB_SUM_ICONS,
    WB_SUM_OCCLUDERS,
    WB_SUM_HIDDEN,
    WB_SUM_COUNT,
};
static char const* const k_wb_sum_names[WB_SUM_COUNT] = {
    "window", "heights", "collision", "tile_flags", "scenery", "terrain",
    "painter", "minimap", "icons", "occluders", "hidden",
};
static unsigned long long g_wb_sum_parts[WB_SUM_COUNT];
#define WB_SUM_MARK(section) (g_wb_sum_parts[section] = h)

static unsigned long long
wb_state_checksum(
    struct WorldBuilder* builder,
    struct World* world,
    struct ToriDraw_Scene* scene)
{
    unsigned long long h = 1469598103934665603ull;
    int n = world->_scene_size;

    h = wb_fold(h, (unsigned long long)n);
    h = wb_fold(h, (unsigned long long)(uint32_t)world->_base_tile_x);
    h = wb_fold(h, (unsigned long long)(uint32_t)world->_base_tile_z);
    h = wb_fold(h, (unsigned long long)(uint32_t)world->_chunk_sw_x);
    h = wb_fold(h, (unsigned long long)(uint32_t)world->_chunk_sw_z);
    h = wb_fold(h, (unsigned long long)(uint32_t)world->_chunk_ne_x);
    h = wb_fold(h, (unsigned long long)(uint32_t)world->_chunk_ne_z);
    h = wb_fold(h, (unsigned long long)world->load_complete);

    WB_SUM_MARK(WB_SUM_WINDOW);
    {
        struct Heightmap* hm = world->heightmap;
        int cells = hm->size_x * hm->size_z * hm->levels;
        for( int i = 0; i < cells; i++ )
            h = wb_fold(h, (unsigned long long)(uint16_t)hm->heights[i]);
    }
    WB_SUM_MARK(WB_SUM_HEIGHTS);
    for( int l = 0; l < COLLISION_LEVELS; l++ )
    {
        struct CollisionMap* cm = world->collision_maps[l];
        h = wb_fold(h, cm ? 1u : 0u);
        if( !cm )
            continue;
        for( int i = 0; i < cm->size_x * cm->size_z; i++ )
            h = wb_fold(h, (unsigned long long)(uint32_t)cm->flags[i]);
    }
    WB_SUM_MARK(WB_SUM_COLLISION);
    for( int i = 0; i < n * n * WORLD_MAP_TERRAIN_LEVELS; i++ )
        h = wb_fold(h, world->tile_flags[i]);

    WB_SUM_MARK(WB_SUM_TILE_FLAGS);
    {
        struct World_EntityPool* spool = &world->entities.scenery;
        h = wb_fold(h, (unsigned long long)spool->active_count);
        for( int i = World_EntityPoolHead(spool); i != WORLD_ENTITY_NIL;
             i = World_EntityPoolNext(spool, i) )
        {
            struct WorldEntity_Scenery* sc = World_EntityPoolGet(spool, i);
            if( !sc )
                continue;
            h = wb_fold(h, (unsigned long long)(uint32_t)sc->loc_id);
            h = wb_fold(h, (unsigned long long)(uint32_t)sc->shape);
            h = wb_fold(h, (unsigned long long)(uint32_t)sc->angle);
            h = wb_fold(h, (unsigned long long)(uint32_t)sc->grid_position.x);
            h = wb_fold(h, (unsigned long long)(uint32_t)sc->grid_position.z);
            h = wb_fold(h, (unsigned long long)(uint32_t)sc->grid_position.level);
            h = wb_fold(h, (unsigned long long)sc->runtime_spawn);
            struct ToriDraw_SceneElement* el = ToriDraw_SceneElementGet(scene, sc->element_id);
            h = wb_fold(h, el ? 1u : 0u);
            if( !el )
                continue;
            h = wb_fold(h, (unsigned long long)(uint32_t)el->world_position.x);
            h = wb_fold(h, (unsigned long long)(uint32_t)el->world_position.y);
            h = wb_fold(h, (unsigned long long)(uint32_t)el->world_position.z);
            h = wb_fold(h, (unsigned long long)(uint32_t)el->world_position.yaw);
            if( ToriDraw_ModelKindIsFull(el->model.kind) && el->model.u.model.model )
                h = wb_fold_model(h, el->model.u.model.model);
        }
    }

    WB_SUM_MARK(WB_SUM_SCENERY);
    for( int l = 0; l < WORLD_MAP_TERRAIN_LEVELS; l++ )
        for( int z = 0; z < n; z++ )
            for( int x = 0; x < n; x++ )
            {
                int id = World_TerrainElementAt(world, x, z, l);
                struct ToriDraw_SceneElement* el =
                    id >= 0 ? ToriDraw_SceneElementGet(scene, id) : NULL;
                h = wb_fold(h, id >= 0 ? 1u : 0u);
                if( el && ToriDraw_ModelKindIsFull(el->model.kind) && el->model.u.model.model )
                    h = wb_fold_model(h, el->model.u.model.model);
            }

    WB_SUM_MARK(WB_SUM_TERRAIN);
    if( world->painter )
        for( int x = 0; x < n; x++ )
            for( int z = 0; z < n; z++ )
                for( int g = 0; g < painter_max_levels(world->painter); g++ )
                    h = wb_fold(h, painter_tile_get_terrain_levels(world->painter, x, z, g));

    WB_SUM_MARK(WB_SUM_PAINTER);
    if( world->minimap )
    {
        struct Minimap* mm = world->minimap;
        h = wb_fold(h, (unsigned long long)mm->tiles_count);
        for( int i = 0; i < mm->tiles_count; i++ )
        {
            struct MinimapTile const* t = &mm->tiles[i];
            h = wb_fold(h, t->background_rgb);
            h = wb_fold(h, t->foreground_rgb);
            h = wb_fold(h, t->wall);
            h = wb_fold(h, ((unsigned long long)t->shape << 16) | ((unsigned long long)t->rotation << 8) |
                               t->locs_count);
            for( int k = 0; k < t->locs_count && k < 8; k++ )
                h = wb_fold(h, t->locs[k]);
        }
    }

    WB_SUM_MARK(WB_SUM_MINIMAP);
    h = wb_fold(h, (unsigned long long)world->mapfunc_count);
    for( int i = 0; i < world->mapfunc_count; i++ )
    {
        struct World_MapFunctionIcon const* ic = &world->mapfuncs[i];
        h = wb_fold(h, (unsigned long long)(uint32_t)ic->x);
        h = wb_fold(h, (unsigned long long)(uint32_t)ic->z);
        h = wb_fold(h, (unsigned long long)(uint32_t)ic->level);
        h = wb_fold(h, (unsigned long long)(uint32_t)ic->func);
    }
    h = wb_fold(h, (unsigned long long)world->mapscene_count);
    for( int i = 0; i < world->mapscene_count; i++ )
    {
        struct World_MapSceneIcon const* ic = &world->mapscenes[i];
        h = wb_fold(h, (unsigned long long)(uint32_t)ic->x);
        h = wb_fold(h, (unsigned long long)(uint32_t)ic->z);
        h = wb_fold(h, (unsigned long long)(uint32_t)ic->level);
        h = wb_fold(h, (unsigned long long)(uint32_t)ic->mapscene);
        h = wb_fold(h, (unsigned long long)(uint32_t)ic->width);
        h = wb_fold(h, (unsigned long long)(uint32_t)ic->length);
    }
    h = wb_fold(h, (unsigned long long)world->area_sound_count);

    WB_SUM_MARK(WB_SUM_ICONS);
    {
        struct SceneOccluders* occ = world->painter ? painter_get_occluders(world->painter) : NULL;
        h = wb_fold(h, occ ? 1u : 0u);
        if( occ )
            for( int l = 0; l < occ->levels && l < 4; l++ )
            {
                h = wb_fold(h, (unsigned long long)occ->level_occluder_count[l]);
                for( int i = 0; i < occ->level_occluder_count[l]; i++ )
                {
                    struct SceneOccluder const* o = &occ->level_occluders[l][i];
                    h = wb_fold(h, (unsigned long long)(uint32_t)o->min_x);
                    h = wb_fold(h, (unsigned long long)(uint32_t)o->max_x);
                    h = wb_fold(h, (unsigned long long)(uint32_t)o->min_y);
                    h = wb_fold(h, (unsigned long long)(uint32_t)o->max_y);
                    h = wb_fold(h, (unsigned long long)(uint32_t)o->min_z);
                    h = wb_fold(h, (unsigned long long)(uint32_t)o->max_z);
                    h = wb_fold(h, ((unsigned long long)(uint16_t)o->tile_min_x << 48) |
                                       ((unsigned long long)(uint16_t)o->tile_max_x << 32) |
                                       ((unsigned long long)(uint16_t)o->tile_min_z << 16) |
                                       (unsigned long long)(uint16_t)o->tile_max_z);
                    h = wb_fold(h, o->plane);
                }
            }
    }

    WB_SUM_MARK(WB_SUM_OCCLUDERS);
    h = wb_fold(h, (unsigned long long)WorldBuilder_HiddenLocCount(builder));
    for( int i = 0; i < WorldBuilder_HiddenLocCount(builder); i++ )
    {
        struct WorldBuilderHiddenLoc const* r = WorldBuilder_HiddenLocGet(builder, i);
        h = wb_fold(h, (unsigned long long)(uint32_t)r->scene_x);
        h = wb_fold(h, (unsigned long long)(uint32_t)r->scene_z);
        h = wb_fold(h, (unsigned long long)(uint32_t)r->level);
        h = wb_fold(h, (unsigned long long)(uint32_t)r->loc_id);
        h = wb_fold(h, (unsigned long long)(uint32_t)r->shape);
        h = wb_fold(h, (unsigned long long)(uint32_t)r->angle);
    }
    WB_SUM_MARK(WB_SUM_HIDDEN);
    return h;
}

/* A state checksum and its cumulative sections. */
struct WbState
{
    unsigned long long total;
    unsigned long long parts[WB_SUM_COUNT];
};

static struct WbState
wb_state(
    struct WorldBuilder* builder,
    struct World* world,
    struct ToriDraw_Scene* scene)
{
    struct WbState st;
    st.total = wb_state_checksum(builder, world, scene);
    memcpy(st.parts, g_wb_sum_parts, sizeof(st.parts));
    return st;
}

/* Equal, or say which section first differs (the sections are cumulative). */
static int
wb_state_same(
    struct WbState const* a,
    struct WbState const* b,
    char const* what)
{
    if( a->total == b->total )
        return 1;
    for( int i = 0; i < WB_SUM_COUNT; i++ )
        if( a->parts[i] != b->parts[i] )
        {
            fprintf(stderr, "  %s: first difference in section '%s'\n", what, k_wb_sum_names[i]);
            break;
        }
    return 0;
}

/* A slice that is always over: one unit per WorldBuilder_BuildStep. */
static int
wb_spent_always(void* userdata)
{
    (*(int*)userdata)++;
    return 1;
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

    struct ToriRS_IOBatch* io = ToriRS_IOBatch_New();
    struct ToriRS_TaskQueue* queue = ToriRS_TaskQueue_New();
    struct Dat2BuildCache* bc = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(bc);
    CacheProvider_SetAssetQueue(provider, queue);
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        printf("SKIP: could not open dat2 disk cache at %s\n", cache_dir);
        ToriRS_TaskQueue_Free(queue);
        ToriRS_IOBatch_Free(io);
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
        ToriRS_IOBatch_Free(io);
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
        CreateTask_WorldLoad(
            provider, builder, queue, chunks, count, zone_x, zone_z, 104, NULL, NULL, NULL));
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

    /*
     * Walk mode (WB_WALK=laps): the leak probe. Each lap runs the full
     * REBUILD_NORMAL path (Task_WorldLoad: trim, IO, rebuild) around a ring
     * of eight map-square centres and ends back on Lumbridge, so lap-over-lap
     * heap growth with every asset already resident is memory a rebuild
     * failed to give back. The first lap pays for the squares and models it
     * visits; from the second lap on the growth must be ~0.
     */
    int walk_laps = getenv("WB_WALK") ? atoi(getenv("WB_WALK")) : 0;
    if( walk_laps > 0 )
    {
        static const int ring[8][2] = {
            { 51, 50 }, { 52, 50 }, { 52, 51 }, { 52, 52 },
            { 51, 52 }, { 50, 52 }, { 50, 51 }, { 50, 50 },
        };
        size_t lap_start = heap_in_use();
        printf("walk: heap in use before = %zu KB\n", lap_start / 1024);
        for( int lap = 0; lap < walk_laps; lap++ )
        {
            for( int s = 0; s < 8; s++ )
            {
                int cx = ring[s][0];
                int cz = ring[s][1];
                int wchunks[18];
                int wc = 0;
                for( int mx = cx - 1; mx <= cx + 1; mx++ )
                    for( int mz = cz - 1; mz <= cz + 1; mz++ )
                    {
                        wchunks[wc * 2] = mx;
                        wchunks[wc * 2 + 1] = mz;
                        wc++;
                    }
                int zx = (cx * 64 + 32) / 8;
                int zz = (cz * 64 + 32) / 8;
                run_task(
                    queue, io, px,
                    CreateTask_WorldLoad(
                        provider, builder, queue, wchunks, wc, zx, zz, 104, NULL, NULL, NULL));
                printf(
                    "walk: lap %d step %d centre %d,%d heap in use = %zu KB\n",
                    lap, s, cx, cz, heap_in_use() / 1024);
            }
            size_t now = heap_in_use();
            printf("walk: lap %d growth = %ld KB\n", lap, (long)(now - lap_start) / 1024);
            lap_start = now;
        }
    }

    printf(
        "bench: scene elements terrain=%d scenery=%d\n",
        world->entities.terrain.active_count,
        world->entities.scenery.active_count);

    /* Lighting checksum over every scenery model's lit face colours: any
     * change to the sharelight merge/apply pipeline must keep this stable. */
    printf("bench: lighting checksum = %llx\n", wb_lighting_checksum(world, scene));

    /* The same rebuild stepped one unit per call: what the stepping costs
     * (TORIRS_REBUILD_TIMING=1 adds the unit count and the longest unit). */
    for( int iter = 0; iter < 2; iter++ )
    {
        int steps = 0;
        double s0 = bench_now_ms();
        WorldBuilder_BuildBeginCenterzone(builder, zone_x, zone_z, 104);
        while( !WorldBuilder_BuildStep(builder, wb_spent_always, &steps) )
        {
        }
        double s1 = bench_now_ms();
        printf("bench: stepped rebuild %d = %.1f ms (%d steps)\n", iter, s1 - s0, steps + 1);
    }
    printf("bench: stepped lighting checksum = %llx\n", wb_lighting_checksum(world, scene));

    WorldBuilder_Free(builder);
    World_Free(world);
    VarPManager_Free(&varp);
    PlatformX_IO_Free(px);
    RSCache_Dat2DiskFree(disk);
    ToriRS_TaskQueue_Free(queue);
    ToriRS_IOBatch_Free(io);
    dat2_buildcache_free(bc);
}

/* ------------------------------------------------------------------ */
/* Stepped build vs the blocking build                                 */
/* ------------------------------------------------------------------ */

/*
 * The minimap's mapfunction spread is a rand() walk
 * (world_builder_minimap_spread_mapfunctions), so no two builds agree on the
 * icons unless the generator starts each one in the same place. Every build
 * this test compares is seeded first; the build calls rand() in the same order
 * either way.
 */
#define WB_TEST_SEED 0x5eed

/* Run a stepped build one unit per call; returns the number of units. */
static int
wb_step_to_completion(struct WorldBuilder* builder)
{
    int spent_calls = 0;
    int steps = 1;
    while( !WorldBuilder_BuildStep(builder, wb_spent_always, &spent_calls) )
        steps++;
    TEST_ASSERT(!WorldBuilder_BuildActive(builder), "a completed stepped build leaves the builder idle");
    /* One unit per call: every call but the last asked `spent` once. */
    TEST_ASSERT(spent_calls == steps - 1, "a spent slice stops after exactly one unit");
    return steps;
}

/*
 * A REBUILD_REGION grid over Lumbridge's squares, every zone turned by its own
 * rotation, a few void and a few planes copied onto another: enough to walk
 * every branch of the instance path (void skip, rotations, plane remap).
 */
static void
wb_test_instance_grid(int32_t* zones)
{
    memset(zones, 0, sizeof(int32_t) * WORLD_MAP_TERRAIN_LEVELS * WORLD_INSTANCE_ZONES *
                         WORLD_INSTANCE_ZONES);
    for( int level = 0; level < 2; level++ )
        for( int zx = 0; zx < WORLD_INSTANCE_ZONES; zx++ )
            for( int zz = 0; zz < WORLD_INSTANCE_ZONES; zz++ )
            {
                int src_zone_x = 50 * 8 + ((zx * 3 + level) % 8);
                int src_zone_z = 50 * 8 + ((zz * 5 + level) % 8);
                int rotation = (zx + zz * 2 + level) & 3;
                int src_level = level == 1 && (zx & 1) ? 0 : level;
                if( (zx * 7 + zz) % 11 == 0 )
                    continue;
                if( level == 1 && zz > 8 )
                    continue;
                zones[level * WORLD_INSTANCE_ZONES * WORLD_INSTANCE_ZONES +
                      zx * WORLD_INSTANCE_ZONES + zz] =
                    (rotation << 1) | ((src_zone_z & 0x7ff) << 3) | ((src_zone_x & 0x3ff) << 14) |
                    ((src_level & 3) << 24);
            }
}

/*
 * The stepped build (WorldBuilder_BuildStep) against the blocking one on
 * Lumbridge: centerzone, chunk list and an instance grid, each stepped one
 * unit per call, must leave exactly the blocking build's state. Then aborts at
 * points across every stage, each followed by a complete stepped build: same
 * state again, the world never left marked loaded by an abort, and no heap
 * growth across the repeats.
 */
void
test_world_builder_stepped(void)
{
    const char* cache_dir = getenv("CACHE_DIR");
    if( !cache_dir )
        cache_dir = DEFAULT_CACHE_DIR;

    struct stat st;
    if( stat(cache_dir, &st) != 0 || !S_ISDIR(st.st_mode) )
    {
        printf("SKIP: stepped build: cache dir not found: %s\n", cache_dir);
        return;
    }

    struct ToriRS_IOBatch* io = ToriRS_IOBatch_New();
    struct ToriRS_TaskQueue* queue = ToriRS_TaskQueue_New();
    struct Dat2BuildCache* bc = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(bc);
    CacheProvider_SetAssetQueue(provider, queue);
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        printf("SKIP: stepped build: could not open dat2 disk cache at %s\n", cache_dir);
        ToriRS_TaskQueue_Free(queue);
        ToriRS_IOBatch_Free(io);
        dat2_buildcache_free(bc);
        return;
    }

    struct RSCache profile;
    const char* profile_name = profile_name_for_cache_dir(cache_dir);
    if( !profile_name || !RSCache_ProfileByName(profile_name, &profile) )
    {
        printf("SKIP: stepped build: cache dir %s has no labelled profile\n", cache_dir);
        RSCache_Dat2DiskFree(disk);
        ToriRS_TaskQueue_Free(queue);
        ToriRS_IOBatch_Free(io);
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

    /* Lumbridge's 3x3, every asset made resident by the load task (whose own
     * rebuild is the blocking one). */
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
    run_task(
        queue, io, px,
        CreateTask_WorldLoad(
            provider, builder, queue, chunks, count, zone_x, zone_z, 104, NULL, NULL, NULL));

    /* --- centerzone --- */
    srand(WB_TEST_SEED);
    WorldBuilder_RebuildCenterzone(builder, zone_x, zone_z, 104);
    struct WbState cz_blocking = wb_state(builder, world, scene);
    unsigned long long cz_light = wb_lighting_checksum(world, scene);
    TEST_ASSERT(world->load_complete, "blocking centerzone build is loaded");

    srand(WB_TEST_SEED);
    WorldBuilder_BuildBeginCenterzone(builder, zone_x, zone_z, 104);
    TEST_ASSERT(WorldBuilder_BuildActive(builder), "BuildBegin opens a build");
    TEST_ASSERT(world->load_complete, "BuildBegin only records: the world is untouched");
    int cz_units = wb_step_to_completion(builder);
    struct WbState cz_stepped = wb_state(builder, world, scene);
    TEST_ASSERT(world->load_complete, "stepped centerzone build is loaded");
    TEST_ASSERT(wb_state_same(&cz_stepped, &cz_blocking, "centerzone"), "stepped centerzone == blocking centerzone");
    TEST_ASSERT(
        wb_lighting_checksum(world, scene) == cz_light, "stepped lighting == blocking lighting");
    /* The pre-change blocking build's value on this cache (the bench prints
     * it): the refactor that the stepper shares must not have moved it. */
    if( strcmp(profile_name, "osrs239") == 0 )
        TEST_ASSERT(cz_light == 0x92f3c81632830f69ull, "blocking lighting == pre-change baseline");
    printf(
        "stepped: centerzone %d units, state %llx (blocking %llx), lighting %llx\n",
        cz_units,
        cz_stepped.total,
        cz_blocking.total,
        cz_light);

    /* NULL spent: one call runs the whole build. */
    srand(WB_TEST_SEED);
    WorldBuilder_BuildBeginCenterzone(builder, zone_x, zone_z, 104);
    TEST_ASSERT(WorldBuilder_BuildStep(builder, NULL, NULL) == 1, "NULL spent runs to completion");
    {
        struct WbState unsliced = wb_state(builder, world, scene);
        TEST_ASSERT(wb_state_same(&unsliced, &cz_blocking, "unsliced"), "unsliced step == blocking");
    }

    /* --- chunk list --- */
    srand(WB_TEST_SEED);
    WorldBuilder_RebuildChunklist(builder, chunks, count);
    struct WbState cl_blocking = wb_state(builder, world, scene);
    {
        /* The list is copied at Begin: scribbling over the caller's array
         * between steps must not reach the build. */
        int scratch[18];
        memcpy(scratch, chunks, sizeof(scratch));
        srand(WB_TEST_SEED);
        WorldBuilder_BuildBeginChunklist(builder, scratch, count);
        memset(scratch, 0xff, sizeof(scratch));
    }
    int cl_units = wb_step_to_completion(builder);
    struct WbState cl_stepped = wb_state(builder, world, scene);
    TEST_ASSERT(wb_state_same(&cl_stepped, &cl_blocking, "chunklist"), "stepped chunklist == blocking chunklist");
    TEST_ASSERT(cl_blocking.total != cz_blocking.total, "chunklist and centerzone windows differ");
    printf("stepped: chunklist %d units, state %llx (blocking %llx)\n", cl_units, cl_stepped.total,
        cl_blocking.total);

    /* --- instance --- */
    {
        int32_t zones[WORLD_MAP_TERRAIN_LEVELS * WORLD_INSTANCE_ZONES * WORLD_INSTANCE_ZONES];
        int32_t scratch[WORLD_MAP_TERRAIN_LEVELS * WORLD_INSTANCE_ZONES * WORLD_INSTANCE_ZONES];
        wb_test_instance_grid(zones);
        srand(WB_TEST_SEED);
        WorldBuilder_RebuildInstance(builder, zone_x, zone_z, 104, zones);
        struct WbState in_blocking = wb_state(builder, world, scene);
        TEST_ASSERT(world->entities.scenery.active_count > 0, "instance grid placed scenery");
        memcpy(scratch, zones, sizeof(scratch));
        srand(WB_TEST_SEED);
        WorldBuilder_BuildBeginInstance(builder, zone_x, zone_z, 104, scratch);
        memset(scratch, 0, sizeof(scratch));
        int in_units = wb_step_to_completion(builder);
        struct WbState in_stepped = wb_state(builder, world, scene);
        TEST_ASSERT(wb_state_same(&in_stepped, &in_blocking, "instance"), "stepped instance == blocking instance");
        printf("stepped: instance %d units, state %llx (blocking %llx)\n", in_units, in_stepped.total,
            in_blocking.total);
    }

    /* --- abort --- */
    {
        /* Points across every stage: before the first step, after the reset,
         * in terrain, in scenery, in End's early passes, in the mesh, in the
         * lighting, and on the last unit before the batch closes. */
        int const abort_at[] = { 0, 1, 4, 12, 20, 26, 34, cz_units - 1 };
        int const abort_count = (int)(sizeof(abort_at) / sizeof(abort_at[0]));
        size_t heap_after_first = 0;

        srand(WB_TEST_SEED);
        WorldBuilder_RebuildCenterzone(builder, zone_x, zone_z, 104);
        for( int rep = 0; rep < 4; rep++ )
        {
            for( int a = 0; a < abort_count; a++ )
            {
                int spent_calls = 0;
                int ran = 0;
                srand(WB_TEST_SEED);
                WorldBuilder_BuildBeginCenterzone(builder, zone_x, zone_z, 104);
                while( ran < abort_at[a] )
                {
                    int done = WorldBuilder_BuildStep(builder, wb_spent_always, &spent_calls);
                    ran++;
                    TEST_ASSERT(!done, "abort point lies inside the build");
                    if( done )
                        break;
                }
                if( !WorldBuilder_BuildActive(builder) )
                    continue;
                WorldBuilder_BuildAbort(builder);
                TEST_ASSERT(!WorldBuilder_BuildActive(builder), "abort leaves the builder idle");
                if( abort_at[a] > 0 && abort_at[a] < cz_units - 1 )
                    TEST_ASSERT(!world->load_complete, "a half-built world is not loaded");
                if( abort_at[a] == 0 )
                {
                    struct WbState untouched = wb_state(builder, world, scene);
                    TEST_ASSERT(
                        wb_state_same(&untouched, &cz_blocking, "abort at 0"),
                        "abort before the first step touches nothing");
                }

                srand(WB_TEST_SEED);
                WorldBuilder_BuildBeginCenterzone(builder, zone_x, zone_z, 104);
                wb_step_to_completion(builder);
                {
                    struct WbState rebuilt = wb_state(builder, world, scene);
                    TEST_ASSERT(
                        wb_state_same(&rebuilt, &cz_blocking, "after abort"),
                        "abort, then a complete build == blocking");
                }
                TEST_ASSERT(world->load_complete, "the build after an abort is loaded");
            }
            if( rep == 0 )
                heap_after_first = heap_in_use();
        }
        {
            size_t heap_end = heap_in_use();
            long growth_kb = (long)(heap_end - heap_after_first) / 1024;
            printf("stepped: abort repeats heap growth = %ld KB\n", growth_kb);
            /* Three further repeats of eight abort+rebuild pairs each; an
             * abort that leaked a build map (~1 MB a set) or the window copy
             * would show here many times over. */
            TEST_ASSERT(growth_kb < 256, "abort + rebuild repeats do not grow the heap");
        }
    }

    WorldBuilder_Free(builder);
    World_Free(world);
    VarPManager_Free(&varp);
    PlatformX_IO_Free(px);
    RSCache_Dat2DiskFree(disk);
    ToriRS_TaskQueue_Free(queue);
    ToriRS_IOBatch_Free(io);
    dat2_buildcache_free(bc);
}

/* ------------------------------------------------------------------ */
/* Hot swap: a scene built beside the live one, then swapped in        */
/* ------------------------------------------------------------------ */

/* The scene view whose pool pair the staging builder takes: the background
 * rebuild's (app_world_job.c, APP_WORLD_STAGE_VIEW). */
#define WB_TEST_STAGE_VIEW (TORIDRAW_SCENE_POOL_VIEW_MAX - 1)

/* Count the batch events the swap left on the scene's queue. */
static void
wb_count_batch_events(
    struct ToriDraw_Scene* scene,
    int* clears,
    int* begins,
    int* adds,
    int* ends,
    int* model_loads)
{
    struct ToriDraw_EventQueue* eq = ToriDraw_SceneEvents(scene);

    *clears = *begins = *adds = *ends = *model_loads = 0;
    for( int i = 0; i < eq->count; i++ )
    {
        switch( eq->events[i].kind )
        {
        case TORIDRAW_EVENT_BATCH_CLEAR:
            (*clears)++;
            break;
        case TORIDRAW_EVENT_BATCH_BEGIN:
            (*begins)++;
            break;
        case TORIDRAW_EVENT_BATCH_MODEL_ADD:
            if( eq->events[i].model.kind != TORIDRAWMK_NONE )
                (*adds)++;
            break;
        case TORIDRAW_EVENT_BATCH_END:
            (*ends)++;
            break;
        case TORIDRAW_EVENT_MODEL_LOAD:
            (*model_loads)++;
            break;
        default:
            break;
        }
    }
}

/* What the entity pools hold: every record's slot, element id and tile. */
static unsigned long long
wb_entity_checksum(struct World* world)
{
    unsigned long long h = 1469598103934665603ull;
    struct World_EntityPool* pool = &world->entities.player;

    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Player* p = World_EntityPoolGet(pool, i);
        h = wb_fold(h, (unsigned long long)i);
        h = wb_fold(h, (unsigned long long)(uint32_t)p->element_id);
        h = wb_fold(h, (unsigned long long)(uint32_t)p->grid_position.x);
        h = wb_fold(h, (unsigned long long)(uint32_t)p->grid_position.z);
    }
    pool = &world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* n = World_EntityPoolGet(pool, i);
        h = wb_fold(h, (unsigned long long)i);
        h = wb_fold(h, (unsigned long long)(uint32_t)n->element_id);
        h = wb_fold(h, (unsigned long long)(uint32_t)n->grid_position.x);
        h = wb_fold(h, (unsigned long long)(uint32_t)n->grid_position.z);
    }
    return h;
}

/*
 * WorldBuilder_HotSwap on Lumbridge. World 1 holds scene X, built in place,
 * plus a player and an npc with their own dynamic elements. Scene Y is built
 * a unit at a time into a staging world on its own pools -- world 1 unchanged
 * between every unit -- and swapped into world 1. The result must be what a
 * build of Y in place leaves (the stepped build's state checksum), the
 * entities and their elements untouched, the old scene's elements gone (the
 * live element count back to one scene's worth), the retained renderer's
 * stream a rebuild's (BATCH_CLEAR, one batch with an add per element, no
 * per-element loads), and repeats must not grow the heap.
 */
void
test_world_builder_hot_swap(void)
{
    const char* cache_dir = getenv("CACHE_DIR");
    if( !cache_dir )
        cache_dir = DEFAULT_CACHE_DIR;

    struct stat st;
    if( stat(cache_dir, &st) != 0 || !S_ISDIR(st.st_mode) )
    {
        printf("SKIP: hot swap: cache dir not found: %s\n", cache_dir);
        return;
    }

    struct ToriRS_IOBatch* io = ToriRS_IOBatch_New();
    struct ToriRS_TaskQueue* queue = ToriRS_TaskQueue_New();
    struct Dat2BuildCache* bc = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(bc);
    CacheProvider_SetAssetQueue(provider, queue);
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !disk )
    {
        printf("SKIP: hot swap: could not open dat2 disk cache at %s\n", cache_dir);
        ToriRS_TaskQueue_Free(queue);
        ToriRS_IOBatch_Free(io);
        dat2_buildcache_free(bc);
        return;
    }

    struct RSCache profile;
    const char* profile_name = profile_name_for_cache_dir(cache_dir);
    if( !profile_name || !RSCache_ProfileByName(profile_name, &profile) )
    {
        printf("SKIP: hot swap: cache dir %s has no labelled profile\n", cache_dir);
        RSCache_Dat2DiskFree(disk);
        ToriRS_TaskQueue_Free(queue);
        ToriRS_IOBatch_Free(io);
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
    World_SetScene(world, scene);
    struct VarPManager varp;
    VarPManager_Init(&varp);
    struct WorldBuilder* builder = WorldBuilder_New(world, provider, scene, &varp);

    /* Lumbridge's 3x3, which holds both windows: X centred on the square,
     * Y two zones (16 tiles) east of it. */
    int chunks[18];
    int count = 0;
    for( int mx = 49; mx <= 51; mx++ )
        for( int mz = 49; mz <= 51; mz++ )
        {
            chunks[count * 2] = mx;
            chunks[count * 2 + 1] = mz;
            count++;
        }
    int const zone_x = (50 * 64 + 32) / 8;
    int const zone_z = (50 * 64 + 32) / 8;
    int const zone_y_x = zone_x + 2;
    run_task(
        queue, io, px,
        CreateTask_WorldLoad(
            provider, builder, queue, chunks, count, zone_x, zone_z, 104, NULL, NULL, NULL));

    /* The entities: one player, one npc, each holding a tagged dynamic
     * element, as app_world_scene_element_create makes them. */
    int const player_element = ElementId_Raw(ElementId_Make(
        TORIDRAW_ELEMENT_KIND_PLAYER, ToriDraw_SceneElementAddPool(scene, TORIDRAW_SCENE_POOL_DYNAMIC)));
    int const npc_element = ElementId_Raw(ElementId_Make(
        TORIDRAW_ELEMENT_KIND_NPC, ToriDraw_SceneElementAddPool(scene, TORIDRAW_SCENE_POOL_DYNAMIC)));
    {
        int const ps = World_EntityPoolAlloc(&world->entities.player);
        int const ns = World_EntityPoolAlloc(&world->entities.npc);
        struct WorldEntity_Player* p = World_EntityPoolGet(&world->entities.player, ps);
        struct WorldEntity_NPC* n = World_EntityPoolGet(&world->entities.npc, ns);
        p->element_id = player_element;
        p->grid_position.x = 52;
        p->grid_position.z = 51;
        n->element_id = npc_element;
        n->grid_position.x = 40;
        n->grid_position.z = 61;
    }
    unsigned long long const entities = wb_entity_checksum(world);

    /* The reference: Y built in place, by the stepped build the swap uses. */
    srand(WB_TEST_SEED);
    WorldBuilder_BuildBeginCenterzone(builder, zone_y_x, zone_z, 104);
    wb_step_to_completion(builder);
    struct WbState const y_in_place = wb_state(builder, world, scene);
    unsigned long long const y_light = wb_lighting_checksum(world, scene);
    int const live_one_scene = ToriDraw_SceneElementLiveCount(scene);
    int const y_static = ToriDraw_ScenePoolCount(scene, TORIDRAW_SCENE_POOL_STATIC);
    /* And the blocking build of Y, which must agree with the stepped one. */
    srand(WB_TEST_SEED);
    WorldBuilder_RebuildCenterzone(builder, zone_y_x, zone_z, 104);
    {
        struct WbState const y_blocking = wb_state(builder, world, scene);
        TEST_ASSERT(wb_state_same(&y_blocking, &y_in_place, "Y blocking"), "Y: blocking == stepped");
    }

    size_t heap_after_first = 0;
    for( int rep = 0; rep < 4; rep++ )
    {
        /* X in place in world 1. */
        srand(WB_TEST_SEED);
        WorldBuilder_RebuildCenterzone(builder, zone_x, zone_z, 104);
        struct WbState const x_state = wb_state(builder, world, scene);
        TEST_ASSERT(x_state.total != y_in_place.total, "X and Y are different scenes");
        int const live_x = ToriDraw_SceneElementLiveCount(scene);

        /* Y beside it, in a staging world on its own pools, loads withheld. */
        struct World* stage_world = World_New();
        World_SetScene(stage_world, scene);
        struct WorldBuilder* stage = WorldBuilder_New(stage_world, provider, scene, &varp);
        WorldBuilder_SetSceneView(stage, WB_TEST_STAGE_VIEW);
        ToriDraw_SceneSetDeferredPool(scene, stage->static_pool);
        ToriDraw_SceneFrameEnd(scene);

        srand(WB_TEST_SEED);
        WorldBuilder_BuildBeginCenterzone(stage, zone_y_x, zone_z, 104);
        {
            int spent_calls = 0;
            int units = 1;
            int live_unchanged = 1;
            while( !WorldBuilder_BuildStep(stage, wb_spent_always, &spent_calls) )
            {
                units++;
                /* The live world between two units: still X, its entities
                 * where they were. Checked on a sample of the units; every
                 * unit would make the test minutes long. */
                if( units % 16 == 0 )
                {
                    struct WbState const now = wb_state(builder, world, scene);
                    if( now.total != x_state.total || wb_entity_checksum(world) != entities )
                        live_unchanged = 0;
                }
            }
            TEST_ASSERT(live_unchanged, "the live world is untouched while the staging build steps");
            if( rep == 0 )
                printf("hot swap: staging build %d units\n", units);
        }
        {
            int clears, begins, adds, ends, loads;
            wb_count_batch_events(scene, &clears, &begins, &adds, &ends, &loads);
            TEST_ASSERT(begins == 0 && adds == 0 && loads == 0,
                "a deferred build emits no model load and no batch");
        }
        TEST_ASSERT(
            ToriDraw_SceneElementLiveCount(scene) > live_x,
            "while staged, both scenes are resident");
        /* The staging builder's own state, before the swap moves it. */
        {
            struct WbState const staged = wb_state(stage, stage_world, scene);
            TEST_ASSERT(wb_state_same(&staged, &y_in_place, "staged"), "staged Y == Y built in place");
        }

        ToriDraw_SceneFrameEnd(scene);
        WorldBuilder_HotSwap(builder, stage);
        ToriDraw_SceneSetDeferredPool(scene, -1);

        {
            struct WbState const swapped = wb_state(builder, world, scene);
            TEST_ASSERT(wb_state_same(&swapped, &y_in_place, "swapped"), "swapped Y == Y built in place");
            TEST_ASSERT(wb_lighting_checksum(world, scene) == y_light, "swapped lighting == in place");
        }
        TEST_ASSERT(world->load_complete, "the live world is loaded after the swap");
        TEST_ASSERT(!stage_world->load_complete, "the staging world holds the old scene, unloaded");
        TEST_ASSERT(wb_entity_checksum(world) == entities, "the entity pools are untouched by the swap");
        TEST_ASSERT(ToriDraw_SceneElementIsLive(scene, player_element), "the player's element survived");
        TEST_ASSERT(ToriDraw_SceneElementIsLive(scene, npc_element), "the npc's element survived");
        TEST_ASSERT(
            ToriDraw_ScenePoolCount(scene, stage->static_pool) == 0,
            "the staging pool is empty after the swap");
        TEST_ASSERT(
            ToriDraw_SceneElementLiveCount(scene) == live_one_scene,
            "the old scene's elements are gone: one scene's worth resident");
        TEST_ASSERT(
            ToriDraw_ScenePoolCount(scene, TORIDRAW_SCENE_POOL_STATIC) == y_static,
            "the live static pool holds exactly the new scene");
        {
            int clears, begins, adds, ends, loads;
            int full = 0;
            wb_count_batch_events(scene, &clears, &begins, &adds, &ends, &loads);
            for( int id = 0; id < ToriDraw_SceneElementSlotCount(scene); id++ )
            {
                struct ToriDraw_SceneElement* el;
                if( !ToriDraw_SceneElementIsLive(scene, id) )
                    continue;
                el = ToriDraw_SceneElementGet(scene, id);
                if( el->pool == TORIDRAW_SCENE_POOL_STATIC && el->model.kind != TORIDRAWMK_NONE )
                    full++;
            }
            TEST_ASSERT(clears == 1, "the swap clears the retained world batch once");
            TEST_ASSERT(begins == 1 && ends == 1, "the swap emits one world batch");
            TEST_ASSERT(adds == full, "one batch add per new static element with a model");
            TEST_ASSERT(loads == 0, "no per-element model load: the world goes out as a batch");
            if( rep == 0 )
                printf(
                    "hot swap: batch clear=%d begin=%d adds=%d (static elements with models %d) end=%d\n",
                    clears, begins, adds, full, ends);
        }
        ToriDraw_SceneFrameEnd(scene);

        WorldBuilder_Free(stage);
        World_Free(stage_world);
        if( rep == 0 )
            heap_after_first = heap_in_use();
    }
    {
        long const growth_kb = (long)(heap_in_use() - heap_after_first) / 1024;
        printf("hot swap: repeats heap growth = %ld KB\n", growth_kb);
        TEST_ASSERT(growth_kb < 256, "build X, stage Y, swap: repeats do not grow the heap");
    }

    /* Abort mid-build: the staging pool's elements go with it, and the live
     * scene is still X. */
    {
        srand(WB_TEST_SEED);
        WorldBuilder_RebuildCenterzone(builder, zone_x, zone_z, 104);
        struct WbState const x_state = wb_state(builder, world, scene);
        int const live_x = ToriDraw_SceneElementLiveCount(scene);
        struct World* stage_world = World_New();
        World_SetScene(stage_world, scene);
        struct WorldBuilder* stage = WorldBuilder_New(stage_world, provider, scene, &varp);
        int spent_calls = 0;
        WorldBuilder_SetSceneView(stage, WB_TEST_STAGE_VIEW);
        ToriDraw_SceneSetDeferredPool(scene, stage->static_pool);
        WorldBuilder_BuildBeginCenterzone(stage, zone_y_x, zone_z, 104);
        for( int u = 0; u < 40; u++ )
            (void)WorldBuilder_BuildStep(stage, wb_spent_always, &spent_calls);
        TEST_ASSERT(ToriDraw_ScenePoolCount(scene, stage->static_pool) > 0, "mid-build, the staging pool holds elements");
        /* While a staging build is open, the live builder's loc changes land:
         * its build holds no scene batch (g_wb_batch_build). */
        WorldBuilder_ApplyLocChange(builder, 10, 10, 0, -1, 10, 0);
        WorldBuilder_BuildAbort(stage);
        ToriDraw_SceneClearPool(scene, stage->static_pool);
        ToriDraw_SceneClearPool(scene, stage->dynamic_pool);
        ToriDraw_SceneSetDeferredPool(scene, -1);
        WorldBuilder_Free(stage);
        World_Free(stage_world);
        TEST_ASSERT(ToriDraw_SceneElementLiveCount(scene) <= live_x, "an aborted staging build leaves no element behind");
        (void)x_state;
    }

    WorldBuilder_Free(builder);
    World_Free(world);
    VarPManager_Free(&varp);
    PlatformX_IO_Free(px);
    RSCache_Dat2DiskFree(disk);
    ToriRS_TaskQueue_Free(queue);
    ToriRS_IOBatch_Free(io);
    dat2_buildcache_free(bc);
}
