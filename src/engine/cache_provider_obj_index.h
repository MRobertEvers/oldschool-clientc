#ifndef ENGINE_CACHE_PROVIDER_OBJ_INDEX_H
#define ENGINE_CACHE_PROVIDER_OBJ_INDEX_H

/*
 * The objtypes the provider holds, by id, without hashing.
 *
 * WHAT IT IS. A flat array beside CacheProvider::objtype_cache: slot `id`
 * holds the resident ToriRS_Objtype for that id, NULL when none is. It is
 * only an index -- the map still owns every objtype, and is still what is
 * iterated (the OC_FIND name search, eviction, cleanup).
 *
 * WHAT IT REPLACES. CacheProvider_ObjtypeGet's hmap_search. One bank slot
 * asks the provider about an obj five times (cc_setobject's readiness test,
 * its icon, oc_name, oc_unplaceholder), about 6,000 lookups an open, and
 * hashing the key and probing the map was 42% of CC_SETOBJECT and 97% of
 * OC_NAME. An index lookup is a bounds test and a load.
 *
 * WHAT KEEPS IT RIGHT. Every write of the map's objtype pointer writes this
 * too, in cache_provider.c: CacheProvider_ObjtypeAdd (insert and replace),
 * CacheProvider_ObjtypeReplace (via Add), CacheProvider_RecordRetire
 * (CATALOG_OBJ removes), cache_provider_evict_objtypes and
 * CacheProvider_ObjtypesCleanup (every objtype goes), and
 * CacheProvider_FreeEngineCaches (freed). Ids outside [0,
 * CACHE_PROVIDER_OBJ_INDEX_MAX) are not indexed and the provider answers them
 * from the map, as before.
 *
 * Grown geometrically on the first insert past its end; the obj group of a
 * rev-242 cache numbers about 30,000 ids, so a session holds one 256 KB array
 * at most (ToriMem_Assets).
 */

#include <stddef.h>
#include <stdint.h>

struct ToriRS_Objtype;

/* Ids at or above this stay map-only. Generous: the obj group's file ids are
 * u16 on the wire. */
#define CACHE_PROVIDER_OBJ_INDEX_MAX (1 << 20)

struct CacheProvider_ObjIndex
{
    struct ToriRS_Objtype** by_id;
    int32_t cap;
};

/* Whether `obj_id` is one the index answers for (resident or not). */
static inline int
CacheProvider_ObjIndexCovers(int obj_id)
{
    return obj_id >= 0 && obj_id < CACHE_PROVIDER_OBJ_INDEX_MAX;
}

/* The resident objtype for an id the index covers, NULL when none is. */
static inline struct ToriRS_Objtype*
CacheProvider_ObjIndexGet(
    struct CacheProvider_ObjIndex const* index,
    int obj_id)
{
    if( (uint32_t)obj_id >= (uint32_t)index->cap )
        return NULL;
    return index->by_id[obj_id];
}

/* Record `objtype` (non-NULL) as the resident one for `obj_id`. No-op for an
 * id the index does not cover. */
void
CacheProvider_ObjIndexPut(
    struct CacheProvider_ObjIndex* index,
    int obj_id,
    struct ToriRS_Objtype* objtype);

/* `obj_id` holds nothing now. */
void
CacheProvider_ObjIndexDrop(
    struct CacheProvider_ObjIndex* index,
    int obj_id);

/* Every id holds nothing now; the array is kept. */
void
CacheProvider_ObjIndexReset(struct CacheProvider_ObjIndex* index);

/* Release the array. Accepts an index that was never grown. */
void
CacheProvider_ObjIndexFree(struct CacheProvider_ObjIndex* index);

#endif
