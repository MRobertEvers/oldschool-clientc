#ifndef WORLD_BUILDER_H
#define WORLD_BUILDER_H

#include "world.h"

#include <stdbool.h>

struct GameCache;
struct ToriDraw_Scene;
struct ToriDrawX;
struct Blendmap;
struct Overlaymap;
struct TerrainShapeMap;
struct DecorBuildMap;
struct Lightmap;
struct SharelightMap;
struct Shademap2;
struct FlagMap;

struct ContourGroundQueueEntry
{
    int element_id;
    int loc_id;
    int shape_select;
    int rotation;
    int size_x;
    int size_z;
    int level;
};

struct WorldBuilder
{
    struct World* world;
    struct GameCache* gamecache;
    struct ToriDraw_Scene* scene;
    struct ToriDrawX* toridrawx;

    struct Blendmap* blendmap;
    struct Overlaymap* overlaymap;
    struct TerrainShapeMap* terrain_shapemap;
    struct DecorBuildMap* decor_buildmap;
    struct Lightmap* lightmap;
    struct SharelightMap* sharelight_map;
    struct Shademap2* shademap;
    struct FlagMap* flag_map;
    struct ContourGroundQueueEntry* contour_ground_queue;
    int contour_ground_queue_count;
    int contour_ground_queue_cap;
};

struct WorldBuilder*
world_builder_new(
    struct World* world,
    struct GameCache* gamecache,
    struct ToriDraw_Scene* scene,
    struct ToriDrawX* toridrawx);

void
world_builder_free(struct WorldBuilder* builder);

void
world_builder_rebuild_centerzone(
    struct WorldBuilder* builder,
    int zone_center_x,
    int zone_center_z,
    int scene_size);

void
world_builder_rebuild_centerzone_begin(
    struct WorldBuilder* builder,
    int zone_center_x,
    int zone_center_z,
    int scene_size);

void
world_builder_rebuild_centerzone_chunk_terrain(
    struct WorldBuilder* builder,
    int mapx,
    int mapz);

void
world_builder_rebuild_centerzone_chunk_scenery(
    struct WorldBuilder* builder,
    int mapx,
    int mapz);

void
world_builder_rebuild_centerzone_chunk(
    struct WorldBuilder* builder,
    int mapx,
    int mapz);

void
world_builder_rebuild_centerzone_end(struct WorldBuilder* builder);

#endif
