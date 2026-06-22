#include "dat1a_config_spotanim.h"

#include "../shared/shared_rs_buffer.h"

#include <stdlib.h>
#include <string.h>

struct RSCacheDat1A_ConfigSpotanim*
RSCacheDat1A_ConfigSpotanimDecodeOne(
    void* data,
    int size)
{
    struct RSCacheDat1A_ConfigSpotanim* s = malloc(sizeof(struct RSCacheDat1A_ConfigSpotanim));
    if( !s )
        return NULL;
    memset(s, 0, sizeof(*s));
    s->model = 0;
    s->seq_id = -1;
    s->anim_gfx_height = 0;
    s->resizeh = 128;
    s->resizev = 128;
    s->orientation = 0;
    s->ambient = 0;
    s->contrast = 0;

    struct RSCacheShared_RSBuffer buffer = { .data = (uint8_t*)(data), .size = (uint32_t)(size), .position = 0 };

    while( buffer.position < buffer.size )
    {
        int opcode = g1(&buffer);
        if( opcode == 0 )
            break;

        switch( opcode )
        {
        case 1:
            s->model = g2(&buffer);
            break;
        case 2:
            s->seq_id = g2(&buffer);
            break;
        case 4:
            s->anim_gfx_height = g2(&buffer);
            break;
        case 5:
            s->resizeh = g2(&buffer);
            break;
        case 6:
            s->resizev = g2(&buffer);
            break;
        case 7:
            s->orientation = g2(&buffer);
            break;
        case 8:
            s->ambient = g1(&buffer);
            break;
        case 9:
            s->contrast = g1(&buffer);
            break;
        default:
            break;
        }
    }

    return s;
}

void
RSCacheDat1A_ConfigSpotanimFree(struct RSCacheDat1A_ConfigSpotanim* s)
{
    free(s);
}
