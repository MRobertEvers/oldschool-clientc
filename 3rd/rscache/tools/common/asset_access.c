#include "asset_access.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void
tool_bytes_free(struct Tool_Bytes* b)
{
    if( !b )
        return;
    free(b->data);
    b->data = NULL;
    b->size = 0;
}

int
tool_dat2_open(
    const char* cache_dir,
    const struct RSCache* profile,
    struct Tool_Dat2Cache* out)
{
    assert(cache_dir && profile && out);
    memset(out, 0, sizeof(*out));
    out->profile = *profile;
    out->disk = RSCache_Dat2DiskNewFromDirectory(cache_dir);
    if( !out->disk )
    {
        fprintf(stderr, "Failed to open dat2 cache: %s\n", cache_dir);
        return 0;
    }
    RSCache_Dat2DiskSetProfile(out->disk, &out->profile);
    return 1;
}

void
tool_dat2_close(struct Tool_Dat2Cache* c)
{
    if( !c )
        return;
    if( c->disk )
        RSCache_Dat2DiskFree(c->disk);
    memset(c, 0, sizeof(*c));
}

int
tool_dat1_open(
    const char* cache_dir,
    const struct RSCache* profile,
    struct Tool_Dat1Cache* out)
{
    assert(cache_dir && profile && out);
    memset(out, 0, sizeof(*out));
    out->profile = *profile;
    out->disk = RSCache_Dat1DiskNewFromDirectory(cache_dir);
    if( !out->disk )
    {
        fprintf(stderr, "Failed to open dat1 cache: %s\n", cache_dir);
        return 0;
    }

    struct RSCache_Dat1DiskArchive* archive = RSCache_Dat1DiskArchiveNewLoad(
        out->disk, RSCACHE_DAT1_DISK_TABLE_CONFIGS, RSCACHE_DAT1_CONFIG_CONFIGS);
    if( archive )
    {
        out->configs = RSCache_FileListDatNewFromDecode(archive->data, archive->data_size);
        RSCache_Dat1DiskArchiveFree(archive);
    }
    return 1;
}

void
tool_dat1_close(struct Tool_Dat1Cache* c)
{
    if( !c )
        return;
    if( c->configs )
        RSCache_FileListDatFree(c->configs);
    if( c->disk )
        RSCache_Dat1DiskFree(c->disk);
    memset(c, 0, sizeof(*c));
}

static int
file_global_id(
    const struct RSCache_RecordAddress* addr,
    int archive_id,
    int file_id)
{
    if( addr->group_shift == 0 )
        return file_id;
    return (archive_id << addr->group_shift) + file_id;
}

struct RSCache_Dat2ConfigNpc*
tool_dat2_npc_load(
    struct Tool_Dat2Cache* c,
    int npc_id)
{
    return tool_dat2_npc_load_checked(c, npc_id, NULL);
}

struct RSCache_Dat2ConfigNpc*
tool_dat2_npc_load_checked(
    struct Tool_Dat2Cache* c,
    int npc_id,
    int* out_exact)
{
    assert(c && c->disk);
    if( out_exact )
        *out_exact = 0;
    struct RSCache_RecordAddress addr =
        RSCache_RecordAddressFor(&c->profile, RSCACHE_TYPE_NPC);
    int table;
    int archive_id;

    if( addr.group_shift == 0 )
    {
        table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_CONFIGS);
        archive_id = RSCACHE_DAT2_CONFIG_KIND_NPC;
    }
    else
    {
        table = RSCache_Dat2DiskTableId(c->disk, addr.table);
        archive_id = npc_id >> addr.group_shift;
    }

    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(c->disk, table, archive_id);
    if( !archive )
        return NULL;
    if( !RSCache_Dat2DiskArchiveInitMetadata(c->disk, archive) || archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }

    struct RSCache local = c->profile;
    RSCache_ProfileSetGroupRevision(&local, RSCACHE_TYPE_NPC, archive->revision);

    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }

    struct RSCache_Dat2ConfigNpc* npc = NULL;
    for( int i = 0; i < files->file_count; i++ )
    {
        if( files->file_sizes[i] <= 0 )
            continue;
        int file_id = (archive->file_ids && i < archive->file_count) ? archive->file_ids[i] : i;
        int id = file_global_id(&addr, archive->archive_id, file_id);
        if( id != npc_id )
            continue;
        npc = RSCache_Dat2ConfigNpcNewDecodeProfile(
            &local, files->files[i], files->file_sizes[i]);
        if( npc && out_exact )
            *out_exact = npc->_consumed == files->file_sizes[i];
        break;
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return npc;
}

struct RSCache_Dat1ConfigNpc*
tool_dat1_npc_load(
    struct Tool_Dat1Cache* c,
    int npc_id)
{
    assert(c && c->configs);
    int data_idx = RSCache_FileListDatFindFileByName(c->configs, "npc.dat");
    int index_idx = RSCache_FileListDatFindFileByName(c->configs, "npc.idx");
    if( data_idx < 0 || index_idx < 0 )
        return NULL;

    struct RSCache_FileListDatIndexed* indexed = RSCache_FileListDatIndexedNewFromDecode(
        c->configs->files[index_idx],
        c->configs->file_sizes[index_idx],
        c->configs->files[data_idx],
        c->configs->file_sizes[data_idx]);
    if( !indexed || npc_id < 0 || npc_id >= indexed->offset_count )
    {
        if( indexed )
            RSCache_FileListDatIndexedFree(indexed);
        return NULL;
    }

    int offset = indexed->offsets[npc_id];
    int next = (npc_id + 1 < indexed->offset_count) ? indexed->offsets[npc_id + 1]
                                                    : indexed->data_size;
    if( offset < 0 || next < offset || next > indexed->data_size )
    {
        RSCache_FileListDatIndexedFree(indexed);
        return NULL;
    }

    struct RSCache_Buffer buffer;
    RSCache_BufferInit(
        &buffer, (uint8_t*)indexed->data + offset, (uint32_t)(next - offset));
    struct RSCache_Dat1ConfigNpc* npc = RSCache_Dat1ConfigNpcDecodeOne(&buffer);
    RSCache_FileListDatIndexedFree(indexed);
    return npc;
}

struct RSCache_Dat2ConfigSequence*
tool_dat2_seq_load(
    struct Tool_Dat2Cache* c,
    int seq_id)
{
    assert(c && c->disk);
    struct RSCache_RecordAddress addr =
        RSCache_RecordAddressFor(&c->profile, RSCACHE_TYPE_SEQUENCE);
    int table;
    int archive_id;

    if( addr.group_shift == 0 )
    {
        table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_CONFIGS);
        archive_id = RSCACHE_DAT2_CONFIG_KIND_SEQUENCE;
    }
    else
    {
        table = RSCache_Dat2DiskTableId(c->disk, addr.table);
        archive_id = seq_id >> addr.group_shift;
    }

    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(c->disk, table, archive_id);
    if( !archive )
        return NULL;
    if( !RSCache_Dat2DiskArchiveInitMetadata(c->disk, archive) || archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }

    struct RSCache local = c->profile;
    RSCache_ProfileSetGroupRevision(&local, RSCACHE_TYPE_SEQUENCE, archive->revision);

    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }

    struct RSCache_Dat2ConfigSequence* seq = NULL;
    for( int i = 0; i < files->file_count; i++ )
    {
        if( files->file_sizes[i] <= 0 )
            continue;
        int file_id = (archive->file_ids && i < archive->file_count) ? archive->file_ids[i] : i;
        int id = file_global_id(&addr, archive->archive_id, file_id);
        if( id != seq_id )
            continue;
        seq = RSCache_Dat2ConfigSequenceNewDecodeProfile(
            &local, files->files[i], files->file_sizes[i]);
        if( seq )
            seq->id = id;
        break;
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return seq;
}

static int
load_seq_group(
    struct Tool_Dat2Cache* c,
    const struct RSCache_RecordAddress* addr,
    struct RSCache_Dat2DiskArchive* archive,
    struct RSCache_Dat2ConfigSequence*** seqs,
    int** ids,
    int* count,
    int* capacity)
{
    struct RSCache local = c->profile;
    RSCache_ProfileSetGroupRevision(&local, RSCACHE_TYPE_SEQUENCE, archive->revision);

    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
        return 0;

    for( int i = 0; i < files->file_count; i++ )
    {
        if( files->file_sizes[i] <= 0 )
            continue;
        int file_id = (archive->file_ids && i < archive->file_count) ? archive->file_ids[i] : i;
        int id = file_global_id(addr, archive->archive_id, file_id);
        struct RSCache_Dat2ConfigSequence* seq =
            RSCache_Dat2ConfigSequenceNewDecodeProfile(
                &local, files->files[i], files->file_sizes[i]);
        if( !seq )
            continue;
        seq->id = id;

        if( *count >= *capacity )
        {
            int new_cap = *capacity == 0 ? 256 : (*capacity) * 2;
            struct RSCache_Dat2ConfigSequence** ns =
                realloc(*seqs, (size_t)new_cap * sizeof(*ns));
            int* ni = realloc(*ids, (size_t)new_cap * sizeof(*ni));
            if( !ns || !ni )
            {
                RSCache_Dat2ConfigSequenceFree(seq);
                free(ns);
                free(ni);
                RSCache_FileListFree(files);
                return 0;
            }
            *seqs = ns;
            *ids = ni;
            *capacity = new_cap;
        }
        (*seqs)[*count] = seq;
        (*ids)[*count] = id;
        (*count)++;
    }

    RSCache_FileListFree(files);
    return 1;
}

int
tool_dat2_seq_load_all(
    struct Tool_Dat2Cache* c,
    struct RSCache_Dat2ConfigSequence*** out_seqs,
    int** out_ids,
    int* out_count)
{
    assert(c && out_seqs && out_ids && out_count);
    *out_seqs = NULL;
    *out_ids = NULL;
    *out_count = 0;

    struct RSCache_RecordAddress addr =
        RSCache_RecordAddressFor(&c->profile, RSCACHE_TYPE_SEQUENCE);
    int capacity = 0;
    int count = 0;
    struct RSCache_Dat2ConfigSequence** seqs = NULL;
    int* ids = NULL;

    if( addr.group_shift == 0 )
    {
        int table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_CONFIGS);
        struct RSCache_Dat2DiskArchive* archive = RSCache_Dat2DiskArchiveNewLoad(
            c->disk, table, RSCACHE_DAT2_CONFIG_KIND_SEQUENCE);
        if( !archive )
            return 0;
        if( !RSCache_Dat2DiskArchiveInitMetadata(c->disk, archive) || archive->file_count <= 0 )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            return 0;
        }
        int ok = load_seq_group(c, &addr, archive, &seqs, &ids, &count, &capacity);
        RSCache_Dat2DiskArchiveFree(archive);
        if( !ok )
        {
            for( int i = 0; i < count; i++ )
                RSCache_Dat2ConfigSequenceFree(seqs[i]);
            free(seqs);
            free(ids);
            return 0;
        }
    }
    else
    {
        int table_id = RSCache_Dat2DiskTableId(c->disk, addr.table);
        struct RSCache_ReferenceTable* table =
            table_id == RSCACHE_DAT2_DISK_TABLE_ABSENT ? NULL : c->disk->tables[table_id];
        if( !table )
            return 0;
        for( int i = 0; i < table->id_count; i++ )
        {
            int archive_id = table->ids[i];
            struct RSCache_Dat2DiskArchive* archive =
                RSCache_Dat2DiskArchiveNewLoad(c->disk, table_id, archive_id);
            if( !archive )
                continue;
            if( !RSCache_Dat2DiskArchiveInitMetadata(c->disk, archive) ||
                archive->file_count <= 0 )
            {
                RSCache_Dat2DiskArchiveFree(archive);
                continue;
            }
            int ok = load_seq_group(c, &addr, archive, &seqs, &ids, &count, &capacity);
            RSCache_Dat2DiskArchiveFree(archive);
            if( !ok )
            {
                for( int j = 0; j < count; j++ )
                    RSCache_Dat2ConfigSequenceFree(seqs[j]);
                free(seqs);
                free(ids);
                return 0;
            }
        }
    }

    *out_seqs = seqs;
    *out_ids = ids;
    *out_count = count;
    return 1;
}

struct RSCache_Dat1ConfigSeqList*
tool_dat1_seq_list_load(struct Tool_Dat1Cache* c)
{
    assert(c && c->configs);
    int idx = RSCache_FileListDatFindFileByName(c->configs, "seq.dat");
    if( idx < 0 )
        return NULL;
    return RSCache_Dat1ConfigSeqListNewDecode(
        c->configs->files[idx], c->configs->file_sizes[idx]);
}

struct RSCache_Dat2ConfigBas*
tool_dat2_bas_load(
    struct Tool_Dat2Cache* c,
    int bas_id)
{
    assert(c && c->disk);
    /* BasType lives in config group 32 on RS2. OSRS uses the same numeric id for
     * hitsplat, so only call this when the profile is RS2 dat2. */
    int table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_CONFIGS);
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(c->disk, table, RSCACHE_DAT2_CONFIG_KIND_BAS);
    if( !archive )
        return NULL;
    if( !RSCache_Dat2DiskArchiveInitMetadata(c->disk, archive) || archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }

    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return NULL;
    }

    struct RSCache_Dat2ConfigBas* bas = NULL;
    for( int i = 0; i < files->file_count; i++ )
    {
        if( files->file_sizes[i] <= 0 )
            continue;
        int file_id = (archive->file_ids && i < archive->file_count) ? archive->file_ids[i] : i;
        if( file_id != bas_id )
            continue;
        bas = RSCache_Dat2ConfigBasNewDecode(files->files[i], files->file_sizes[i]);
        break;
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return bas;
}

int
tool_archive_file_position(
    const struct RSCache_Dat2DiskArchive* archive,
    int file_id)
{
    assert(archive);
    if( archive->file_ids )
    {
        for( int i = 0; i < archive->file_count; i++ )
        {
            if( archive->file_ids[i] == file_id )
                return i;
        }
        return -1;
    }
    if( file_id >= 0 && file_id < archive->file_count )
        return file_id;
    return -1;
}

int
tool_dat2_seq_framemap_id(
    struct Tool_Dat2Cache* c,
    const struct RSCache_Dat2ConfigSequence* seq,
    int verbose)
{
    assert(c && seq);
    if( !seq->frame_ids || seq->frame_count <= 0 )
        return -1;

    int frame_id = seq->frame_ids[0];
    int archive_id = (frame_id >> 16) & 0xFFFF;
    int file_id = frame_id & 0xFFFF;
    int anim_table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_ANIMATIONS);

    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(c->disk, anim_table, archive_id);
    if( !archive )
    {
        if( verbose )
            fprintf(stderr, "warn: failed to load anim archive %d\n", archive_id);
        return -1;
    }
    if( !RSCache_Dat2DiskArchiveInitMetadata(c->disk, archive) || archive->file_count <= 0 )
    {
        if( verbose )
            fprintf(stderr, "warn: anim archive %d has no metadata\n", archive_id);
        RSCache_Dat2DiskArchiveFree(archive);
        return -1;
    }

    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return -1;
    }

    int pos = tool_archive_file_position(archive, file_id);
    int framemap_id = -1;
    if( pos >= 0 && pos < files->file_count && files->file_sizes[pos] > 0 )
    {
        framemap_id = RSCache_Dat2FrameFramemapIdFromFileProfile(
            &c->profile, files->files[pos], files->file_sizes[pos]);
    }
    else if( verbose )
    {
        fprintf(
            stderr,
            "warn: seq %d frame file %d not in archive %d\n",
            seq->id,
            file_id,
            archive_id);
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return framemap_id;
}

struct RSCache_Dat2Framemap*
tool_dat2_framemap_load(
    struct Tool_Dat2Cache* c,
    int framemap_id)
{
    assert(c);
    return RSCache_Dat2FramemapNewFromCache(c->disk, framemap_id);
}

struct RSCache_Model*
tool_dat2_model_load(
    struct Tool_Dat2Cache* c,
    int model_id)
{
    assert(c);
    int table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_MODELS);
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(c->disk, table, model_id);
    if( !archive )
        return NULL;
    struct RSCache_Model* model =
        RSCache_ModelNewDecode((uint8_t*)archive->data, archive->data_size);
    RSCache_Dat2DiskArchiveFree(archive);
    return model;
}

struct RSCache_Model*
tool_dat1_model_load(
    struct Tool_Dat1Cache* c,
    int model_id)
{
    assert(c);
    struct RSCache_Dat1DiskArchive* archive = RSCache_Dat1DiskArchiveNewLoad(
        c->disk, RSCACHE_DAT1_DISK_TABLE_MODELS, model_id);
    if( !archive )
        return NULL;
    struct RSCache_Model* model =
        RSCache_ModelNewDecode((uint8_t*)archive->data, archive->data_size);
    RSCache_Dat1DiskArchiveFree(archive);
    return model;
}

int
tool_dat2_archive_bytes(
    struct Tool_Dat2Cache* c,
    int table_id,
    int archive_id,
    struct Tool_Bytes* out)
{
    assert(c && out);
    memset(out, 0, sizeof(*out));
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(c->disk, table_id, archive_id);
    if( !archive )
        return 0;
    out->data = (uint8_t*)archive->data;
    out->size = archive->data_size;
    archive->data = NULL; /* transfer ownership */
    RSCache_Dat2DiskArchiveFree(archive);
    return 1;
}
