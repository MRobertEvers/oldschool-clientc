#include "scenebuilder.h"

#include "graphics/dash.h"

#include <assert.h>

// clang-format off
#include "terrain_grid.u.c"
#include "scenebuilder_terrain.u.c"
#include "scenebuilder_scenery.u.c"
#include "scenebuild_compat.u.c"
#include "scenebuilder.u.c"
// clang-format on

#define ENTRYS 4096

struct SceneBuilder*
scenebuilder_new_painter(
    struct Painter* painter,
    struct Minimap* minimap)
{
    struct DashMapConfig config;
    struct SceneBuilder* scene_builder = malloc(sizeof(struct SceneBuilder));
    memset(scene_builder, 0, sizeof(struct SceneBuilder));

    scene_builder->painter = painter;
    scene_builder->minimap = minimap;

    return scene_builder;
}

void
scenebuilder_free(struct SceneBuilder* scene_builder)
{
    build_grid_free(scene_builder->build_grid);
    shademap_free(scene_builder->shademap);

    free(scene_builder);
}

static void
init_terrain_grid(
    struct SceneBuilder* scene_builder,
    struct TerrainGrid* terrain_grid)
{
    int mapxz = 0;
    memset(terrain_grid, 0, sizeof(struct TerrainGrid));

    int mapx_sw = scene_builder->wx_sw / 64;
    int mapz_sw = scene_builder->wz_sw / 64;
    int mapx_ne = (scene_builder->wx_ne) / 64;
    int mapz_ne = (scene_builder->wz_ne) / 64;

    int offset_x = scene_builder->wx_sw % 64;
    int offset_z = scene_builder->wz_sw % 64;

    terrain_grid->mapx_ne = mapx_ne;
    terrain_grid->mapz_ne = mapz_ne;
    terrain_grid->mapx_sw = mapx_sw;
    terrain_grid->mapz_sw = mapz_sw;
    terrain_grid->offset_x = offset_x;
    terrain_grid->offset_z = offset_z;
    terrain_grid->size_x = scene_builder->size_x;
    terrain_grid->size_z = scene_builder->size_z;

    int map_index = 0;
    struct CacheMapTerrain* map_terrain = NULL;
    for( int mapz = mapz_sw; mapz <= mapz_ne; mapz++ )
    {
        for( int mapx = mapx_sw; mapx <= mapx_ne; mapx++ )
        {
            map_terrain = scenebuilder_compat_get_map_terrain(scene_builder, mapx, mapz);
            assert(map_terrain && "Map terrain must be found");

            terrain_grid->map_terrain[map_index] = map_terrain;
            map_index++;
        }
    }
}

static void
init_build_grid(
    struct TerrainGrid* terrain_grid,
    struct BuildGrid* build_grid)
{
    struct TileHeights tile_heights = { 0 };
    struct BuildTile* build_tile = NULL;

    build_grid->tile_width_x = terrain_grid_x_width(terrain_grid);
    build_grid->tile_width_z = terrain_grid_z_height(terrain_grid);

    for( int sx = 0; sx < build_grid->tile_width_x; sx++ )
    {
        for( int sz = 0; sz < build_grid->tile_width_z; sz++ )
        {
            for( int slevel = 0; slevel < MAP_TERRAIN_LEVELS; slevel++ )
            {
                tile_heights_at_sized(terrain_grid, sx, sz, slevel, 1, 1, &tile_heights);

                int index = build_grid_index(
                    build_grid->tile_width_x, build_grid->tile_width_z, sx, sz, slevel);
                init_build_tile(
                    &build_grid->tiles[index], sx, sz, slevel, tile_heights.height_center);
            }
        }
    }
}

/** Match world build: painter slevel from cache VisBelow / LinkBelow after bridge layout. */
static void
scenebuilder_apply_vis_below_draw_levels(
    struct Painter* painter,
    struct TerrainGrid* tg,
    int tile_width_x,
    int tile_width_z)
{
    for( int x = 0; x < tile_width_x; x++ )
    {
        for( int z = 0; z < tile_width_z; z++ )
        {
            struct CacheMapFloor* b = tile_from_sw_origin(tg, x, z, 1);
            int link_l1 = (b->settings & FLOFLAG_LINK_BELOW) != 0;
            for( int g = 0; g < painter_max_levels(painter); g++ )
            {
                int src = link_l1 ? (g < 3 ? g + 1 : 0) : g;
                struct CacheMapFloor* srcf = tile_from_sw_origin(tg, x, z, src);
                int draw = map_floor_vis_below_draw_level(srcf->settings, src, link_l1);
                painter_tile_set_draw_level(painter, x, z, g, draw);
            }
        }
    }
}

static struct Scene*
scenebuiler_build(
    struct SceneBuilder* scene_builder,
    int wx_sw,
    int wz_sw,
    int wx_ne,
    int wz_ne,
    int size_x,
    int size_z)
{
    scene_builder->wx_sw = wx_sw;
    scene_builder->wz_sw = wz_sw;
    scene_builder->wx_ne = wx_ne;
    scene_builder->wz_ne = wz_ne;
    scene_builder->size_x = size_x;
    scene_builder->size_z = size_z;

    int height = size_z; // ((mapz_ne - mapz_sw + 1) * MAP_TERRAIN_Z) - base_tile_x;
    int width = size_x;  // (mapx_ne - mapx_sw + 1) * MAP_TERRAIN_X - base_tile_z;

    scene_builder->build_grid = build_grid_new(width, height);
    scene_builder->shademap = shademap_new(width, height, MAP_TERRAIN_LEVELS);

    struct TerrainGrid terrain_grid;

    struct Scene* scene = NULL;

    init_terrain_grid(scene_builder, &terrain_grid);

    init_build_grid(&terrain_grid, scene_builder->build_grid);

    scene = scene_new(width, height, 1024);

    build_scene_scenery(scene_builder, &terrain_grid, scene);
    build_scene_terrain(scene_builder, &terrain_grid, scene->terrain);

    // Update bridge terrain heights.

    // Adjust bridges.
    /**
     * Bridges are adjusted from an upper level.
     *
     * The "bridge_tile" is actually the tiles below the bridge.
     * The bridge itself is taken from the level above.
     *
     * E.g.
     *
     * Buffer Level 0: Tile := (Water and Bridge Walls), Bridge := Nothing
     * Buffer Level 1: Tile := (Bridge Walking Surface and Walls)
     * Buffer Level 2: Nothing
     * Buffer Level 3: Nothing
     *
     * After this adjustment,
     *
     * Buffer Level 0: Tile := (Previous Level 1),
     * Buffer Level 1: Nothing
     * Buffer Level 2: Nothing
     * Buffer Level 3: Tile := (Previous Level 0)
     */
    struct CacheMapFloor* ground = NULL;
    struct CacheMapFloor* bridge = NULL;
    struct PaintersTile bridge_tile_tmp = { 0 };

    for( int x = 0; x < scene->tile_width_x; x++ )
    {
        for( int z = 0; z < scene->tile_width_z; z++ )
        {
            /**
             * OS1:
             * 	for (int var76 = 0; var76 < 104; var76++) {
             *	for (int var77 = 0; var77 < 104; var77++) {
             *		if ((mapl[1][var76][var77] & 0x2) == 2) {
             *			arg0.pushDown(var76, var77);
             *		}
             *	}
             * }
             * Dane's 317
             *   for (int x = 0; x < maxTileX; x++) {
             *     for (int z = 0; z < maxTileZ; z++) {
             *         if ((levelTileFlags[1][x][z] & 0x2) == 2) {
             *             scene.setBridge(x, z);
             *         }
             *     }
             * }
             */
            ground = tile_from_sw_origin(&terrain_grid, x, z, 0);
            bridge = tile_from_sw_origin(&terrain_grid, x, z, 1);

            if( (bridge->settings & FLOFLAG_LINK_BELOW) != 0 )
            {
                bridge_tile_tmp = *painter_tile_at(scene_builder->painter, x, z, 0);

                /**
                 * Shift tile definitions down
                 */
                for( int level = 0; level < painter_max_levels(scene_builder->painter) - 1;
                     level++ )
                {
                    painter_tile_copyto(
                        scene_builder->painter,
                        // From
                        x,
                        z,
                        level + 1,
                        // To
                        x,
                        z,
                        level);
                }

                // Use the newly unused tile on level 3 as the bridge slot.
                *painter_tile_at(scene_builder->painter, x, z, 3) = bridge_tile_tmp;
                painters_tile_set_grid_level(painter_tile_at(scene_builder->painter, x, z, 3), 3);
                painter_tile_set_bridge(scene_builder->painter, x, z, 0, x, z, 3);

                // Update the collisionmap

                for( int i = 0;
                     i < sizeof(scene->collision_maps) / sizeof(scene->collision_maps[0]) - 1;
                     i++ )
                {
                    struct CollisionMap* cm_below = scene->collision_maps[i];
                    struct CollisionMap* cm_above = scene->collision_maps[i + 1];

                    cm_below->flags[collision_map_index(x, z)] =
                        cm_above->flags[collision_map_index(x, z)];
                }

                // Update terrain grid heights.
                for( int i = 0; i < 3; i++ )
                {
                    struct SceneTerrainTile* tile_below =
                        scene_terrain_tile_at(scene->terrain, x, z, i);
                    struct SceneTerrainTile* tile_above =
                        scene_terrain_tile_at(scene->terrain, x, z, i + 1);

                    tile_below->height = tile_above->height;
                    tile_below->slevel = i + 1;
                }
            }
        }
    }

    scenebuilder_apply_vis_below_draw_levels(
        scene_builder->painter,
        &terrain_grid,
        scene->tile_width_x,
        scene->tile_width_z);

    painter_mark_static_count(scene_builder->painter);

    return scene;
}

struct Scene*
scenebuilder_load_from_buildcachedat(
    struct SceneBuilder* scene_builder,
    int wx_sw,
    int wz_sw,
    int wx_ne,
    int wz_ne,
    int size_x,
    int size_z,
    struct BuildCacheDat* buildcachedat,
    struct Scene2* texture_scene2)
{
    scene_builder->buildcachedat = buildcachedat;
    scene_builder->texture_scene2 = texture_scene2;
    return scenebuiler_build(scene_builder, wx_sw, wz_sw, wx_ne, wz_ne, size_x, size_z);
}

struct Scene*
scenebuilder_load_from_buildcache(
    struct SceneBuilder* scene_builder,
    int wx_sw,
    int wz_sw,
    int wx_ne,
    int wz_ne,
    int size_x,
    int size_z,
    struct BuildCache* buildcache)
{
    scene_builder->buildcache = buildcache;
    return scenebuiler_build(scene_builder, wx_sw, wz_sw, wx_ne, wz_ne, size_x, size_z);
}

// struct SceneAnimation*
// scenebuilder_new_animation(
//     struct SceneBuilder* scene_builder,
//     int sequence_id)
// {
//     struct DatSequenceEntry* sequence_entry = NULL;
//     sequence_entry = (struct DatSequenceEntry*)dashmap_search(
//         scene_builder->sequences_configmap, &sequence_id, DASHMAP_FIND);
//     assert(sequence_entry && "Sequence must be found in hmap");

//     struct SceneAnimation* animation = NULL;
//     animation = load_model_animations_dati(
//         sequence_id, scene_builder->sequences_configmap, scene_builder->frames_hmap);

//     return animation;
// }

static void
dash_position_from_offset_wxh_elem(
    struct SceneElement* element,
    int sx,
    int sz,
    int height_center,
    int size_x,
    int size_z)
{
    element->dash_position->x = sx * TILE_SIZE + size_x * 64;
    element->dash_position->z = sz * TILE_SIZE + size_z * 64;
    element->dash_position->y = height_center;
}

static struct DashPosition*
dash_position_new_from_offset_1x1_elem(
    int sx,
    int sz,
    int height_center)
{
    struct DashPosition* dash_position = malloc(sizeof(struct DashPosition));
    memset(dash_position, 0, sizeof(struct DashPosition));

    dash_position_from_offset_wxh_elem(dash_position, sx, sz, height_center, 1, 1);

    return dash_position;
}

void
scenebuilder_push_dynamic_element(
    struct SceneBuilder* scene_builder,
    struct Scene* scene,
    int sx,
    int sz,
    int slevel,
    int size_x,
    int size_z,
    struct SceneElement* element)
{
    struct SceneElement scene_element = { 0 };
    assert(sx >= 0 && sx < scene->tile_width_x);
    assert(sz >= 0 && sz < scene->tile_width_z);
    assert(slevel >= 0 && slevel < MAP_TERRAIN_LEVELS);
    assert(element != NULL);

    memcpy(&scene_element, element, sizeof(struct SceneElement));

    int element_id = scene_scenery_push_dynamic_element_move(scene->scenery, &scene_element);

    painter_add_normal_scenery(scene_builder->painter, sx, sz, slevel, element_id, 1, 1);
}

void
scenebuilder_reset_dynamic_elements(
    struct SceneBuilder* scene_builder,
    struct Scene* scene)
{
    painter_reset_to_static(scene_builder->painter);
    scene_scenery_reset_dynamic_elements(scene->scenery);
}