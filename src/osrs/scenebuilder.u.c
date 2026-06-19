#ifndef SCENEBUILDER_U_C
#define SCENEBUILDER_U_C

#include "configmap.h"
#include "graphics/dash.h"
#include "osrs/buildcache.h"
#include "osrs/painters.h"
#include "osrs/rscache/dat2a/rscache_dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/rscache_dat2a_frame.h"
#include "osrs/rscache/dat2a/rscache_dat2a_framemap.h"
#include "osrs/rscache/dat2a/rscache_dat2a_maps.h"
#include "osrs/rscache/dat2a/rscache_dat2a_model.h"
#include "osrs/scene2.h"

// clang-format off
#include "scenebuilder_buildgrid.u.c"
#include "scenebuilder_shademap.u.c"
// clang-format on

struct FlotypeEntry
{
    int id;
    struct RSCacheDat2A_ConfigOverlay* flotype;
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
    struct RSCacheDat2A_Frame* frame;
};

struct AnimframeEntry
{
    int id;
    struct RSCacheDat1A_AnimFrame* animframe;
};

struct DatSequenceEntry
{
    int id;
    struct RSCacheDat1A_ConfigSequence* dat_sequence;
};

struct ModelEntry
{
    int id;
    struct RSCacheDat2A_Model* model;
};

struct MapLocsEntry
{
    int id;
    int mapx;
    int mapz;
    struct RSCacheDat2A_MapLocs* locs;
};

struct MapTerrainEntry
{
    int id;
    int mapx;
    int mapz;
    struct RSCacheDat2A_MapTerrain* map_terrain;
};

struct FrameEntry
{
    int id;
    struct RSCacheDat2A_Frame* frame;
};

struct FramemapEntry
{
    int id;
    struct RSCacheDat2A_Framemap* framemap;
};

struct RSCacheDat2A_ConfigLocationEntry
{
    int id;
    struct RSCacheDat2A_ConfigLocation* config_loc;
};

struct SceneBuilder
{
    struct Painter* painter;
    struct Minimap* minimap;
    int wx_sw;
    int wz_sw;
    int wx_ne;
    int wz_ne;
    int size_x;
    int size_z;

    struct BuildCacheDat* buildcachedat;
    struct BuildCache* buildcache;
    /** When using buildcachedat, textures live on Scene2; set by load_from_buildcachedat. */
    struct Scene2* texture_scene2;

    struct BuildGrid* build_grid;
    struct Shademap* shademap;
};

#endif