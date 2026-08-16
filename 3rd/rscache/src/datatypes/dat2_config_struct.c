#include "dat2_config_struct.h"

#include <stdlib.h>
#include <string.h>

void
RSCache_Dat2ConfigStructDecodeInplace(
    struct RSCache_Dat2ConfigStruct* entry,
    const void* data,
    int data_size)
{
    struct RSCache_Buffer buf;

    if( !entry )
        return;
    memset(&entry->params, 0, sizeof(entry->params));
    if( !data || data_size <= 0 || (data_size == 1 && ((const uint8_t*)data)[0] == 0) )
        return;

    RSCache_BufferInit(&buf, (uint8_t*)data, (uint32_t)data_size);

    for( ;; )
    {
        int opcode = g1(&buf);
        if( opcode == 0 )
            break;
        if( !RSCache_Dat2ConfigStructDecodeOp(entry, opcode, &buf, 0) )
            break;
    }
}

/* Declared in the header; defined here beside the loop it was lifted out of. */
bool
RSCache_Dat2ConfigStructDecodeOp(
    struct RSCache_Dat2ConfigStruct* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags)
{
    (void)flags; /* No era-dependent opcode in this type. */
    if( opcode == 249 )
    {
        RSCache_BufferReadParams(buffer, &entry->params);
        return true;
    }
    /*
     * Stop, where this decoder used to ignore the opcode and carry on. An unknown
     * opcode's payload width is unknown, so continuing reads a payload byte as the
     * next opcode and decodes the remainder from a false position — the reason
     * every other type here stops. Verified unobservable on osrs239: all 3,988
     * struct records carry only opcode 249.
     */
    return false;
}

uint32_t
RSCache_Dat2ConfigStructEncode(
    const struct RSCache_Dat2ConfigStruct* entry,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !entry || !out )
        return 0;

    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, out, out_capacity);

    /* A struct record is nothing but a param map, so an empty one is a bare
     * terminator — which is also what the decoder treats as "no record". */
    if( entry->params.count > 0 )
    {
        p1(&buf, 249);
        pparams(&buf, &entry->params);
    }

    p1(&buf, 0);
    return buf.position;
}

void
RSCache_Dat2ConfigStructFreeInplace(struct RSCache_Dat2ConfigStruct* entry)
{
    int i;

    if( !entry )
        return;
    if( entry->params.values )
    {
        for( i = 0; i < entry->params.count; i++ )
            free(entry->params.values[i]);
        free(entry->params.values);
        entry->params.values = NULL;
    }
    free(entry->params.keys);
    entry->params.keys = NULL;
    free(entry->params.kinds);
    entry->params.kinds = NULL;
    entry->params.count = 0;
    entry->params.capacity = 0;
}

void
RSCache_Dat2ConfigStructFree(struct RSCache_Dat2ConfigStruct* entry)
{
    if( !entry )
        return;
    RSCache_Dat2ConfigStructFreeInplace(entry);
    free(entry);
}

uint32_t
RSCache_Dat2ConfigStructEncodeBound(const struct RSCache_Dat2ConfigStruct* entry)
{
    /* Params opcode (1) + its payload, then the terminator (1). */
    if( !entry )
        return 2u;
    return 2u + RSCache_BufferParamsBound(&entry->params);
}
