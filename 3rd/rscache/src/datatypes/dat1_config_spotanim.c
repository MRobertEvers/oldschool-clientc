#include "dat1_config_spotanim.h"

#include "../rsbuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// SpotType.decode (LostCity JavaClient / Client-TS config/SpotType.ts).
static void
init_dat1_spotanim(struct RSCache_Dat1ConfigSpotanim* spotanim)
{
    memset(spotanim, 0, sizeof(*spotanim));
    spotanim->model = 0;
    spotanim->anim = -1;
    spotanim->resizeh = 128;
    spotanim->resizev = 128;
    spotanim->angle = 0;
    spotanim->ambient = 0;
    spotanim->contrast = 0;
    spotanim->dispose_alpha = false;
}

int
RSCache_Dat1ConfigSpotanimDecodeInplace(
    struct RSCache_Dat1ConfigSpotanim* spotanim,
    char* data,
    int data_size)
{
    struct RSCache_Buffer buffer;

    init_dat1_spotanim(spotanim);
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);

    while( true )
    {
        int opcode = g1(&buffer);
        if( opcode == 0 )
            break;

        if( spotanim->opcode_count <
            (int)(sizeof(spotanim->opcodes) / sizeof(spotanim->opcodes[0])) )
            spotanim->opcodes[spotanim->opcode_count++] = opcode;

        if( opcode == 1 )
            spotanim->model = g2(&buffer);
        else if( opcode == 2 )
            spotanim->anim = g2(&buffer);
        else if( opcode == 3 )
            spotanim->dispose_alpha = true;
        else if( opcode == 4 )
            spotanim->resizeh = g2(&buffer);
        else if( opcode == 5 )
            spotanim->resizev = g2(&buffer);
        else if( opcode == 6 )
            spotanim->angle = g2(&buffer);
        else if( opcode == 7 )
            spotanim->ambient = g1(&buffer);
        else if( opcode == 8 )
            spotanim->contrast = g1(&buffer);
        /* Ranges are 10 wide, arrays hold 6. Consume the value either way to keep
         * the stream aligned, but only store it if it fits — writing
         * unconditionally overflowed recol_s into recol_d. */
        else if( opcode >= 40 && opcode < 50 )
        {
            int slot = opcode - 40;
            int value = g2(&buffer);
            if( slot < RSCACHE_SPOTANIM_COLOUR_SLOTS )
                spotanim->recol_s[slot] = value;
        }
        else if( opcode >= 50 && opcode < 60 )
        {
            int slot = opcode - 50;
            int value = g2(&buffer);
            if( slot < RSCACHE_SPOTANIM_COLOUR_SLOTS )
                spotanim->recol_d[slot] = value;
        }
        else
        {
            /* An unknown opcode desynchronises the rest of the table (entries
             * are only sequentially addressable), so stop here rather than
             * decode garbage into later ids. */
            printf("Unrecognized dat1 spotanim opcode %d\n", opcode);
            return (int)buffer.position;
        }
    }

    return (int)buffer.position;
}

struct RSCache_Dat1ConfigSpotanimList*
RSCache_Dat1ConfigSpotanimListNewDecode(
    char* data,
    int data_size)
{
    struct RSCache_Dat1ConfigSpotanimList* list;
    struct RSCache_Buffer buffer;
    int count;

    if( !data || data_size <= 0 )
        return NULL;

    list = malloc(sizeof(*list));
    if( !list )
        return NULL;
    memset(list, 0, sizeof(*list));

    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    count = g2(&buffer);
    if( count <= 0 )
        return list;

    list->spotanims = calloc((size_t)count, sizeof(*list->spotanims));
    if( !list->spotanims )
    {
        free(list);
        return NULL;
    }
    list->spotanims_count = count;

    for( int i = 0; i < count; i++ )
    {
        buffer.position += (uint32_t)RSCache_Dat1ConfigSpotanimDecodeInplace(
            &list->spotanims[i],
            (char*)buffer.data + buffer.position,
            (int)(buffer.size - buffer.position));
    }

    return list;
}

void
RSCache_Dat1ConfigSpotanimListFree(struct RSCache_Dat1ConfigSpotanimList* list)
{
    if( !list )
        return;
    free(list->spotanims);
    free(list);
}

uint32_t
RSCache_Dat1ConfigSpotanimEncodeBound(const struct RSCache_Dat1ConfigSpotanim* spotanim)
{
    if( !spotanim )
        return 8u;
    /* Every opcode is a code byte plus at most two payload bytes. */
    return 8u + (uint32_t)spotanim->opcode_count * 3u;
}

uint32_t
RSCache_Dat1ConfigSpotanimEncode(
    const struct RSCache_Dat1ConfigSpotanim* spotanim,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Buffer buffer;
    int i;

    if( !spotanim || !out )
        return 0;
    if( out_capacity < RSCache_Dat1ConfigSpotanimEncodeBound(spotanim) )
        return 0;
    RSCache_BufferInit(&buffer, out, out_capacity);

    for( i = 0; i < spotanim->opcode_count; i++ )
    {
        int opcode = spotanim->opcodes[i];

        p1(&buffer, opcode);
        if( opcode == 1 )
            p2(&buffer, spotanim->model);
        else if( opcode == 2 )
            p2(&buffer, spotanim->anim);
        else if( opcode == 3 )
        {
            /* A bare flag: the opcode byte is the whole payload. */
        }
        else if( opcode == 4 )
            p2(&buffer, spotanim->resizeh);
        else if( opcode == 5 )
            p2(&buffer, spotanim->resizev);
        else if( opcode == 6 )
            p2(&buffer, spotanim->angle);
        else if( opcode == 7 )
            p1(&buffer, spotanim->ambient);
        else if( opcode == 8 )
            p1(&buffer, spotanim->contrast);
        else if( opcode >= 40 && opcode < 50 )
        {
            int slot = opcode - 40;

            /* Slots past the array's width were consumed and discarded by the
             * decoder, so their value is gone. Zero keeps the stream aligned. */
            p2(&buffer,
               slot < RSCACHE_SPOTANIM_COLOUR_SLOTS ? spotanim->recol_s[slot] : 0);
        }
        else if( opcode >= 50 && opcode < 60 )
        {
            int slot = opcode - 50;

            p2(&buffer,
               slot < RSCACHE_SPOTANIM_COLOUR_SLOTS ? spotanim->recol_d[slot] : 0);
        }
        else
        {
            return 0;
        }
    }

    p1(&buffer, 0);
    return buffer.position;
}
