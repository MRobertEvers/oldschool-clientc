#ifndef WORLD_BUILDER_H
#define WORLD_BUILDER_H

#include "world/world.h"
#include "contour_ground_queue.h"

#include <stdbool.h>

struct CacheProvider;
struct ToriDraw_Scene;
struct VarPManager;
struct Blendmap;
struct Overlaymap;
struct TerrainShapeMap;
struct DecorBuildMap;
struct Lightmap;
struct SharelightMap;
struct Shademap2;
struct FlagMap;

struct WorldBuilder
{
    struct World* world;
    struct CacheProvider* cache;
    struct ToriDraw_Scene* scene;
    struct VarPManager* varp;

    struct Blendmap* blendmap;
    struct Overlaymap* overlaymap;
    struct TerrainShapeMap* terrain_shapemap;
    struct DecorBuildMap* decor_buildmap;
    struct Lightmap* lightmap;
    struct SharelightMap* sharelight_map;
    struct Shademap2* shademap;
    struct FlagMap* flag_map;
    struct ContourGroundQueue contour_ground_queue;

    /** Set during scenery chunk rebuild for scenery_load_model diagnostics. */
    int scenery_mapx;
    int scenery_mapz;
    int scenery_base_loc_id;
};

struct WorldBuilder*
WorldBuilder_New(
    struct World* world,
    struct CacheProvider* cache,
    struct ToriDraw_Scene* scene,
    struct VarPManager* varp);

void
WorldBuilder_Free(struct WorldBuilder* builder);

void
WorldBuilder_RebuildCenterzone(
    struct WorldBuilder* builder,
    int zone_center_x,
    int zone_center_z,
    int scene_size);

void
WorldBuilder_RebuildCenterzoneBegin(
    struct WorldBuilder* builder,
    int zone_center_x,
    int zone_center_z,
    int scene_size);

void
WorldBuilder_RebuildCenterzoneChunkTerrain(
    struct WorldBuilder* builder,
    int mapx,
    int mapz);

void
WorldBuilder_RebuildCenterzoneChunkScenery(
    struct WorldBuilder* builder,
    int mapx,
    int mapz);

void
WorldBuilder_RebuildCenterzoneChunk(
    struct WorldBuilder* builder,
    int mapx,
    int mapz);

void
WorldBuilder_RebuildCenterzoneEnd(struct WorldBuilder* builder);

void
WorldBuilder_RebuildChunklist(
    struct WorldBuilder* builder,
    const int* chunks_xz,
    int count);

void
WorldBuilder_RebuildChunklistBegin(
    struct WorldBuilder* builder,
    const int* chunks_xz,
    int count);

#endif
