#ifndef BUILD_CACHE_H
#define BUILD_CACHE_H

#include "graphics/dash.h"
#include "osrs/rscache/dat2a/dat2a_config_floortype.h"
#include "osrs/rscache/dat2a/dat2a_config_locs.h"
#include "osrs/rscache/dat2a/dat2a_config_sequence.h"
#include "osrs/rscache/dat2a/dat2a_frame.h"
#include "osrs/rscache/dat2a/dat2a_framemap.h"
#include "osrs/rscache/dat2a/dat2a_maps.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/dat2a/dat2a_sprites.h"
#include "osrs/rscache/dat2a/dat2a_textures.h"

typedef struct RSCacheDat2A_SpritePack CacheSpritepack;

struct RSCacheDat2A_FrameBlob; /* opaque; implemented as FileList when filelist.h included */

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
buildRSCacheDat2Disk_Free(struct BuildCache* buildcache);

void
buildcache_add_map_terrain(
    struct BuildCache* buildcache,
    int mapx,
    int mapz,
    struct RSCacheDat2A_MapTerrain* map_terrain);

struct RSCacheDat2A_MapTerrain*
buildcache_get_map_terrain(
    struct BuildCache* buildcache,
    int mapx,
    int mapz);

void
buildcache_add_map_scenery(
    struct BuildCache* buildcache,
    int mapx,
    int mapz,
    struct RSCacheDat2A_MapLocs* locs);

struct RSCacheDat2A_MapLocs*
buildcache_get_map_scenery(
    struct BuildCache* buildcache,
    int mapx,
    int mapz);

struct DashMapIter*
buildcache_iter_new_map_scenery(struct BuildCache* buildcache);

struct RSCacheDat2A_MapLocs*
buildcache_iter_next_map_scenery(
    struct DashMapIter* iter,
    int* mapx,
    int* mapz);

void
buildcache_iter_free_map_scenery(struct DashMapIter* iter);

struct DashMapIter*
buildcache_iter_new_models(struct BuildCache* buildcache);

struct RSCacheDat2A_Model*
buildcache_iter_next_models(
    struct DashMapIter* iter,
    int* model_id);

void
buildcache_iter_free_models(struct DashMapIter* iter);

void
buildcache_add_config_location(
    struct BuildCache* buildcache,
    int config_location_id,
    struct RSCacheDat2A_ConfigLocation* config_location);

struct RSCacheDat2A_ConfigLocation*
buildcache_get_config_location(
    struct BuildCache* buildcache,
    int config_location_id);

void
buildcache_add_config_overlay(
    struct BuildCache* buildcache,
    int config_overlay_id,
    struct RSCacheDat2A_ConfigOverlay* config_overlay);

struct RSCacheDat2A_ConfigOverlay*
buildcache_get_config_overlay(
    struct BuildCache* buildcache,
    int config_overlay_id);

struct DashMapIter*
buildcache_iter_new_config_overlay(struct BuildCache* buildcache);

struct RSCacheDat2A_ConfigOverlay*
buildcache_iter_next_config_overlay(struct DashMapIter* iter);

void
buildcache_iter_free_config_overlay(struct DashMapIter* iter);

void
buildcache_add_config_underlay(
    struct BuildCache* buildcache,
    int config_underlay_id,
    struct RSCacheDat2A_ConfigUnderlay* config_underlay);

struct RSCacheDat2A_ConfigUnderlay*
buildcache_get_config_underlay(
    struct BuildCache* buildcache,
    int config_underlay_id);

void
buildcache_add_config_sequence(
    struct BuildCache* buildcache,
    int config_sequence_id,
    struct RSCacheDat2A_ConfigSequence* config_sequence);

struct RSCacheDat2A_ConfigSequence*
buildcache_get_config_sequence(
    struct BuildCache* buildcache,
    int config_sequence_id);

void
buildcache_add_model(
    struct BuildCache* buildcache,
    int model_id,
    struct RSCacheDat2A_Model* model);

struct RSCacheDat2A_Model*
buildcache_get_model(
    struct BuildCache* buildcache,
    int model_id);

void
buildcache_add_spritepack(
    struct BuildCache* buildcache,
    int spritepack_id,
    struct RSCacheDat2A_SpritePack* spritepack);

struct RSCacheDat2A_SpritePack*
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
    struct RSCacheDat2A_FrameBlob* frame_blob);

struct RSCacheDat2A_FrameBlob*
buildcache_get_frame_blob(
    struct BuildCache* buildcache,
    int frame_blob_id);

void
buildcache_add_frame_anim(
    struct BuildCache* buildcache,
    int frame_anim_id,
    struct RSCacheDat2A_Frame* frame);

struct RSCacheDat2A_Frame*
buildcache_get_frame_anim(
    struct BuildCache* buildcache,
    int frame_anim_id);

void
buildcache_add_framemap(
    struct BuildCache* buildcache,
    int framemap_id,
    struct RSCacheDat2A_Framemap* framemap);

struct RSCacheDat2A_Framemap*
buildcache_get_framemap(
    struct BuildCache* buildcache,
    int framemap_id);

#endif