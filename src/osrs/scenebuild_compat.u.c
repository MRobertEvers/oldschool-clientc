#ifndef SCENEBUILDER_COMPAT_U_C
#define SCENEBUILDER_COMPAT_U_C

#include "osrs/buildcachedat.h"
#include "osrs/rscache/tables/config_floortype.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"
#include "scenebuilder.u.c"

struct CacheMapTerrain*
scenebuilder_compat_get_map_terrain(
    struct SceneBuilder* scene_builder,
    int mapx,
    int mapz)
{
    if( scene_builder->buildcachedat != NULL )
    {
        int map_index = MAPREGIONXZ(mapx, mapz);
        return buildcachedat_get_map_terrain(scene_builder->buildcachedat, mapx, mapz);
    }
    return NULL;
}

struct CacheConfigOverlay*
scenebuilder_compat_get_flotype(
    struct SceneBuilder* scene_builder,
    int flotype_id)
{
    if( scene_builder->buildcachedat != NULL )
    {
        return buildcachedat_get_flotype(scene_builder->buildcachedat, flotype_id);
    }
    return NULL;
}

uint32_t
scenebuilder_compat_get_underlay_rgb(
    struct SceneBuilder* scene_builder,
    int underlay_id)
{
    if( scene_builder->buildcachedat != NULL )
    {
        struct CacheConfigOverlay* flotype =
            buildcachedat_get_flotype(scene_builder->buildcachedat, underlay_id);
        if( flotype )
            return flotype->rgb_color;
    }
    return -1;
}

struct CacheModel*
scenebuilder_compat_get_model(
    struct SceneBuilder* scene_builder,
    int model_id)
{
    if( scene_builder->buildcachedat != NULL )
    {
        return buildcachedat_get_model(scene_builder->buildcachedat, model_id);
    }
}

struct CacheMapLocs*
scenebuilder_compat_get_scenery(
    struct SceneBuilder* scene_builder,
    int mapx,
    int mapz)
{
    if( scene_builder->buildcachedat != NULL )
    {
        return buildcachedat_get_scenery(scene_builder->buildcachedat, mapx, mapz);
    }
    return NULL;
}

struct DashTexture*
scenebuilder_compat_get_texture(
    struct SceneBuilder* scene_builder,
    int texture_id)
{
    if( scene_builder->buildcachedat != NULL )
    {
        return buildcachedat_get_texture(scene_builder->buildcachedat, texture_id);
    }
    if( scene_builder->buildcache != NULL )
    {
        return buildcache_get_texture(scene_builder->buildcache, texture_id);
    }
    return NULL;
}

#endif