#include "dat2_config_mapelement.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAPELEMENT_SET(entry, field)                                                               \
    RSCache_PresenceSet(&(entry)->present, RSCACHE_MAPELEMENT_FIELD_##field)
#define MAPELEMENT_HAS(entry, field)                                                               \
    RSCache_PresenceHas(&(entry)->present, RSCACHE_MAPELEMENT_FIELD_##field)

void
RSCache_MapElementInit(struct RSCache_MapElement* entry)
{
    assert(entry);
    RSCache_PresenceReset(&entry->present);
    /* -1, not 0: sprite 0 and category 0 both exist. */
    entry->sprite_id = -1;
    entry->hover_sprite_id = -1;
    entry->category = -1;
    entry->visibility.varbit = -1;
    entry->visibility.varp = -1;
    entry->visibility2.varbit = -1;
    entry->visibility2.varp = -1;
}

static int
read_var_id(struct RSCache_Buffer* buffer)
{
    int id = RSCache_BufferG2(buffer);
    return id == 65535 ? -1 : id;
}

static void
read_visibility(
    struct RSCache_Buffer* buffer,
    struct RSCache_MapElementVisibility* visibility)
{
    visibility->varbit = read_var_id(buffer);
    visibility->varp = read_var_id(buffer);
    visibility->min = RSCache_BufferG4(buffer);
    visibility->max = RSCache_BufferG4(buffer);
}

static void
read_polygon(
    struct RSCache_MapElement* entry,
    struct RSCache_Buffer* buffer)
{
    int count = RSCache_BufferG1(buffer);

    free(entry->polygon_points);
    free(entry->polygon_point_flags);
    free(entry->polygon_extra);
    entry->polygon_points = NULL;
    entry->polygon_point_flags = NULL;
    entry->polygon_extra = NULL;

    entry->polygon_point_count = count;
    if( count > 0 )
    {
        entry->polygon_points = malloc((size_t)count * 2 * sizeof(int));
        assert(entry->polygon_points);
        entry->polygon_point_flags = malloc((size_t)count * sizeof(int));
        assert(entry->polygon_point_flags);
    }
    for( int i = 0; i < count * 2; i++ )
        entry->polygon_points[i] = RSCache_BufferG2b(buffer);
    entry->polygon_fill = RSCache_BufferG4(buffer);
    entry->polygon_extra_count = RSCache_BufferG1(buffer);
    if( entry->polygon_extra_count > 0 )
    {
        entry->polygon_extra = malloc((size_t)entry->polygon_extra_count * sizeof(int));
        assert(entry->polygon_extra);
    }
    for( int i = 0; i < entry->polygon_extra_count; i++ )
        entry->polygon_extra[i] = RSCache_BufferG4(buffer);
    for( int i = 0; i < count; i++ )
        entry->polygon_point_flags[i] = RSCache_BufferG1b(buffer);
}

static void
replace_string(
    char** slot,
    struct RSCache_Buffer* buffer)
{
    free(*slot);
    *slot = RSCache_BufferReadStringNullTerminated(buffer);
}

void
RSCache_MapElementDecodeInplace(
    struct RSCache_MapElement* entry,
    const void* data,
    int data_size)
{
    struct RSCache_Buffer buffer;

    assert(entry);
    RSCache_MapElementInit(entry);
    entry->_consumed = 0;
    /* No bytes is the empty record: the defaults, nothing stated. */
    if( data_size <= 0 )
        return;
    assert(data);

    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)data_size);

    for( ;; )
    {
        if( buffer.position >= buffer.size )
            break;
        int opcode = RSCache_BufferG1(&buffer);
        if( opcode == 0 )
            break;

        switch( opcode )
        {
        case 1:
            entry->sprite_id = RSCache_BufferReadBigSmart(&buffer);
            MAPELEMENT_SET(entry, SPRITE);
            break;
        case 2:
            entry->hover_sprite_id = RSCache_BufferReadBigSmart(&buffer);
            MAPELEMENT_SET(entry, HOVER_SPRITE);
            break;
        case 3:
            replace_string(&entry->name, &buffer);
            MAPELEMENT_SET(entry, NAME);
            break;
        case 4:
            entry->text_colour = RSCache_BufferG3(&buffer);
            MAPELEMENT_SET(entry, TEXT_COLOUR);
            break;
        case 5:
            entry->hover_text_colour = RSCache_BufferG3(&buffer);
            MAPELEMENT_SET(entry, HOVER_TEXT_COLOUR);
            break;
        case 6:
            entry->text_size = RSCache_BufferG1(&buffer);
            MAPELEMENT_SET(entry, TEXT_SIZE);
            break;
        case 7:
            entry->flags = RSCache_BufferG1(&buffer);
            MAPELEMENT_SET(entry, FLAGS);
            break;
        case 8:
            entry->randomize_position = RSCache_BufferG1(&buffer);
            MAPELEMENT_SET(entry, RANDOMIZE_POSITION);
            break;
        case 9:
            read_visibility(&buffer, &entry->visibility);
            MAPELEMENT_SET(entry, VISIBILITY);
            break;
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
            replace_string(&entry->ops[opcode - 10], &buffer);
            RSCache_PresenceSet(&entry->present, RSCACHE_MAPELEMENT_FIELD_OP1 + (opcode - 10));
            break;
        case 15:
            read_polygon(entry, &buffer);
            MAPELEMENT_SET(entry, POLYGON);
            break;
        case 16:
            MAPELEMENT_SET(entry, UNKNOWN16);
            break;
        case 17:
            replace_string(&entry->target_name, &buffer);
            MAPELEMENT_SET(entry, TARGET_NAME);
            break;
        case 18:
            entry->unknown18 = RSCache_BufferReadBigSmart(&buffer);
            MAPELEMENT_SET(entry, UNKNOWN18);
            break;
        case 19:
            entry->category = RSCache_BufferG2(&buffer);
            MAPELEMENT_SET(entry, CATEGORY);
            break;
        case 20:
            read_visibility(&buffer, &entry->visibility2);
            MAPELEMENT_SET(entry, VISIBILITY2);
            break;
        case 21:
            entry->unknown21 = RSCache_BufferG4(&buffer);
            MAPELEMENT_SET(entry, UNKNOWN21);
            break;
        case 22:
            entry->unknown22 = RSCache_BufferG4(&buffer);
            MAPELEMENT_SET(entry, UNKNOWN22);
            break;
        case 23:
            for( int i = 0; i < 3; i++ )
                entry->unknown23[i] = RSCache_BufferG1(&buffer);
            MAPELEMENT_SET(entry, UNKNOWN23);
            break;
        case 24:
            for( int i = 0; i < 2; i++ )
                entry->unknown24[i] = RSCache_BufferG2b(&buffer);
            MAPELEMENT_SET(entry, UNKNOWN24);
            break;
        case 25:
            entry->unknown25 = RSCache_BufferReadBigSmart(&buffer);
            MAPELEMENT_SET(entry, UNKNOWN25);
            break;
        case 28:
            entry->unknown28 = RSCache_BufferG1(&buffer);
            MAPELEMENT_SET(entry, UNKNOWN28);
            break;
        case 29:
            entry->horizontal_align = RSCache_BufferG1(&buffer);
            MAPELEMENT_SET(entry, HORIZONTAL_ALIGN);
            break;
        case 30:
            entry->vertical_align = RSCache_BufferG1(&buffer);
            MAPELEMENT_SET(entry, VERTICAL_ALIGN);
            break;
        case 249:
            RSCache_BufferReadParams(&buffer, &entry->params);
            MAPELEMENT_SET(entry, PARAMS);
            break;
        default:
            /* Everything after an unknown opcode is misaligned, so stop rather
             * than decode garbage into the fields already read. The short
             * `_consumed` is the signal. */
            printf(
                "RSCache_MapElementDecodeInplace: map element %d unknown opcode %d\n",
                entry->id,
                opcode);
            entry->_consumed = (int)buffer.position;
            return;
        }
    }
    entry->_consumed = (int)buffer.position;
}

static void
write_visibility(
    struct RSCache_Buffer* buffer,
    const struct RSCache_MapElementVisibility* visibility)
{
    p2(buffer, visibility->varbit < 0 ? 65535 : visibility->varbit);
    p2(buffer, visibility->varp < 0 ? 65535 : visibility->varp);
    p4(buffer, visibility->min);
    p4(buffer, visibility->max);
}

static void
write_string(
    struct RSCache_Buffer* buffer,
    int opcode,
    const char* value)
{
    assert(value);
    p1(buffer, opcode);
    pjstr(buffer, value, RSCACHE_JSTR_TERMINATOR_NULL);
}

uint32_t
RSCache_MapElementEncode(
    const struct RSCache_MapElement* entry,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(entry);
    assert(out);

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* The two shapes in the caches -- labels `3,6,4,19,8[,7]` and icons
     * `19,1,[10],8,[7],[17]` -- interleave into this one order, so both
     * re-encode byte-exactly. */
    if( MAPELEMENT_HAS(entry, NAME) )
        write_string(&buffer, 3, entry->name);
    if( MAPELEMENT_HAS(entry, TEXT_SIZE) )
    {
        p1(&buffer, 6);
        p1(&buffer, entry->text_size);
    }
    if( MAPELEMENT_HAS(entry, TEXT_COLOUR) )
    {
        p1(&buffer, 4);
        p3(&buffer, entry->text_colour);
    }
    if( MAPELEMENT_HAS(entry, CATEGORY) )
    {
        p1(&buffer, 19);
        p2(&buffer, entry->category);
    }
    if( MAPELEMENT_HAS(entry, SPRITE) )
    {
        p1(&buffer, 1);
        pbigsmart(&buffer, entry->sprite_id);
    }
    for( int i = 0; i < RSCACHE_MAPELEMENT_OPS; i++ )
    {
        if( RSCache_PresenceHas(&entry->present, RSCACHE_MAPELEMENT_FIELD_OP1 + i) )
            write_string(&buffer, 10 + i, entry->ops[i]);
    }
    if( MAPELEMENT_HAS(entry, RANDOMIZE_POSITION) )
    {
        p1(&buffer, 8);
        p1(&buffer, entry->randomize_position);
    }
    if( MAPELEMENT_HAS(entry, FLAGS) )
    {
        p1(&buffer, 7);
        p1(&buffer, entry->flags);
    }
    if( MAPELEMENT_HAS(entry, TARGET_NAME) )
        write_string(&buffer, 17, entry->target_name);

    /* No cache in the tree carries any of these; ascending. */
    if( MAPELEMENT_HAS(entry, HOVER_SPRITE) )
    {
        p1(&buffer, 2);
        pbigsmart(&buffer, entry->hover_sprite_id);
    }
    if( MAPELEMENT_HAS(entry, HOVER_TEXT_COLOUR) )
    {
        p1(&buffer, 5);
        p3(&buffer, entry->hover_text_colour);
    }
    if( MAPELEMENT_HAS(entry, VISIBILITY) )
    {
        p1(&buffer, 9);
        write_visibility(&buffer, &entry->visibility);
    }
    if( MAPELEMENT_HAS(entry, POLYGON) )
    {
        int count = entry->polygon_point_count;
        assert(count >= 0);
        assert(count <= 255);
        assert(count == 0 || entry->polygon_points);
        assert(count == 0 || entry->polygon_point_flags);
        assert(entry->polygon_extra_count >= 0);
        assert(entry->polygon_extra_count <= 255);
        assert(entry->polygon_extra_count == 0 || entry->polygon_extra);
        p1(&buffer, 15);
        p1(&buffer, count);
        for( int i = 0; i < count * 2; i++ )
            p2(&buffer, entry->polygon_points[i]);
        p4(&buffer, entry->polygon_fill);
        p1(&buffer, entry->polygon_extra_count);
        for( int i = 0; i < entry->polygon_extra_count; i++ )
            p4(&buffer, entry->polygon_extra[i]);
        for( int i = 0; i < count; i++ )
            p1(&buffer, entry->polygon_point_flags[i]);
    }
    if( MAPELEMENT_HAS(entry, UNKNOWN16) )
        p1(&buffer, 16);
    if( MAPELEMENT_HAS(entry, UNKNOWN18) )
    {
        p1(&buffer, 18);
        pbigsmart(&buffer, entry->unknown18);
    }
    if( MAPELEMENT_HAS(entry, VISIBILITY2) )
    {
        p1(&buffer, 20);
        write_visibility(&buffer, &entry->visibility2);
    }
    if( MAPELEMENT_HAS(entry, UNKNOWN21) )
    {
        p1(&buffer, 21);
        p4(&buffer, entry->unknown21);
    }
    if( MAPELEMENT_HAS(entry, UNKNOWN22) )
    {
        p1(&buffer, 22);
        p4(&buffer, entry->unknown22);
    }
    if( MAPELEMENT_HAS(entry, UNKNOWN23) )
    {
        p1(&buffer, 23);
        for( int i = 0; i < 3; i++ )
            p1(&buffer, entry->unknown23[i]);
    }
    if( MAPELEMENT_HAS(entry, UNKNOWN24) )
    {
        p1(&buffer, 24);
        for( int i = 0; i < 2; i++ )
            p2(&buffer, entry->unknown24[i]);
    }
    if( MAPELEMENT_HAS(entry, UNKNOWN25) )
    {
        p1(&buffer, 25);
        pbigsmart(&buffer, entry->unknown25);
    }
    if( MAPELEMENT_HAS(entry, UNKNOWN28) )
    {
        p1(&buffer, 28);
        p1(&buffer, entry->unknown28);
    }
    if( MAPELEMENT_HAS(entry, HORIZONTAL_ALIGN) )
    {
        p1(&buffer, 29);
        p1(&buffer, entry->horizontal_align);
    }
    if( MAPELEMENT_HAS(entry, VERTICAL_ALIGN) )
    {
        p1(&buffer, 30);
        p1(&buffer, entry->vertical_align);
    }
    if( MAPELEMENT_HAS(entry, PARAMS) )
    {
        p1(&buffer, 249);
        pparams(&buffer, &entry->params);
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
    for( int i = 0; i < RSCACHE_MAPELEMENT_OPS; i++ )
        free(entry->ops[i]);
    free(entry->target_name);
    free(entry->polygon_points);
    free(entry->polygon_point_flags);
    free(entry->polygon_extra);
    for( int i = 0; i < entry->params.count; i++ )
        free(entry->params.values[i]);
    free(entry->params.values);
    free(entry->params.keys);
    free(entry->params.kinds);
    memset(entry, 0, sizeof(*entry));
}

static uint32_t
string_bound(const char* value)
{
    return value ? 2u + (uint32_t)strlen(value) + 1u : 0u;
}

uint32_t
RSCache_MapElementEncodeBound(const struct RSCache_MapElement* entry)
{
    /* Every fixed-width opcode at its widest (the visibilities are 13 bytes each,
     * the rest at most 5), plus the variable ones, plus the terminator. */
    uint32_t need = 128u;

    assert(entry);
    need += string_bound(entry->name);
    need += string_bound(entry->target_name);
    for( int i = 0; i < RSCACHE_MAPELEMENT_OPS; i++ )
        need += string_bound(entry->ops[i]);
    need += 7u + (uint32_t)entry->polygon_point_count * 5u +
            (uint32_t)entry->polygon_extra_count * 4u;
    need += 1u + RSCache_BufferParamsBound(&entry->params);
    return need;
}
