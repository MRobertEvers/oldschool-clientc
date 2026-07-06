#include "ie_param_lookup.h"

#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "osrs/rscache/dat2disk/dat2disk.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "osrs/rscache/shared/shared_rs_buffer.h"

#include <stdlib.h>
#include <string.h>

struct IEParamCacheEntry
{
    int param_id;
    char type_char;
    int default_int;
    char* default_string;
};

static struct IEParamCacheEntry* s_param_cache;
static int s_param_cache_count;
static int s_param_cache_cap;

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

static char
param_type_id_to_char(int id)
{
    if( id == 36 )
        return 's';
    if( id == 0 )
        return 'i';
    return 'i';
}

static void
decode_param_type(
    uint8_t const* data,
    int len,
    struct IEParamCacheEntry* entry)
{
    if( !entry || !data || len <= 0 || (len == 1 && data[0] == 0) )
        return;

    struct RSCacheShared_RSBuffer buf;
    RSCacheShared_RSBufferInit(&buf, (uint8_t*)data, len);

    for( ;; )
    {
        int opcode = g1(&buf);
        if( opcode == 0 )
            break;
        switch( opcode )
        {
        case 1:
            entry->type_char = (char)g1(&buf);
            break;
        case 8:
            entry->type_char = param_type_id_to_char((int)g1(&buf));
            break;
        case 2:
            entry->default_int = g4(&buf);
            break;
        case 5:
        {
            char* s = gcstring(&buf);
            free(entry->default_string);
            entry->default_string = s;
            break;
        }
        default:
            break;
        }
    }
}

static struct IEParamCacheEntry*
param_cache_get(
    struct RSCacheDat2Disk* cache,
    int param_id)
{
    for( int i = 0; i < s_param_cache_count; i++ )
    {
        if( s_param_cache[i].param_id == param_id )
            return &s_param_cache[i];
    }

    struct RSCacheDat2Disk_Archive* arch = RSCacheDat2Disk_ArchiveNewLoad(
        cache, RSCacheDat2Disk_Table_Configs, RSCacheDat2A_ConfigKind_Params);
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
    if( param_id >= 0 )
        (void)ie_config_archive_find_file(
            cache, RSCacheDat2A_ConfigKind_Params, fl, param_id, &data, &data_len);

    struct IEParamCacheEntry entry = {
        .param_id = param_id,
        .type_char = 'i',
        .default_int = 0,
    };
    decode_param_type(data, data_len, &entry);

    RSCacheShared_FileListFree(fl);
    RSCacheDat2Disk_ArchiveFree(arch);

    if( s_param_cache_count >= s_param_cache_cap )
    {
        int new_cap = s_param_cache_cap < 8 ? 8 : s_param_cache_cap * 2;
        struct IEParamCacheEntry* grown =
            realloc(s_param_cache, (size_t)new_cap * sizeof(*s_param_cache));
        if( !grown )
            return NULL;
        s_param_cache = grown;
        s_param_cache_cap = new_cap;
    }

    s_param_cache[s_param_cache_count++] = entry;
    return &s_param_cache[s_param_cache_count - 1];
}

bool
ie_param_is_string(
    struct RSCacheDat2Disk* cache,
    int param_id)
{
    struct IEParamCacheEntry* entry = param_cache_get(cache, param_id);
    return entry && entry->type_char == 's';
}

int
ie_param_default_int(
    struct RSCacheDat2Disk* cache,
    int param_id)
{
    struct IEParamCacheEntry* entry = param_cache_get(cache, param_id);
    return entry ? entry->default_int : 0;
}

char const*
ie_param_default_string(
    struct RSCacheDat2Disk* cache,
    int param_id)
{
    struct IEParamCacheEntry* entry = param_cache_get(cache, param_id);
    if( !entry || !entry->default_string )
        return "";
    return entry->default_string;
}
