#include "dat2_config_inv.h"

#include <assert.h>
#include <stdlib.h>

bool
RSCache_Dat2ConfigInvDecodeOp(
    struct RSCache_Dat2ConfigInv* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags)
{
    (void)flags; /* inv has no era-dependent opcode. */
    switch( opcode )
    {
    case 2:
        entry->size = g2(buffer);
        return true;
    case 249:
        RSCache_BufferReadParams(buffer, &entry->params);
        return true;
    default:
        /* Declining stops the stream rather than guessing a width — see the
         * header. The caller's short `_consumed` is the signal. */
        return false;
    }
}

void
RSCache_Dat2ConfigInvDecode(
    struct RSCache_Dat2ConfigInv* entry,
    struct RSCache_Buffer* buffer)
{
    assert(entry);
    assert(buffer);

    for( ;; )
    {
        if( buffer->position >= buffer->size )
            break;

        int opcode = g1(buffer);
        if( opcode == 0 )
            break;

        if( !RSCache_Dat2ConfigInvDecodeOp(entry, opcode, buffer, 0) )
            break;
    }
}

void
RSCache_Dat2ConfigInvDecodeInplace(
    struct RSCache_Dat2ConfigInv* entry,
    const void* data,
    int data_size)
{
    assert(entry);
    if( !data || data_size <= 0 )
        return;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    RSCache_Dat2ConfigInvDecode(entry, &buffer);
    entry->_consumed = (int)buffer.position;
}

void
RSCache_Dat2ConfigInvFreeInplace(struct RSCache_Dat2ConfigInv* entry)
{
    int i;

    if( !entry )
        return;
    for( i = 0; i < entry->params.count; i++ )
        free(entry->params.values[i]);
    free(entry->params.keys);
    free(entry->params.values);
    free(entry->params.kinds);
    entry->params.keys = NULL;
    entry->params.values = NULL;
    entry->params.kinds = NULL;
    entry->params.count = 0;
    entry->params.capacity = 0;
}

uint32_t
RSCache_Dat2ConfigInvEncodeBound(const struct RSCache_Dat2ConfigInv* entry)
{
    /* Size opcode (1+2), params opcode (1) + its payload, terminator (1). */
    if( !entry )
        return 5u;
    return 5u + RSCache_BufferParamsBound(&entry->params);
}

uint32_t
RSCache_Dat2ConfigInvEncode(
    const struct RSCache_Dat2ConfigInv* entry,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !entry || !out )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* Emitted when non-zero. Exact for the corpus, where every record carries a
     * non-zero size; a zero-size record would re-encode as the bare terminator with
     * the same meaning. */
    if( entry->size != 0 )
    {
        p1(&buffer, 2);
        p2(&buffer, entry->size);
    }

    if( entry->params.count > 0 )
    {
        p1(&buffer, 249);
        pparams(&buffer, &entry->params);
    }

    p1(&buffer, 0);
    return buffer.position;
}
