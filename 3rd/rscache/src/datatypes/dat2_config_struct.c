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
        if( opcode == 249 )
            RSCache_BufferReadParams(&buf, &entry->params);
    }
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
