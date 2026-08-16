#ifndef WORLD_BUILDER_H
#define WORLD_BUILDER_H

#include "world.h"
#include "contour_ground_queue.h"

#include <stdbool.h>

struct ToriAuxLibCore;
struct ToriDraw_Scene;
struct ToriAuxLibTD;
struct VarPVarBitManager;
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
    struct ToriAuxLibCore* core;
    struct ToriDraw_Scene* scene;
    struct ToriAuxLibTD* td;
    struct VarPVarBitManager* varp_varbit;

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
world_builder_new(
    struct World* world,
    struct ToriAuxLibCore* core,
    struct ToriDraw_Scene* scene,
    struct ToriAuxLibTD* td,
    struct VarPVarBitManager* varp_varbit);

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

void
world_builder_rebuild_chunklist(
    struct WorldBuilder* builder,
    const int* chunks_xz,
    int count);

void
world_builder_rebuild_chunklist_begin(
    struct WorldBuilder* builder,
    const int* chunks_xz,
    int count);

#endif
