#include "dat2a_config_struct.h"

#include "osrs/rscache/shared/shared_rs_buffer.h"

#include <stdlib.h>
#include <string.h>

void
RSCacheDat2A_ConfigStructDecodeInplace(
    struct RSCacheDat2A_ConfigStruct* entry,
    const void* data,
    int data_size)
{
    if( !entry )
        return;
    memset(&entry->params, 0, sizeof(entry->params));
    if( !data || data_size <= 0 || (data_size == 1 && ((const uint8_t*)data)[0] == 0) )
        return;

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, (uint32_t)data_size);

    for( ;; )
    {
        int opcode = g1(&buf);
        if( opcode == 0 )
            break;
        if( opcode == 249 )
            RSCacheShared_RSBufferReadParams(&buf, &entry->params);
    }
}

void
RSCacheDat2A_ConfigStructFree(struct RSCacheDat2A_ConfigStruct* entry)
{
    int i;

    if( !entry )
        return;
    for( i = 0; i < entry->params.count; i++ )
        free(entry->params.values[i]);
    free(entry->params.keys);
    free(entry->params.values);
    free(entry->params.is_string);
    free(entry);
}
