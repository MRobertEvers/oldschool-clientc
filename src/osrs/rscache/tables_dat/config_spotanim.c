#include "config_spotanim.h"

#include "../rsbuf.h"

#include <stdlib.h>
#include <string.h>

struct CacheDatConfigSpotAnim*
cache_dat_config_spotanim_decode_one(
    void* data,
    int size)
{
    struct CacheDatConfigSpotAnim* s = malloc(sizeof(struct CacheDatConfigSpotAnim));
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

    struct RSBuffer buffer = { .data = data, .size = size, .position = 0 };

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
cache_dat_config_spotanim_free(struct CacheDatConfigSpotAnim* s)
{
    free(s);
}
