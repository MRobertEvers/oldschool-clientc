#ifndef SCENEBUILDER_U_C
#define SCENEBUILDER_U_C

#include "configmap.h"
#include "graphics/dash.h"
#include "osrs/painters.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables/frame.h"
#include "osrs/rscache/tables/framemap.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"

// clang-format off
#include "scenebuilder_buildgrid.u.c"
#include "scenebuilder_shademap.u.c"
// clang-format on

struct FlotypeEntry
{
    int id;
    struct CacheConfigOverlay* flotype;
};

struct TextureEntry
{
    int id;
    struct DashTexture* texture;
};

struct FrameAnimEntry
{
    // frame_file
    int id;
    struct CacheFrame* frame;
};

struct AnimframeEntry
{
    int id;
    struct CacheAnimframe* animframe;
};

struct DatSequenceEntry
{
    int id;
    struct CacheDatSequence* dat_sequence;
};

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

struct FrameEntry
{
    int id;
    struct CacheFrame* frame;
};

struct FramemapEntry
{
    int id;
    struct CacheFramemap* framemap;
};

struct CacheConfigLocationEntry
{
    int id;
    struct CacheConfigLocation* config_loc;
};

struct SceneBuilder
{
    struct Painter* painter;
    struct Minimap* minimap;
    int mapx_sw;
    int mapz_sw;
    int mapx_ne;
    int mapz_ne;

    struct DashMap* models_hmap;
    struct DashMap* map_terrains_hmap;
    struct DashMap* map_locs_hmap;
    struct DashMap* frames_hmap;
    struct DashMap* framemaps_hmap;
    struct DashMap* sequences_configmap;
    struct DashMap* config_locs_hmap;
    struct DashMap* config_locs_configmap;
    struct DashMap* config_underlay_configmap;
    struct DashMap* config_overlay_configmap;

    // TODO:
    struct DashMap* flotypes_hmap;
    struct DashMap* textures_hmap;

    struct BuildGrid* build_grid;
    struct Shademap* shademap;
};

#endif