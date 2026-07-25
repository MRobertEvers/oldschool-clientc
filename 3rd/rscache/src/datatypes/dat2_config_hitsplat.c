#include "dat2_config_hitsplat.h"

#include <string.h>

/** Record the opcode in the order seen, so the encoder can replay it. */
static void
hitsplat_note_opcode(
    struct RSCache_Dat2ConfigHitsplat* entry,
    int opcode)
{
    if( entry->opcode_count < RSCACHE_HITSPLAT_MAX_OPCODES )
        entry->opcodes[entry->opcode_count++] = (uint8_t)opcode;
}

void
RSCache_Dat2ConfigHitsplatDecode(
    struct RSCache_Dat2ConfigHitsplat* entry,
    struct RSCache_Buffer* buffer)
{
    if( !entry || !buffer )
        return;

    /* -1, not 0: sprite 0 exists, so it cannot double as "no sprite". */
    entry->sprite_id = -1;

    for( ;; )
    {
        if( buffer->position >= buffer->size )
            break;

        int opcode = g1(buffer);
        if( opcode == 0 )
            break;

        switch( opcode )
        {
        case 5:
            entry->sprite_id = g2(buffer);
            break;
        case 8:
            entry->opcode_8 = g2(buffer);
            entry->has_opcode_8 = true;
            break;
        case 9:
            entry->opcode_9 = g2(buffer);
            entry->has_opcode_9 = true;
            break;
        case 11:
            entry->opcode_11 = 1;
            break;
        case 13:
            entry->opcode_13 = g2(buffer);
            entry->has_opcode_13 = true;
            break;
        case 18:
            /* An 11-byte composite, kept raw — see the header. */
            if( buffer->position + RSCACHE_HITSPLAT_OPCODE_18_BYTES > buffer->size )
                return;
            memcpy(
                entry->opcode_18,
                buffer->data + buffer->position,
                RSCACHE_HITSPLAT_OPCODE_18_BYTES);
            buffer->position += RSCACHE_HITSPLAT_OPCODE_18_BYTES;
            entry->has_opcode_18 = true;
            break;
        case 49:
            entry->opcode_49 = g1(buffer);
            entry->has_opcode_49 = true;
            break;
        default:
            /* Unknown opcode: stop rather than guess a width. Opcode 18 is exactly
             * why that matters — a wrong width there makes the whole record misparse
             * with no other symptom. */
            return;
        }

        hitsplat_note_opcode(entry, opcode);
    }
}

void
RSCache_Dat2ConfigHitsplatDecodeInplace(
    struct RSCache_Dat2ConfigHitsplat* entry,
    const void* data,
    int data_size)
{
    if( !entry )
        return;
    if( !data || data_size <= 0 )
    {
        entry->sprite_id = -1;
        return;
    }

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    RSCache_Dat2ConfigHitsplatDecode(entry, &buffer);
    entry->_consumed = (int)buffer.position;
}

uint32_t
RSCache_Dat2ConfigHitsplatEncodeBound(const struct RSCache_Dat2ConfigHitsplat* entry)
{
    (void)entry;
    /* Six scalar opcodes at up to 3 bytes each, opcode 18's tag plus payload, and the
     * terminator. */
    return (6u * 3u) + 1u + RSCACHE_HITSPLAT_OPCODE_18_BYTES + 1u;
}

/** Write one opcode and its operand. Returns false if the field is not present. */
static bool
hitsplat_write_opcode(
    struct RSCache_Buffer* buffer,
    const struct RSCache_Dat2ConfigHitsplat* entry,
    int opcode)
{
    switch( opcode )
    {
    case 5:
        if( entry->sprite_id < 0 )
            return false;
        p1(buffer, 5);
        p2(buffer, entry->sprite_id);
        return true;
    case 8:
        if( !entry->has_opcode_8 )
            return false;
        p1(buffer, 8);
        p2(buffer, entry->opcode_8);
        return true;
    case 9:
        if( !entry->has_opcode_9 )
            return false;
        p1(buffer, 9);
        p2(buffer, entry->opcode_9);
        return true;
    case 11:
        if( !entry->opcode_11 )
            return false;
        p1(buffer, 11);
        return true;
    case 13:
        if( !entry->has_opcode_13 )
            return false;
        p1(buffer, 13);
        p2(buffer, entry->opcode_13);
        return true;
    case 18:
        if( !entry->has_opcode_18 )
            return false;
        p1(buffer, 18);
        for( int i = 0; i < RSCACHE_HITSPLAT_OPCODE_18_BYTES; i++ )
            p1(buffer, entry->opcode_18[i]);
        return true;
    case 49:
        if( !entry->has_opcode_49 )
            return false;
        p1(buffer, 49);
        p1(buffer, entry->opcode_49);
        return true;
    default:
        return false;
    }
}

uint32_t
RSCache_Dat2ConfigHitsplatEncode(
    const struct RSCache_Dat2ConfigHitsplat* entry,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !entry || !out )
        return 0;
    if( out_capacity < RSCache_Dat2ConfigHitsplatEncodeBound(entry) )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    if( entry->opcode_count > 0 )
    {
        /* Replay the recorded order. Hitsplat records do not share one — see the
         * header — so this is the only way to reproduce the bytes. */
        for( int i = 0; i < entry->opcode_count; i++ )
            hitsplat_write_opcode(&buffer, entry, entry->opcodes[i]);
    }
    else
    {
        /* No order recorded (a hand-built record): ascending. Decodes back to an equal
         * struct, but will not match a source record byte for byte. */
        static const int ASCENDING[] = { 5, 8, 9, 11, 13, 18, 49 };
        for( size_t i = 0; i < sizeof(ASCENDING) / sizeof(ASCENDING[0]); i++ )
            hitsplat_write_opcode(&buffer, entry, ASCENDING[i]);
    }

    p1(&buffer, 0);
    return buffer.position;
}
