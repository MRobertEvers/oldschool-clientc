#include "game/entity_info_scratch.h"
#include "mem/torirs_mem.h"
#include "streams/rsprot_info.h"

#include <assert.h>

static struct ToriMem_Group*
slot_group(struct EntityInfoScratchSlot const* slot)
{
    return slot->group ? slot->group : ToriMem_Session();
}

void
EntityInfoScratch_Borrow(
    struct EntityInfoScratchSlot* slot,
    struct RsprotInfoList* list)
{
    struct RsprotInfoOp* ops;
    uint8_t* bytes;
    int ops_cap = ENTITY_INFO_SCRATCH_OPS_MAX;
    int bytes_cap = ENTITY_INFO_SCRATCH_BYTES_MAX;

    assert(slot);
    assert(list);
    if( slot->busy )
    {
        ops = ToriMem_Calloc(slot_group(slot), (size_t)ops_cap, sizeof(*ops));
        bytes = ToriMem_Alloc(slot_group(slot), (size_t)bytes_cap);
    }
    else
    {
        if( !slot->ops )
        {
            slot->ops = ToriMem_Calloc(slot_group(slot), (size_t)ops_cap, sizeof(*slot->ops));
            slot->bytes = ToriMem_Alloc(slot_group(slot), (size_t)bytes_cap);
            slot->ops_cap = ops_cap;
            slot->bytes_cap = bytes_cap;
        }
        ops = slot->ops;
        bytes = slot->bytes;
        ops_cap = slot->ops_cap;
        bytes_cap = slot->bytes_cap;
        slot->busy = 1;
    }
    assert(ops);
    assert(bytes);
    rsprot_info_list_init(list, ops, ops_cap, bytes, bytes_cap);
}

/* `list` overflowed: twice the ops and twice the bytes, empty. The kept list
 * stays the slot's (at its new size); a fallback stays a fallback. */
static void
grow(
    struct EntityInfoScratchSlot* slot,
    struct RsprotInfoList* list)
{
    struct ToriMem_Group* group = slot_group(slot);
    int const ops_cap = list->cap * 2;
    int const bytes_cap = list->bytes_cap * 2;
    int const kept = list->ops == slot->ops;
    struct RsprotInfoOp* ops;
    uint8_t* bytes;

    assert(list->cap > 0);
    assert(list->bytes_cap > 0);
    /* A list is bounded by its packet (at most 65,535 bytes): doubling from
     * the defaults reaches any it can need long before this. */
    assert(ops_cap <= ENTITY_INFO_SCRATCH_OPS_MAX * 1024);
    assert(bytes_cap <= ENTITY_INFO_SCRATCH_BYTES_MAX * 1024);
    ToriMem_Free(group, list->ops);
    ToriMem_Free(group, list->bytes);
    ops = ToriMem_Calloc(group, (size_t)ops_cap, sizeof(*ops));
    assert(ops);
    bytes = ToriMem_Alloc(group, (size_t)bytes_cap);
    assert(bytes);
    if( kept )
    {
        slot->ops = ops;
        slot->bytes = bytes;
        slot->ops_cap = ops_cap;
        slot->bytes_cap = bytes_cap;
    }
    rsprot_info_list_init(list, ops, ops_cap, bytes, bytes_cap);
}

int
EntityInfoScratch_Read(
    struct EntityInfoScratchSlot* slot,
    struct RsprotInfoList* list,
    struct EntityInfoReader const* reader,
    uint8_t const* data,
    int len)
{
    int stateful;
    int count;

    assert(slot);
    assert(list);
    assert(list->ops);
    assert(reader);
    /* One reader, of either shape. */
    if( reader->npc_read )
        assert(!reader->read);
    else
        assert(reader->read);
    stateful = reader->state_size != NULL;
    if( stateful )
    {
        size_t const size = reader->state_size();

        assert(reader->state_save);
        assert(reader->state_load);
        assert(size > 0);
        if( slot->state_size != size )
        {
            ToriMem_Free(slot_group(slot), slot->state);
            slot->state = ToriMem_Alloc(slot_group(slot), size);
            assert(slot->state);
            slot->state_size = size;
        }
        reader->state_save(slot->state);
    }
    for( ;; )
    {
        count = reader->npc_read
                    ? reader->npc_read(data, len, reader->lengths, reader->lengths_ctx, list)
                    : reader->read(data, len, list);
        if( !list->overflow )
            return count;
        grow(slot, list);
        /* The read moved the stream on as if it had applied the packet: back
         * to where it stood before it, for the read again. */
        if( stateful )
            reader->state_load(slot->state);
    }
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
        ToriMem_Free(slot_group(slot), list->ops);
        ToriMem_Free(slot_group(slot), list->bytes);
    }
    else
        slot->busy = 0;
    list->ops = NULL;
}

static void
slot_free(struct EntityInfoScratchSlot* slot)
{
    ToriMem_Free(slot_group(slot), slot->ops);
    ToriMem_Free(slot_group(slot), slot->bytes);
    ToriMem_Free(slot_group(slot), slot->state);
    slot->ops = NULL;
    slot->bytes = NULL;
    slot->state = NULL;
    slot->ops_cap = 0;
    slot->bytes_cap = 0;
    slot->state_size = 0;
    slot->busy = 0;
}

void
EntityInfoScratch_Free(struct EntityInfoScratch* scratch)
{
    if( !scratch )
        return;
    slot_free(&scratch->npc);
    slot_free(&scratch->player);
}
