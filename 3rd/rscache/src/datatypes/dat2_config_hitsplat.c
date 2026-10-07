#include "dat2_config_hitsplat.h"

#include "../rscache_profile.h"

#include <assert.h>
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

int
RSCache_Dat2ConfigHitsplatFlags(const struct RSCache* cache)
{
    /* Lineage, not revision — see the header. Unidentified caches take the OSRS
     * set, matching what the previous unconditional decoder did and what every
     * OSRS cache in the tree wants. */
    if( cache && !RSCache_IsOsrs(cache) )
        return 0;
    return RSCACHE_CONFIG_HITSPLAT_DECODE_OSRS;
}

void
RSCache_Dat2ConfigHitsplatDecode(
    struct RSCache_Dat2ConfigHitsplat* entry,
    struct RSCache_Buffer* buffer,
    unsigned flags)
{
    assert(entry);
    assert(buffer);

    RSCache_Dat2ConfigHitsplatInit(entry);

    for( ;; )
    {
        if( buffer->position >= buffer->size )
            break;

        int opcode = g1(buffer);
        if( opcode == 0 )
            break;

        if( !RSCache_Dat2ConfigHitsplatDecodeOp(entry, opcode, buffer, flags) )
            return;
    }
}

void
RSCache_Dat2ConfigHitsplatInit(struct RSCache_Dat2ConfigHitsplat* entry)
{
    assert(entry);
    /* Nothing stated until an opcode says so. */
    RSCache_PresenceReset(&entry->present);
    /* Every one of these is the reference constructor's own pre-loop value
     * (`class420(class617)`), not a convenience.
     *
     * -1 for sprite_id, not 0: sprite 0 exists, so it cannot double as "no
     * sprite". This lived inside the decode loop until the per-opcode split,
     * which is a shape that loses it silently: a codec that only ever calls
     * `DecodeOp` would leave `sprite_id` at 0 and every splat would draw sprite
     * 0. Anything that decodes this type must run this first —
     * `opcode_codec.h`'s `record_init` exists for exactly this.
     *
     * `duration` and `slot_policy` matter the same way and are newer: a zeroed
     * record claims a splat that expires the cycle it appears and an eviction
     * policy of "overwrite the lowest cycle", neither of which is the default
     * behaviour. */
    entry->sprite_id = -1;
    entry->font_id = -1;
    entry->text_colour = 0xFFFFFF;
    entry->icon_sprite_id = -1;
    entry->left_sprite_id = -1;
    entry->right_sprite_id = -1;
    entry->fade_after = -1;
    entry->duration = 70;
    entry->slot_policy = -1;
    entry->variant_fallback = -1;
}

/** Opcode 8's operand: a marker byte, then a NUL-terminated string. The marker is
 *  what the old u16 reading swallowed; see the header. */
static bool
hitsplat_read_text(
    struct RSCache_Dat2ConfigHitsplat* entry,
    struct RSCache_Buffer* buffer)
{
    if( buffer->position >= buffer->size )
        return false;
    entry->text_marker = (uint8_t)g1(buffer);

    int out = 0;
    for( ;; )
    {
        if( buffer->position >= buffer->size )
            return false;
        int c = g1(buffer);
        if( c == 0 )
            break;
        if( out < RSCACHE_HITSPLAT_MAX_TEXT - 1 )
            entry->text[out++] = (char)c;
        else
            return false; /* Would truncate, which would not re-encode. */
    }
    entry->text[out] = '\0';
    return true;
}

/** Opcodes 17 and 18: a varbit id and a varp id (18 adds a fallback id), each a
 *  u16 with 65535 meaning -1, then a count and count+1 more of the same. See the
 *  header — this is the multi-var selector, the same shape as multiloc. */
static bool
hitsplat_read_variants(
    struct RSCache_Dat2ConfigHitsplat* entry,
    struct RSCache_Buffer* buffer,
    int opcode)
{
    int const wide = (opcode == 18) ? 3 : 2;

    if( buffer->position + (uint32_t)(wide * 2) > buffer->size )
        return false;

    int a = g2(buffer);
    int b = g2(buffer);
    int c = (opcode == 18) ? g2(buffer) : 65535;

    entry->variant_varbit = (a == 65535) ? -1 : a;
    entry->variant_varp = (b == 65535) ? -1 : b;
    entry->variant_fallback = (c == 65535) ? -1 : c;

    if( buffer->position >= buffer->size )
        return false;
    int count = g1(buffer);
    if( count + 1 > RSCACHE_HITSPLAT_MAX_VARIANTS )
        return false;
    if( buffer->position + (uint32_t)((count + 1) * 2) > buffer->size )
        return false;

    for( int i = 0; i <= count; i++ )
    {
        int v = g2(buffer);
        entry->variants[i] = (v == 65535) ? -1 : v;
    }
    entry->variant_count = count + 1;
    RSCache_PresenceSet(&entry->present, RSCACHE_HITSPLAT_FIELD_VARIANTS);
    /* The later of the two opcodes wins, fallback included. */
    if( opcode == 18 )
        RSCache_PresenceSet(&entry->present, RSCACHE_HITSPLAT_FIELD_VARIANT_FALLBACK);
    else
        RSCache_PresenceClear(&entry->present, RSCACHE_HITSPLAT_FIELD_VARIANT_FALLBACK);
    return true;
}

bool
RSCache_Dat2ConfigHitsplatDecodeOp(
    struct RSCache_Dat2ConfigHitsplat* entry,
    int opcode,
    struct RSCache_Buffer* buffer,
    unsigned flags)
{
    /* No flags means this cache's group 32 is not an OldSchool hitsplat stream.
     * Claim nothing, so the driver stops and leaves `_consumed` short. */
    if( !(flags & RSCACHE_CONFIG_HITSPLAT_DECODE_OSRS) )
        return false;

    switch( opcode )
    {
    case 1:
        entry->font_id = g2(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_HITSPLAT_FIELD_FONT);
        break;
    case 2:
        /* The one width with no measured sibling — see the header. */
        entry->text_colour = g3(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_HITSPLAT_FIELD_TEXT_COLOUR);
        break;
    case 3:
        entry->icon_sprite_id = g2(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_HITSPLAT_FIELD_ICON_SPRITE);
        break;
    case 4:
        entry->left_sprite_id = g2(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_HITSPLAT_FIELD_LEFT_SPRITE);
        break;
    case 5:
        entry->sprite_id = g2(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_HITSPLAT_FIELD_SPRITE);
        break;
    case 6:
        entry->right_sprite_id = g2(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_HITSPLAT_FIELD_RIGHT_SPRITE);
        break;
    case 7:
        entry->drift_x = g2(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_HITSPLAT_FIELD_DRIFT_X);
        break;
    case 8:
        if( !hitsplat_read_text(entry, buffer) )
            return false;
        RSCache_PresenceSet(&entry->present, RSCACHE_HITSPLAT_FIELD_TEXT);
        break;
    case 9:
        entry->duration = g2(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_HITSPLAT_FIELD_DURATION);
        break;
    case 10:
        entry->drift_up = g2(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_HITSPLAT_FIELD_DRIFT_UP);
        break;
    case 11:
        /* The reference sets the field to 0 here, with no operand. Opcode 14
         * sets the same field with a u16, so the two are distinguished for the
         * encoder rather than collapsed. */
        entry->fade_after = 0;
        RSCache_PresenceSet(&entry->present, RSCACHE_HITSPLAT_FIELD_FADE_FLAG);
        break;
    case 12:
        entry->slot_policy = g1(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_HITSPLAT_FIELD_SLOT_POLICY);
        break;
    case 13:
        entry->text_offset_y = g2(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_HITSPLAT_FIELD_TEXT_OFFSET_Y);
        break;
    case 14:
        entry->fade_after = g2(buffer);
        RSCache_PresenceSet(&entry->present, RSCACHE_HITSPLAT_FIELD_FADE_AFTER);
        break;
    case 17:
    case 18:
        if( !hitsplat_read_variants(entry, buffer, opcode) )
            return false;
        break;
    default:
        /* Unknown opcode: stop rather than guess a width. Opcodes 8 and 18 are
         * exactly why that matters — a wrong width on either makes the whole
         * record misparse with no other symptom, and opcode 8 is how this type
         * was mis-decoded for as long as it existed. */
        return false;
    }

    hitsplat_note_opcode(entry, opcode);
    return true;
}

void
RSCache_Dat2ConfigHitsplatDecodeInplace(
    struct RSCache_Dat2ConfigHitsplat* entry,
    const void* data,
    int data_size,
    unsigned flags)
{
    assert(entry);
    /* No bytes is the empty record: the defaults, nothing stated. */
    if( data_size <= 0 )
    {
        RSCache_Dat2ConfigHitsplatInit(entry);
        return;
    }
    assert(data);

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);
    RSCache_Dat2ConfigHitsplatDecode(entry, &buffer, flags);
    entry->_consumed = (int)buffer.position;
}

uint32_t
RSCache_Dat2ConfigHitsplatEncodeBound(const struct RSCache_Dat2ConfigHitsplat* entry)
{
    (void)entry;
    /* Eleven scalar opcodes at up to 4 bytes each, opcode 8's tag + marker +
     * string + NUL, opcode 17/18's tag + three u16 + count + the array, and the
     * terminator. */
    return (11u * 4u) + (2u + RSCACHE_HITSPLAT_MAX_TEXT) +
           (8u + (uint32_t)RSCACHE_HITSPLAT_MAX_VARIANTS * 2u) + 1u;
}

/** The field `opcode` states, or -1 for an opcode this type does not have. */
static int
hitsplat_field_for_opcode(int opcode)
{
    switch( opcode )
    {
    case 1:
        return RSCACHE_HITSPLAT_FIELD_FONT;
    case 2:
        return RSCACHE_HITSPLAT_FIELD_TEXT_COLOUR;
    case 3:
        return RSCACHE_HITSPLAT_FIELD_ICON_SPRITE;
    case 4:
        return RSCACHE_HITSPLAT_FIELD_LEFT_SPRITE;
    case 5:
        return RSCACHE_HITSPLAT_FIELD_SPRITE;
    case 6:
        return RSCACHE_HITSPLAT_FIELD_RIGHT_SPRITE;
    case 7:
        return RSCACHE_HITSPLAT_FIELD_DRIFT_X;
    case 8:
        return RSCACHE_HITSPLAT_FIELD_TEXT;
    case 9:
        return RSCACHE_HITSPLAT_FIELD_DURATION;
    case 10:
        return RSCACHE_HITSPLAT_FIELD_DRIFT_UP;
    case 11:
        return RSCACHE_HITSPLAT_FIELD_FADE_FLAG;
    case 12:
        return RSCACHE_HITSPLAT_FIELD_SLOT_POLICY;
    case 13:
        return RSCACHE_HITSPLAT_FIELD_TEXT_OFFSET_Y;
    case 14:
        return RSCACHE_HITSPLAT_FIELD_FADE_AFTER;
    case 17:
    case 18:
        /* One selector; which opcode carries it is VARIANT_FALLBACK's presence. */
        return RSCACHE_HITSPLAT_FIELD_VARIANTS;
    default:
        return -1;
    }
}

/** Write the opcode that states `field`, with its operand. */
static void
hitsplat_write_field(
    struct RSCache_Buffer* buffer,
    const struct RSCache_Dat2ConfigHitsplat* entry,
    int field)
{
    switch( field )
    {
    case RSCACHE_HITSPLAT_FIELD_FONT:
        p1(buffer, 1);
        p2(buffer, entry->font_id);
        break;
    case RSCACHE_HITSPLAT_FIELD_TEXT_COLOUR:
        p1(buffer, 2);
        p3(buffer, entry->text_colour);
        break;
    case RSCACHE_HITSPLAT_FIELD_ICON_SPRITE:
        p1(buffer, 3);
        p2(buffer, entry->icon_sprite_id);
        break;
    case RSCACHE_HITSPLAT_FIELD_LEFT_SPRITE:
        p1(buffer, 4);
        p2(buffer, entry->left_sprite_id);
        break;
    case RSCACHE_HITSPLAT_FIELD_SPRITE:
        p1(buffer, 5);
        p2(buffer, entry->sprite_id);
        break;
    case RSCACHE_HITSPLAT_FIELD_RIGHT_SPRITE:
        p1(buffer, 6);
        p2(buffer, entry->right_sprite_id);
        break;
    case RSCACHE_HITSPLAT_FIELD_DRIFT_X:
        p1(buffer, 7);
        p2(buffer, entry->drift_x);
        break;
    case RSCACHE_HITSPLAT_FIELD_TEXT:
        p1(buffer, 8);
        p1(buffer, entry->text_marker);
        for( int i = 0; entry->text[i] != '\0'; i++ )
            p1(buffer, (uint8_t)entry->text[i]);
        p1(buffer, 0);
        break;
    case RSCACHE_HITSPLAT_FIELD_DURATION:
        p1(buffer, 9);
        p2(buffer, entry->duration);
        break;
    case RSCACHE_HITSPLAT_FIELD_DRIFT_UP:
        p1(buffer, 10);
        p2(buffer, entry->drift_up);
        break;
    case RSCACHE_HITSPLAT_FIELD_FADE_FLAG:
        p1(buffer, 11);
        break;
    case RSCACHE_HITSPLAT_FIELD_SLOT_POLICY:
        p1(buffer, 12);
        p1(buffer, entry->slot_policy);
        break;
    case RSCACHE_HITSPLAT_FIELD_TEXT_OFFSET_Y:
        p1(buffer, 13);
        p2(buffer, entry->text_offset_y);
        break;
    case RSCACHE_HITSPLAT_FIELD_FADE_AFTER:
        p1(buffer, 14);
        p2(buffer, entry->fade_after);
        break;
    case RSCACHE_HITSPLAT_FIELD_VARIANTS:
    {
        bool const has_fallback =
            RSCache_PresenceHas(&entry->present, RSCACHE_HITSPLAT_FIELD_VARIANT_FALLBACK);
        /* The wire count is one less than the ids it is followed by, so a
         * selector always carries at least one. */
        assert(entry->variant_count >= 1);
        assert(entry->variant_count <= RSCACHE_HITSPLAT_MAX_VARIANTS);
        p1(buffer, has_fallback ? 18 : 17);
        p2(buffer, entry->variant_varbit < 0 ? 65535 : entry->variant_varbit);
        p2(buffer, entry->variant_varp < 0 ? 65535 : entry->variant_varp);
        if( has_fallback )
            p2(buffer, entry->variant_fallback < 0 ? 65535 : entry->variant_fallback);
        p1(buffer, entry->variant_count - 1);
        for( int i = 0; i < entry->variant_count; i++ )
            p2(buffer, entry->variants[i] < 0 ? 65535 : entry->variants[i]);
        break;
    }
    default:
        assert(!"hitsplat: no opcode states this field");
        break;
    }
}

uint32_t
RSCache_Dat2ConfigHitsplatEncode(
    const struct RSCache_Dat2ConfigHitsplat* entry,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(entry);
    assert(out);
    /* The fallback is opcode 18's third u16: there is no stating it without the
     * selector it belongs to. */
    assert(!RSCache_PresenceHas(&entry->present, RSCACHE_HITSPLAT_FIELD_VARIANT_FALLBACK) ||
           RSCache_PresenceHas(&entry->present, RSCACHE_HITSPLAT_FIELD_VARIANTS));
    if( out_capacity < RSCache_Dat2ConfigHitsplatEncodeBound(entry) )
        return 0;

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* Replay the recorded order -- hitsplat records do not share one, see the
     * header -- writing each stated field once. Then any stated field the order
     * does not name (a hand-built record has no order at all), ascending, so a
     * field cannot be stated and still go unwritten. */
    struct RSCache_Presence written;
    RSCache_PresenceReset(&written);
    for( int i = 0; i < entry->opcode_count; i++ )
    {
        int const field = hitsplat_field_for_opcode(entry->opcodes[i]);
        if( field < 0 || !RSCache_PresenceHas(&entry->present, field) ||
            RSCache_PresenceHas(&written, field) )
            continue;
        hitsplat_write_field(&buffer, entry, field);
        RSCache_PresenceSet(&written, field);
    }
    for( int field = 0; field < RSCACHE_HITSPLAT_FIELD_COUNT; field++ )
    {
        if( field == RSCACHE_HITSPLAT_FIELD_VARIANT_FALLBACK )
            continue; /* written inside VARIANTS */
        if( !RSCache_PresenceHas(&entry->present, field) || RSCache_PresenceHas(&written, field) )
            continue;
        hitsplat_write_field(&buffer, entry, field);
        RSCache_PresenceSet(&written, field);
    }

    p1(&buffer, 0);
    return buffer.position;
}
