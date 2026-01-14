#ifndef SCENEBUILDER_U_C
#define SCENEBUILDER_U_C

#include "configmap.h"

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

    struct HMap* models_hmap;
    struct HMap* map_terrains_hmap;
    struct HMap* map_locs_hmap;
    struct HMap* config_locs_hmap;
    struct ConfigMap* config_underlay_hmap;
    struct ConfigMap* config_overlay_hmap;
};

#endif