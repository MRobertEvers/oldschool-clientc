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
    entry->front_sprite_id = -1;
    entry->back_sprite_id = -1;
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
        entry->draw_order = g1(buffer);
        entry->has_draw_order = true;
        return true;
    case 3:
        entry->evict_priority = g1(buffer);
        entry->has_evict_priority = true;
        return true;
    case 5:
        entry->persist_cycles = g2(buffer);
        entry->has_persist_cycles = true;
        return true;
    case 7:
        entry->front_sprite_id = g2(buffer);
        return true;
    case 8:
        entry->back_sprite_id = g2(buffer);
        return true;
    case 11:
        entry->fade_threshold = g2(buffer);
        entry->has_fade_threshold = true;
        return true;
    case 14:
        entry->width = g1(buffer);
        entry->has_width = true;
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
        entry->front_sprite_id = -1;
        entry->back_sprite_id = -1;
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
    if( entry->front_sprite_id >= 0 )
    {
        p1(&buffer, 7);
        p2(&buffer, entry->front_sprite_id);
    }
    if( entry->back_sprite_id >= 0 )
    {
        p1(&buffer, 8);
        p2(&buffer, entry->back_sprite_id);
    }
    if( entry->has_draw_order )
    {
        p1(&buffer, 2);
        p1(&buffer, entry->draw_order);
    }
    if( entry->has_evict_priority )
    {
        p1(&buffer, 3);
        p1(&buffer, entry->evict_priority);
    }
    if( entry->has_persist_cycles )
    {
        p1(&buffer, 5);
        p2(&buffer, entry->persist_cycles);
    }
    if( entry->has_fade_threshold )
    {
        p1(&buffer, 11);
        p2(&buffer, entry->fade_threshold);
    }
    if( entry->has_width )
    {
        p1(&buffer, 14);
        p1(&buffer, entry->width);
    }

    p1(&buffer, 0);
    return buffer.position;
}
