#ifndef TERRAIN_H
#define TERRAIN_H

#include "configmap.h"
#include "datastruct/hmap.h"
#include "graphics/dash.h"
#include "tables/maps.h"

struct TerrainTileModel
{
    struct DashModel dash_model;

    int cx;
    int cz;
    int clevel;
};

struct Terrain
{
    struct TerrainTileModel* tiles;
    int tile_count;

    int side;
};

struct TileHeights
{
    int sw_height;
    int se_height;
    int ne_height;
    int nw_height;
    int height_center;
};

struct TileHeights
terrain_tile_heights_at(struct CacheMapTerrainIter* terrain, int sx, int sz, int level);

struct TerrainTileModel* //
terrain_tile_model_at(struct Terrain* terrain, int sx, int sz, int slevel);

struct CacheMapTerrain;
struct Terrain* terrain_new_from_map_terrain(
    struct CacheMapTerrainIter* terrain,
    int* shade_map_nullable,
    struct ConfigMap* config_overlay_map,
    struct ConfigMap* config_underlay_map);

void terrain_free(struct Terrain* terrain);

#endif