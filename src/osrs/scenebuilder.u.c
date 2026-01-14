#ifndef SCENEBUILDER_U_C
#define SCENEBUILDER_U_C

#include "configmap.h"
#include "graphics/dash.h"

struct ModelEntry
{
    int id;
    struct CacheModel* model;
};

struct MapLocsEntry
{
    int id;
    int mapx;
    int mapz;
    struct CacheMapLocs* locs;
};

struct MapTerrainEntry
{
    int id;
    int mapx;
    int mapz;
    struct CacheMapTerrain* map_terrain;
};

struct ConfigLocEntry
{
    int id;
    struct CacheConfigLocation* config_loc;
};

struct SceneBuilder
{
    int mapx_sw;
    int mapz_sw;
    int mapx_ne;
    int mapz_ne;

    struct DashMap* models_hmap;
    struct DashMap* map_terrains_hmap;
    struct DashMap* map_locs_hmap;
    struct DashMap* config_locs_hmap;
    struct DashMap* config_underlay_hmap;
    struct DashMap* config_overlay_hmap;
};

#endif