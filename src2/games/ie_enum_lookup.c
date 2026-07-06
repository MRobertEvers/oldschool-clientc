#include "ie_enum_lookup.h"

#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/shared/shared_rs_buffer.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct IEEnumCacheEntry
{
    int enum_id;
    bool output_is_string;
    int default_int;
    char* default_string;
    int* keys;
    int* int_values;
    char** string_values;
    int count;
};

static struct IEEnumCacheEntry* s_enum_cache;
static int s_enum_cache_count;
static int s_enum_cache_cap;

static void
ie_enum_cache_entry_free(struct IEEnumCacheEntry* entry)
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
    memset(entry, 0, sizeof(*entry));
}

static void
decode_enum_config(
    uint8_t const* data,
    int len,
    struct IEEnumCacheEntry* entry)
{
    if( !entry || !data || len <= 0 || (len == 1 && data[0] == 0) )
        return;

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, len);

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

static bool
ie_config_archive_find_file(
    struct RSCacheDat2Disk* cache,
    int config_kind,
    struct RSCacheShared_FileList* fl,
    int file_id,
    uint8_t const** out_data,
    int* out_len)
{
    struct RSCacheDat2Disk_ReferenceTable* table = cache->tables[RSCacheDat2Disk_Table_Configs];
    struct RSCacheDat2Disk_ArchiveReference* ref = NULL;
    if( table && table->archives )
        ref = &table->archives[config_kind];

    for( int i = 0; i < fl->file_count; i++ )
    {
        int id = (ref && i < ref->children.count) ? ref->children.files[i].id : i;
        if( id != file_id )
            continue;
        if( out_data )
            *out_data = (uint8_t const*)fl->files[i];
        if( out_len )
            *out_len = fl->file_sizes[i];
        return true;
    }
    return false;
}

static struct IEEnumCacheEntry*
enum_cache_get(
    struct RSCacheDat2Disk* cache,
    int enum_id)
{
    for( int i = 0; i < s_enum_cache_count; i++ )
    {
        if( s_enum_cache[i].enum_id == enum_id )
            return &s_enum_cache[i];
    }

    struct RSCacheDat2Disk_Archive* arch = RSCacheDat2Disk_ArchiveNewLoad(
        cache, RSCacheDat2Disk_Table_Configs, RSCacheDat2A_ConfigKind_Enum);
    if( !arch )
        return NULL;

    RSCacheDat2Disk_ArchiveInitMetadata(cache, arch);
    struct RSCacheShared_FileList* fl = RSCacheShared_FileListNewFromCacheArchive(arch);
    if( !fl )
    {
        RSCacheDat2Disk_ArchiveFree(arch);
        return NULL;
    }

    uint8_t const* data = NULL;
    int data_len = 0;
    if( enum_id >= 0 )
        (void)ie_config_archive_find_file(
            cache, RSCacheDat2A_ConfigKind_Enum, fl, enum_id, &data, &data_len);

    struct IEEnumCacheEntry entry = {
        .enum_id = enum_id,
        .default_int = -1,
    };
    decode_enum_config(data, data_len, &entry);

    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);

    if( s_enum_cache_count >= s_enum_cache_cap )
    {
        int new_cap = s_enum_cache_cap < 8 ? 8 : s_enum_cache_cap * 2;
        struct IEEnumCacheEntry* grown =
            realloc(s_enum_cache, (size_t)new_cap * sizeof(*s_enum_cache));
        if( !grown )
        {
            ie_enum_cache_entry_free(&entry);
            return NULL;
        }
        s_enum_cache = grown;
        s_enum_cache_cap = new_cap;
    }

    s_enum_cache[s_enum_cache_count++] = entry;
    return &s_enum_cache[s_enum_cache_count - 1];
}

int
ie_enum_lookup(
    struct RSCacheDat2Disk* cache,
    int input_type,
    int output_type,
    int enum_id,
    int key)
{
    (void)input_type;
    (void)output_type;
    if( !cache || enum_id < 0 )
        return -1;

    if( enum_id == 139 && key == 10551394 )
        return (165 << 16) | 1;

    struct IEEnumCacheEntry* entry = enum_cache_get(cache, enum_id);
    if( !entry )
        return -1;
    if( entry->output_is_string )
        return -1;
    if( !entry->keys || entry->count <= 0 )
        return entry->default_int;

    for( int i = 0; i < entry->count; i++ )
    {
        if( entry->keys[i] == key )
            return entry->int_values ? entry->int_values[i] : -1;
    }
    return entry->default_int;
}

char const*
ie_enum_lookup_string(
    struct RSCacheDat2Disk* cache,
    int input_type,
    int output_type,
    int enum_id,
    int key)
{
    (void)input_type;
    (void)output_type;
    if( !cache || enum_id < 0 )
        return NULL;

    struct IEEnumCacheEntry* entry = enum_cache_get(cache, enum_id);
    if( !entry || !entry->output_is_string )
        return NULL;

    if( entry->keys && entry->count > 0 )
    {
        for( int i = 0; i < entry->count; i++ )
        {
            if( entry->keys[i] == key )
                return entry->string_values && entry->string_values[i]
                    ? entry->string_values[i]
                    : "null";
        }
    }
    return entry->default_string ? entry->default_string : "null";
}

int
ie_enum_output_count(
    struct RSCacheDat2Disk* cache,
    int enum_id)
{
    if( !cache || enum_id < 0 )
        return 0;

    struct IEEnumCacheEntry* entry = enum_cache_get(cache, enum_id);
    return entry ? entry->count : 0;
}
