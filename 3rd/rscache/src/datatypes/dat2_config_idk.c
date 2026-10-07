#include "dat2_config_idk.h"

#include "../rsbuffer.h"
#include "dat2_configs.h"

#include <assert.h>
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
    assert(idk);

    RSCache_Dat2ConfigIdkDecodeInplace(idk, buffer, buffer_size);
    return idk;
}

void
RSCache_Dat2ConfigIdkFree(struct RSCache_Dat2ConfigIdk* idk)
{
    free(idk);
}

void
RSCache_Dat2ConfigIdkInit(struct RSCache_Dat2ConfigIdk* idk)
{
    memset(idk, 0, sizeof(struct RSCache_Dat2ConfigIdk));
    idk->body_part_id = -1;

    for( int i = 0; i < 10; i++ )
        idk->if_model_ids[i] = -1;
}

unsigned
RSCache_Dat2ConfigIdkFlags(const struct RSCache* cache)
{
    assert(cache);
    if( !RSCache_IsOsrs(cache) )
        return 0;
    /* Bracketed by the corpus: osrs230 writes 2 / 60+i, osrs239 writes 5 / 70+i.
     * 237 is where obj moved its model ids to u32 (RSCACHE_CONFIG_OBJ_DECODE_REV237_
     * INT_MODEL_IDS), and identkits are the same models. */
    if( RSCache_RevisionAtLeastOsrs(
            cache, RSCACHE_TYPE_IDK, 237, RSCACHE_GROUP_REVISION_UNKNOWN, false) )
        return RSCACHE_CONFIG_IDK_ENCODE_INT_MODEL_IDS;
    return 0;
}

static bool
fits_u16(int value)
{
    return value >= 0 && value <= 0xFFFF;
}

uint32_t
RSCache_Dat2ConfigIdkEncodeFlags(
    const struct RSCache_Dat2ConfigIdk* idk,
    unsigned flags,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(idk);
    assert(out);
    assert(out_capacity >= RSCache_Dat2ConfigIdkEncodeBound(idk));

    bool int_ids = (flags & RSCACHE_CONFIG_IDK_ENCODE_INT_MODEL_IDS) != 0;
    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /*
     * Exactly the stated fields, in the order every identkit in the corpus writes
     * them: 3, 1, 40, the if-models, the model list. That is not numeric order --
     * (3, 1, 2) and (1, 60, 2) are the cache's own shapes -- and writing them
     * numerically is half of why this type re-encoded 0 of 307 records; the other
     * half is RSCACHE_CONFIG_IDK_ENCODE_INT_MODEL_IDS. Where 41 falls is not
     * attested; it is written beside 40.
     */
    if( RSCache_PresenceHas(&idk->present, RSCACHE_IDK_FIELD_NOT_SELECTABLE) )
        p1(&buffer, 3);

    if( RSCache_PresenceHas(&idk->present, RSCACHE_IDK_FIELD_BODY_PART) )
    {
        assert(idk->body_part_id >= 0);
        assert(idk->body_part_id <= 255);
        p1(&buffer, 1);
        p1(&buffer, idk->body_part_id);
    }

    if( RSCache_PresenceHas(&idk->present, RSCACHE_IDK_FIELD_RECOLOURS) )
    {
        assert(idk->recolor_count >= 0);
        assert(idk->recolor_count <= 255);
        p1(&buffer, 40);
        p1(&buffer, idk->recolor_count);
        for( int i = 0; i < idk->recolor_count; i++ )
        {
            p2(&buffer, idk->recolors_from[i]);
            p2(&buffer, idk->recolors_to[i]);
        }
    }

    if( RSCache_PresenceHas(&idk->present, RSCACHE_IDK_FIELD_RETEXTURES) )
    {
        assert(idk->retexture_count >= 0);
        assert(idk->retexture_count <= 255);
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
        if( !RSCache_PresenceHas(&idk->present, RSCACHE_IDK_FIELD_IF_MODEL_FIRST + i) )
            continue;
        if( int_ids || !fits_u16(idk->if_model_ids[i]) )
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

    if( RSCache_PresenceHas(&idk->present, RSCACHE_IDK_FIELD_MODELS) )
    {
        bool use_int_ids = int_ids;

        assert(idk->model_ids_count >= 0);
        assert(idk->model_ids_count <= 255);
        for( int i = 0; i < idk->model_ids_count && !use_int_ids; i++ )
        {
            if( !fits_u16(idk->model_ids[i]) )
                use_int_ids = true;
        }
        p1(&buffer, use_int_ids ? 5 : 2);
        p1(&buffer, idk->model_ids_count);
        for( int i = 0; i < idk->model_ids_count; i++ )
        {
            if( use_int_ids )
                p4(&buffer, idk->model_ids[i]);
            else
                p2(&buffer, idk->model_ids[i]);
        }
    }

    p1(&buffer, 0);
    return buffer.position;
}

uint32_t
RSCache_Dat2ConfigIdkEncode(
    const struct RSCache_Dat2ConfigIdk* idk,
    uint8_t* out,
    uint32_t out_capacity)
{
    return RSCache_Dat2ConfigIdkEncodeFlags(idk, 0, out, out_capacity);
}

uint32_t
RSCache_Dat2ConfigIdkEncodeProfile(
    const struct RSCache* cache,
    const struct RSCache_Dat2ConfigIdk* idk,
    uint8_t* out,
    uint32_t out_capacity)
{
    return RSCache_Dat2ConfigIdkEncodeFlags(
        idk, RSCache_Dat2ConfigIdkFlags(cache), out, out_capacity);
}

bool
RSCache_Dat2ConfigIdkDecodeOp(
    struct RSCache_Dat2ConfigIdk* idk,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags)
{
    (void)flags; /* No era-dependent opcode in this type. */
        switch( opcode )
        {
        case 1:
            idk->body_part_id = g1(buffer);
            RSCache_PresenceSet(&idk->present, RSCACHE_IDK_FIELD_BODY_PART);
            break;
        case 2:
        case 5:
            /* The same list, with u16 or u32 ids. */
            free(idk->model_ids);
            idk->model_ids_count = g1(buffer);
            idk->model_ids = malloc((size_t)(idk->model_ids_count + 1) * sizeof(int));
            assert(idk->model_ids);
            for( int i = 0; i < idk->model_ids_count; i++ )
                idk->model_ids[i] = opcode == 2 ? g2(buffer) : g4(buffer);
            RSCache_PresenceSet(&idk->present, RSCACHE_IDK_FIELD_MODELS);
            break;
        case 3:
            idk->is_not_selectable = true;
            RSCache_PresenceSet(&idk->present, RSCACHE_IDK_FIELD_NOT_SELECTABLE);
            break;
        case 40:
            free(idk->recolors_from);
            free(idk->recolors_to);
            idk->recolor_count = g1(buffer);
            idk->recolors_from = malloc((size_t)(idk->recolor_count + 1) * sizeof(int));
            assert(idk->recolors_from);
            idk->recolors_to = malloc((size_t)(idk->recolor_count + 1) * sizeof(int));
            assert(idk->recolors_to);
            for( int i = 0; i < idk->recolor_count; i++ )
            {
                idk->recolors_from[i] = g2(buffer);
                idk->recolors_to[i] = g2(buffer);
            }
            RSCache_PresenceSet(&idk->present, RSCACHE_IDK_FIELD_RECOLOURS);
            break;
        case 41:
            free(idk->retextures_from);
            free(idk->retextures_to);
            idk->retexture_count = g1(buffer);
            idk->retextures_from = malloc((size_t)(idk->retexture_count + 1) * sizeof(int));
            assert(idk->retextures_from);
            idk->retextures_to = malloc((size_t)(idk->retexture_count + 1) * sizeof(int));
            assert(idk->retextures_to);
            for( int i = 0; i < idk->retexture_count; i++ )
            {
                idk->retextures_from[i] = g2(buffer);
                idk->retextures_to[i] = g2(buffer);
            }
            RSCache_PresenceSet(&idk->present, RSCACHE_IDK_FIELD_RETEXTURES);
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
            idk->if_model_ids[opcode - 60] = g2(buffer);
            RSCache_PresenceSet(&idk->present, RSCACHE_IDK_FIELD_IF_MODEL_FIRST + (opcode - 60));
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
            idk->if_model_ids[opcode - 70] = g4(buffer);
            RSCache_PresenceSet(&idk->present, RSCACHE_IDK_FIELD_IF_MODEL_FIRST + (opcode - 70));
            break;
        }
            default:
            /*
             * There was no `default` here at all: an unknown opcode fell straight
             * through the switch and the loop read its payload as the next opcode,
             * decoding from a false position with no error and no short read to
             * show for it. That is the same failure that hid a real width bug in
             * `dat2_config_obj.c` opcode 115 — see its note. Stopping is the only
             * safe answer when a payload width is unknown.
             */
            return false;
        }

    /* Fell out of the switch: a case handled this opcode. */
    return true;
}

void
RSCache_Dat2ConfigIdkDecodeInplace(
    struct RSCache_Dat2ConfigIdk* idk,
    char* buffer,
    int buffer_size)
{
    struct RSCache_Buffer rsbuf;

    RSCache_BufferInit(&rsbuf, (uint8_t*)buffer, (uint32_t)(buffer_size));
    RSCache_Dat2ConfigIdkInit(idk);

    while( true )
    {
        if( rsbuf.position >= rsbuf.size )
            break;

        int opcode = g1(&rsbuf);
        if( opcode == 0 )
            break;

        if( !RSCache_Dat2ConfigIdkDecodeOp(idk, opcode, &rsbuf, 0) )
            break;
    }
    idk->_consumed = (int)rsbuf.position;
}

uint32_t
RSCache_Dat2ConfigIdkEncodeBound(const struct RSCache_Dat2ConfigIdk* idk)
{
    /* Opcodes 1/3 are tiny; the variable parts are the model list, the recolour
     * and retexture pairs, and the ten if-model slots. Worst case g4 per model. */
    uint32_t need = 64u;

    assert(idk);
    need += (uint32_t)idk->model_ids_count * 4u + 2u;
    need += (uint32_t)idk->recolor_count * 4u + 2u;
    need += (uint32_t)idk->retexture_count * 4u + 2u;
    need += 10u * 5u;
    return need;
}
