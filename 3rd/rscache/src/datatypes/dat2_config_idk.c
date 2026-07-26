#include "dat2_config_idk.h"

#include "../rsbuffer.h"
#include "dat2_configs.h"

#include <stdbool.h>
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

uint32_t
RSCache_Dat2ConfigIdkEncode(
    const struct RSCache_Dat2ConfigIdk* idk,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !idk || !out )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* body_part_id defaults to -1, which is not representable in the u8 the
     * opcode writes, so -1 means "field absent". */
    if( idk->body_part_id != -1 )
    {
        p1(&buffer, 1);
        p1(&buffer, idk->body_part_id);
    }

    if( idk->model_ids_count > 0 )
    {
        bool use_int_ids = false;
        for( int i = 0; i < idk->model_ids_count; i++ )
        {
            if( idk->model_ids[i] < 0 || idk->model_ids[i] > 0xFFFF )
            {
                use_int_ids = true;
                break;
            }
        }
        if( use_int_ids )
        {
            p1(&buffer, 5);
            p1(&buffer, idk->model_ids_count);
            for( int i = 0; i < idk->model_ids_count; i++ )
                p4(&buffer, idk->model_ids[i]);
        }
        else
        {
            p1(&buffer, 2);
            p1(&buffer, idk->model_ids_count);
            for( int i = 0; i < idk->model_ids_count; i++ )
                p2(&buffer, idk->model_ids[i]);
        }
    }

    if( idk->is_not_selectable )
        p1(&buffer, 3);

    if( idk->recolor_count > 0 )
    {
        p1(&buffer, 40);
        p1(&buffer, idk->recolor_count);
        for( int i = 0; i < idk->recolor_count; i++ )
        {
            p2(&buffer, idk->recolors_from[i]);
            p2(&buffer, idk->recolors_to[i]);
        }
    }

    if( idk->retexture_count > 0 )
    {
        p1(&buffer, 41);
        p1(&buffer, idk->retexture_count);
        for( int i = 0; i < idk->retexture_count; i++ )
        {
            p2(&buffer, idk->retextures_from[i]);
            p2(&buffer, idk->retextures_to[i]);
        }
    }

    for( int i = 0; i < 10; i++ )
    {
        if( idk->if_model_ids[i] != -1 )
        {
            if( idk->if_model_ids[i] < 0 || idk->if_model_ids[i] > 0xFFFF )
            {
                p1(&buffer, 70 + i);
                p4(&buffer, idk->if_model_ids[i]);
            }
            else
            {
                p1(&buffer, 60 + i);
                p2(&buffer, idk->if_model_ids[i]);
            }
        }
    }

    p1(&buffer, 0);
    return buffer.position;
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
        case 5:
            idk->model_ids_count = g1(&rsbuf);
            idk->model_ids = malloc(idk->model_ids_count * sizeof(int));
            for( int i = 0; i < idk->model_ids_count; i++ )
                idk->model_ids[i] = g4(&rsbuf);
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
        case 70:
        case 71:
        case 72:
        case 73:
        case 74:
        case 75:
        case 76:
        case 77:
        case 78:
        case 79:
        {
            idk->if_model_ids[opcode - 70] = g4(&rsbuf);
            break;
        }
        }
    }
}
