#ifndef DAT1_BUILDCACHE_H
#define DAT1_BUILDCACHE_H

#include "engine/cache_provider.h"

#include <rscache.h>
#include <stdbool.h>
#include <stddef.h>

/* Synthetic provider ids for name-keyed dat1 media sprites. Above any plausible
 * dat2-style numeric archive id and font slot; monotonic per buildcache. */
#define DAT1_SPRITE_ID_BASE 0x100000

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
    /** Decoded INTERFACES jagfile "data" file; loaded once by pack-load tasks. */
    struct RSCache_Dat1ConfigComponentList* interfaces_list;
    /** "loc.idx" + "loc.dat" paired into id -> offset lookups. Held here
     * because building it copies the whole loc.dat, which must not happen once
     * per requested loc id. */
    struct RSCache_FileListDatIndexed* loc_index;
    /** Decoded TEXTURES jagfile; every dat1 texture comes out of this one. */
    struct RSCache_FileListDat* textures_jagfile;
    /** Whole decoded "seq.dat" table (entries are sequentially addressable). */
    struct RSCache_Dat1ConfigSeqList* seq_list;
    /** ANIMATIONS-table archives, keyed by archive id: one archive holds a
     * frame set plus its shared base, and several sequences reuse the same
     * archive. */
    struct HMap* animbaseframes_hmap;
    int next_sprite_id;
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
dat1_buildcache_set_textures_jagfile(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCache_FileListDat* textures_jagfile);

struct RSCache_FileListDat*
dat1_buildcache_get_textures_jagfile(struct Dat1BuildCache* dat1_buildcache);

/** Build (once) the loc.idx/loc.dat pair from the config jagfile. Returns NULL
 * when either file is missing. */
struct RSCache_FileListDatIndexed*
dat1_buildcache_get_loc_index(struct Dat1BuildCache* dat1_buildcache);

/** Decode (once) the whole seq.dat table from the config jagfile. NULL when
 * seq.dat is missing. */
struct RSCache_Dat1ConfigSeqList*
dat1_buildcache_get_seq_list(struct Dat1BuildCache* dat1_buildcache);

void
dat1_buildcache_animbaseframes_add(
    struct Dat1BuildCache* dat1_buildcache,
    int animbaseframes_id,
    struct RSCache_Dat1AnimBaseFrames* animbaseframes);

struct RSCache_Dat1AnimBaseFrames*
dat1_buildcache_animbaseframes_get(
    struct Dat1BuildCache* dat1_buildcache,
    int animbaseframes_id);

void
dat1_buildcache_animbaseframes_cleanup(struct Dat1BuildCache* dat1_buildcache);

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

void
dat1_buildcache_set_interfaces_list(
    struct Dat1BuildCache* dat1_buildcache,
    struct RSCache_Dat1ConfigComponentList* interfaces_list);

struct RSCache_Dat1ConfigComponentList*
dat1_buildcache_get_interfaces_list(struct Dat1BuildCache* dat1_buildcache);

int
dat1_buildcache_sprite_id_alloc(struct Dat1BuildCache* dat1_buildcache);

/**
 * Resolve a dat1 sprite name ref ("name" / "name,idx") to a provider sprite id,
 * decoding it from the media jagfile and registering name -> id on first use.
 * Returns -1 when the media jagfile is absent or the ref fails to decode.
 */
int
dat1_buildcache_sprite_ref_acquire(
    struct Dat1BuildCache* dat1_buildcache,
    char const* ref);

#endif
