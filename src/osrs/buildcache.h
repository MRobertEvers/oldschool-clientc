#ifndef BUILD_CACHE_H
#define BUILD_CACHE_H

#include "graphics/dash.h"
#include "osrs/rscache/tables/config_floortype.h"
#include "osrs/rscache/tables/config_locs.h"
#include "osrs/rscache/tables/config_sequence.h"
#include "osrs/rscache/tables/frame.h"
#include "osrs/rscache/tables/framemap.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"
#include "osrs/rscache/tables/sprites.h"
#include "osrs/rscache/tables/textures.h"

typedef struct CacheSpritePack CacheSpritepack;

struct CacheFrameBlob; /* opaque; implemented as FileList when filelist.h included */

struct BuildCache
{
    struct DashMap* map_terrain_hmap;
    struct DashMap* map_scenery_hmap;
    struct DashMap* config_overlay_hmap;
    struct DashMap* config_underlay_hmap;
    struct DashMap* config_sequence_hmap;
    struct DashMap* config_location_hmap;
    struct DashMap* models_hmap;
    struct DashMap* spritepacks_hmap;
    struct DashMap* textures_hmap;
    struct DashMap* frame_blob_hmap;
    struct DashMap* frame_anim_hmap;
    struct DashMap* framemaps_hmap;
};

struct BuildCache*
buildcache_new(void);

void
buildcache_free(struct BuildCache* buildcache);

void
buildcache_add_map_terrain(
    struct BuildCache* buildcache,
    int mapx,
    int mapz,
    struct CacheMapTerrain* map_terrain);

struct CacheMapTerrain*
buildcache_get_map_terrain(
    struct BuildCache* buildcache,
    int mapx,
    int mapz);

void
buildcache_add_map_scenery(
    struct BuildCache* buildcache,
    int mapx,
    int mapz,
    struct CacheMapLocs* locs);

struct CacheMapLocs*
buildcache_get_map_scenery(
    struct BuildCache* buildcache,
    int mapx,
    int mapz);

struct DashMapIter*
buildcache_iter_new_map_scenery(struct BuildCache* buildcache);

struct CacheMapLocs*
buildcache_iter_next_map_scenery(
    struct DashMapIter* iter,
    int* mapx,
    int* mapz);

void
buildcache_iter_free_map_scenery(struct DashMapIter* iter);

struct DashMapIter*
buildcache_iter_new_models(struct BuildCache* buildcache);

struct CacheModel*
buildcache_iter_next_models(
    struct DashMapIter* iter,
    int* model_id);

void
buildcache_iter_free_models(struct DashMapIter* iter);

void
buildcache_add_config_location(
    struct BuildCache* buildcache,
    int config_location_id,
    struct CacheConfigLocation* config_location);

struct CacheConfigLocation*
buildcache_get_config_location(
    struct BuildCache* buildcache,
    int config_location_id);

void
buildcache_add_config_overlay(
    struct BuildCache* buildcache,
    int config_overlay_id,
    struct CacheConfigOverlay* config_overlay);

struct CacheConfigOverlay*
buildcache_get_config_overlay(
    struct BuildCache* buildcache,
    int config_overlay_id);

struct DashMapIter*
buildcache_iter_new_config_overlay(struct BuildCache* buildcache);

struct CacheConfigOverlay*
buildcache_iter_next_config_overlay(struct DashMapIter* iter);

void
buildcache_iter_free_config_overlay(struct DashMapIter* iter);

void
buildcache_add_config_underlay(
    struct BuildCache* buildcache,
    int config_underlay_id,
    struct CacheConfigUnderlay* config_underlay);

struct CacheConfigUnderlay*
buildcache_get_config_underlay(
    struct BuildCache* buildcache,
    int config_underlay_id);

void
buildcache_add_config_sequence(
    struct BuildCache* buildcache,
    int config_sequence_id,
    struct CacheConfigSequence* config_sequence);

struct CacheConfigSequence*
buildcache_get_config_sequence(
    struct BuildCache* buildcache,
    int config_sequence_id);

void
buildcache_add_model(
    struct BuildCache* buildcache,
    int model_id,
    struct CacheModel* model);

struct CacheModel*
buildcache_get_model(
    struct BuildCache* buildcache,
    int model_id);

void
buildcache_add_spritepack(
    struct BuildCache* buildcache,
    int spritepack_id,
    struct CacheSpritePack* spritepack);

struct CacheSpritePack*
buildcache_get_spritepack(
    struct BuildCache* buildcache,
    int spritepack_id);

void
buildcache_add_texture(
    struct BuildCache* buildcache,
    int texture_id,
    struct DashTexture* texture);

struct DashTexture*
buildcache_get_texture(
    struct BuildCache* buildcache,
    int texture_id);

void
buildcache_add_frame_blob(
    struct BuildCache* buildcache,
    int frame_blob_id,
    struct CacheFrameBlob* frame_blob);

struct CacheFrameBlob*
buildcache_get_frame_blob(
    struct BuildCache* buildcache,
    int frame_blob_id);

void
buildcache_add_frame_anim(
    struct BuildCache* buildcache,
    int frame_anim_id,
    struct CacheFrame* frame);

struct CacheFrame*
buildcache_get_frame_anim(
    struct BuildCache* buildcache,
    int frame_anim_id);

void
buildcache_add_framemap(
    struct BuildCache* buildcache,
    int framemap_id,
    struct CacheFramemap* framemap);

struct CacheFramemap*
buildcache_get_framemap(
    struct BuildCache* buildcache,
    int framemap_id);

#endif