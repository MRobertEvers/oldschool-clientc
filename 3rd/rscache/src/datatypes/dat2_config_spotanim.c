#include "dat2_config_spotanim.h"

#include "../rsbuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
init_dat2_spotanim(struct RSCache_Dat2ConfigSpotanim* s)
{
    memset(s, 0, sizeof(*s));
    s->model = 0;
    s->anim = -1;
    s->resizeh = 128;
    s->resizev = 128;
    s->angle = 0;
    s->ambient = 0;
    s->contrast = 0;
}

static void
decode_dat2_spotanim(struct RSCache_Dat2ConfigSpotanim* s, struct RSCache_Buffer* buffer)
{
    while( true )
    {
        int opcode = g1(buffer);
        if( opcode == 0 )
            break;

        if( opcode == 1 )
            s->model = g2(buffer);
        else if( opcode == 2 )
            s->anim = g2(buffer);
        else if( opcode == 4 )
            s->resizeh = g2(buffer);
        else if( opcode == 5 )
            s->resizev = g2(buffer);
        else if( opcode == 6 )
            s->angle = g2(buffer);
        else if( opcode == 7 )
            s->ambient = g1(buffer);
        else if( opcode == 8 )
            s->contrast = g1(buffer);
        else if( opcode >= 40 && opcode < 50 )
            s->recol_s[opcode - 40] = g2(buffer);
        else if( opcode >= 50 && opcode < 60 )
            s->recol_d[opcode - 50] = g2(buffer);
        else if( opcode >= 60 && opcode < 70 )
            s->retex_s[opcode - 60] = g2(buffer);
        else if( opcode >= 70 && opcode < 80 )
            s->retex_d[opcode - 70] = g2(buffer);
        else
        {
            printf("Unrecognized dat2 spotanim opcode %d\n", opcode);
            return;
        }
    }
}

struct RSCache_Dat2ConfigSpotanim*
RSCache_Dat2ConfigSpotanimNewDecode(int revision, char* data, int data_size)
{
    struct RSCache_Dat2ConfigSpotanim* s;
    struct RSCache_Buffer buffer;

    (void)revision;

    s = calloc(1, sizeof(*s));
    if( !s )
        return NULL;
    init_dat2_spotanim(s);

    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    decode_dat2_spotanim(s, &buffer);
    return s;
}

void
RSCache_Dat2ConfigSpotanimFree(struct RSCache_Dat2ConfigSpotanim* spotanim)
{
    free(spotanim);
}
