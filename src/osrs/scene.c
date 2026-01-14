#include "scene.h"

#include "tables/maps.h"

#include <stdlib.h>
#include <string.h>

struct Scene*
scene_new()
{
    struct Scene* scene = (struct Scene*)malloc(sizeof(struct Scene));
    memset(scene, 0, sizeof(struct Scene));
    return scene;
}
void
scene_free(struct Scene* scene)
{
    free(scene->terrain);
    free(scene);
}

struct SceneTerrain*
scene_terrain_new_sized(
    int tile_width_x,
    int tile_width_z)
{
    struct SceneTerrain* terrain = (struct SceneTerrain*)malloc(sizeof(struct SceneTerrain));
    memset(terrain, 0, sizeof(struct SceneTerrain));

    int count = tile_width_x * tile_width_z * MAP_TERRAIN_LEVELS;

    terrain->tiles = malloc(count * sizeof(struct SceneTerrainTile));
    memset(terrain->tiles, 0, count * sizeof(struct SceneTerrainTile));

    terrain->tile_width_x = tile_width_x;
    terrain->tile_width_z = tile_width_z;

    terrain->tiles_length = count;
    terrain->tiles_capacity = count;

    return terrain;
}

void
scene_terrain_free(struct SceneTerrain* terrain)
{
    free(terrain->tiles);
    free(terrain);
}

struct SceneTerrainTile*
scene_terrain_tile_at(
    struct SceneTerrain* terrain,
    int sx,
    int sz,
    int slevel)
{
    int index =
        sx + sz * terrain->tile_width_x + slevel * terrain->tile_width_x * terrain->tile_width_z;
    return &terrain->tiles[index];
}