#ifndef BUILD_CACHE_DAT_H
#define BUILD_CACHE_DAT_H

#include "graphics/dash.h"

/** Max font name length stored in BuildCacheDat (bytes; zero-padded key, no trailing NUL if 24
 * chars). */
#define BUILDCACHEDAT_FONT_NAME_MAX 24
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
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/config_idk.h"
#include "osrs/rscache/tables_dat/config_npc.h"
#include "osrs/rscache/tables_dat/config_obj.h"
#include "osrs/rscache/tables_dat/config_textures.h"
#include "osrs/rscache/tables_dat/pix32.h"
#include "osrs/rscache/tables_dat/pix8.h"
#include "osrs/rscache/tables_dat/pixfont.h"

enum BuildCacheDatEventType
{
    BUILDCACHEDAT_EVENT_NONE = 0,
    BUILDCACHEDAT_EVENT_TEXTURE_ADDED = 1,
};

struct BuildCacheDatEvent
{
    enum BuildCacheDatEventType type;
    int texture_id;
};

struct BuildCacheDat
{
    struct FileListDat* cfg_config_jagfile;
    struct FileListDat* cfg_versionlist_jagfile;
    struct FileListDat* cfg_media_jagfile;

    struct DashMap* containers_hmap;

    /** Reftable: named sprite -> UIScene element id (no DashSprite* stored here). */
    struct DashMap* sprites_reftable;

    struct DashMap* map_terrains_hmap;
    struct DashMap* flotype_hmap;
    /** Reftable: texture id loaded into Scene2 (no DashTexture* stored here). */
    struct DashMap* textures_reftable;
    /** Reftable: font name -> UIScene font slot id only. UIScene owns DashPixFont; BuildCacheDat
     * never frees fonts in buildcachedat_free / reset_uiscene_linked_reftables. */
    struct DashMap* fonts_reftable;
    struct DashMap* scenery_hmap;
    struct DashMap* models_hmap;

    struct DashMap* idk_models_hmap;
    struct DashMap* obj_models_hmap;
    struct DashMap* npc_models_hmap;

    struct DashMap* config_loc_hmap;
    /** Reftable: animframe id -> animbaseframes blob + frame index (no CacheAnimframe* stored). */
    struct DashMap* animframes_reftable;
    struct DashMap* animbaseframes_hmap;
    struct DashMap* sequences_hmap;
    struct DashMap* idk_hmap;
    struct DashMap* obj_hmap;
    struct DashMap* npc_hmap;
    struct DashMap* component_hmap;
    /** Reftable: sprite name -> UIScene element id. */
    struct DashMap* component_sprites_reftable;

    struct BuildCacheDatEvent* eventbuffer;
    int eventbuffer_capacity;
    int eventbuffer_head;
    int eventbuffer_tail;
    int eventbuffer_count;
};

struct BuildCacheDat*
buildcachedat_new(void);

void
buildcachedat_free(struct BuildCacheDat* buildcachedat);

/** Free map terrain and map scenery chunk entries; maps are recreated empty. Other tables unchanged. */
void
buildcachedat_clear_map_chunks(struct BuildCacheDat* buildcachedat);

/** Free only the raw map terrain chunks (CacheMapTerrain). The empty hmap is recreated; call after
 * all terrain passes are complete to reduce peak memory. */
void
buildcachedat_clear_map_terrain_chunks(struct BuildCacheDat* buildcachedat);

/** Free only the raw map scenery chunks (CacheMapLocs). The empty hmap is recreated; call after
 * all scenery passes are complete to reduce peak memory. */
void
buildcachedat_clear_map_scenery_chunks(struct BuildCacheDat* buildcachedat);

/** Free every decoded interface component (CacheDatConfigComponent) and recreate an empty
 * component_hmap. Safe after static UI is built into the uitree; buildcachedat_get_component
 * returns NULL until interfaces are loaded again. Reftables (sprites, fonts, component_sprites)
 * are unchanged. */
void
buildcachedat_clear_component_cache(struct BuildCacheDat* buildcachedat);

/** Free the raw jagfile FileListDat buffers (config, versionlist, media) that were only needed
 * during the decode phase. All decoded entries remain valid in their hashmaps. Call before the
 * world rebuild to reduce peak concurrent memory. */
void
buildcachedat_clear_jagfiles(struct BuildCacheDat* buildcachedat);

/** Pre-size a BuildCacheDat hashmap to hold at least min_count entries without any subsequent
 * incremental resize. Call before bulk-inserting a known number of entries to avoid the transient
 * double-buffer spike from repeated grow operations. */
void
buildcachedat_reserve_hmap(struct DashMap* map, size_t min_count);

/** Free every owning BuildCacheDat cache (hash maps that own decoded data, config/versionlist/media
 * jagfiles, containers) and reinitialize those maps and event buffer. Reftable maps (textures,
 * fonts, sprites, component_sprites, animframes) are not cleared. The struct remains valid for reuse.
 * DashModel meshes live on Scene2, not in BuildCacheDat. */
void
buildcachedat_clear(struct BuildCacheDat* buildcachedat);

/** Drop font / sprite / component-sprite reftables (UIScene element and font slot ids invalidated).
 * Recreates empty maps; textures and animframe reftables unchanged. Call when replacing UIScene. */
void
buildcachedat_reset_uiscene_linked_reftables(struct BuildCacheDat* buildcachedat);

void
buildcachedat_set_config_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct FileListDat* config_jagfile);

struct FileListDat*
buildcachedat_config_jagfile(struct BuildCacheDat* buildcachedat);

void
buildcachedat_set_2d_media_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct FileListDat* media_jagile);

void
buildcachedat_set_versionlist_jagfile(
    struct BuildCacheDat* buildcachedat,
    struct FileListDat* versionlist_jagfile);

struct FileListDat*
buildcachedat_versionlist_jagfile(struct BuildCacheDat* buildcachedat);

void
buildcachedat_set_named_jagfile(
    struct BuildCacheDat* buildcachedat,
    const char* name,
    struct FileListDat* jagfile);

struct FileListDat*
buildcachedat_named_jagfile(
    struct BuildCacheDat* buildcachedat,
    char const* name);

struct JagfilePack
{
    void* data;
    int data_size;
};
struct JagfilePackIndexed
{
    void* data;
    int data_size;
    void* index_data;
    int index_data_size;
};

enum BuildCacheContainerKind
{
    BuildCacheContainerKind_Jagfile = 0,
    BuildCacheContainerKind_JagfilePack = 1,
    BuildCacheContainerKind_JagfilePackIndexed = 2
};

struct BuildCacheDatContainer
{
    enum BuildCacheContainerKind kind;

    union
    {
        struct FileListDat* _filelist;
        struct JagfilePack _jagfilepack;
        struct JagfilePackIndexed _jagfilepack_indexed;
    };
};

void
buildcachedat_add_jagfilepack(
    struct BuildCacheDat* buildcachedat,
    const char* name,
    void* data,
    int size);

void
buildcachedat_add_jagfilepack_indexed(
    struct BuildCacheDat* buildcachedat,
    const char* name,
    void* data,
    int data_size,
    void* index_data,
    int index_data_size);

struct BuildCacheDatContainer*
buildcachedat_named_container(
    struct BuildCacheDat* buildcachedat,
    const char* name);

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

/** Register that texture_id was loaded into Scene2 (reftable only). */
void
buildcachedat_add_texture_ref(struct BuildCacheDat* buildcachedat, int texture_id);

bool
buildcachedat_has_texture_ref(struct BuildCacheDat* buildcachedat, int texture_id);

void
buildcachedat_add_font_ref(
    struct BuildCacheDat* buildcachedat,
    const char* font_name,
    int uiscene_font_id);

/** Returns UIScene font slot id, or -1 if unknown. */
int
buildcachedat_get_font_ref_id(
    struct BuildCacheDat* buildcachedat,
    const char* font_name);

struct DashMapIter*
buildcachedat_iter_new_fonts_reftable(struct BuildCacheDat* buildcachedat);

/** Iterates font reftable; returns true while entries remain. */
bool
buildcachedat_iter_next_font_ref(
    struct DashMapIter* iter,
    char* out_name,
    int out_name_cap,
    int* out_uiscene_font_id);

void
buildcachedat_add_sprite_ref(
    struct BuildCacheDat* buildcachedat,
    const char* name,
    int uiscene_element_id);

int
buildcachedat_get_sprite_element_id(
    struct BuildCacheDat* buildcachedat,
    const char* name);

bool
buildcachedat_eventbuffer_is_empty(struct BuildCacheDat* buildcachedat);

bool
buildcachedat_eventbuffer_pop(
    struct BuildCacheDat* buildcachedat,
    struct BuildCacheDatEvent* out_event);

void
buildcachedat_eventbuffer_clear(struct BuildCacheDat* buildcachedat);

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
buildcachedat_add_animframe_ref(
    struct BuildCacheDat* buildcachedat,
    int animframe_id,
    int animbaseframes_id,
    int frame_index);

struct CacheAnimframe*
buildcachedat_get_animframe(
    struct BuildCacheDat* buildcachedat,
    int animframe_id);

void
buildcachedat_add_animbaseframes(
    struct BuildCacheDat* buildcachedat,
    int animbaseframes_id,
    struct CacheDatAnimBaseFrames* animbaseframes);

struct CacheDatAnimBaseFrames*
buildcachedat_get_animbaseframes(
    struct BuildCacheDat* buildcachedat,
    int animbaseframes_id);

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

struct DashMapIter*
buildcachedat_component_iter_new(struct BuildCacheDat* buildcachedat);

struct CacheDatConfigComponent*
buildcachedat_component_iter_next(
    struct DashMapIter* iter,
    int* id_out);

void
buildcachedat_add_component_sprite_ref(
    struct BuildCacheDat* buildcachedat,
    const char* sprite_name,
    int uiscene_element_id);

int
buildcachedat_get_component_sprite_element_id(
    struct BuildCacheDat* buildcachedat,
    const char* sprite_name);

#endif
