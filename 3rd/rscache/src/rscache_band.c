#include "rscache_band.h"

#include "rsbuffer.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
RSCache_BandRecordReset(struct RSCache_BandRecord* record)
{
    assert(record);
    memset(record, 0, sizeof(*record));
}

void
RSCache_BandRecordFree(struct RSCache_BandRecord* record)
{
    if( !record )
        return;
    for( int i = 0; i < RSCACHE_REGISTER_MAX; i++ )
    {
        struct RSCache_BandList* list = record->lists[i];

        free(record->strings[i]);
        if( list )
        {
            for( int k = 0; k < list->count * list->arity; k++ )
                free(list->items[k].s);
            free(list->items);
            free(list);
        }
    }
    memset(record, 0, sizeof(*record));
}

void
RSCache_BandRecordSetString(
    struct RSCache_BandRecord* record,
    int index,
    const char* value)
{
    assert(record);
    assert(index >= 0);
    assert(index < RSCACHE_REGISTER_MAX);
    assert(value);
    free(record->strings[index]);
    record->strings[index] = strdup(value);
    assert(record->strings[index]);
    RSCache_PresenceSet(&record->present, index);
}

void
RSCache_BandRecordAppendTuple(
    struct RSCache_BandRecord* record,
    int index,
    const struct RSCache_BandValue* tuple,
    int arity)
{
    struct RSCache_BandList* list;

    assert(record);
    assert(index >= 0);
    assert(index < RSCACHE_REGISTER_MAX);
    assert(arity >= 0);
    assert(tuple || arity == 0);
    list = record->lists[index];
    if( !list )
    {
        list = calloc(1, sizeof(*list));
        assert(list);
        record->lists[index] = list;
        RSCache_PresenceSet(&record->present, index);
    }
    if( arity == 0 )
        return;
    assert(list->arity == 0 || list->arity == arity);
    list->arity = arity;
    if( (list->count + 1) * arity > list->capacity )
    {
        int next = list->capacity ? list->capacity * 2 : arity * 8;
        while( next < (list->count + 1) * arity )
            next *= 2;
        list->items = realloc(list->items, (size_t)next * sizeof(*list->items));
        assert(list->items);
        list->capacity = next;
    }
    for( int e = 0; e < arity; e++ )
    {
        struct RSCache_BandValue* slot = &list->items[list->count * arity + e];

        slot->i = tuple[e].i;
        slot->s = NULL;
        if( tuple[e].s )
        {
            slot->s = strdup(tuple[e].s);
            assert(slot->s);
        }
    }
    list->count++;
}

int
RSCache_BandElementIsString(
    const struct RSCache_Register* reg,
    int index,
    int element)
{
    assert(reg);
    assert(index >= 0);
    assert(index < reg->band_count);
    assert(element >= 0);
    assert(element < reg->entries[index].type_count);
    return strcmp(reg->entries[index].types[element], "string") == 0;
}

void
RSCache_BandRecordSet(
    struct RSCache_BandRecord* record,
    int index,
    int32_t value)
{
    assert(record);
    assert(index >= 0);
    assert(index < RSCACHE_REGISTER_MAX);
    record->values[index] = value;
    RSCache_PresenceSet(&record->present, index);
}

int
RSCache_BandIndex(
    const struct RSCache_Register* reg,
    const char* name)
{
    assert(reg);
    assert(name);
    for( int i = 0; i < reg->band_count; i++ )
    {
        if( strcmp(reg->entries[i].name, name) == 0 )
            return i;
    }
    return -1;
}

int
RSCache_BandFits(
    const struct RSCache_Register* reg,
    int index,
    int32_t value)
{
    assert(reg);
    assert(index >= 0);
    assert(index < reg->band_count);
    switch( reg->entries[index].wire )
    {
    case RSCACHE_REGISTER_WIRE_U1:
        return value >= 0 && value <= 0xFF;
    case RSCACHE_REGISTER_WIRE_U2:
        return value >= 0 && value <= 0xFFFF;
    case RSCACHE_REGISTER_WIRE_U4:
        /* Every 32-bit pattern reads back as the int it was, so -1 (an absent
         * reference, `death_drop` dropping nothing) is representable here. */
        return 1;
    default:
        return 0;
    }
}

uint32_t
RSCache_BandEncodeBound(
    const struct RSCache_Register* reg,
    const struct RSCache_BandRecord* record)
{
    uint32_t bound = 1;

    assert(reg);
    assert(record);
    for( int i = 0; i < reg->band_count; i++ )
    {
        const struct RSCache_BandList* list = record->lists[i];

        bound += 5;
        if( record->strings[i] )
            bound += (uint32_t)strlen(record->strings[i]) + 1;
        if( list )
        {
            bound += 2;
            for( int k = 0; k < list->count * list->arity; k++ )
                bound += 4 + (list->items[k].s ? (uint32_t)strlen(list->items[k].s) + 1 : 0);
        }
    }
    return bound;
}

static void
put_string(
    struct RSCache_Buffer* buffer,
    const char* s)
{
    size_t n = strlen(s);

    for( size_t k = 0; k < n; k++ )
        RSCache_BufferP1(buffer, (uint8_t)s[k]);
    RSCache_BufferP1(buffer, 0);
}

/* A NUL-terminated string at the cursor, or NULL if the stream ends first. */
static char*
get_string(struct RSCache_Buffer* buffer)
{
    uint32_t start = buffer->position;
    uint32_t end = start;
    char* out;

    while( end < buffer->size && buffer->data[end] != 0 )
        end++;
    if( end >= buffer->size )
        return NULL;
    out = malloc((size_t)(end - start) + 1);
    assert(out);
    memcpy(out, buffer->data + start, (size_t)(end - start));
    out[end - start] = '\0';
    buffer->position = end + 1;
    return out;
}

uint32_t
RSCache_BandEncode(
    const struct RSCache_Register* reg,
    const struct RSCache_BandRecord* record,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Buffer buffer;

    assert(reg);
    assert(record);
    assert(out);
    assert(out_capacity >= RSCache_BandEncodeBound(reg, record));
    RSCache_BufferInit(&buffer, out, out_capacity);

    /* `entries[0, band_count)` is ascending by opcode, so the stream is too. */
    for( int i = 0; i < reg->band_count; i++ )
    {
        const struct RSCache_RegisterField* field = &reg->entries[i];
        int32_t value = record->values[i];

        if( !RSCache_PresenceHas(&record->present, i) )
            continue;
        if( field->wire == RSCACHE_REGISTER_WIRE_STRING )
        {
            assert(record->strings[i]);
            RSCache_BufferP1(&buffer, field->opcode);
            put_string(&buffer, record->strings[i]);
            continue;
        }
        if( field->wire == RSCACHE_REGISTER_WIRE_LIST )
        {
            const struct RSCache_BandList* list = record->lists[i];

            assert(list);
            assert(list->count == 0 || list->arity == field->type_count);
            assert(list->count <= 0xFFFF);
            RSCache_BufferP1(&buffer, field->opcode);
            RSCache_BufferP2(&buffer, list->count);
            for( int t = 0; t < list->count; t++ )
            {
                for( int e = 0; e < field->type_count; e++ )
                {
                    const struct RSCache_BandValue* v = &list->items[t * list->arity + e];

                    if( RSCache_BandElementIsString(reg, i, e) )
                    {
                        assert(v->s);
                        put_string(&buffer, v->s);
                    }
                    else
                        RSCache_BufferP4(&buffer, v->i);
                }
            }
            continue;
        }
        assert(RSCache_BandFits(reg, i, value));
        RSCache_BufferP1(&buffer, field->opcode);
        if( field->wire == RSCACHE_REGISTER_WIRE_U1 )
            RSCache_BufferP1(&buffer, value);
        else if( field->wire == RSCACHE_REGISTER_WIRE_U2 )
            RSCache_BufferP2(&buffer, value);
        else
            RSCache_BufferP4(&buffer, value);
    }
    RSCache_BufferP1(&buffer, 0);
    return RSCache_BufferLength(&buffer);
}

static uint32_t
wire_width(enum RSCache_RegisterWire wire)
{
    if( wire == RSCACHE_REGISTER_WIRE_U1 )
        return 1;
    if( wire == RSCACHE_REGISTER_WIRE_U2 )
        return 2;
    return 4;
}

int
RSCache_BandDecode(
    const struct RSCache_Register* reg,
    struct RSCache_BandRecord* record,
    const uint8_t* data,
    int size)
{
    struct RSCache_Buffer buffer;

    assert(reg);
    assert(record);
    assert(size >= 0);
    assert(data || size == 0);
    RSCache_BandRecordReset(record);
    /* The cursor takes a writable pointer because it is shared with the encoder;
     * only `g` functions run here. */
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)size);
    while( RSCache_BufferRemaining(&buffer) > 0 )
    {
        int opcode = RSCache_BufferG1(&buffer);
        int index = -1;

        if( opcode == 0 )
            break;
        for( int i = 0; i < reg->band_count; i++ )
        {
            if( reg->entries[i].opcode == opcode )
            {
                index = i;
                break;
            }
        }
        if( index < 0 )
            return -1;
        if( reg->entries[index].wire == RSCACHE_REGISTER_WIRE_STRING )
        {
            char* text = get_string(&buffer);

            if( !text )
                return -1;
            free(record->strings[index]);
            record->strings[index] = text;
            RSCache_PresenceSet(&record->present, index);
            continue;
        }
        if( reg->entries[index].wire == RSCACHE_REGISTER_WIRE_LIST )
        {
            const struct RSCache_RegisterField* field = &reg->entries[index];
            int count;

            if( RSCache_BufferRemaining(&buffer) < 2 )
                return -1;
            count = RSCache_BufferG2(&buffer);
            RSCache_BandRecordAppendTuple(record, index, NULL, 0);
            for( int t = 0; t < count; t++ )
            {
                struct RSCache_BandValue tuple[RSCACHE_REGISTER_TYPES_MAX];

                memset(tuple, 0, sizeof(tuple));
                for( int e = 0; e < field->type_count; e++ )
                {
                    if( RSCache_BandElementIsString(reg, index, e) )
                    {
                        tuple[e].s = get_string(&buffer);
                        if( !tuple[e].s )
                        {
                            for( int k = 0; k < e; k++ )
                                free(tuple[k].s);
                            return -1;
                        }
                    }
                    else
                    {
                        if( RSCache_BufferRemaining(&buffer) < 4 )
                        {
                            for( int k = 0; k < e; k++ )
                                free(tuple[k].s);
                            return -1;
                        }
                        tuple[e].i = (int32_t)RSCache_BufferG4(&buffer);
                    }
                }
                RSCache_BandRecordAppendTuple(record, index, tuple, field->type_count);
                for( int e = 0; e < field->type_count; e++ )
                    free(tuple[e].s);
            }
            continue;
        }
        /* A payload cut short is not a stated 0: the stream is unreadable. */
        if( RSCache_BufferRemaining(&buffer) < wire_width(reg->entries[index].wire) )
            return -1;
        if( reg->entries[index].wire == RSCACHE_REGISTER_WIRE_U1 )
            RSCache_BandRecordSet(record, index, RSCache_BufferG1(&buffer));
        else if( reg->entries[index].wire == RSCACHE_REGISTER_WIRE_U2 )
            RSCache_BandRecordSet(record, index, RSCache_BufferG2(&buffer));
        else
            RSCache_BandRecordSet(record, index, (int32_t)RSCache_BufferG4(&buffer));
    }
    return (int)buffer.position;
}

int
RSCache_BandBindingApply(
    const struct RSCache_Register* reg,
    const struct RSCache_BandBinding* bindings,
    int binding_count,
    const struct RSCache_BandRecord* record,
    void* object)
{
    int written = 0;

    assert(reg);
    assert(bindings || binding_count == 0);
    assert(record);
    assert(object);
    for( int b = 0; b < binding_count; b++ )
    {
        int index = RSCache_BandIndex(reg, bindings[b].name);
        unsigned char* member = (unsigned char*)object + bindings[b].offset;
        int32_t value;

        if( index < 0 || !RSCache_PresenceHas(&record->present, index) )
            continue;
        if( bindings[b].apply )
        {
            bindings[b].apply(object, record, index);
            written++;
            continue;
        }
        value = record->values[index];
        if( bindings[b].size == 1 )
        {
            uint8_t v = (uint8_t)value;
            memcpy(member, &v, 1);
        }
        else if( bindings[b].size == 2 )
        {
            uint16_t v = (uint16_t)value;
            memcpy(member, &v, 2);
        }
        else
        {
            assert(bindings[b].size == 4);
            memcpy(member, &value, 4);
        }
        written++;
    }
    return written;
}

int
RSCache_BandBindingCheck(
    const struct RSCache_Register* reg,
    const struct RSCache_BandBinding* bindings,
    int binding_count)
{
    int problems = 0;

    assert(reg);
    assert(bindings || binding_count == 0);
    for( int b = 0; b < binding_count; b++ )
    {
        int index = RSCache_BandIndex(reg, bindings[b].name);

        if( index < 0 )
        {
            fprintf(stderr, "fields/%s.ini: the reader binds `%s`, which the register gives no "
                            "server opcode\n",
                    reg->type, bindings[b].name);
            problems++;
            continue;
        }
        if( reg->entries[index].wire == RSCACHE_REGISTER_WIRE_STRING ||
            reg->entries[index].wire == RSCACHE_REGISTER_WIRE_LIST )
        {
            if( !bindings[b].apply )
            {
                fprintf(stderr, "fields/%s.ini: [%s.%s] is a string or list, which the reader "
                                "binds to a member instead of an `apply`\n",
                        reg->type, reg->type, bindings[b].name);
                problems++;
            }
            continue;
        }
        if( !bindings[b].apply && wire_width(reg->entries[index].wire) > bindings[b].size )
        {
            fprintf(stderr, "fields/%s.ini: [%s.%s] is %zu bytes wide on the wire and the "
                            "reader's member holds %zu\n",
                    reg->type, reg->type, bindings[b].name,
                    (size_t)wire_width(reg->entries[index].wire), bindings[b].size);
            problems++;
        }
    }
    return problems;
}
