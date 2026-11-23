/*
 * An animated loc whose sequence lands after its scene was built looks the
 * same as one whose sequence was there for the build.
 *
 * Task_WorldLoad no longer waits for the sequences the region's locs play
 * (a sequence's frames are only drawn): the build binds those already
 * registered, and the rest are bound when they arrive
 * (WorldBuilder_LocSequencesMissing / WorldBuilder_BindLocSequence, from the
 * client's Task_LocSeqBind). An animated loc is built at its authored pose
 * with its orient, resize and offset left as post transforms, which the bind
 * applies after it has captured that pose. The build lights every model at
 * its end -- so a loc bound DURING the build is lit after its post
 * transforms, and one bound AFTER it was lit before them and then turned.
 * Lumbridge's ap_indicator (loc 37348, shown as ap_barrel, the shell's seq
 * 8490 oge_banner_flutter, rotation 2) at 3231,3235 drew with its lit side
 * facing away from the light in the map editor on the branch, and lit on
 * 1528fc3a1, which waited for the sequence before the build.
 *
 * Against the real osrs242 cache: map square 50,50 is built with no loc
 * sequence registered, sequence 8490 is loaded and bound late; then the same
 * square is built again with the sequence registered, so it binds during the
 * build. The barrel's vertices, its bind pose and its lit colours must be the
 * same both ways.
 *
 * And until its sequence binds -- for ever, if the sequence never loads --
 * the barrel is drawn in its static form: turned, resized and offset as the
 * map places it and lit that way, the rest pose the bind gives it. Before,
 * the post transforms were applied only by the bind, so in that window the
 * loc stood at its authored pose, unturned (rotation 2 drew it facing the
 * other way).
 *
 *   make -C src test-loc-seq-bind
 */

#include "asyncio.h"
#include "engine/asset_requests.h"
#include "engine/asset_cache_loaders.h"
#include "engine/cache_provider.h"
#include "engine/dat2/dat2_tasks.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/world_builder/task_world_load.h"
#include "engine/world_builder/world_builder.h"
#include "platform/platform_x_io.h"
#include "task_runner.h"
#include "varp/varp_manager.h"
#include "world/world.h"

#include <assert.h>
#include <rscache.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <toridraw.h>
#include <toridraw_scene.h>

static int g_failures;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            g_failures++;                                                                          \
        }                                                                                          \
    } while( 0 )

#define LOC_AP_INDICATOR 37348
#define SEQ_OGE_BANNER_FLUTTER 8490

/* What the barrel looks like: its geometry, its bind pose, its lit colours. */
struct LocLook
{
    int found;
    int animated;
    int vertex_count;
    int face_count;
    long vertex_sum;
    long original_sum;
    hsl16_t* colors; /* a, b, c per face */
};

static void
run_all(
    struct ToriRS_TaskQueue* queue,
    struct ToriRS_IOBatch* io,
    struct PlatformX_IO* px,
    struct ToriRS_Task* task)
{
    struct TaskRunner runner = {
        .queue = queue,
        .io = io,
        .px = px,
        .parallel = 1,
    };

    assert(task);
    ToriRS_TaskQueue_Add(queue, task);
    while( TaskRunner_Step(&runner) != TASK_RUNNER_IDLE )
    {
    }
}

static void
look_at(
    struct World* world,
    struct ToriDraw_Scene* scene,
    struct LocLook* out)
{
    struct World_EntityPool* pool = &world->entities.scenery;

    memset(out, 0, sizeof(*out));
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL; i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_Scenery const* sc = World_EntityPoolGet(pool, i);
        struct ToriDraw_SceneElement* element;
        struct ToriDraw_Model* m;

        if( !sc || sc->loc_id != LOC_AP_INDICATOR || sc->element_id < 0 )
            continue;
        element = ToriDraw_SceneElementGet(scene, sc->element_id);
        assert(element);
        assert(element->model.kind == TORIDRAWMK_MODEL);
        m = element->model.u.model.model;
        assert(m);
        out->found = 1;
        out->animated = element->animation != NULL;
        out->vertex_count = m->vertex_count;
        out->face_count = m->face_count;
        for( int v = 0; v < m->vertex_count; v++ )
            out->vertex_sum += m->vertices_x[v] * 3L + m->vertices_y[v] * 7L + m->vertices_z[v] * 11L;
        for( int v = 0; m->original_vertices_x && v < m->vertex_count; v++ )
            out->original_sum +=
                m->original_vertices_x[v] * 3L + m->original_vertices_y[v] * 7L + m->original_vertices_z[v] * 11L;
        out->colors = malloc(sizeof(hsl16_t) * 3 * (size_t)m->face_count);
        assert(out->colors);
        for( int f = 0; f < m->face_count; f++ )
        {
            out->colors[f * 3 + 0] = m->face_colors_a[f];
            out->colors[f * 3 + 1] = m->face_colors_b ? m->face_colors_b[f] : 0;
            out->colors[f * 3 + 2] = m->face_colors_c ? m->face_colors_c[f] : 0;
        }
        return;
    }
}

int
main(
    int argc,
    char** argv)
{
    char const* root = argc > 1 ? argv[1] : "..";
    char path[1024];
    struct RSCache profile;
    int chunks[2] = { 50, 50 };
    struct LocLook before;
    struct LocLook late;
    struct LocLook built;
    int missing[4096];
    int n;
    int has_8490 = 0;
    int bound;
    int differ = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("TEST: cache.osrs242 m50_50: a loc whose sequence binds after the build looks as one bound during it\n");
    snprintf(path, sizeof(path), "%s/cache.osrs242", root);
    struct RSCache_Dat2Disk* disk = RSCache_Dat2DiskNewFromDirectory(path);
    if( !disk )
    {
        printf("   SKIP %s: not present\n", path);
        return 0;
    }
    if( !RSCache_ProfileByName("osrs242", &profile) )
        abort();
    RSCache_Dat2DiskSetProfile(disk, &profile);
    if( RSCache_MapLocsEncrypted(&profile) )
    {
        char xtea_path[1100];
        snprintf(xtea_path, sizeof(xtea_path), "%s/xteas.json", path);
        RSCache_XteaConfigLoadKeys(xtea_path);
    }

    struct ToriRS_IOBatch* io = ToriRS_IOBatch_New();
    struct ToriRS_TaskQueue* queue = ToriRS_TaskQueue_New();
    struct Dat2BuildCache* bc = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(bc);
    struct PlatformX_IO* px = PlatformX_IO_New();

    CacheProvider_SetAssetQueue(provider, queue);
    CacheProvider_SetComputeQueue(provider, queue);
    CacheProvider_SetProfile(provider, &profile);
    PlatformX_IO_InitDat2Disk(px, disk);
    /* The reference tables a map load awaits, as the client attaches them
     * (App_AssetsInit). */
    struct Assets* assets = Assets_New();
    assert(assets);
    Dat2BuildCache_AttachAssets(bc, assets);
    AssetCache_RegisterLoaders(assets, provider);
    ToriDraw_Init();
    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0, TORIDRAW_SCRATCH_BUFFER_HIGH_8K);
    struct World* world = World_New();
    struct VarPManager varp;
    VarPManager_Init(&varp);
    struct WorldBuilder* builder = WorldBuilder_New(world, provider, scene, &varp);

    /* 1. Built with no loc sequence registered: the barrel is not animated,
     * its sequence is among the missing, and it is bound when it lands. */
    run_all(queue, io, px, CreateTask_WorldLoad(provider, builder, queue, chunks, 1, -1, -1, 104, NULL, NULL, NULL));
    CHECK(!ToriDraw_SceneAnimationGet(scene, SEQ_OGE_BANNER_FLUTTER), "sequence 8490 was registered by the build");
    n = WorldBuilder_LocSequencesMissing(builder, missing, (int)(sizeof(missing) / sizeof(missing[0])));
    for( int i = 0; i < n; i++ )
        has_8490 |= missing[i] == SEQ_OGE_BANNER_FLUTTER;
    CHECK(has_8490, "sequence 8490 is not among the %d the build left missing", n);
    look_at(world, scene, &before);
    run_all(queue, io, px, CreateTask_SequenceLoad(provider, scene, SEQ_OGE_BANNER_FLUTTER));
    CHECK(ToriDraw_SceneAnimationGet(scene, SEQ_OGE_BANNER_FLUTTER) != NULL, "sequence 8490 did not load");
    bound = WorldBuilder_BindLocSequence(builder, SEQ_OGE_BANNER_FLUTTER);
    CHECK(bound >= 1, "the late bind bound %d element(s)", bound);
    look_at(world, scene, &late);

    /* 2. The same square built again, the sequence registered: bound during
     * the build. */
    run_all(queue, io, px, CreateTask_WorldLoad(provider, builder, queue, chunks, 1, -1, -1, 104, NULL, NULL, NULL));
    look_at(world, scene, &built);

    CHECK(late.found && built.found, "the barrel (loc 37348) is not in the scene (late %d, built %d)", late.found,
        built.found);
    if( late.found && built.found )
    {
        CHECK(late.animated && built.animated, "not animated (late %d, built %d)", late.animated, built.animated);
        CHECK(late.vertex_count == built.vertex_count && late.face_count == built.face_count,
            "shape differs: %d/%d vertices, %d/%d faces", late.vertex_count, built.vertex_count, late.face_count,
            built.face_count);
        CHECK(late.vertex_sum == built.vertex_sum, "vertices differ (%ld late, %ld built)", late.vertex_sum,
            built.vertex_sum);
        CHECK(late.original_sum == built.original_sum, "bind pose differs (%ld late, %ld built)", late.original_sum,
            built.original_sum);
        if( late.face_count == built.face_count )
            for( int i = 0; i < late.face_count * 3; i++ )
                differ += late.colors[i] != built.colors[i];
        CHECK(differ == 0, "%d of %d lit corner colours differ between the late bind and the build's bind", differ,
            late.face_count * 3);
        printf("   barrel: %d faces; late bind vs bound in the build: %d of %d lit corner colours differ\n",
            late.face_count, differ, late.face_count * 3);
    }
    CHECK(before.found, "the barrel is not in the scene before its sequence binds");
    if( before.found && built.found )
    {
        int before_differ = 0;

        CHECK(!before.animated, "animated before its sequence was loaded");
        CHECK(before.vertex_count == built.vertex_count && before.face_count == built.face_count,
            "shape differs before the bind: %d/%d vertices, %d/%d faces", before.vertex_count, built.vertex_count,
            before.face_count, built.face_count);
        CHECK(before.vertex_sum == built.vertex_sum,
            "before its sequence binds the barrel is drawn at another pose than its rest pose (%ld, rest %ld): "
            "its orient, resize and offset are not applied",
            before.vertex_sum, built.vertex_sum);
        if( before.face_count == built.face_count )
            for( int i = 0; i < before.face_count * 3; i++ )
                before_differ += before.colors[i] != built.colors[i];
        CHECK(before_differ == 0, "%d of %d lit corner colours differ between the unbound barrel and the bound one",
            before_differ, before.face_count * 3);
        printf("   barrel before its sequence binds: vertices %s the rest pose, %d of %d lit corner colours differ\n",
            before.vertex_sum == built.vertex_sum ? "at" : "NOT at", before_differ, before.face_count * 3);
    }
    free(before.colors);
    free(late.colors);
    free(built.colors);

    WorldBuilder_Free(builder);
    World_Free(world);
    VarPManager_Free(&varp);
    PlatformX_IO_Free(px);
    RSCache_Dat2DiskFree(disk);
    Assets_Close(assets);
    ToriRS_TaskQueue_Free(queue);
    ToriRS_IOBatch_Free(io);
    Assets_Free(assets);
    dat2_buildcache_free(bc);
    if( g_failures )
    {
        printf("loc_seq_late_bind_test: %d failure(s)\n", g_failures);
        return 1;
    }
    printf("loc_seq_late_bind_test: OK\n");
    return 0;
}
