#include "enum_lookup.h"

#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/shared/shared_rs_buffer.h"

#include <stdlib.h>
#include <string.h>

struct Interface161EnumCacheEntry
{
    int enum_id;
    int* keys;
    int* values;
    int count;
};

static struct Interface161EnumCacheEntry* s_enum_cache;
static int s_enum_cache_count;
static int s_enum_cache_cap;

static void
skip_enum_opcode(
    struct RSCacheShared_RSBuffer* buf,
    int opcode)
{
    switch( opcode )
    {
    case 1:
    case 2:
        (void)g1(buf);
        break;
    case 3:
    {
        char* s = gcstring(buf);
        free(s);
        break;
    }
    case 4:
        (void)g4(buf);
        break;
    case 5:
    {
        int size = g2(buf);
        for( int i = 0; i < size; i++ )
        {
            (void)g4(buf);
            char* s = gcstring(buf);
            free(s);
        }
        break;
    }
    case 6:
    {
        int size = g2(buf);
        for( int i = 0; i < size; i++ )
        {
            (void)g4(buf);
            (void)g4(buf);
        }
        break;
    }
    case 7:
    {
        int size = g2(buf);
        for( int i = 0; i < size; i++ )
        {
            (void)g4(buf);
            (void)g8(buf);
        }
        break;
    }
    case 8:
        (void)g8(buf);
        break;
    default:
        break;
    }
}

static int
decode_enum_int_map(
    uint8_t const* data,
    int len,
    int* out_keys,
    int* out_values,
    int max_count)
{
    if( !data || len <= 0 || (len == 1 && data[0] == 0) )
        return 0;

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, len);

    int count = 0;
    for( ;; )
    {
        int opcode = g1(&buf);
        if( opcode == 0 )
            break;
        if( opcode == 6 )
        {
            int size = g2(&buf);
            for( int i = 0; i < size; i++ )
            {
                int key = g4(&buf);
                int value = g4(&buf);
                if( out_keys && out_values && count < max_count )
                {
                    out_keys[count] = key;
                    out_values[count] = value;
                }
                count++;
            }
        }
        else
        {
            skip_enum_opcode(&buf, opcode);
        }
    }
    return count;
}

static bool
interface161_config_archive_find_file(
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

static struct Interface161EnumCacheEntry*
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
        (void)interface161_config_archive_find_file(
            cache, RSCacheDat2A_ConfigKind_Enum, fl, enum_id, &data, &data_len);

    int count = decode_enum_int_map(data, data_len, NULL, NULL, 0);
    struct Interface161EnumCacheEntry entry = {
        .enum_id = enum_id,
        .count = count,
        .keys = NULL,
        .values = NULL,
    };
    if( count > 0 )
    {
        entry.keys = calloc((size_t)count, sizeof(int));
        entry.values = calloc((size_t)count, sizeof(int));
        if( entry.keys && entry.values )
            (void)decode_enum_int_map(data, data_len, entry.keys, entry.values, count);
        else
        {
            free(entry.keys);
            free(entry.values);
            entry.keys = NULL;
            entry.values = NULL;
            entry.count = 0;
        }
    }

    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);

    if( s_enum_cache_count >= s_enum_cache_cap )
    {
        int new_cap = s_enum_cache_cap < 8 ? 8 : s_enum_cache_cap * 2;
        struct Interface161EnumCacheEntry* grown =
            realloc(s_enum_cache, (size_t)new_cap * sizeof(*s_enum_cache));
        if( !grown )
            return NULL;
        s_enum_cache = grown;
        s_enum_cache_cap = new_cap;
    }

    s_enum_cache[s_enum_cache_count++] = entry;
    return &s_enum_cache[s_enum_cache_count - 1];
}

int
interface161_enum_lookup(
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

    struct Interface161EnumCacheEntry* entry = enum_cache_get(cache, enum_id);
    if( !entry || !entry->keys || entry->count <= 0 )
        return -1;

    for( int i = 0; i < entry->count; i++ )
    {
        if( entry->keys[i] == key )
            return entry->values[i];
    }
    return -1;
}

void
interface161_enum_dump(
    struct RSCacheDat2Disk* cache,
    int enum_id)
{
    struct Interface161EnumCacheEntry* entry = enum_cache_get(cache, enum_id);
    if( !entry || !entry->keys )
    {
        printf("enum %d: <empty>\n", enum_id);
        return;
    }
    printf("enum %d: %d entries\n", enum_id, entry->count);
    for( int i = 0; i < entry->count; i++ )
    {
        int v = entry->values[i];
        printf(
            "  key=%d value=%d  (iface=%d component=%d)\n",
            entry->keys[i],
            v,
            (v >> 16) & 0xffff,
            v & 0xffff);
    }
}

int
interface161_enum_output_count(
    struct RSCacheDat2Disk* cache,
    int enum_id)
{
    if( !cache || enum_id < 0 )
        return 0;

    struct Interface161EnumCacheEntry* entry = enum_cache_get(cache, enum_id);
    return entry ? entry->count : 0;
}
