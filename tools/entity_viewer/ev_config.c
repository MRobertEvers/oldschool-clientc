#include "ev_config.h"

#include "rscache.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>


/*
 * The last archive ev_config_record decoded, kept when a caller asks
 * (ev_config_memo). A record lives inside an archive that is decompressed
 * whole, and the obj configs are one archive: a walk over every obj that
 * loads each record (ev_icons, and ev_build_obj_model inside it) otherwise
 * bunzips the same ~1 MB once per obj -- an hour for a cache. Opt-in and
 * per thread because ev_server switches caches under a worker thread, and a
 * memo keyed by a pointer can outlive the cache it came from.
 */
static _Thread_local struct
{
    int on;
    struct Tool_Dat2Cache* cache;
    int table;
    int archive_id;
    int revision;
    int file_count;
    int* file_ids;
    struct RSCache_FileList* files;
} g_memo;

static void
memo_drop(void)
{
    if( g_memo.files )
        RSCache_FileListFree(g_memo.files);
    free(g_memo.file_ids);
    g_memo.files = NULL;
    g_memo.file_ids = NULL;
    g_memo.cache = NULL;
}

void
ev_config_memo(int on)
{
    memo_drop();
    g_memo.on = on;
}

int
ev_config_record(
    struct Tool_Dat2Cache* c,
    enum RSCache_Type type,
    int config_kind,
    int record_id,
    char** out_bytes,
    int* out_size,
    struct RSCache* out_profile)
{
    struct RSCache_RecordAddress addr;
    struct RSCache_Dat2DiskArchive* archive;
    struct RSCache_FileList* files;
    int table;
    int archive_id;
    int found = 0;

    assert(c);
    assert(c->disk);
    assert(out_bytes);
    assert(out_size);
    *out_bytes = NULL;
    *out_size = 0;

    addr = RSCache_RecordAddressFor(&c->profile, type);
    if( addr.group_shift == 0 )
    {
        table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_CONFIGS);
        archive_id = config_kind;
    }
    else
    {
        table = RSCache_Dat2DiskTableId(c->disk, addr.table);
        archive_id = record_id >> addr.group_shift;
    }

    if( g_memo.on && g_memo.files && g_memo.cache == c && g_memo.table == table &&
        g_memo.archive_id == archive_id )
    {
        archive = NULL;
        files = g_memo.files;
        if( out_profile )
        {
            *out_profile = c->profile;
            RSCache_ProfileSetGroupRevision(out_profile, type, g_memo.revision);
        }
    }
    else
    {
        archive = RSCache_Dat2DiskArchiveNewLoad(c->disk, table, archive_id);
        if( !archive )
            return 0;
        if( !RSCache_Dat2DiskArchiveInitMetadata(c->disk, archive) || archive->file_count <= 0 )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            return 0;
        }

        if( out_profile )
        {
            *out_profile = c->profile;
            RSCache_ProfileSetGroupRevision(out_profile, type, archive->revision);
        }

        files = RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
        if( !files )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            return 0;
        }
        if( g_memo.on )
        {
            memo_drop();
            g_memo.cache = c;
            g_memo.table = table;
            g_memo.archive_id = archive_id;
            g_memo.revision = archive->revision;
            g_memo.file_count = archive->file_count;
            if( archive->file_ids )
            {
                g_memo.file_ids = malloc((size_t)archive->file_count * sizeof(int));
                assert(g_memo.file_ids);
                memcpy(g_memo.file_ids, archive->file_ids, (size_t)archive->file_count * sizeof(int));
            }
            g_memo.files = files;
            RSCache_Dat2DiskArchiveFree(archive);
            archive = NULL;
        }
    }

    for( int i = 0; i < files->file_count; i++ )
    {
        int const* ids = archive ? archive->file_ids : g_memo.file_ids;
        int id_count = archive ? archive->file_count : g_memo.file_count;
        int file_id = (ids && i < id_count) ? ids[i] : i;
        int global = addr.group_shift == 0
                         ? file_id
                         : ((archive_id << addr.group_shift) | (file_id & addr.file_mask));
        if( global != record_id || files->file_sizes[i] <= 0 )
            continue;
        *out_bytes = malloc((size_t)files->file_sizes[i]);
        assert(*out_bytes);
        memcpy(*out_bytes, files->files[i], (size_t)files->file_sizes[i]);
        *out_size = files->file_sizes[i];
        found = 1;
        break;
    }

    if( archive )
    {
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
    }
    return found;
}

struct RSCache_Dat2ConfigIdk*
ev_idk_load(struct Tool_Dat2Cache* c, int idk_id)
{
    char* bytes = NULL;
    int size = 0;
    struct RSCache_Dat2ConfigIdk* idk;

    if( !ev_config_record(
            c, RSCACHE_TYPE_IDK, RSCACHE_DAT2_CONFIG_KIND_IDENTKIT, idk_id, &bytes, &size, NULL) )
        return NULL;
    idk = RSCache_Dat2ConfigIdkNewDecode(bytes, size);
    free(bytes);
    return idk;
}

struct RSCache_Dat2ConfigObj*
ev_obj_load(struct Tool_Dat2Cache* c, int obj_id)
{
    char* bytes = NULL;
    int size = 0;
    struct RSCache profile;
    struct RSCache_Dat2ConfigObj* obj;

    if( !ev_config_record(
            c, RSCACHE_TYPE_OBJ, RSCACHE_DAT2_CONFIG_KIND_OBJECT, obj_id, &bytes, &size, &profile) )
        return NULL;
    obj = RSCache_Dat2ConfigObjNewDecodeProfile(&profile, bytes, size);
    free(bytes);
    return obj;
}

struct RSCache_Dat2ConfigLoc*
ev_loc_load(struct Tool_Dat2Cache* c, int loc_id)
{
    char* bytes = NULL;
    int size = 0;
    struct RSCache profile;
    struct RSCache_Dat2ConfigLoc* loc;

    if( !ev_config_record(
            c, RSCACHE_TYPE_LOC, RSCACHE_DAT2_CONFIG_KIND_LOCS, loc_id, &bytes, &size, &profile) )
        return NULL;
    loc = RSCache_Dat2ConfigLocNewDecodeProfile(&profile, bytes, size);
    free(bytes);
    return loc;
}

struct RSCache_Dat2ConfigSpotanim*
ev_spotanim_load(struct Tool_Dat2Cache* c, int spotanim_id)
{
    char* bytes = NULL;
    int size = 0;
    struct RSCache profile;
    struct RSCache_Dat2ConfigSpotanim* spot;

    if( !ev_config_record(
            c,
            RSCACHE_TYPE_SPOTANIM,
            RSCACHE_DAT2_CONFIG_KIND_SPOTANIM,
            spotanim_id,
            &bytes,
            &size,
            &profile) )
        return NULL;
    spot = RSCache_Dat2ConfigSpotanimNewDecodeProfile(&profile, bytes, size);
    free(bytes);
    return spot;
}
