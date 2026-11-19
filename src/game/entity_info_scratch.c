#include "game/entity_info_scratch.h"
#include "mem/torirs_mem.h"
#include "streams/rsprot_info.h"

#include <assert.h>

void
EntityInfoScratch_Borrow(
    struct EntityInfoScratchSlot* slot,
    struct RsprotInfoList* list)
{
    struct RsprotInfoOp* ops;
    uint8_t* bytes;

    assert(slot);
    assert(list);
    if( slot->busy )
    {
        ops = ToriMem_Calloc(ToriMem_Session(), ENTITY_INFO_SCRATCH_OPS_MAX, sizeof(*ops));
        bytes = ToriMem_Alloc(ToriMem_Session(), ENTITY_INFO_SCRATCH_BYTES_MAX);
    }
    else
    {
        if( !slot->ops )
        {
            slot->ops =
                ToriMem_Calloc(ToriMem_Session(), ENTITY_INFO_SCRATCH_OPS_MAX, sizeof(*slot->ops));
            slot->bytes = ToriMem_Alloc(ToriMem_Session(), ENTITY_INFO_SCRATCH_BYTES_MAX);
        }
        ops = slot->ops;
        bytes = slot->bytes;
        slot->busy = 1;
    }
    assert(ops);
    assert(bytes);
    rsprot_info_list_init(
        list, ops, ENTITY_INFO_SCRATCH_OPS_MAX, bytes, ENTITY_INFO_SCRATCH_BYTES_MAX);
}

void
EntityInfoScratch_Release(
    struct EntityInfoScratchSlot* slot,
    struct RsprotInfoList* list)
{
    assert(slot);
    if( !list )
        return;
    if( !list->ops )
        return;
    if( list->ops != slot->ops )
    {
        ToriMem_Free(ToriMem_Session(), list->ops);
        ToriMem_Free(ToriMem_Session(), list->bytes);
    }
    else
        slot->busy = 0;
    list->ops = NULL;
}

void
EntityInfoScratch_Free(struct EntityInfoScratch* scratch)
{
    if( !scratch )
        return;
    ToriMem_Free(ToriMem_Session(), scratch->npc.ops);
    ToriMem_Free(ToriMem_Session(), scratch->player.ops);
    ToriMem_Free(ToriMem_Session(), scratch->npc.bytes);
    ToriMem_Free(ToriMem_Session(), scratch->player.bytes);
    scratch->npc.ops = NULL;
    scratch->player.ops = NULL;
    scratch->npc.bytes = NULL;
    scratch->player.bytes = NULL;
    scratch->npc.busy = 0;
    scratch->player.busy = 0;
}
