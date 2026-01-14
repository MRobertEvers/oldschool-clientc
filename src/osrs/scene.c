#include "scene.h"

#include "tables/maps.h"

#include <stdlib.h>
#include <string.h>

static struct SceneScenery*
scene_scenery_new(int element_count_hint)
{
    struct SceneScenery* scenery = (struct SceneScenery*)malloc(sizeof(struct SceneScenery));
    memset(scenery, 0, sizeof(struct SceneScenery));
    scenery->elements = malloc(element_count_hint * sizeof(struct SceneElement));
    scenery->elements_capacity = element_count_hint;
    return scenery;
}

static void
scene_scenery_free(struct SceneScenery* scenery)
{
    free(scenery->elements);
    free(scenery);
}

static struct SceneTerrain*
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

static void
scene_terrain_free(struct SceneTerrain* terrain)
{
    free(terrain->tiles);
    free(terrain);
}

struct Scene*
scene_new(
    int tile_width_x,
    int tile_width_z,
    int element_count_hint)
{
    struct Scene* scene = (struct Scene*)malloc(sizeof(struct Scene));
    memset(scene, 0, sizeof(struct Scene));

    scene->scenery = scene_scenery_new(element_count_hint);
    scene->terrain = scene_terrain_new_sized(tile_width_x, tile_width_z);

    return scene;
}
void
scene_free(struct Scene* scene)
{
    scene_scenery_free(scene->scenery);
    scene_terrain_free(scene->terrain);
    free(scene);
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

int
scene_scenery_push_element_move(
    struct SceneScenery* scenery,
    struct SceneElement* element)
{
    if( scenery->elements_length >= scenery->elements_capacity )
    {
        scenery->elements_capacity *= 2;
        scenery->elements =
            realloc(scenery->elements, scenery->elements_capacity * sizeof(struct SceneElement));
    }
    scenery->elements[scenery->elements_length] = *element;

    element->id = scenery->elements_length;
    scenery->elements_length++;

    memset(element, 0, sizeof(struct SceneElement));

    return scenery->elements_length - 1;
}

struct DashModel*
scene_element_model(
    struct Scene* scene,
    int element)
{
    assert(element >= 0 && element < scene->scenery->elements_length);
    return scene->scenery->elements[element].dash_model;
}

struct DashPosition*
scene_element_position(
    struct Scene* scene,
    int element)
{
    assert(element >= 0 && element < scene->scenery->elements_length);
    return scene->scenery->elements[element].dash_position;
}