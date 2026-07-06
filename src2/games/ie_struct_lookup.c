#include "ie_struct_lookup.h"

#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/shared/shared_rs_buffer.h"

#include <stdlib.h>
#include <string.h>

struct IEStructCacheEntry
{
    int struct_id;
    struct RSCacheShared_Params params;
};

static struct IEStructCacheEntry* s_struct_cache;
static int s_struct_cache_count;
static int s_struct_cache_cap;

static bool
decode_struct_params(
    uint8_t const* data,
    int len,
    struct RSCacheShared_Params* out_params)
{
    if( !out_params )
        return false;
    memset(out_params, 0, sizeof(*out_params));
    if( !data || len <= 0 || (len == 1 && data[0] == 0) )
        return true;

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, len);

    for( ;; )
    {
        int opcode = g1(&buf);
        if( opcode == 0 )
            break;
        if( opcode == 249 )
            RSCacheShared_RSBufferReadParams(&buf, out_params);
    }
    return true;
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

static struct IEStructCacheEntry*
struct_cache_get(
    struct RSCacheDat2Disk* cache,
    int struct_id)
{
    for( int i = 0; i < s_struct_cache_count; i++ )
    {
        if( s_struct_cache[i].struct_id == struct_id )
            return &s_struct_cache[i];
    }

    struct RSCacheDat2Disk_Archive* arch = RSCacheDat2Disk_ArchiveNewLoad(
        cache, RSCacheDat2Disk_Table_Configs, RSCacheDat2A_ConfigKind_Struct);
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
    if( struct_id >= 0 )
        (void)ie_config_archive_find_file(
            cache, RSCacheDat2A_ConfigKind_Struct, fl, struct_id, &data, &data_len);

    struct IEStructCacheEntry entry = {
        .struct_id = struct_id,
    };
    (void)decode_struct_params(data, data_len, &entry.params);

    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);

    if( s_struct_cache_count >= s_struct_cache_cap )
    {
        int new_cap = s_struct_cache_cap < 8 ? 8 : s_struct_cache_cap * 2;
        struct IEStructCacheEntry* grown =
            realloc(s_struct_cache, (size_t)new_cap * sizeof(*s_struct_cache));
        if( !grown )
            return NULL;
        s_struct_cache = grown;
        s_struct_cache_cap = new_cap;
    }

    s_struct_cache[s_struct_cache_count++] = entry;
    return &s_struct_cache[s_struct_cache_count - 1];
}

bool
ie_struct_param_lookup(
    struct RSCacheDat2Disk* cache,
    int struct_id,
    int param_id,
    bool* out_is_string,
    int* out_int,
    char const** out_str)
{
    if( out_is_string )
        *out_is_string = false;
    if( out_int )
        *out_int = 0;
    if( out_str )
        *out_str = NULL;
    if( !cache || struct_id < 0 || param_id < 0 )
        return false;

    struct IEStructCacheEntry* entry = struct_cache_get(cache, struct_id);
    if( !entry || entry->params.count <= 0 )
        return false;

    for( int i = 0; i < entry->params.count; i++ )
    {
        if( entry->params.keys[i] != param_id )
            continue;
        if( entry->params.is_string[i] )
        {
            if( out_is_string )
                *out_is_string = true;
            if( out_str )
                *out_str = (char const*)entry->params.values[i];
            return true;
        }
        if( out_int )
            *out_int = entry->params.values[i] ? *(int*)entry->params.values[i] : 0;
        return true;
    }
    return false;
}
