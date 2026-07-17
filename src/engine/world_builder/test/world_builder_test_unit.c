#include "test_harness.h"

#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/world_builder/world_builder.h"
#include "painters/painters.h"
#include "varp/varp_manager.h"
#include "world/world.h"

#include <stdlib.h>

#include <toridraw.h>
#include <toridraw_scene.h>

void
test_painters_smoke(void)
{
    struct Painter* painter = painter_new(32, 32, 4, PAINTER_NEW_CTX_BUCKET);
    TEST_ASSERT(painter != NULL, "painter_new");
    if( !painter )
        return;

    /* A couple of scenery elements and a wall around the center of the grid. */
    painter_add_normal_scenery(painter, 16, 16, 0, 1, 1, 1);
    painter_add_normal_scenery(painter, 15, 16, 0, 2, 1, 1);
    painter_add_normal_scenery(painter, 17, 15, 0, 3, 2, 1);
    painter_add_wall(painter, 16, 15, 0, 4, WALL_A, WALL_SIDE_NORTH);

    painter_mark_static_count(painter);

    painter_set_level_mask(painter, 0xF);
    painter_set_camera_angles(painter, 128, 0);

    struct PaintersBuffer* buffer = painter_buffer_new();
    TEST_ASSERT(buffer != NULL, "painter_buffer_new");

    painter_paint_bucket(painter, buffer, 16, 16, 0);
    TEST_ASSERT(buffer->command_count > 0, "painter_paint_bucket emitted commands");

    free(buffer->commands);
    free(buffer);
    painter_free(painter);
}

void
test_builder_lifecycle(void)
{
    ToriDraw_Init();

    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0);
    TEST_ASSERT(scene != NULL, "ToriDraw_SceneNew");

    struct World* world = World_New();
    TEST_ASSERT(world != NULL, "World_New");

    struct VarPManager varp;
    VarPManager_Init(&varp);

    /* Empty in-memory provider (no disk backing). */
    struct Dat2BuildCache* bc = dat2_buildcache_new();
    struct CacheProvider* provider = dat2_buildcache_as_provider(bc);
    TEST_ASSERT(provider != NULL, "dat2_buildcache_as_provider");

    struct WorldBuilder* builder = WorldBuilder_New(world, provider, scene, &varp);
    TEST_ASSERT(builder != NULL, "WorldBuilder_New");

    WorldBuilder_RebuildCenterzoneBegin(builder, 50, 50, 104);
    TEST_ASSERT(world->painter != NULL, "RebuildCenterzoneBegin allocated painter");
    TEST_ASSERT(world->heightmap != NULL, "RebuildCenterzoneBegin allocated heightmap");
    TEST_ASSERT(world->_scene_size == 104, "scene size set to 104");

    /* No maps loaded: End builds an (empty) terrain pass without touching ChunkTerrain,
     * which would assert on missing map data. */
    WorldBuilder_RebuildCenterzoneEnd(builder);

    WorldBuilder_Free(builder);
    World_Free(world);
    /* ToriDraw_SceneFree pulls in ToriDraw_FontFree, which is not part of the current
     * toridraw build; the scene is reclaimed at process exit. */
    VarPManager_Free(&varp);
    dat2_buildcache_free(bc);
}
