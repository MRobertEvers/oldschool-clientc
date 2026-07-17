#include "dat2_config_idk.h"

#include "../rsbuffer.h"
#include "dat2_configs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct RSCache_Dat2ConfigIdk*
RSCache_Dat2ConfigIdkNewDecode(
    char* buffer,
    int buffer_size)
{
    struct RSCache_Dat2ConfigIdk* idk = malloc(sizeof(struct RSCache_Dat2ConfigIdk));

    RSCache_Dat2ConfigIdkDecodeInplace(idk, buffer, buffer_size);
    return idk;
}

void
RSCache_Dat2ConfigIdkFree(struct RSCache_Dat2ConfigIdk* idk)
{
    free(idk);
}

static void
init_idk(struct RSCache_Dat2ConfigIdk* idk)
{
    memset(idk, 0, sizeof(struct RSCache_Dat2ConfigIdk));
    idk->body_part_id = -1;

    for( int i = 0; i < 10; i++ )
        idk->if_model_ids[i] = -1;
}

void
RSCache_Dat2ConfigIdkDecodeInplace(
    struct RSCache_Dat2ConfigIdk* idk,
    char* buffer,
    int buffer_size)
{
    struct RSCache_Buffer rsbuf;
    RSCache_BufferInit(&rsbuf, (uint8_t*)buffer, (uint32_t)(buffer_size));

    init_idk(idk);

    while( true )
    {
        if( rsbuf.position >= rsbuf.size )
        {
            printf(
                "RSCache_Dat2ConfigIdkDecodeInplace: Buffer position %d exceeded data size %d\n",
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
