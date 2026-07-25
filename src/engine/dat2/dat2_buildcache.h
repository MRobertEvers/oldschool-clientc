#ifndef DAT2_BUILDCACHE_H
#define DAT2_BUILDCACHE_H

#include "engine/cache_provider.h"

#include <rscache.h>
#include <stdbool.h>
#include <stddef.h>

struct Dat2BuildCache
{
    struct CacheProvider base;
    struct HMap* models_hmap;
    struct HMap* componentpacks_hmap;
    struct HMap* object_hmap;
    struct HMap* npctype_hmap;
    struct HMap* identkit_hmap;
    struct HMap* map_terrain_hmap;
    struct HMap* map_scenery_hmap;
    struct HMap* loc_hmap;
    struct HMap* underlay_hmap;
    struct HMap* overlay_hmap;
    struct HMap* texture_hmap;
    struct HMap* clientscripts_hmap;
    struct RSCache_ReferenceTable* reference_tables[RSCACHE_DAT2_DISK_TABLE_COUNT];
    size_t map_buffer_bytes;

    /*
     * Procedural texture state (RS2 / 643 only).
     *
     * Which texture system a cache uses is decided by whether it ships a materials table, not
     * by its revision (rs-map-viewer gates on exactly that). Rather than ask the disk layer
     * synchronously — which the async IO design does not offer — the texture task probes table
     * 26 once on first texture load and memoises the answer here. `proctex_mode` is tri-state
     * so "not probed yet" is distinguishable from "probed, absent".
     */
    int proctex_mode; /* enum Dat2ProcTexMode */
    struct RSCache_Dat2MaterialTable* materials;
};

/** Tri-state for `Dat2BuildCache.proctex_mode`. */
enum Dat2ProcTexMode
{
    DAT2_PROCTEX_UNPROBED = 0,
    /** Sprite-backed textures (OSRS, and RS2 before 474). */
    DAT2_PROCTEX_SPRITE,
    /** Procedural materials (RS2 474+ with a table 26). */
    DAT2_PROCTEX_PROCEDURAL,
};

struct Dat2BuildCache*
dat2_buildcache_new(void);

void
dat2_buildcache_free(struct Dat2BuildCache* dat2_buildcache);

struct CacheProvider*
dat2_buildcache_as_provider(struct Dat2BuildCache* dat2_buildcache);

size_t
dat2_buildcache_bytes_total(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_model_add(
    struct Dat2BuildCache* dat2_buildcache,
    int model_id,
    struct RSCache_Model* model);

struct RSCache_Model*
dat2_buildcache_model_get(
    struct Dat2BuildCache* dat2_buildcache,
    int model_id);

void
dat2_buildcache_model_remove(
    struct Dat2BuildCache* dat2_buildcache,
    int model_id);

void
dat2_buildcache_models_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_map_terrain_add(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id,
    struct RSCache_MapTerrain* terrain);

struct RSCache_MapTerrain*
dat2_buildcache_map_terrain_get(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id);

bool
dat2_buildcache_map_terrain_has(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id);

void
dat2_buildcache_map_terrain_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_map_scenery_add(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id,
    struct RSCache_MapLocs* locs);

struct RSCache_MapLocs*
dat2_buildcache_map_scenery_get(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id);

bool
dat2_buildcache_map_scenery_has(
    struct Dat2BuildCache* dat2_buildcache,
    int map_id);

void
dat2_buildcache_map_scenery_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_componentpack_add(
    struct Dat2BuildCache* dat2_buildcache,
    int iface_id,
    struct RSCache_Dat2ComponentPack* pack);

struct RSCache_Dat2ComponentPack*
dat2_buildcache_componentpack_get(
    struct Dat2BuildCache* dat2_buildcache,
    int iface_id);

bool
dat2_buildcache_componentpack_has(
    struct Dat2BuildCache* dat2_buildcache,
    int iface_id);

void
dat2_buildcache_componentpacks_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_object_add(
    struct Dat2BuildCache* dat2_buildcache,
    int obj_id,
    struct RSCache_Dat2ConfigObj* object);

struct RSCache_Dat2ConfigObj*
dat2_buildcache_object_get(
    struct Dat2BuildCache* dat2_buildcache,
    int obj_id);

bool
dat2_buildcache_object_has(
    struct Dat2BuildCache* dat2_buildcache,
    int obj_id);

void
dat2_buildcache_objects_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_objects_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count);

void
dat2_buildcache_npctype_add(
    struct Dat2BuildCache* dat2_buildcache,
    int npc_id,
    struct RSCache_Dat2ConfigNpc* npc);

struct RSCache_Dat2ConfigNpc*
dat2_buildcache_npctype_get(
    struct Dat2BuildCache* dat2_buildcache,
    int npc_id);

bool
dat2_buildcache_npctype_has(
    struct Dat2BuildCache* dat2_buildcache,
    int npc_id);

void
dat2_buildcache_npctypes_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_npctypes_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count);

void
dat2_buildcache_identkit_add(
    struct Dat2BuildCache* dat2_buildcache,
    int idk_id,
    struct RSCache_Dat2ConfigIdk* idk);

struct RSCache_Dat2ConfigIdk*
dat2_buildcache_identkit_get(
    struct Dat2BuildCache* dat2_buildcache,
    int idk_id);

bool
dat2_buildcache_identkit_has(
    struct Dat2BuildCache* dat2_buildcache,
    int idk_id);

void
dat2_buildcache_identkits_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_identkits_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count);

void
dat2_buildcache_loc_add(
    struct Dat2BuildCache* dat2_buildcache,
    int loc_id,
    struct RSCache_Dat2ConfigLoc* loc);

struct RSCache_Dat2ConfigLoc*
dat2_buildcache_loc_get(
    struct Dat2BuildCache* dat2_buildcache,
    int loc_id);

bool
dat2_buildcache_loc_has(
    struct Dat2BuildCache* dat2_buildcache,
    int loc_id);

void
dat2_buildcache_locs_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_locs_init_from_archive_based(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count,
    int base_id);

void
dat2_buildcache_locs_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count);

void
dat2_buildcache_underlay_add(
    struct Dat2BuildCache* dat2_buildcache,
    int underlay_id,
    struct RSCache_Dat2ConfigUnderlay* underlay);

struct RSCache_Dat2ConfigUnderlay*
dat2_buildcache_underlay_get(
    struct Dat2BuildCache* dat2_buildcache,
    int underlay_id);

bool
dat2_buildcache_underlay_has(
    struct Dat2BuildCache* dat2_buildcache,
    int underlay_id);

void
dat2_buildcache_underlays_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_underlays_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count);

void
dat2_buildcache_overlay_add(
    struct Dat2BuildCache* dat2_buildcache,
    int overlay_id,
    struct RSCache_Dat2ConfigOverlay* overlay);

struct RSCache_Dat2ConfigOverlay*
dat2_buildcache_overlay_get(
    struct Dat2BuildCache* dat2_buildcache,
    int overlay_id);

bool
dat2_buildcache_overlay_has(
    struct Dat2BuildCache* dat2_buildcache,
    int overlay_id);

void
dat2_buildcache_overlays_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_overlays_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count);

void
dat2_buildcache_texture_add(
    struct Dat2BuildCache* dat2_buildcache,
    int texture_id,
    struct RSCache_Dat2Texture* texture);

struct RSCache_Dat2Texture*
dat2_buildcache_texture_get(
    struct Dat2BuildCache* dat2_buildcache,
    int texture_id);

bool
dat2_buildcache_texture_has(
    struct Dat2BuildCache* dat2_buildcache,
    int texture_id);

void
dat2_buildcache_textures_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_textures_init_from_archive(
    struct Dat2BuildCache* dat2_buildcache,
    struct RSCache_Dat2DiskArchive* archive,
    const int* wanted_ids,
    int wanted_count);

void
dat2_buildcache_clientscript_add(
    struct Dat2BuildCache* dat2_buildcache,
    int script_id,
    struct RSCache_ClientScript* script);

struct RSCache_ClientScript*
dat2_buildcache_clientscript_get(
    struct Dat2BuildCache* dat2_buildcache,
    int script_id);

bool
dat2_buildcache_clientscript_has(
    struct Dat2BuildCache* dat2_buildcache,
    int script_id);

void
dat2_buildcache_clientscripts_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_reference_table_add(
    struct Dat2BuildCache* dat2_buildcache,
    int table_id,
    struct RSCache_ReferenceTable* table);

struct RSCache_ReferenceTable*
dat2_buildcache_reference_table_get(
    struct Dat2BuildCache* dat2_buildcache,
    int table_id);

bool
dat2_buildcache_reference_table_has(
    struct Dat2BuildCache* dat2_buildcache,
    int table_id);

void
dat2_buildcache_reference_tables_cleanup(struct Dat2BuildCache* dat2_buildcache);

void
dat2_buildcache_prune(struct Dat2BuildCache* dat2_buildcache);

#endif
