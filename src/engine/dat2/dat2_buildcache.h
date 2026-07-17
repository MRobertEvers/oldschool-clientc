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
    struct HMap* map_terrain_hmap;
    struct HMap* map_scenery_hmap;
    struct HMap* clientscripts_hmap;
    struct RSCache_ReferenceTable* reference_tables[RSCACHE_DAT2_DISK_TABLE_COUNT];
    size_t map_buffer_bytes;
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
