#include "dat2_config_enum.h"

#include "../rsbuffer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
RSCache_Dat2ConfigEnumDecodeInplace(
    struct RSCache_Dat2ConfigEnum* entry,
    const void* data,
    int data_size)
{
    struct RSCache_Buffer buf;
    int key_cap = 0;
    int* keys = NULL;
    int* int_values = NULL;
    int64_t* long_values = NULL;
    char** string_values = NULL;
    int count = 0;

    assert(entry);
    RSCache_PresenceReset(&entry->present);
    /*
     * The lone-terminator case is *not* special-cased out any more. Returning
     * early for a 1-byte `00` record skipped setting `_consumed`, so every empty
     * enum — 3,270 of the 5,862 in osrs239 — reported a short read while decoding
     * perfectly. The loop below handles that record correctly on its own: it
     * reads opcode 0, stops, and records one byte consumed.
     */
    if( !data || data_size <= 0 )
        return;

    RSCache_BufferInit(&buf, (uint8_t*)data, (uint32_t)data_size);

    for( ;; )
    {
        int opcode = g1(&buf);
        if( opcode == 0 )
            break;
        switch( opcode )
        {
        case 1:
            entry->input_type = (char)g1(&buf);
            RSCache_PresenceSet(&entry->present, RSCACHE_ENUM_FIELD_INPUT_TYPE);
            break;
        case 2:
            entry->output_type = (char)g1(&buf);
            entry->output_is_string = entry->output_type == 's';
            RSCache_PresenceSet(&entry->present, RSCACHE_ENUM_FIELD_OUTPUT_TYPE);
            break;
        case 3:
        {
            char* s = gcstring(&buf);
            free(entry->default_string);
            entry->default_string = s;
            RSCache_PresenceSet(&entry->present, RSCACHE_ENUM_FIELD_DEFAULT_STRING);
            break;
        }
        case 4:
            entry->default_int = g4(&buf);
            RSCache_PresenceSet(&entry->present, RSCACHE_ENUM_FIELD_DEFAULT_INT);
            break;
        case 5:
        {
            RSCache_PresenceSet(&entry->present, RSCACHE_ENUM_FIELD_STRING_VALUES);
            int size = g2(&buf);
            for( int i = 0; i < size; i++ )
            {
                int key = g4(&buf);
                char* value = gcstring(&buf);
                if( count >= key_cap )
                {
                    int new_cap = key_cap < 8 ? 8 : key_cap * 2;
                    int* new_keys = realloc(keys, (size_t)new_cap * sizeof(int));
                    char** new_strings =
                        realloc(string_values, (size_t)new_cap * sizeof(char*));
                    assert(new_keys);
                    assert(new_strings);
                    keys = new_keys;
                    string_values = new_strings;
                    key_cap = new_cap;
                }
                keys[count] = key;
                string_values[count] = value;
                count++;
            }
            break;
        }
        case 6:
        {
            RSCache_PresenceSet(&entry->present, RSCACHE_ENUM_FIELD_INT_VALUES);
            int size = g2(&buf);
            for( int i = 0; i < size; i++ )
            {
                int key = g4(&buf);
                int value = g4(&buf);
                if( count >= key_cap )
                {
                    int new_cap = key_cap < 8 ? 8 : key_cap * 2;
                    int* new_keys = realloc(keys, (size_t)new_cap * sizeof(int));
                    int* new_values = realloc(int_values, (size_t)new_cap * sizeof(int));
                    assert(new_keys);
                    assert(new_values);
                    keys = new_keys;
                    int_values = new_values;
                    key_cap = new_cap;
                }
                keys[count] = key;
                int_values[count] = value;
                count++;
            }
            break;
        }
        case 7:
        {
            RSCache_PresenceSet(&entry->present, RSCACHE_ENUM_FIELD_LONG_VALUES);
            int size = g2(&buf);
            for( int i = 0; i < size; i++ )
            {
                int key = g4(&buf);
                int64_t value = g8(&buf);
                if( count >= key_cap )
                {
                    int new_cap = key_cap < 8 ? 8 : key_cap * 2;
                    int* new_keys = realloc(keys, (size_t)new_cap * sizeof(int));
                    int64_t* new_values =
                        realloc(long_values, (size_t)new_cap * sizeof(int64_t));
                    assert(new_keys);
                    assert(new_values);
                    keys = new_keys;
                    long_values = new_values;
                    key_cap = new_cap;
                }
                keys[count] = key;
                long_values[count] = value;
                count++;
            }
            break;
        }
        case 8:
            entry->default_long = g8(&buf);
            RSCache_PresenceSet(&entry->present, RSCACHE_ENUM_FIELD_DEFAULT_LONG);
            break;
        default:
            /*
             * Stop, where this used to skip and carry on.
             *
             * `break` here left the *switch*, not the loop, so an unknown opcode
             * was stepped over and its payload read as the next opcode — the
             * record then decoded from a false position with nothing to show for
             * it. That is exactly how a real width bug hid in
             * `dat2_config_obj.c` opcode 115. Stopping keeps whatever was decoded
             * and leaves `_consumed` short, which is the signal.
             */
            goto decode_done;
        }
    }

decode_done:
    entry->_consumed = (int)buf.position;
    entry->keys = keys;
    entry->int_values = int_values;
    entry->long_values = long_values;
    entry->string_values = string_values;
    entry->count = count;
}

uint32_t
RSCache_Dat2ConfigEnumEncode(
    const struct RSCache_Dat2ConfigEnum* entry,
    uint8_t* out,
    uint32_t out_capacity)
{
    assert(entry);
    assert(out);
    assert(out_capacity >= RSCache_Dat2ConfigEnumEncodeBound(entry));

    const struct RSCache_Presence* has = &entry->present;
    bool strings = RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_STRING_VALUES);
    bool ints = RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_INT_VALUES);
    bool longs = RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_LONG_VALUES);

    /* The decoder appends every map opcode into the same `keys`, so a record
     * stating two maps has no one representation to write back. */
    assert(strings + ints + longs <= 1);

    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, out, out_capacity);

    /*
     * Exactly the stated fields, in the order every enum in cache.osrs239 writes
     * them: the two type characters, the map, then the default. Two things used
     * to cost 2,592 records: the default was written before the map (2,417
     * re-encoded at the same length), and a field was written only when its value
     * differed from the zeroed struct, which dropped 157 explicit `default=0`s
     * and 18 stated maps with no entries.
     */
    if( RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_INPUT_TYPE) )
    {
        p1(&buf, 1);
        p1(&buf, (int)entry->input_type);
    }
    if( RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_OUTPUT_TYPE) )
    {
        p1(&buf, 2);
        p1(&buf, (int)entry->output_type);
    }

    if( strings || ints || longs )
    {
        assert(entry->count >= 0);
        assert(entry->count <= 0xFFFF);
        assert(entry->count == 0 || entry->keys);
        p1(&buf, strings ? 5 : longs ? 7 : 6);
        p2(&buf, entry->count);
        for( int i = 0; i < entry->count; i++ )
        {
            p4(&buf, entry->keys[i]);
            if( strings )
            {
                assert(entry->string_values);
                assert(entry->string_values[i]);
                pjstr(&buf, entry->string_values[i], RSCACHE_JSTR_TERMINATOR_NULL);
            }
            else if( longs )
            {
                assert(entry->long_values);
                p8(&buf, entry->long_values[i]);
            }
            else
            {
                assert(entry->int_values);
                p4(&buf, entry->int_values[i]);
            }
        }
    }

    if( RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_DEFAULT_STRING) )
    {
        assert(entry->default_string);
        p1(&buf, 3);
        pjstr(&buf, entry->default_string, RSCACHE_JSTR_TERMINATOR_NULL);
    }
    if( RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_DEFAULT_INT) )
    {
        p1(&buf, 4);
        p4(&buf, entry->default_int);
    }
    if( RSCache_PresenceHas(has, RSCACHE_ENUM_FIELD_DEFAULT_LONG) )
    {
        p1(&buf, 8);
        p8(&buf, entry->default_long);
    }

    p1(&buf, 0);
    return buf.position;
}

void
RSCache_Dat2ConfigEnumFreeInplace(struct RSCache_Dat2ConfigEnum* entry)
{
    int i;

    if( !entry )
        return;
    free(entry->keys);
    entry->keys = NULL;
    free(entry->int_values);
    entry->int_values = NULL;
    free(entry->long_values);
    entry->long_values = NULL;
    free(entry->default_string);
    entry->default_string = NULL;
    if( entry->string_values )
    {
        for( i = 0; i < entry->count; i++ )
            free(entry->string_values[i]);
        free(entry->string_values);
        entry->string_values = NULL;
    }
    entry->count = 0;
}

void
RSCache_Dat2ConfigEnumFree(struct RSCache_Dat2ConfigEnum* entry)
{
    if( !entry )
        return;
    RSCache_Dat2ConfigEnumFreeInplace(entry);
    free(entry);
}

uint32_t
RSCache_Dat2ConfigEnumEncodeBound(const struct RSCache_Dat2ConfigEnum* entry)
{
    /* Scalars (opcodes 1-4, 8) plus, per entry, a 4-byte key and its value: 4
     * bytes for an int, 8 for a long, or the string plus terminator. */
    uint32_t need = 64u;
    int i;

    assert(entry);
    if( entry->default_string )
        need += (uint32_t)strlen(entry->default_string) + 2u;
    need += (uint32_t)entry->count * 12u + 8u;
    if( entry->string_values )
    {
        for( i = 0; i < entry->count; i++ )
        {
            if( entry->string_values[i] )
                need += (uint32_t)strlen(entry->string_values[i]) + 1u;
        }
    }
    return need;
}
