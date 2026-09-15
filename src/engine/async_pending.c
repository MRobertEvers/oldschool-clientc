#include "engine/async_pending.h"

#include <assert.h>

bool
AsyncPendingTextures_Has(
    struct AsyncPendingTextures const* pending,
    int texture_id)
{
    assert(pending);
    for( int i = 0; i < pending->count; i++ )
        if( pending->ids[i] == texture_id )
            return true;
    return false;
}

bool
AsyncPendingTextures_Add(
    struct AsyncPendingTextures* pending,
    int texture_id)
{
    assert(pending);
    if( AsyncPendingTextures_Has(pending, texture_id) )
        return false;
    if( pending->count >= ASYNC_PENDING_TEXTURE_MAX )
        return false;
    pending->ids[pending->count++] = texture_id;
    return true;
}

void
AsyncPendingTextures_Keep(
    struct AsyncPendingTextures* pending,
    int kept)
{
    assert(pending);
    assert(kept >= 0);
    assert(kept <= pending->count);
    pending->count = kept;
}

bool
AsyncPendingSeqBinds_Add(
    struct AsyncPendingSeqBinds* pending,
    int element_id,
    int seq_id,
    int start_cycle)
{
    assert(pending);
    if( pending->count >= ASYNC_PENDING_SEQ_BIND_MAX )
        return false;
    pending->items[pending->count].element_id = element_id;
    pending->items[pending->count].seq_id = seq_id;
    pending->items[pending->count].start_cycle = start_cycle;
    pending->count++;
    return true;
}

void
AsyncPendingSeqBinds_DropElement(
    struct AsyncPendingSeqBinds* pending,
    int element_id)
{
    int kept = 0;

    assert(pending);
    for( int i = 0; i < pending->count; i++ )
    {
        if( pending->items[i].element_id == element_id )
            continue;
        pending->items[kept++] = pending->items[i];
    }
    pending->count = kept;
}

void
AsyncPendingSeqBinds_Keep(
    struct AsyncPendingSeqBinds* pending,
    int kept)
{
    assert(pending);
    assert(kept >= 0);
    assert(kept <= pending->count);
    pending->count = kept;
}
