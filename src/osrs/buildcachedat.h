#ifndef BUILD_CACHE_DAT_H
#define BUILD_CACHE_DAT_H

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
#include "osrs/rscache/tables_dat/animframe.h"
#include "osrs/rscache/tables_dat/config_idk.h"
#include "osrs/rscache/tables_dat/config_npc.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/config_obj.h"
#include "osrs/rscache/tables_dat/config_textures.h"
#include "osrs/rscache/tables_dat/pix32.h"
#include "osrs/rscache/tables_dat/pix8.h"
#include "osrs/rscache/tables_dat/pixfont.h"

struct BuildCacheDat
{
    struct FileListDat* cfg_config_jagfile;
    struct FileListDat* cfg_versionlist_jagfile;

    struct DashMap* map_terrains_hmap;
    struct DashMap* flotype_hmap;
    struct DashMap* textures_hmap;
    struct DashMap* scenery_hmap;
    struct DashMap* models_hmap;

    struct DashMap* idk_models_hmap;
    struct DashMap* obj_models_hmap;
    struct DashMap* npc_models_hmap;

    struct DashMap* config_loc_hmap;
    struct DashMap* animframes_hmap;
    struct DashMap* animbaseframes_hmap;
    struct DashMap* sequences_hmap;
    struct DashMap* idk_hmap;
    struct DashMap* obj_hmap;
    struct DashMap* npc_hmap;
    struct DashMap* component_hmap;
    struct DashMap* component_sprites_hmap;
};

struct BuildCacheDat*
buildcachedat_new(void);

void
buildcachedat_free(struct BuildCacheDat* buildcachedat);

void
buildcachedat_set_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct FileListDat* config_jagfile);

struct FileListDat*
buildcachedat_config_jagfile(struct BuildCacheDat* buildcachedat);

void
buildcachedat_set_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct FileListDat* versionlist_jagfile);

struct FileListDat*
buildcachedat_versionlist_jagfile(struct BuildCacheDat* buildcachedat);

void
buildcachedat_add_flotype(
    struct BuildCacheDat* buildcachedat,
    int flotype_id,
    struct CacheConfigOverlay* flotype);

struct CacheConfigOverlay*
buildcachedat_get_flotype(
    struct BuildCacheDat* buildcachedat,
    int flotype_id);

struct DashMapIter*
buildcachedat_iter_new_flotypes(struct BuildCacheDat* buildcachedat);

struct CacheConfigOverlay*
buildcachedat_iter_next_flotype(struct DashMapIter* iter);

void
buildcachedat_add_texture(
    struct BuildCacheDat* buildcachedat,
    int texture_id,
    struct DashTexture* texture);

struct DashTexture*
buildcachedat_get_texture(
    struct BuildCacheDat* buildcachedat,
    int texture_id);

struct DashMapIter*
buildcachedat_iter_new_textures(struct BuildCacheDat* buildcachedat);

struct DashTexture*
buildcachedat_iter_next_texture(struct DashMapIter* iter);

void
buildcachedat_add_scenery(
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz,
    struct CacheMapLocs* locs);

struct CacheMapLocs*
buildcachedat_get_scenery(
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz);

struct CacheMapLocs*
buildcachedat_iter_next_scenery(struct DashMapIter* iter);

struct DashMapIter*
buildcachedat_iter_new_scenery(struct BuildCacheDat* buildcachedat);

void
buildcachedat_add_model(
    struct BuildCacheDat* buildcachedat,
    int model_id,
    struct CacheModel* model);

struct CacheModel*
buildcachedat_get_model(
    struct BuildCacheDat* buildcachedat,
    int model_id);

void
buildcachedat_add_idk_model(
    struct BuildCacheDat* buildcachedat,
    int idk_id,
    struct CacheModel* model);

struct CacheModel*
buildcachedat_get_idk_model(
    struct BuildCacheDat* buildcachedat,
    int idk_id);

void
buildcachedat_add_obj_model(
    struct BuildCacheDat* buildcachedat,
    int obj_id,
    struct CacheModel* model);

struct CacheModel*
buildcachedat_get_obj_model(
    struct BuildCacheDat* buildcachedat,
    int obj_id);

void
buildcachedat_add_npc(
    struct BuildCacheDat* buildcachedat,
    int npc_id,
    struct CacheDatConfigNpc* npc);

struct CacheDatConfigNpc*
buildcachedat_get_npc(
    struct BuildCacheDat* buildcachedat,
    int npc_id);

struct DashMapIter*
buildcachedat_iter_new_npcs(struct BuildCacheDat* buildcachedat);

struct CacheDatConfigNpc*
buildcachedat_iter_next_npc(struct DashMapIter* iter);

void
buildcachedat_add_npc_model(
    struct BuildCacheDat* buildcachedat,
    int npc_id,
    struct CacheModel* model);

struct CacheModel*
buildcachedat_get_npc_model(
    struct BuildCacheDat* buildcachedat,
    int npc_id);

struct CacheModel*
buildcachedat_iter_next_model(struct DashMapIter* iter);

struct DashMapIter*
buildcachedat_iter_new_models(struct BuildCacheDat* buildcachedat);

void
buildcachedat_add_config_loc(
    struct BuildCacheDat* buildcachedat,
    int config_loc_id,
    struct CacheConfigLocation* config_loc);

struct CacheConfigLocation*
buildcachedat_iter_next_config_loc(struct DashMapIter* iter);

struct CacheConfigLocation*
buildcachedat_get_config_loc(
    struct BuildCacheDat* buildcachedat,
    int config_loc_id);

struct DashMapIter*
buildcachedat_iter_new_config_locs(struct BuildCacheDat* buildcachedat);

void
buildcachedat_add_animframe(
    struct BuildCacheDat* buildcachedat,
    int animframe_id,
    struct CacheAnimframe* animframe);

struct CacheAnimframe*
buildcachedat_get_animframe(
    struct BuildCacheDat* buildcachedat,
    int animframe_id);

struct CacheAnimframe*
buildcachedat_iter_next_animframe(struct DashMapIter* iter);

struct DashMapIter*
buildcachedat_iter_new_animframes(struct BuildCacheDat* buildcachedat);

void
buildcachedat_add_animbaseframes(
    struct BuildCacheDat* buildcachedat,
    int animbaseframes_id,
    struct CacheDatAnimBaseFrames* animbaseframes);

struct CacheDatAnimBaseFrames*
buildcachedat_iter_next_animbaseframes(struct DashMapIter* iter);

struct DashMapIter*
buildcachedat_iter_new_animbaseframes(struct BuildCacheDat* buildcachedat);

void
buildcachedat_add_sequence(
    struct BuildCacheDat* buildcachedat,
    int sequence_id,
    struct CacheDatSequence* sequence);

struct CacheDatSequence*
buildcachedat_get_sequence(
    struct BuildCacheDat* buildcachedat,
    int sequence_id);

struct CacheDatSequence*
buildcachedat_iter_next_sequence(struct DashMapIter* iter);

struct DashMapIter*
buildcachedat_iter_new_sequences(struct BuildCacheDat* buildcachedat);

void
buildcachedat_add_idk(
    struct BuildCacheDat* buildcachedat,
    int idk_id,
    struct CacheDatConfigIdk* idk);

struct CacheDatConfigIdk*
buildcachedat_get_idk(
    struct BuildCacheDat* buildcachedat,
    int idk_id);

struct CacheDatConfigIdk*
buildcachedat_iter_next_idk(struct DashMapIter* iter);

struct DashMapIter*
buildcachedat_iter_new_idks(struct BuildCacheDat* buildcachedat);

void
buildcachedat_add_obj(
    struct BuildCacheDat* buildcachedat,
    int obj_id,
    struct CacheDatConfigObj* obj);

struct CacheDatConfigObj*
buildcachedat_get_obj(
    struct BuildCacheDat* buildcachedat,
    int obj_id);

struct CacheDatConfigObj*
buildcachedat_iter_next_obj(struct DashMapIter* iter);

struct DashMapIter*
buildcachedat_iter_new_objs(struct BuildCacheDat* buildcachedat);

void
buildcachedat_add_map_terrain(
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz,
    struct CacheMapTerrain* map_terrain);

struct CacheMapTerrain*
buildcachedat_get_map_terrain(
    struct BuildCacheDat* buildcachedat,
    int mapx,
    int mapz);

void
buildcachedat_add_component(
    struct BuildCacheDat* buildcachedat,
    int component_id,
    struct CacheDatConfigComponent* component);

struct CacheDatConfigComponent*
buildcachedat_get_component(
    struct BuildCacheDat* buildcachedat,
    int component_id);

void
buildcachedat_add_component_sprite(
    struct BuildCacheDat* buildcachedat,
    const char* sprite_name,
    struct DashSprite* sprite);

struct DashSprite*
buildcachedat_get_component_sprite(
    struct BuildCacheDat* buildcachedat,
    const char* sprite_name);

#endif