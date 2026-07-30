#include "dat2_config_mapelement.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
rscache_mapelement_skip_params(struct RSCache_Buffer* buffer)
{
    struct RSCache_Params params = { 0 };

    RSCache_BufferReadParams(buffer, &params);
    if( params.values )
    {
        for( int i = 0; i < params.count; i++ )
            free(params.values[i]);
    }
    free(params.values);
    free(params.keys);
    free(params.kinds);
}

void
RSCache_MapElementDecodeInplace(
    struct RSCache_MapElement* entry,
    const void* data,
    int data_size)
{
    struct RSCache_Buffer buffer;

    if( !entry || !data || data_size <= 0 )
        return;

    entry->sprite_id = -1;
    entry->category = -1;

    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);

    for( ;; )
    {
        int opcode = RSCache_BufferG1(&buffer);
        if( opcode == 0 )
            return;

        switch( opcode )
        {
        case 1:
            entry->sprite_id = RSCache_BufferReadBigSmart(&buffer);
            break;
        case 2:
            (void)RSCache_BufferReadBigSmart(&buffer); /* hover sprite */
            break;
        case 3:
            free(entry->name);
            entry->name = RSCache_BufferReadStringNullTerminated(&buffer);
            break;
        case 4:
        case 5:
            (void)RSCache_BufferG3(&buffer); /* text / hover text colour */
            break;
        case 6:
            entry->text_size = RSCache_BufferG1(&buffer);
            break;
        case 7:  /* visibility flags */
        case 8:  /* randomize position */
        case 28: /* unused byte */
            (void)RSCache_BufferG1(&buffer);
            break;
        case 9:  /* primary visibility varbit/varp + value range */
        case 20: /* secondary visibility varbit/varp + value range */
            (void)RSCache_BufferG2(&buffer);
            (void)RSCache_BufferG2(&buffer);
            (void)RSCache_BufferG4(&buffer);
            (void)RSCache_BufferG4(&buffer);
            break;
        case 10: /* ops 1..5 */
        case 11:
        case 12:
        case 13:
        case 14:
        case 17: /* target name */
            free(RSCache_BufferReadStringNullTerminated(&buffer));
            break;
        case 15:
        {
            int count = RSCache_BufferG1(&buffer);
            for( int i = 0; i < count * 2; i++ )
                (void)RSCache_BufferG2(&buffer);
            (void)RSCache_BufferG4(&buffer);
            int extra = RSCache_BufferG1(&buffer);
            for( int i = 0; i < extra; i++ )
                (void)RSCache_BufferG4(&buffer);
            for( int i = 0; i < count; i++ )
                (void)RSCache_BufferG1(&buffer);
            break;
        }
        case 16: /* flag with no payload */
            break;
        case 18:
        case 25:
            (void)RSCache_BufferReadBigSmart(&buffer);
            break;
        case 19:
            entry->category = RSCache_BufferG2(&buffer);
            break;
        case 21:
        case 22:
            (void)RSCache_BufferG4(&buffer);
            break;
        case 23:
            (void)RSCache_BufferG1(&buffer);
            (void)RSCache_BufferG1(&buffer);
            (void)RSCache_BufferG1(&buffer);
            break;
        case 24:
            (void)RSCache_BufferG2(&buffer);
            (void)RSCache_BufferG2(&buffer);
            break;
        case 29: /* horizontal alignment */
        case 30: /* vertical alignment */
            (void)RSCache_BufferG1(&buffer);
            break;
        case 249:
            rscache_mapelement_skip_params(&buffer);
            break;
        default:
            /* Everything after an unknown opcode is misaligned, so stop rather
             * than decode garbage into the fields already read. */
            printf(
                "RSCache_MapElementDecodeInplace: map element %d unknown opcode %d\n",
                entry->id,
                opcode);
            return;
        }
    }
}

uint32_t
RSCache_MapElementEncode(
    const struct RSCache_MapElement* entry,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !entry || !out )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* This decoder keeps four fields and consumes the other two dozen opcodes
     * purely to stay aligned, so an encoded record carries only those four. The
     * result is a valid, readable map element with the same icon, label, size and
     * category — but it is *not* a faithful copy of the source record, and
     * re-encoding a decoded one loses hover sprites, colours, visibility varbits,
     * ops and params. Byte-exactness is impossible by construction here; anything
     * that needs the full record has to extend the decoder first. */
    if( entry->sprite_id != 0 )
    {
        p1(&buffer, 1);
        pbigsmart(&buffer, entry->sprite_id);
    }
    if( entry->name )
    {
        p1(&buffer, 3);
        pjstr(&buffer, entry->name, RSCACHE_JSTR_TERMINATOR_NULL);
    }
    if( entry->text_size != 0 )
    {
        p1(&buffer, 6);
        p1(&buffer, entry->text_size);
    }
    if( entry->category != 0 )
    {
        p1(&buffer, 19);
        p2(&buffer, entry->category);
    }

    p1(&buffer, 0);
    return buffer.position;
}

void
RSCache_MapElementFreeInplace(struct RSCache_MapElement* entry)
{
    if( !entry )
        return;
    free(entry->name);
    memset(entry, 0, sizeof(*entry));
}

uint32_t
RSCache_MapElementEncodeBound(const struct RSCache_MapElement* entry)
{
    /* The decoder retains four fields; the encoder emits only those, so a flat
     * allowance plus the two strings covers it. */
    uint32_t need = 64u;

    if( !entry )
        return need;
    if( entry->name )
        need += (uint32_t)strlen(entry->name) + 2u;
    return need;
}
