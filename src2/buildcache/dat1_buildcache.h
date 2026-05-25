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

#define DAT1_TEXTURE_COUNT 50

struct Dat1BuildCache
{
    struct FileListDat* fromconfigtable_config_jagfile;
    struct FileListDat* versionlist_jagfile;
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
dat1_buildcache_set_versionlist_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct FileListDat* versionlist_jagfile);

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

void
dat1_buildcache_map_scenery_add(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id,
    struct CacheMapLocs* locs);

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

#endif
