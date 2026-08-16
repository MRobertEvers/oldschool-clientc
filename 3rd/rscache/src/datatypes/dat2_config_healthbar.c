#include "dat2_config_healthbar.h"

void
RSCache_Dat2ConfigHealthbarDecode(
    struct RSCache_Dat2ConfigHealthbar* entry,
    struct RSCache_Buffer* buffer)
{
    if( !entry || !buffer )
        return;

    RSCache_Dat2ConfigHealthbarInit(entry);

    for( ;; )
    {
        if( buffer->position >= buffer->size )
            break;

        int opcode = g1(buffer);
        if( opcode == 0 )
            break;

        if( !RSCache_Dat2ConfigHealthbarDecodeOp(entry, opcode, buffer, 0) )
            return;
    }
}

void
RSCache_Dat2ConfigHealthbarInit(struct RSCache_Dat2ConfigHealthbar* entry)
{
    if( !entry )
        return;
    /* -1, not 0: sprite 0 exists, so it cannot double as "no sprite". Kept out of
     * the loop so a per-opcode caller cannot miss it — see the hitsplat note. */
    entry->sprite_id_a = -1;
    entry->sprite_id_b = -1;
}

bool
RSCache_Dat2ConfigHealthbarDecodeOp(
    struct RSCache_Dat2ConfigHealthbar* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags)
{
    (void)flags; /* No era-dependent opcode in this type. */
    switch( opcode )
    {
    case 2:
        entry->opcode_2 = g1(buffer);
        entry->has_opcode_2 = true;
        return true;
    case 3:
        entry->opcode_3 = g1(buffer);
        entry->has_opcode_3 = true;
        return true;
    case 5:
        entry->opcode_5 = g2(buffer);
        entry->has_opcode_5 = true;
        return true;
    case 7:
        entry->sprite_id_a = g2(buffer);
        return true;
    case 8:
        entry->sprite_id_b = g2(buffer);
        return true;
    case 11:
        entry->opcode_11 = g2(buffer);
        entry->has_opcode_11 = true;
        return true;
    case 14:
        entry->opcode_14 = g1(buffer);
        entry->has_opcode_14 = true;
        return true;
    default:
        /* Unknown opcode: stop rather than guess its width. See the header. */
        return false;
    }
}

void
RSCache_Dat2ConfigHealthbarDecodeInplace(
    struct RSCache_Dat2ConfigHealthbar* entry,
    const void* data,
    int data_size)
{
    if( !entry )
        return;
    if( !data || data_size <= 0 )
    {
        entry->sprite_id_a = -1;
        entry->sprite_id_b = -1;
        return;
    }

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    RSCache_Dat2ConfigHealthbarDecode(entry, &buffer);
    entry->_consumed = (int)buffer.position;
}

uint32_t
RSCache_Dat2ConfigHealthbarEncodeBound(const struct RSCache_Dat2ConfigHealthbar* entry)
{
    (void)entry;
    /* Seven opcodes, the widest being u16, plus the terminator. */
    return (7u * 3u) + 1u;
}

uint32_t
RSCache_Dat2ConfigHealthbarEncode(
    const struct RSCache_Dat2ConfigHealthbar* entry,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !entry || !out )
        return 0;
    if( out_capacity < RSCache_Dat2ConfigHealthbarEncodeBound(entry) )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /*
     * Opcode order is **7, 8, 2, 3, 5, 11, 14** — not ascending.
     *
     * That is what the records actually contain: every one begins with the two sprite
     * ids and only then drops to the low opcodes, e.g.
     * `07 0b 97 | 08 0b 98 | 02 fa | 03 fa | 05 01 2c | 0b 01 18 | 0e 32 | 00`.
     * Emitting ascending order would round-trip semantically but move every field, so
     * byte-exactness would read 0% and the tracked figure would stop being a
     * regression signal.
     */
    if( entry->sprite_id_a >= 0 )
    {
        p1(&buffer, 7);
        p2(&buffer, entry->sprite_id_a);
    }
    if( entry->sprite_id_b >= 0 )
    {
        p1(&buffer, 8);
        p2(&buffer, entry->sprite_id_b);
    }
    if( entry->has_opcode_2 )
    {
        p1(&buffer, 2);
        p1(&buffer, entry->opcode_2);
    }
    if( entry->has_opcode_3 )
    {
        p1(&buffer, 3);
        p1(&buffer, entry->opcode_3);
    }
    if( entry->has_opcode_5 )
    {
        p1(&buffer, 5);
        p2(&buffer, entry->opcode_5);
    }
    if( entry->has_opcode_11 )
    {
        p1(&buffer, 11);
        p2(&buffer, entry->opcode_11);
    }
    if( entry->has_opcode_14 )
    {
        p1(&buffer, 14);
        p1(&buffer, entry->opcode_14);
    }

    p1(&buffer, 0);
    return buffer.position;
}
