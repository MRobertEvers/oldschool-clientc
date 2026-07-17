#ifndef DAT1_BUILDCACHE_H
#define DAT1_BUILDCACHE_H

#include "engine/cache_provider.h"

#include <rscache.h>
#include <stdbool.h>
#include <stddef.h>

struct Dat1BuildCache
{
    struct CacheProvider base;
    struct HMap* models_hmap;
    struct HMap* obj_hmap;
    struct HMap* npc_hmap;
    struct HMap* idk_hmap;
    struct HMap* map_terrain_hmap;
    struct HMap* map_scenery_hmap;
    struct RSCache_FileListDat* config_jagfile;
    struct RSCache_FileListDat* versionlist_jagfile;
    struct RSCache_FileListDat* media_2d_graphics_jagfile;
    struct RSCache_FileListDat* title_fonts_jagfile;
};

struct Dat1BuildCache*
dat1_buildcache_new(void);

void
dat1_buildcache_free(struct Dat1BuildCache* dat1_buildcache);

struct CacheProvider*
dat1_buildcache_as_provider(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_set_config_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCache_FileListDat* config_jagfile);

void
dat1_buildcache_clear_config_jagfile(struct Dat1BuildCache* dat1_buildcache);

struct RSCache_FileListDat*
dat1_buildcache_get_config_jagfile(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_set_versionlist_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCache_FileListDat* versionlist_jagfile);

void
dat1_buildcache_clear_versionlist_jagfile(struct Dat1BuildCache* dat1_buildcache);

struct RSCache_FileListDat*
dat1_buildcache_get_versionlist_jagfile(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_set_media_2d_graphics_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCache_FileListDat* media_2d_graphics_jagfile);

void
dat1_buildcache_clear_media_2d_graphics_jagfile(struct Dat1BuildCache* dat1_buildcache);

struct RSCache_FileListDat*
dat1_buildcache_get_media_2d_graphics_jagfile(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_set_title_fonts_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCache_FileListDat* title_fonts_jagfile);

void
dat1_buildcache_clear_title_fonts_jagfile(struct Dat1BuildCache* dat1_buildcache);

struct RSCache_FileListDat*
dat1_buildcache_get_title_fonts_jagfile(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_model_add(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id,
    struct RSCache_Model* model);

struct RSCache_Model*
dat1_buildcache_model_get(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id);

void
dat1_buildcache_model_remove(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id);

void
dat1_buildcache_models_cleanup(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_obj_add(
    struct Dat1BuildCache* dat1_buildcache,
    int obj_id,
    struct RSCache_Dat1ConfigObj* obj);

struct RSCache_Dat1ConfigObj*
dat1_buildcache_obj_get(
    struct Dat1BuildCache* dat1_buildcache,
    int obj_id);

bool
dat1_buildcache_obj_has(
    struct Dat1BuildCache* dat1_buildcache,
    int obj_id);

void
dat1_buildcache_objs_cleanup(struct Dat1BuildCache* dat1_buildcache);

struct RSCache_Dat1ConfigObj*
dat1_buildcache_obj_load_from_config_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    int obj_id);

void
dat1_buildcache_npc_add(
    struct Dat1BuildCache* dat1_buildcache,
    int npc_id,
    struct RSCache_Dat1ConfigNpc* npc);

struct RSCache_Dat1ConfigNpc*
dat1_buildcache_npc_get(
    struct Dat1BuildCache* dat1_buildcache,
    int npc_id);

bool
dat1_buildcache_npc_has(
    struct Dat1BuildCache* dat1_buildcache,
    int npc_id);

void
dat1_buildcache_npcs_cleanup(struct Dat1BuildCache* dat1_buildcache);

struct RSCache_Dat1ConfigNpc*
dat1_buildcache_npc_load_from_config_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    int npc_id);

void
dat1_buildcache_idk_add(
    struct Dat1BuildCache* dat1_buildcache,
    int idk_id,
    struct RSCache_Dat1ConfigIdk* idk);

struct RSCache_Dat1ConfigIdk*
dat1_buildcache_idk_get(
    struct Dat1BuildCache* dat1_buildcache,
    int idk_id);

bool
dat1_buildcache_idk_has(
    struct Dat1BuildCache* dat1_buildcache,
    int idk_id);

void
dat1_buildcache_idks_cleanup(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_idks_init_from_config_jagfile(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_map_terrain_add(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id,
    struct RSCache_MapTerrain* terrain);

struct RSCache_MapTerrain*
dat1_buildcache_map_terrain_get(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id);

bool
dat1_buildcache_map_terrain_has(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id);

void
dat1_buildcache_map_terrain_cleanup(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_map_scenery_add(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id,
    struct RSCache_MapLocs* locs);

struct RSCache_MapLocs*
dat1_buildcache_map_scenery_get(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id);

bool
dat1_buildcache_map_scenery_has(
    struct Dat1BuildCache* dat1_buildcache,
    int map_id);

void
dat1_buildcache_map_scenery_cleanup(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_prune(struct Dat1BuildCache* dat1_buildcache);

#endif
