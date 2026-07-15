#include "dat2a_config_idk.h"

#include "../shared/shared_rs_buffer.h"
#include "dat2a_configs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct RSCacheDat2A_ConfigIdk*
RSCacheDat2A_ConfigIdkNewDecode(
    char* buffer,
    int buffer_size)
{
    struct RSCacheDat2A_ConfigIdk* idk = malloc(sizeof(struct RSCacheDat2A_ConfigIdk));

    RSCacheDat2A_ConfigIdkDecodeInplace(idk, buffer, buffer_size);
    return idk;
}

void
RSCacheDat2A_ConfigIdkFree(struct RSCacheDat2A_ConfigIdk* idk)
{
    free(idk);
}

static void
init_idk(struct RSCacheDat2A_ConfigIdk* idk)
{
    memset(idk, 0, sizeof(struct RSCacheDat2A_ConfigIdk));
    idk->body_part_id = -1;

    for( int i = 0; i < 10; i++ )
        idk->if_model_ids[i] = -1;
}

void
RSCacheDat2A_ConfigIdkDecodeInplace(
    struct RSCacheDat2A_ConfigIdk* idk,
    char* buffer,
    int buffer_size)
{
    struct RSCacheShared_RSBuffer rsbuf = { .data = (uint8_t*)buffer, .size = (uint32_t)(buffer_size), .position = 0 };

    init_idk(idk);

    while( true )
    {
        if( rsbuf.position >= rsbuf.size )
        {
            printf(
                "RSCacheDat2A_ConfigIdkDecodeInplace: Buffer position %d exceeded data size %d\n",
                rsbuf.position,
                rsbuf.size);
            return;
        }

        int opcode = g1(&rsbuf);
        if( opcode == 0 )
        {
            break;
        }

        switch( opcode )
        {
        case 1:
            idk->body_part_id = g1(&rsbuf);
            break;
        case 2:
            idk->model_ids_count = g1(&rsbuf);
            idk->model_ids = malloc(idk->model_ids_count * sizeof(int));
            for( int i = 0; i < idk->model_ids_count; i++ )
                idk->model_ids[i] = g2(&rsbuf);
            break;
        case 3:
            idk->is_not_selectable = true;
            break;
        case 40:
            idk->recolor_count = g1(&rsbuf);
            idk->recolors_from = malloc(idk->recolor_count * sizeof(int));
            idk->recolors_to = malloc(idk->recolor_count * sizeof(int));
            for( int i = 0; i < idk->recolor_count; i++ )
            {
                idk->recolors_from[i] = g2(&rsbuf);
                idk->recolors_to[i] = g2(&rsbuf);
            }
            break;
        case 41:
            idk->retexture_count = g1(&rsbuf);
            idk->retextures_from = malloc(idk->retexture_count * sizeof(int));
            idk->retextures_to = malloc(idk->retexture_count * sizeof(int));
            for( int i = 0; i < idk->retexture_count; i++ )
            {
                idk->retextures_from[i] = g2(&rsbuf);
                idk->retextures_to[i] = g2(&rsbuf);
            }
            break;
        case 60:
        case 61:
        case 62:
        case 63:
        case 64:
        case 65:
        case 66:
        case 67:
        case 68:
        case 69:
        {
            idk->if_model_ids[opcode - 60] = g2(&rsbuf);
            break;
        }
        }
    }
}
