#include "scene.h"

#include "rscache/tables/maps.h"

#include <stdlib.h>
#include <string.h>

struct SceneAnimation*
scene_animation_new(int frame_count_hint)
{
    struct SceneAnimation* animation =
        (struct SceneAnimation*)malloc(sizeof(struct SceneAnimation));
    memset(animation, 0, sizeof(struct SceneAnimation));

    animation->dash_frames = malloc(frame_count_hint * sizeof(struct DashFrame));
    memset(animation->dash_frames, 0, frame_count_hint * sizeof(struct DashFrame));

    animation->frame_capacity = frame_count_hint;

    animation->dash_framemap = NULL;
    animation->frame_count = 0;

    return animation;
}

struct SceneAnimation*
scene_animation_push_frame(
    struct SceneAnimation* animation,
    struct DashFrame* dash_frame,
    struct DashFramemap* dash_framemap)
{
    if( animation->frame_count >= animation->frame_capacity )
    {
        if( animation->frame_capacity == 0 )
            animation->frame_capacity = 8;
        animation->frame_capacity *= 2;
        animation->dash_frames =
            realloc(animation->dash_frames, animation->frame_capacity * sizeof(struct DashFrame*));
    }
    animation->dash_frames[animation->frame_count] = dash_frame;
    animation->frame_count++;

    animation->dash_framemap = dash_framemap;

    return animation;
}

void
scene_animation_free(struct SceneAnimation* animation)
{
    free(animation->dash_frames);
    free(animation);
}

static struct SceneScenery*
scene_scenery_new(
    int tile_width_x,
    int tile_width_z,
    int element_count_hint)
{
    struct SceneScenery* scenery = (struct SceneScenery*)malloc(sizeof(struct SceneScenery));
    memset(scenery, 0, sizeof(struct SceneScenery));
    scenery->elements = malloc(element_count_hint * sizeof(struct SceneElement));
    scenery->elements_capacity = element_count_hint;
    memset(scenery->elements, 0, element_count_hint * sizeof(struct SceneElement));

    scenery->tile_width_x = tile_width_x;
    scenery->tile_width_z = tile_width_z;
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

    scene->tile_width_x = tile_width_x;
    scene->tile_width_z = tile_width_z;

    scene->scenery = scene_scenery_new(tile_width_x, tile_width_z, element_count_hint);
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

static bool
inbounds(
    struct SceneTerrain* terrain,
    int sx,
    int sz,
    int slevel)
{
    return sx >= 0 && sx < terrain->tile_width_x && sz >= 0 && sz < terrain->tile_width_z &&
           slevel >= 0 && slevel < MAP_TERRAIN_LEVELS;
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
    assert(sx >= 0 && sx < terrain->tile_width_x);
    assert(sz >= 0 && sz < terrain->tile_width_z);
    assert(slevel >= 0 && slevel < MAP_TERRAIN_LEVELS);
    assert(index >= 0 && index < terrain->tiles_capacity);
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
    memcpy(&scenery->elements[scenery->elements_length], element, sizeof(struct SceneElement));
    scenery->elements[scenery->elements_length].id = scenery->elements_length;
    scenery->elements_length++;

    return scenery->elements_length - 1;
}

struct SceneElement*
scene_element_at(
    struct SceneScenery* scenery,
    int element_idx)
{
    assert(element_idx >= 0 && element_idx < scenery->elements_length);
    return &scenery->elements[element_idx];
}

int
scene_push_element_move(
    struct Scene* scene,
    struct SceneElement* element)
{
    return scene_scenery_push_element_move(scene->scenery, element);
}

bool
scene_element_interactable(
    struct Scene* scene,
    int element)
{
    assert(element >= 0 && element < scene->scenery->elements_length);
    return scene->scenery->elements[element].interactable;
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

char*
scene_element_name(
    struct Scene* scene,
    int element)
{
    assert(element >= 0 && element < scene->scenery->elements_length);
    return scene->scenery->elements[element]._dbg_name;
}

int
scene_terrain_height_center(
    struct Scene* scene,
    int sx,
    int sz,
    int slevel)
{
    struct SceneTerrain* terrain = scene->terrain;
    struct SceneTerrainTile* tile = scene_terrain_tile_at(terrain, sx, sz, slevel);
    int height_sw = tile->height;

    int height_se = 0;
    int height_ne = 0;
    int height_nw = 0;

    if( inbounds(terrain, sx + 1, sz, slevel) )
    {
        height_se = scene_terrain_tile_at(terrain, sx + 1, sz, slevel)->height;
    }
    if( inbounds(terrain, sx, sz + 1, slevel) )
    {
        height_ne = scene_terrain_tile_at(terrain, sx, sz + 1, slevel)->height;
    }

    if( inbounds(terrain, sx - 1, sz, slevel) )
    {
        height_nw = scene_terrain_tile_at(terrain, sx - 1, sz, slevel)->height;
    }
    if( inbounds(terrain, sx, sz - 1, slevel) )
    {
        height_nw = scene_terrain_tile_at(terrain, sx, sz - 1, slevel)->height;
    }

    return (height_sw + height_se + height_ne + height_nw) >> 2;
}