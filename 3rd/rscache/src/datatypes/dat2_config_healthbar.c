#include "dat2_config_healthbar.h"

#include <assert.h>

void
RSCache_Dat2ConfigHealthbarDecode(
    struct RSCache_Dat2ConfigHealthbar* entry,
    struct RSCache_Buffer* buffer)
{
    assert(entry);
    assert(buffer);

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
    assert(entry);
    /* Nothing stated until an opcode says so. */
    RSCache_PresenceReset(&entry->present);
    entry->has_draw_order = false;
    entry->has_evict_priority = false;
    entry->has_persist_cycles = false;
    entry->has_fade_threshold = false;
    entry->has_width = false;
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
        RSCache_PresenceSet(&entry->present, RSCACHE_HEALTHBAR_FIELD_DRAW_ORDER);
        return true;
    case 3:
        entry->evict_priority = g1(buffer);
        entry->has_evict_priority = true;
        RSCache_PresenceSet(&entry->present, RSCACHE_HEALTHBAR_FIELD_EVICT_PRIORITY);
        return true;
    case 5:
        entry->persist_cycles = g2(buffer);
        entry->has_persist_cycles = true;
        RSCache_PresenceSet(&entry->present, RSCACHE_HEALTHBAR_FIELD_PERSIST_CYCLES);
        return true;
    case 7:
        entry->front_sprite_id = g2(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_HEALTHBAR_FIELD_FRONT_SPRITE);
        return true;
    case 8:
        entry->back_sprite_id = g2(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_HEALTHBAR_FIELD_BACK_SPRITE);
        return true;
    case 11:
        entry->fade_threshold = g2(buffer);
        entry->has_fade_threshold = true;
        RSCache_PresenceSet(&entry->present, RSCACHE_HEALTHBAR_FIELD_FADE_THRESHOLD);
        return true;
    case 14:
        entry->width = g1(buffer);
        entry->has_width = true;
        RSCache_PresenceSet(&entry->present, RSCACHE_HEALTHBAR_FIELD_WIDTH);
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
    assert(entry);
    /* No bytes is the empty record: the defaults, nothing stated. */
    if( data_size <= 0 )
    {
        RSCache_Dat2ConfigHealthbarInit(entry);
        return;
    }
    assert(data);

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
    assert(entry);
    assert(out);
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
    if( RSCache_PresenceHas(&entry->present, RSCACHE_HEALTHBAR_FIELD_FRONT_SPRITE) )
    {
        p1(&buffer, 7);
        p2(&buffer, entry->front_sprite_id);
    }
    if( RSCache_PresenceHas(&entry->present, RSCACHE_HEALTHBAR_FIELD_BACK_SPRITE) )
    {
        p1(&buffer, 8);
        p2(&buffer, entry->back_sprite_id);
    }
    if( RSCache_PresenceHas(&entry->present, RSCACHE_HEALTHBAR_FIELD_DRAW_ORDER) )
    {
        p1(&buffer, 2);
        p1(&buffer, entry->draw_order);
    }
    if( RSCache_PresenceHas(&entry->present, RSCACHE_HEALTHBAR_FIELD_EVICT_PRIORITY) )
    {
        p1(&buffer, 3);
        p1(&buffer, entry->evict_priority);
    }
    if( RSCache_PresenceHas(&entry->present, RSCACHE_HEALTHBAR_FIELD_PERSIST_CYCLES) )
    {
        p1(&buffer, 5);
        p2(&buffer, entry->persist_cycles);
    }
    if( RSCache_PresenceHas(&entry->present, RSCACHE_HEALTHBAR_FIELD_FADE_THRESHOLD) )
    {
        p1(&buffer, 11);
        p2(&buffer, entry->fade_threshold);
    }
    if( RSCache_PresenceHas(&entry->present, RSCACHE_HEALTHBAR_FIELD_WIDTH) )
    {
        p1(&buffer, 14);
        p1(&buffer, entry->width);
    }

    p1(&buffer, 0);
    return buffer.position;
}
