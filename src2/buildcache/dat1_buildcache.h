#ifndef DAT1_BUILDCACHE_H
#define DAT1_BUILDCACHE_H

#include "osrs/rscache/filelist.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables/model.h"

#include <stdint.h>

struct ToriDraw_Map;
struct ToriDraw_Texture;
struct CacheDatSequence;
struct CacheConfigOverlay;
struct CacheConfigLocation;
struct CacheDatAnimBaseFrames;

struct CacheDatConfigComponentList;

#define DAT1_TEXTURE_COUNT 50

struct Dat1BuildCache
{
    struct FileListDat* fromconfigtable_config_jagfile;
    struct FileListDat* versionlist_jagfile;
    struct FileListDat* media_2d_graphics_jagfile;
    struct CacheDatConfigComponentList* interfaces;
    struct ToriDraw_Map* models_hmap;
    struct ToriDraw_Map* map_terrain_hmap;
    struct ToriDraw_Map* map_scenery_hmap;
    struct ToriDraw_Map* sequences_hmap;
    struct ToriDraw_Map* flotype_hmap;
    struct ToriDraw_Map* config_loc_hmap;
    struct ToriDraw_Map* animbaseframes_hmap;
    struct ToriDraw_Texture* textures[DAT1_TEXTURE_COUNT];
    int texture_count;
};

struct Dat1BuildCache*
dat1_buildcache_new(void);

void
dat1_buildcache_free(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_set_fromconfigtable_config_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct FileListDat* fromconfigtable_config_jagfile);

void
dat1_buildcache_clear_config_jagfile(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_set_versionlist_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct FileListDat* versionlist_jagfile);

void
dat1_buildcache_clear_versionlist_jagfile(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_set_media_2d_graphics_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct FileListDat* media_2d_graphics_jagfile);

struct FileListDat*
dat1_buildcache_get_media_2d_graphics_jagfile(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_set_interfaces(
    struct Dat1BuildCache* dat1_buildcache,
    struct CacheDatConfigComponentList* interfaces);

struct CacheDatConfigComponentList*
dat1_buildcache_get_interfaces(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_model_add(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id,
    struct CacheModel* model);

struct CacheModel*
dat1_buildcache_model_get(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id);

void
dat1_buildcache_model_remove(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id);

void
dat1_buildcache_map_terrain_add(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id,
    struct CacheMapTerrain* terrain);

struct CacheMapTerrain*
dat1_buildcache_map_terrain_get(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id);

bool
dat1_buildcache_map_terrain_has(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id);

void
dat1_buildcache_map_scenery_add(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id,
    struct CacheMapLocs* locs);

struct CacheMapLocs*
dat1_buildcache_map_scenery_get(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id);

bool
dat1_buildcache_map_scenery_has(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id);

void
dat1_buildcache_texture_set(
    struct Dat1BuildCache* dat1_buildcache,
    int index,
    struct ToriDraw_Texture* texture);

struct ToriDraw_Texture*
dat1_buildcache_texture_get(
    struct Dat1BuildCache* dat1_buildcache,
    int index);

void
dat1_buildcache_texture_clear(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_sequences_init_from_config_jagfile(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_floortypes_init_from_config_jagfile(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_init_scenery_configs_from_config_jagfile(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_animbaseframes_add(
    struct Dat1BuildCache* dat1_buildcache,
    int animbaseframes_id,
    struct CacheDatAnimBaseFrames* animbaseframes);

struct CacheDatAnimBaseFrames*
dat1_buildcache_animbaseframes_get(
    struct Dat1BuildCache* dat1_buildcache,
    int animbaseframes_id);

bool
dat1_buildcache_animbaseframes_has(
    struct Dat1BuildCache* dat1_buildcache,
    int animbaseframes_id);

int
dat1_buildcache_get_animbaseframes_count_from_versionlist_jagfile(
    struct Dat1BuildCache* dat1_buildcache);

struct CacheConfigLocation*
dat1_buildcache_config_loc_get(
    struct Dat1BuildCache* dat1_buildcache,
    int loc_id);

int
dat1_buildcache_get_scenery_model_ids(
    struct Dat1BuildCache* dat1_buildcache,
    int loc_id,
    int** model_ids_out);

int
dat1_buildcache_get_all_unique_scenery_model_ids(
    struct Dat1BuildCache* dat1_buildcache,
    int** model_ids_out);

typedef void (*Dat1BuildCacheSequenceCallback)(
    int seq_id,
    struct CacheDatSequence* sequence,
    void* user_data);

typedef void (*Dat1BuildCacheFlotypeCallback)(
    int flo_id,
    struct CacheConfigOverlay* flotype,
    void* user_data);

typedef void (*Dat1BuildCacheLocationCallback)(
    int loc_id,
    struct CacheConfigLocation* config_loc,
    void* user_data);

void
dat1_buildcache_foreach_sequence(
    struct Dat1BuildCache* dat1_buildcache,
    Dat1BuildCacheSequenceCallback callback,
    void* user_data);

void
dat1_buildcache_foreach_flotype(
    struct Dat1BuildCache* dat1_buildcache,
    Dat1BuildCacheFlotypeCallback callback,
    void* user_data);

void
dat1_buildcache_foreach_config_loc(
    struct Dat1BuildCache* dat1_buildcache,
    Dat1BuildCacheLocationCallback callback,
    void* user_data);

void
dat1_buildcache_sequences_reset(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_floortypes_reset(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_scenery_configs_reset(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_animbaseframes_reset(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_sequences_cleanup(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_floortypes_cleanup(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_scenery_configs_cleanup(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_animbaseframes_cleanup(struct Dat1BuildCache* dat1_buildcache);

#endif
