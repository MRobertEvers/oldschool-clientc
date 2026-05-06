#ifndef GAME_CACHE_H
#define GAME_CACHE_H

#include "graphics/dash.h"

/** Max font name length stored in GameCache (bytes; zero-padded key, no trailing NUL if 24
 * chars). */
#define GAMECACHE_FONT_NAME_MAX 24

/* GameCache-owned type headers -- no rscache dependencies */
#include "gamecache_animbase.h"
#include "gamecache_animframe.h"
#include "gamecache_component.h"
#include "gamecache_idk.h"
#include "gamecache_loc.h"
#include "gamecache_locs.h"
#include "gamecache_model.h"
#include "gamecache_npc.h"
#include "gamecache_obj.h"
#include "gamecache_overlay.h"
#include "gamecache_sequence.h"
#include "gamecache_spotanim.h"
#include "gamecache_terrain.h"
#include "gamecache_underlay.h"

/* Reftable types that remain rscache-format (sprites/textures/fonts/frames are pass-through). */
#include "osrs/rscache/tables/sprites.h"
#include "osrs/rscache/tables/textures.h"
#include "osrs/rscache/tables_dat/config_textures.h"
#include "osrs/rscache/tables_dat/pix32.h"
#include "osrs/rscache/tables_dat/pix8.h"
#include "osrs/rscache/tables_dat/pixfont.h"

enum GameCacheEventType
{
    GAMECACHE_EVENT_NONE = 0,
    GAMECACHE_EVENT_TEXTURE_ADDED = 1,
};

struct GameCacheEvent
{
    enum GameCacheEventType type;
    int texture_id;
};

struct GameCache
{
    /** Reftable: named sprite -> UIScene element id (no DashSprite* stored here). */
    struct DashMap* sprites_reftable;

    struct DashMap* map_terrains_hmap;
    struct DashMap* flotype_hmap;
    /** Reftable: texture id loaded into Scene2 (no DashTexture* stored here). */
    struct DashMap* textures_reftable;
    /** Reftable: font name -> UIScene font slot id only. */
    struct DashMap* fonts_reftable;
    struct DashMap* scenery_hmap;
    struct DashMap* models_hmap;

    struct DashMap* idk_models_hmap;
    struct DashMap* obj_models_hmap;
    struct DashMap* npc_models_hmap;

    struct DashMap* config_loc_hmap;
    /** Reftable: animframe id -> animbaseframes blob + frame index (no GameCacheAnimframe* stored). */
    struct DashMap* animframes_reftable;
    struct DashMap* animbaseframes_hmap;
    struct DashMap* sequences_hmap;
    struct DashMap* idk_hmap;
    struct DashMap* obj_hmap;
    struct DashMap* npc_hmap;
    struct DashMap* component_hmap;
    struct DashMap* spotanim_hmap;
    /** Reftable: sprite name -> UIScene element id. */
    struct DashMap* component_sprites_reftable;

    struct GameCacheEvent* eventbuffer;
    int eventbuffer_capacity;
    int eventbuffer_head;
    int eventbuffer_tail;
    int eventbuffer_count;
};

struct GameCache*
gamecache_new(void);

void
gamecache_free(struct GameCache* gamecache);

void
gamecache_clear_map_chunks(struct GameCache* gamecache);

void
gamecache_map_terrain_cache_clear(struct GameCache* gamecache);

void
gamecache_map_scenery_cache_clear(struct GameCache* gamecache);

void
gamecache_model_cache_clear(struct GameCache* gamecache);

void
gamecache_animbaseframes_cache_clear(struct GameCache* gamecache);

void
gamecache_scenery_config_clear(struct GameCache* gamecache);

void
gamecache_sequences_clear(struct GameCache* gamecache);

void
gamecache_floortypes_clear(struct GameCache* gamecache);

void
gamecache_objects_clear(struct GameCache* gamecache);

void
gamecache_component_cache_clear(struct GameCache* gamecache);

void
gamecache_reserve_hmap(struct DashMap* map, size_t min_count);

void
gamecache_clear(struct GameCache* gamecache);

void
gamecache_reset_uiscene_linked_reftables(struct GameCache* gamecache);

void
gamecache_add_flotype(struct GameCache* gamecache, int flotype_id, struct GameCacheOverlay* flotype);

struct GameCacheOverlay*
gamecache_get_flotype(struct GameCache* gamecache, int flotype_id);

struct DashMapIter*
gamecache_iter_new_flotypes(struct GameCache* gamecache);

struct GameCacheOverlay*
gamecache_iter_next_flotype(struct DashMapIter* iter);

void
gamecache_add_texture_ref(struct GameCache* gamecache, int texture_id);

bool
gamecache_has_texture_ref(struct GameCache* gamecache, int texture_id);

void
gamecache_add_font_ref(struct GameCache* gamecache, const char* font_name, int uiscene_font_id);

int
gamecache_get_font_ref_id(struct GameCache* gamecache, const char* font_name);

struct DashMapIter*
gamecache_iter_new_fonts_reftable(struct GameCache* gamecache);

bool
gamecache_iter_next_font_ref(
    struct DashMapIter* iter,
    char* out_name,
    int out_name_cap,
    int* out_uiscene_font_id);

void
gamecache_add_sprite_ref(struct GameCache* gamecache, const char* name, int uiscene_element_id);

int
gamecache_get_sprite_element_id(struct GameCache* gamecache, const char* name);

bool
gamecache_eventbuffer_is_empty(struct GameCache* gamecache);

bool
gamecache_eventbuffer_pop(struct GameCache* gamecache, struct GameCacheEvent* out_event);

void
gamecache_eventbuffer_clear(struct GameCache* gamecache);

void
gamecache_add_scenery(struct GameCache* gamecache, int mapx, int mapz, struct GameCacheMapLocs* locs);

struct GameCacheMapLocs*
gamecache_get_scenery(struct GameCache* gamecache, int mapx, int mapz);

struct GameCacheMapLocs*
gamecache_iter_next_scenery(struct DashMapIter* iter);

struct DashMapIter*
gamecache_iter_new_scenery(struct GameCache* gamecache);

void
gamecache_add_model(struct GameCache* gamecache, int model_id, struct GameCacheModel* model);

struct GameCacheModel*
gamecache_get_model(struct GameCache* gamecache, int model_id);

struct DashMapIter*
gamecache_iter_new_models(struct GameCache* gamecache);

struct GameCacheModel*
gamecache_iter_next_model(struct DashMapIter* iter);

void
gamecache_add_idk_model(struct GameCache* gamecache, int idk_id, struct GameCacheModel* model);

struct GameCacheModel*
gamecache_get_idk_model(struct GameCache* gamecache, int idk_id);

void
gamecache_add_obj_model(struct GameCache* gamecache, int obj_id, struct GameCacheModel* model);

struct GameCacheModel*
gamecache_get_obj_model(struct GameCache* gamecache, int obj_id);

void
gamecache_add_npc(struct GameCache* gamecache, int npc_id, struct GameCacheNpc* npc);

struct GameCacheNpc*
gamecache_get_npc(struct GameCache* gamecache, int npc_id);

struct DashMapIter*
gamecache_iter_new_npcs(struct GameCache* gamecache);

struct GameCacheNpc*
gamecache_iter_next_npc(struct DashMapIter* iter);

void
gamecache_add_npc_model(struct GameCache* gamecache, int npc_id, struct GameCacheModel* model);

struct GameCacheModel*
gamecache_get_npc_model(struct GameCache* gamecache, int npc_id);

void
gamecache_add_config_loc(
    struct GameCache* gamecache,
    int config_loc_id,
    struct GameCacheLoc* config_loc);

struct GameCacheLoc*
gamecache_iter_next_config_loc(struct DashMapIter* iter);

struct GameCacheLoc*
gamecache_get_config_loc(struct GameCache* gamecache, int config_loc_id);

struct DashMapIter*
gamecache_iter_new_config_locs(struct GameCache* gamecache);

void
gamecache_add_animframe_ref(
    struct GameCache* gamecache,
    int animframe_id,
    int animbaseframes_id,
    int frame_index);

struct GameCacheAnimframe*
gamecache_get_animframe(struct GameCache* gamecache, int animframe_id);

void
gamecache_add_animbaseframes(
    struct GameCache* gamecache,
    int animbaseframes_id,
    struct GameCacheAnimBaseFrames* animbaseframes);

struct GameCacheAnimBaseFrames*
gamecache_get_animbaseframes(struct GameCache* gamecache, int animbaseframes_id);

struct GameCacheAnimBaseFrames*
gamecache_iter_next_animbaseframes(struct DashMapIter* iter);

struct DashMapIter*
gamecache_iter_new_animbaseframes(struct GameCache* gamecache);

void
gamecache_add_sequence(struct GameCache* gamecache, int sequence_id, struct GameCacheSequence* sequence);

struct GameCacheSequence*
gamecache_get_sequence(struct GameCache* gamecache, int sequence_id);

struct GameCacheSequence*
gamecache_iter_next_sequence(struct DashMapIter* iter);

struct DashMapIter*
gamecache_iter_new_sequences(struct GameCache* gamecache);

void
gamecache_add_idk(struct GameCache* gamecache, int idk_id, struct GameCacheIdk* idk);

struct GameCacheIdk*
gamecache_get_idk(struct GameCache* gamecache, int idk_id);

struct GameCacheIdk*
gamecache_iter_next_idk(struct DashMapIter* iter);

struct DashMapIter*
gamecache_iter_new_idks(struct GameCache* gamecache);

void
gamecache_add_obj(struct GameCache* gamecache, int obj_id, struct GameCacheObj* obj);

struct GameCacheObj*
gamecache_get_obj(struct GameCache* gamecache, int obj_id);

struct GameCacheObj*
gamecache_iter_next_obj(struct DashMapIter* iter);

struct DashMapIter*
gamecache_iter_new_objs(struct GameCache* gamecache);

void
gamecache_add_map_terrain(
    struct GameCache* gamecache,
    int mapx,
    int mapz,
    struct GameCacheTerrainChunk* map_terrain);

struct GameCacheTerrainChunk*
gamecache_get_map_terrain(struct GameCache* gamecache, int mapx, int mapz);

void
gamecache_add_component(
    struct GameCache* gamecache,
    int component_id,
    struct GameCacheComponent* component);

struct GameCacheComponent*
gamecache_get_component(struct GameCache* gamecache, int component_id);

struct DashMapIter*
gamecache_component_iter_new(struct GameCache* gamecache);

struct GameCacheComponent*
gamecache_component_iter_next(struct DashMapIter* iter, int* id_out);

void
gamecache_add_component_sprite_ref(
    struct GameCache* gamecache,
    const char* sprite_name,
    int uiscene_element_id);

int
gamecache_get_component_sprite_element_id(struct GameCache* gamecache, const char* sprite_name);

void
gamecache_add_spotanim(struct GameCache* gamecache, int spotanim_id, struct GameCacheSpotAnim* spotanim);

struct GameCacheSpotAnim*
gamecache_get_spotanim(struct GameCache* gamecache, int spotanim_id);

#endif
