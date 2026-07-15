#include "dat2a_config_enum.h"

#include "osrs/rscache/shared/shared_rs_buffer.h"

#include <stdlib.h>
#include <string.h>

void
RSCacheDat2A_ConfigEnumDecodeInplace(
    struct RSCacheDat2A_ConfigEnum* entry,
    const void* data,
    int data_size)
{
    if( !entry || !data || data_size <= 0 || (data_size == 1 && ((const uint8_t*)data)[0] == 0) )
        return;

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, (uint32_t)data_size);

    int key_cap = 0;
    int* keys = NULL;
    int* int_values = NULL;
    char** string_values = NULL;
    int count = 0;

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
                (void)g4(&buf);
                (void)g8(&buf);
            }
            break;
        }
        case 8:
            (void)g8(&buf);
            break;
        default:
            break;
        }
    }

    entry->keys = keys;
    entry->int_values = int_values;
    entry->string_values = string_values;
    entry->count = count;
    return;

decode_fail:
    free(keys);
    free(int_values);
    if( string_values )
    {
        for( int i = 0; i < count; i++ )
            free(string_values[i]);
        free(string_values);
    }
}

void
RSCacheDat2A_ConfigEnumFree(struct RSCacheDat2A_ConfigEnum* entry)
{
    if( !entry )
        return;
    free(entry->keys);
    free(entry->int_values);
    free(entry->default_string);
    if( entry->string_values )
    {
        for( int i = 0; i < entry->count; i++ )
            free(entry->string_values[i]);
        free(entry->string_values);
    }
    free(entry);
}
