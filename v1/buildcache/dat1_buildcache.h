#ifndef DAT1_BUILDCACHE_H
#define DAT1_BUILDCACHE_H

#include "osrs/rscache/dat2a/dat2a_maps.h"
#include "osrs/rscache/dat2a/dat2a_model.h"
#include "osrs/rscache/shared/shared_file_list.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ToriDraw_Map;
struct ToriDraw_Texture;
struct RSCacheDat1A_ConfigSequence;
struct RSCacheDat2A_ConfigOverlay;
struct RSCacheDat2A_ConfigLocation;
struct RSCacheDat1A_AnimBaseFrames;

struct RSCacheDat1A_ConfigComponentList;
struct RSCacheDat1A_ConfigIdk;
struct RSCacheDat1A_ConfigObj;
struct RSCacheDat1A_ConfigNpc;

#define DAT1_TEXTURE_COUNT 50

struct Dat1BuildCache
{
    struct RSCacheShared_FileListDat* fromconfigtable_config_jagfile;
    struct RSCacheShared_FileListDat* versionlist_jagfile;
    struct RSCacheShared_FileListDat* media_2d_graphics_jagfile;
    struct RSCacheShared_FileListDat* title_fonts_jagfile;
    struct RSCacheDat1A_ConfigComponentList* interfaces;
    struct ToriDraw_Map* models_hmap;
    struct ToriDraw_Map* map_terrain_hmap;
    struct ToriDraw_Map* map_scenery_hmap;
    struct ToriDraw_Map* sequences_hmap;
    struct ToriDraw_Map* flotype_hmap;
    struct ToriDraw_Map* config_loc_hmap;
    struct ToriDraw_Map* animbaseframes_hmap;
    struct ToriDraw_Map* idk_hmap;
    struct ToriDraw_Map* obj_hmap;
    struct ToriDraw_Map* npc_hmap;
    struct ToriDraw_Texture* textures[DAT1_TEXTURE_COUNT];
    int texture_count;
};

struct Dat1BuildCache*
dat1_buildcache_new(void);

void
dat1_buildRSCacheDat2Disk_Free(struct Dat1BuildCache* dat1_buildcache);

size_t
dat1_buildcache_bytes_total(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_set_fromconfigtable_config_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCacheShared_FileListDat* fromconfigtable_config_jagfile);

void
dat1_buildcache_clear_config_jagfile(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_set_versionlist_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCacheShared_FileListDat* versionlist_jagfile);

void
dat1_buildcache_clear_versionlist_jagfile(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_set_media_2d_graphics_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCacheShared_FileListDat* media_2d_graphics_jagfile);

void
dat1_buildcache_set_title_fonts_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCacheShared_FileListDat* title_fonts_jagfile);

struct RSCacheShared_FileListDat*
dat1_buildcache_get_media_2d_graphics_jagfile(struct Dat1BuildCache* dat1_buildcache);

struct RSCacheShared_FileListDat*
dat1_buildcache_get_title_fonts_jagfile(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_set_interfaces(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCacheDat1A_ConfigComponentList* interfaces);

struct RSCacheDat1A_ConfigComponentList*
dat1_buildcache_get_interfaces(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_model_add(
    struct Dat1BuildCache* dat1_buildcache,
    int model_id,
    struct RSCacheDat2A_Model* model);

struct RSCacheDat2A_Model*
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
    struct RSCacheDat2A_MapTerrain* terrain);

struct RSCacheDat2A_MapTerrain*
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
    struct RSCacheDat2A_MapLocs* locs);

struct RSCacheDat2A_MapLocs*
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
dat1_buildcache_idk_add(
    struct Dat1BuildCache* dat1_buildcache,
    int idk_id,
    struct RSCacheDat1A_ConfigIdk* idk);

struct RSCacheDat1A_ConfigIdk*
dat1_buildcache_idk_get(
    struct Dat1BuildCache* dat1_buildcache,
    int idk_id);

void
dat1_buildcache_obj_add(
    struct Dat1BuildCache* dat1_buildcache,
    int obj_id,
    struct RSCacheDat1A_ConfigObj* obj);

struct RSCacheDat1A_ConfigObj*
dat1_buildcache_obj_get(
    struct Dat1BuildCache* dat1_buildcache,
    int obj_id);

void
dat1_buildcache_npc_add(
    struct Dat1BuildCache* dat1_buildcache,
    int npc_id,
    struct RSCacheDat1A_ConfigNpc* npc);

struct RSCacheDat1A_ConfigNpc*
dat1_buildcache_npc_get(
    struct Dat1BuildCache* dat1_buildcache,
    int npc_id);

void
dat1_buildcache_idks_init_from_config_jagfile(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_objs_init_from_config_jagfile(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_npcs_init_from_config_jagfile(struct Dat1BuildCache* dat1_buildcache);

struct RSCacheDat1A_ConfigNpc*
dat1_buildcache_npc_load_from_config_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    int npc_id);

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
    struct RSCacheDat1A_AnimBaseFrames* animbaseframes);

struct RSCacheDat1A_AnimBaseFrames*
dat1_buildcache_animbaseframes_get(
    struct Dat1BuildCache* dat1_buildcache,
    int animbaseframes_id);

struct RSCacheDat1A_AnimBaseFrames*
dat1_buildcache_animbaseframes_take(
    struct Dat1BuildCache* dat1_buildcache,
    int animbaseframes_id);

bool
dat1_buildcache_animbaseframes_has(
    struct Dat1BuildCache* dat1_buildcache,
    int animbaseframes_id);

int
dat1_buildcache_get_animbaseframes_count_from_versionlist_jagfile(
    struct Dat1BuildCache* dat1_buildcache);

struct RSCacheDat2A_ConfigLocation*
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
    struct RSCacheDat1A_ConfigSequence* sequence,
    void* user_data);

typedef void (*Dat1BuildCacheFlotypeCallback)(
    int flo_id,
    struct RSCacheDat2A_ConfigOverlay* flotype,
    void* user_data);

typedef void (*Dat1BuildCacheLocationCallback)(
    int loc_id,
    struct RSCacheDat2A_ConfigLocation* config_loc,
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
dat1_buildcache_idks_reset(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_objs_reset(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_npcs_reset(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_map_terrain_cleanup(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_map_scenery_cleanup(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_sequences_cleanup(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_floortypes_cleanup(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_scenery_configs_cleanup(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_animbaseframes_cleanup(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_idks_cleanup(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_objs_cleanup(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_npcs_cleanup(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_models_cleanup(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_prune(struct Dat1BuildCache* dat1_buildcache);

#endif
