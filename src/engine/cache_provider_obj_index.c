/* The objtypes the provider holds, by id: see cache_provider_obj_index.h. */
#include "engine/cache_provider_obj_index.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <string.h>

void
CacheProvider_ObjIndexPut(
    struct CacheProvider_ObjIndex* index,
    int obj_id,
    struct ToriRS_Objtype* objtype)
{
    assert(index);
    assert(objtype);
    if( !CacheProvider_ObjIndexCovers(obj_id) )
        return;
    if( obj_id >= index->cap )
    {
        int32_t cap = index->cap > 0 ? index->cap : 4096;
        while( cap <= obj_id )
            cap *= 2;
        struct ToriRS_Objtype** grown = ToriMem_Realloc(
            ToriMem_Assets(), index->by_id, (size_t)cap * sizeof(*grown));
        assert(grown);
        memset(grown + index->cap, 0, (size_t)(cap - index->cap) * sizeof(*grown));
        index->by_id = grown;
        index->cap = cap;
    }
    index->by_id[obj_id] = objtype;
}

void
CacheProvider_ObjIndexDrop(
    struct CacheProvider_ObjIndex* index,
    int obj_id)
{
    assert(index);
    if( (uint32_t)obj_id < (uint32_t)index->cap )
        index->by_id[obj_id] = NULL;
}

void
CacheProvider_ObjIndexReset(struct CacheProvider_ObjIndex* index)
{
    assert(index);
    if( index->cap > 0 )
        memset(index->by_id, 0, (size_t)index->cap * sizeof(*index->by_id));
}

void
CacheProvider_ObjIndexFree(struct CacheProvider_ObjIndex* index)
{
    if( !index )
        return;
    ToriMem_Free(ToriMem_Assets(), index->by_id);
    index->by_id = NULL;
    index->cap = 0;
}
