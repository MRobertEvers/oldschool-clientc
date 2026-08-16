#include "dat2a_config_param.h"

#include "osrs/rscache/shared/shared_rs_buffer.h"

#include <stdlib.h>
#include <string.h>

static char
param_type_id_to_char(int id)
{
    if( id == 36 )
        return 's';
    if( id == 0 )
        return 'i';
    return 'i';
}

void
RSCacheDat2A_ConfigParamDecodeInplace(
    struct RSCacheDat2A_ConfigParam* param,
    const void* data,
    int data_size)
{
    if( !param || !data || data_size <= 0 || (data_size == 1 && ((const uint8_t*)data)[0] == 0) )
        return;

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, (uint32_t)data_size);

    for( ;; )
    {
        int opcode = g1(&buf);
        if( opcode == 0 )
            break;
        switch( opcode )
        {
        case 1:
            param->type_char = (char)g1(&buf);
            break;
        case 8:
            param->type_char = param_type_id_to_char((int)g1(&buf));
            break;
        case 2:
            param->default_int = g4(&buf);
            break;
        case 5:
        {
            char* s = gcstring(&buf);
            free(param->default_string);
            param->default_string = s;
            break;
        }
        default:
            break;
        }
    }
}

void
RSCacheDat2A_ConfigParamFree(struct RSCacheDat2A_ConfigParam* param)
{
    if( !param )
        return;
    free(param->default_string);
    free(param);
}
