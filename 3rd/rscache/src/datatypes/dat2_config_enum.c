#include "dat2_config_enum.h"

#include "../rsbuffer.h"

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

    if( !entry )
        return;
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
            (void)g1(&buf);
            break;
        case 2:
            entry->output_is_string = g1(&buf) == (int)'s';
            break;
        case 3:
        {
            char* s = gcstring(&buf);
            free(entry->default_string);
            entry->default_string = s;
            break;
        }
        case 4:
            entry->default_int = g4(&buf);
            break;
        case 5:
        {
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
                    if( !new_keys || !new_strings )
                    {
                        free(value);
                        goto decode_fail;
                    }
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
                    if( !new_keys || !new_values )
                        goto decode_fail;
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
                    if( !new_keys || !new_values )
                        goto decode_fail;
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
    return;

decode_fail:
    free(keys);
    free(int_values);
    free(long_values);
    if( string_values )
    {
        for( int i = 0; i < count; i++ )
            free(string_values[i]);
        free(string_values);
    }
}

uint32_t
RSCache_Dat2ConfigEnumEncode(
    const struct RSCache_Dat2ConfigEnum* entry,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !entry || !out )
        return 0;

    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, out, out_capacity);

    /* Opcode 2 carries the value type as a character, and the decoder keeps only
     * whether it was 's'. Absent opcode 2 therefore decodes to "not a string", so
     * the field is only emitted for string enums — writing it unconditionally
     * would append bytes to every integer enum that omitted it. */
    if( entry->output_is_string )
    {
        p1(&buf, 2);
        p1(&buf, (int)'s');
    }

    if( entry->default_string )
    {
        p1(&buf, 3);
        pjstr(&buf, entry->default_string, RSCACHE_JSTR_TERMINATOR_NULL);
    }
    if( entry->default_int != 0 )
    {
        p1(&buf, 4);
        p4(&buf, entry->default_int);
    }
    if( entry->default_long != 0 )
    {
        p1(&buf, 8);
        p8(&buf, entry->default_long);
    }

    if( entry->count > 0 )
    {
        /* One block of the appropriate type. The decoder appends opcode 5/6/7
         * entries into the same arrays, so a record that used both cannot be
         * distinguished afterwards and re-encodes as a single block. */
        if( entry->output_is_string )
        {
            p1(&buf, 5);
            p2(&buf, entry->count);
            for( int i = 0; i < entry->count; i++ )
            {
                p4(&buf, entry->keys[i]);
                pjstr(
                    &buf,
                    entry->string_values && entry->string_values[i] ? entry->string_values[i] : "",
                    RSCACHE_JSTR_TERMINATOR_NULL);
            }
        }
        else if( entry->long_values )
        {
            p1(&buf, 7);
            p2(&buf, entry->count);
            for( int i = 0; i < entry->count; i++ )
            {
                p4(&buf, entry->keys[i]);
                p8(&buf, entry->long_values[i]);
            }
        }
        else
        {
            p1(&buf, 6);
            p2(&buf, entry->count);
            for( int i = 0; i < entry->count; i++ )
            {
                p4(&buf, entry->keys[i]);
                p4(&buf, entry->int_values ? entry->int_values[i] : 0);
            }
        }
    }

    /* Opcode 1 is consumed and discarded by the decoder, so it cannot be
     * reproduced. A record carrying it round-trips semantically but not
     * byte-exactly. */

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

    if( !entry )
        return need;
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
