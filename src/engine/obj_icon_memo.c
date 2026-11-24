/* Per-item "icon ready" answers: see obj_icon_memo.h. */
#include "engine/obj_icon_memo.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <string.h>

void
ObjIconMemo_Put(
    struct ObjIconMemo* memo,
    int obj_id,
    int count,
    uint64_t revision,
    int32_t scene_id)
{
    struct ObjIconMemoEntry* e;

    assert(memo);
    assert(scene_id > 0);
    if( obj_id < 0 || obj_id >= OBJ_ICON_MEMO_MAX_ID )
        return;
    if( obj_id >= memo->cap )
    {
        int32_t cap = memo->cap > 0 ? memo->cap : 4096;
        while( cap <= obj_id )
            cap *= 2;
        struct ObjIconMemoEntry* grown =
            ToriMem_Realloc(ToriMem_Ui(), memo->by_id, (size_t)cap * sizeof(*grown));
        assert(grown);
        memset(grown + memo->cap, 0, (size_t)(cap - memo->cap) * sizeof(*grown));
        memo->by_id = grown;
        memo->cap = cap;
    }
    e = &memo->by_id[obj_id];
    e->scene_id = scene_id;
    e->count = count;
    e->revision = revision;
}

void
ObjIconMemo_Free(struct ObjIconMemo* memo)
{
    if( !memo )
        return;
    ToriMem_Free(ToriMem_Ui(), memo->by_id);
    memo->by_id = NULL;
    memo->cap = 0;
}
