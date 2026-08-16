#include "ev_config.h"

#include "rscache.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

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

    for( int i = 0; i < files->file_count; i++ )
    {
        int file_id = (archive->file_ids && i < archive->file_count) ? archive->file_ids[i] : i;
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

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
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
