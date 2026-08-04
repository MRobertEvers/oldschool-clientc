#include "test_harness.h"

#include "engine/cache_provider.h"
#include "engine/dat2/dat2_buildcache.h"
#include "engine/world_builder/minimap.h"
#include "engine/world_builder/world_builder.h"
#include "painters/painters.h"
#include "varp/varp_manager.h"
#include "world/world.h"

#include <stdio.h>
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
    painter_add_normal_scenery(painter, 16, 16, 0, 1, 1, 1, 0);
    painter_add_normal_scenery(painter, 15, 16, 0, 2, 1, 1, 0);
    painter_add_normal_scenery(painter, 17, 15, 0, 3, 2, 1, 0);
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

/* Index of the first PNTR_CMD_ELEMENT command carrying `entity`, or -1. */
static int
command_index_of_entity(
    const struct PaintersBuffer* buffer,
    int entity)
{
    for( int i = 0; i < buffer->command_count; i++ )
    {
        const struct PaintersElementCommand* cmd = &buffer->commands[i];
        if( cmd->_bf_kind == PNTR_CMD_ELEMENT && (int)cmd->_entity._bf_entity == entity )
            return i;
    }
    return -1;
}

/*
 * Locs sharing a tile must draw in the order they were added -- the reference client walks the
 * tile's loc array 0..count-1, and the map file's loc order is what decides which loc sits on top.
 * Regression guard for the prepend-linked-list scenery chain, which reversed this.
 */
void
test_painters_tile_order(void)
{
    struct Painter* painter = painter_new(32, 32, 4, PAINTER_NEW_CTX_BUCKET);
    TEST_ASSERT(painter != NULL, "painter_new (tile order)");
    if( !painter )
        return;

    /* Three 1x1 statics on the same tile, added in ascending entity order. */
    painter_add_normal_scenery(painter, 16, 16, 0, 101, 1, 1, 0);
    painter_add_normal_scenery(painter, 16, 16, 0, 102, 1, 1, 0);
    painter_add_normal_scenery(painter, 16, 16, 0, 103, 1, 1, 0);

    painter_mark_static_count(painter);

    painter_set_level_mask(painter, 0xF);
    painter_set_camera_angles(painter, 128, 0);

    struct PaintersBuffer* buffer = painter_buffer_new();
    TEST_ASSERT(buffer != NULL, "painter_buffer_new (tile order)");
    if( !buffer )
    {
        painter_free(painter);
        return;
    }

    painter_paint_bucket(painter, buffer, 16, 16, 0);

    int i101 = command_index_of_entity(buffer, 101);
    int i102 = command_index_of_entity(buffer, 102);
    int i103 = command_index_of_entity(buffer, 103);

    TEST_ASSERT(i101 >= 0 && i102 >= 0 && i103 >= 0, "all same-tile statics emitted");
    TEST_ASSERT(i101 < i102 && i102 < i103, "same-tile statics draw in add order");

    /* Dynamics (players/npcs) re-register every cycle after painter_reset_to_static; they must
     * land after the tile's statics, not in front of them. */
    painter_reset_to_static(painter);
    painter_add_normal_scenery(painter, 16, 16, 0, 900, 1, 1, 0);
    painter_paint_bucket(painter, buffer, 16, 16, 0);

    int i900 = command_index_of_entity(buffer, 900);
    i103 = command_index_of_entity(buffer, 103);
    TEST_ASSERT(i900 >= 0 && i103 >= 0, "dynamic and static both emitted");
    TEST_ASSERT(i103 < i900, "dynamic scenery draws after the tile's statics");

    free(buffer->commands);
    free(buffer);
    painter_free(painter);
}

/* Bridge minimap fix: a LinkBelow column's baked minimap tiles are shifted down
 * a plane (World.pushDown mirror) so the deck at cache level 1 lands on paint
 * level 0. Assert the exact plane remap the painter uses: 0←1, 1←2, 2←3, 3←old0. */
void
test_minimap_push_down(void)
{
    struct Minimap* mm = minimap_new(4, 4);
    TEST_ASSERT(mm != NULL, "minimap_new (push down)");
    if( !mm )
        return;

    const int sx = 1, sz = 2;
    /* Tag each level with a distinct background colour so we can track it. */
    const uint32_t c0 = 0xFF000010u, c1 = 0xFF000011u, c2 = 0xFF000012u, c3 = 0xFF000013u;
    minimap_set_tile_color(mm, sx, sz, 0, c0, MINIMAP_BACKGROUND);
    minimap_set_tile_color(mm, sx, sz, 1, c1, MINIMAP_BACKGROUND);
    minimap_set_tile_color(mm, sx, sz, 2, c2, MINIMAP_BACKGROUND);
    minimap_set_tile_color(mm, sx, sz, 3, c3, MINIMAP_BACKGROUND);
    /* A neighbouring column must be untouched by the shift. */
    minimap_set_tile_color(mm, sx + 1, sz, 0, 0xFF00FF00u, MINIMAP_BACKGROUND);

    minimap_push_down_tiles(mm, sx, sz);

    TEST_ASSERT(
        (uint32_t)minimap_tile_rgb(mm, sx, sz, 0, MINIMAP_BACKGROUND) == c1,
        "push down: level 0 takes former deck (cache level 1)");
    TEST_ASSERT(
        (uint32_t)minimap_tile_rgb(mm, sx, sz, 1, MINIMAP_BACKGROUND) == c2,
        "push down: level 1 takes former cache level 2");
    TEST_ASSERT(
        (uint32_t)minimap_tile_rgb(mm, sx, sz, 2, MINIMAP_BACKGROUND) == c3,
        "push down: level 2 takes former cache level 3");
    TEST_ASSERT(
        (uint32_t)minimap_tile_rgb(mm, sx, sz, 3, MINIMAP_BACKGROUND) == c0,
        "push down: former underpass (cache level 0) wraps to top plane");
    TEST_ASSERT(
        (uint32_t)minimap_tile_rgb(mm, sx + 1, sz, 0, MINIMAP_BACKGROUND) == 0xFF00FF00u,
        "push down: neighbouring column untouched");

    minimap_free(mm);
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

/*
 * world_builder_prerotate_placement: applying a loc's resize/offset BEFORE n
 * quarter turns must equal applying them AFTER.
 *
 * This exists because nothing else can catch a sign error here — the port only
 * defers a rotation for animated locs, and no loc in the shipped caches has an
 * animation together with a non-uniform resize or an x/z offset, so the
 * compensation is a no-op on every asset available to test against. The
 * equivalence is asserted directly instead: rotate a probe point by hand with
 * the same rule ToriDraw_ModelOrient uses (x' = z, z' = -x) and check that both
 * orderings land on the same coordinate.
 */
static void
prerotate_reference(
    int quarter_turns,
    int* x,
    int* z)
{
    for( int i = 0; i < (quarter_turns & 3); i++ )
    {
        int const tmp = *x;
        *x = *z;
        *z = -tmp;
    }
}

void
test_prerotate_placement(void)
{
    /* Deliberately asymmetric so a swapped axis or a flipped sign cannot pass. */
    int const probe_x = 17;
    int const probe_z = -40;
    int const resize_x = 200;
    int const resize_z = 50;
    int const offset_x = 11;
    int const offset_z = -29;

    for( int angle = 0; angle < 4; angle++ )
    {
        /* Reference order: rotate the vertex, THEN resize and translate. */
        int ref_x = probe_x;
        int ref_z = probe_z;
        prerotate_reference(angle, &ref_x, &ref_z);
        ref_x = ref_x * resize_x / 128 + offset_x;
        ref_z = ref_z * resize_z / 128 + offset_z;

        /* Port order: resize and translate with pre-rotated values, THEN let
         * the deferred draw-time yaw rotate the result. */
        int pre_rx = resize_x;
        int pre_rz = resize_z;
        int pre_ox = offset_x;
        int pre_oz = offset_z;
        world_builder_prerotate_placement(angle, &pre_rx, &pre_rz, &pre_ox, &pre_oz);

        int got_x = probe_x * pre_rx / 128 + pre_ox;
        int got_z = probe_z * pre_rz / 128 + pre_oz;
        prerotate_reference(angle, &got_x, &got_z);

        TEST_ASSERT(got_x == ref_x, "prerotate x matches rotate-then-place");
        TEST_ASSERT(got_z == ref_z, "prerotate z matches rotate-then-place");
        if( got_x != ref_x || got_z != ref_z )
            printf(
                "  angle %d: got (%d,%d) want (%d,%d)\n", angle, got_x, got_z, ref_x, ref_z);
    }
}
