#include "cache_write.h"
#include "transcode.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir_p(path) _mkdir(path)
#else
#define mkdir_p(path) mkdir(path, 0755)
#endif

static int
ensure_dir(const char* path)
{
    struct stat st;
    if( stat(path, &st) == 0 )
        return S_ISDIR(st.st_mode) ? 0 : -1;
    /*
     * EEXIST is success -- the ordinary mkdir -p rule, and on Windows it is
     * also what makes an ABSOLUTE path work: callers split on '/' from index 1,
     * so the first component of "C:/Users/..." is the bare drive designator
     * "C:", where MSVCRT's stat() fails with ENOENT and the mkdir then fails
     * with EEXIST. Without this, every absolute path is an error.
     */
    if( mkdir_p(path) == 0 )
        return 0;
    return errno == EEXIST ? 0 : -1;
}

int
tool_copy_cache_dir(
    const char* src_dir,
    const char* out_dir)
{
    assert(src_dir && out_dir);
    if( ensure_dir(out_dir) != 0 )
    {
        fprintf(stderr, "Failed to create %s: %s\n", out_dir, strerror(errno));
        return 1;
    }

    /* Copy common cache files if present. */
    static const char* names[] = {
        "main_file_cache.dat2",
        "main_file_cache.dat",
        "xteas.json",
        NULL,
    };
    char src[1024];
    char dst[1024];
    for( int i = 0; names[i]; i++ )
    {
        snprintf(src, sizeof(src), "%s/%s", src_dir, names[i]);
        snprintf(dst, sizeof(dst), "%s/%s", out_dir, names[i]);
        FILE* in = fopen(src, "rb");
        if( !in )
            continue;
        FILE* out = fopen(dst, "wb");
        if( !out )
        {
            fclose(in);
            return 1;
        }
        char buf[65536];
        size_t n;
        while( (n = fread(buf, 1, sizeof(buf), in)) > 0 )
        {
            if( fwrite(buf, 1, n, out) != n )
            {
                fclose(in);
                fclose(out);
                return 1;
            }
        }
        fclose(in);
        fclose(out);
    }

    /* Copy index files idx0..idx255. */
    for( int i = 0; i <= 255; i++ )
    {
        snprintf(src, sizeof(src), "%s/main_file_cache.idx%d", src_dir, i);
        snprintf(dst, sizeof(dst), "%s/main_file_cache.idx%d", out_dir, i);
        FILE* in = fopen(src, "rb");
        if( !in )
            continue;
        FILE* out = fopen(dst, "wb");
        if( !out )
        {
            fclose(in);
            return 1;
        }
        char buf[65536];
        size_t n;
        while( (n = fread(buf, 1, sizeof(buf), in)) > 0 )
        {
            if( fwrite(buf, 1, n, out) != n )
            {
                fclose(in);
                fclose(out);
                return 1;
            }
        }
        fclose(in);
        fclose(out);
    }
    return 0;
}

struct free_ctx_dat2
{
    struct Tool_Dat2Cache* c;
    enum RSCache_Type type;
    int table_id; /* for models/framemaps/animations */
    int is_config_file; /* 1 = config group file id, 0 = archive id */
    int config_group;
};

static int
dat2_archive_exists(
    struct Tool_Dat2Cache* c,
    int table_id,
    int archive_id)
{
    if( table_id < 0 || table_id >= RSCACHE_DAT2_DISK_TABLE_CAPACITY )
        return 0;
    struct RSCache_ReferenceTable* table = c->disk->tables[table_id];
    if( !table )
        return 0;
    for( int i = 0; i < table->id_count; i++ )
    {
        if( table->ids[i] == archive_id )
            return 1;
    }
    return 0;
}

static int
dat2_config_file_exists(
    struct Tool_Dat2Cache* c,
    int group,
    int file_id)
{
    int table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_CONFIGS);
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(c->disk, table, group);
    if( !archive )
        return 0;
    int exists = 0;
    if( RSCache_Dat2DiskArchiveInitMetadata(c->disk, archive) )
    {
        if( archive->file_ids )
        {
            for( int i = 0; i < archive->file_count; i++ )
            {
                if( archive->file_ids[i] == file_id )
                {
                    exists = 1;
                    break;
                }
            }
        }
        else if( file_id >= 0 && file_id < archive->file_count )
            exists = 1;
    }
    RSCache_Dat2DiskArchiveFree(archive);
    return exists;
}

int
tool_dat2_id_is_free(
    int id,
    void* userdata)
{
    struct free_ctx_dat2* ctx = userdata;
    if( ctx->is_config_file )
        return !dat2_config_file_exists(ctx->c, ctx->config_group, id);
    return !dat2_archive_exists(ctx->c, ctx->table_id, id);
}

/* Exposed for port_npc planning. */
int
tool_dat2_model_id_free(struct Tool_Dat2Cache* c, int id)
{
    struct free_ctx_dat2 ctx = {
        .c = c,
        .table_id = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_MODELS),
        .is_config_file = 0,
    };
    return tool_dat2_id_is_free(id, &ctx);
}

int
tool_dat2_framemap_id_free(struct Tool_Dat2Cache* c, int id)
{
    struct free_ctx_dat2 ctx = {
        .c = c,
        .table_id = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_SKELETONS),
        .is_config_file = 0,
    };
    return tool_dat2_id_is_free(id, &ctx);
}

int
tool_dat2_anim_archive_id_free(struct Tool_Dat2Cache* c, int id)
{
    struct free_ctx_dat2 ctx = {
        .c = c,
        .table_id = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_ANIMATIONS),
        .is_config_file = 0,
    };
    return tool_dat2_id_is_free(id, &ctx);
}

int
tool_dat2_seq_id_free(struct Tool_Dat2Cache* c, int id)
{
    struct RSCache_RecordAddress addr =
        RSCache_RecordAddressFor(&c->profile, RSCACHE_TYPE_SEQUENCE);
    if( addr.group_shift == 0 )
    {
        struct free_ctx_dat2 ctx = {
            .c = c,
            .is_config_file = 1,
            .config_group = RSCACHE_DAT2_CONFIG_KIND_SEQUENCE,
        };
        return tool_dat2_id_is_free(id, &ctx);
    }
    /* Sharded: free if the file slot is unused in its archive. */
    int table = RSCache_Dat2DiskTableId(c->disk, addr.table);
    int archive_id = id >> addr.group_shift;
    int file_id = id & addr.file_mask;
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(c->disk, table, archive_id);
    if( !archive )
        return 1;
    int free_slot = 1;
    if( RSCache_Dat2DiskArchiveInitMetadata(c->disk, archive) && archive->file_ids )
    {
        for( int i = 0; i < archive->file_count; i++ )
        {
            if( archive->file_ids[i] == file_id )
            {
                free_slot = 0;
                break;
            }
        }
    }
    RSCache_Dat2DiskArchiveFree(archive);
    return free_slot;
}

int
tool_dat2_npc_id_free(struct Tool_Dat2Cache* c, int id)
{
    struct RSCache_RecordAddress addr =
        RSCache_RecordAddressFor(&c->profile, RSCACHE_TYPE_NPC);
    if( addr.group_shift == 0 )
    {
        struct free_ctx_dat2 ctx = {
            .c = c,
            .is_config_file = 1,
            .config_group = RSCACHE_DAT2_CONFIG_KIND_NPC,
        };
        return tool_dat2_id_is_free(id, &ctx);
    }
    int table = RSCache_Dat2DiskTableId(c->disk, addr.table);
    int archive_id = id >> addr.group_shift;
    int file_id = id & addr.file_mask;
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(c->disk, table, archive_id);
    if( !archive )
        return 1;
    int free_slot = 1;
    if( RSCache_Dat2DiskArchiveInitMetadata(c->disk, archive) && archive->file_ids )
    {
        for( int i = 0; i < archive->file_count; i++ )
        {
            if( archive->file_ids[i] == file_id )
            {
                free_slot = 0;
                break;
            }
        }
    }
    RSCache_Dat2DiskArchiveFree(archive);
    return free_slot;
}

int
tool_dat2_bas_id_free(struct Tool_Dat2Cache* c, int id)
{
    struct free_ctx_dat2 ctx = {
        .c = c,
        .is_config_file = 1,
        .config_group = RSCACHE_DAT2_CONFIG_KIND_BAS,
    };
    return tool_dat2_id_is_free(id, &ctx);
}

static int
dat1_archive_present(
    struct Tool_Dat1Cache* c,
    int table_id,
    int archive_id)
{
    char path[1024];
    snprintf(
        path,
        sizeof(path),
        "%s/main_file_cache.idx%d",
        c->disk->directory,
        table_id);
    FILE* f = fopen(path, "rb");
    if( !f )
        return 0;
    if( fseek(f, (long)archive_id * 6, SEEK_SET) != 0 )
    {
        fclose(f);
        return 0;
    }
    unsigned char rec[6];
    if( fread(rec, 1, 6, f) != 6 )
    {
        fclose(f);
        return 0;
    }
    fclose(f);
    int length = (rec[0] << 16) | (rec[1] << 8) | rec[2];
    int sector = (rec[3] << 16) | (rec[4] << 8) | rec[5];
    return length > 0 && sector > 0;
}

int
tool_dat1_npc_count(struct Tool_Dat1Cache* c)
{
    assert(c && c->configs);
    int data_idx = RSCache_FileListDatFindFileByName(c->configs, "npc.dat");
    int index_idx = RSCache_FileListDatFindFileByName(c->configs, "npc.idx");
    if( data_idx < 0 || index_idx < 0 )
        return 0;
    struct RSCache_FileListDatIndexed* indexed = RSCache_FileListDatIndexedNewFromDecode(
        c->configs->files[index_idx],
        c->configs->file_sizes[index_idx],
        c->configs->files[data_idx],
        c->configs->file_sizes[data_idx]);
    if( !indexed )
        return 0;
    int count = indexed->offset_count;
    RSCache_FileListDatIndexedFree(indexed);
    return count;
}

int
tool_dat1_seq_count(struct Tool_Dat1Cache* c)
{
    struct RSCache_Dat1ConfigSeqList* list = tool_dat1_seq_list_load(c);
    if( !list )
        return 0;
    int count = list->seqs_count;
    RSCache_Dat1ConfigSeqListFree(list);
    return count;
}

int
tool_dat1_model_id_free(struct Tool_Dat1Cache* c, int id)
{
    assert(c);
    return !dat1_archive_present(c, RSCACHE_DAT1_DISK_TABLE_MODELS, id);
}

int
tool_dat1_anim_archive_id_free(struct Tool_Dat1Cache* c, int id)
{
    assert(c);
    return !dat1_archive_present(c, RSCACHE_DAT1_DISK_TABLE_ANIMATIONS, id);
}

int
tool_dat1_max_frame_id(struct Tool_Dat1Cache* c)
{
    assert(c);
    int max_id = -1;
    int max_archives = 0;

    struct RSCache_Dat1DiskArchive* vl_arch = RSCache_Dat1DiskArchiveNewLoad(
        c->disk, RSCACHE_DAT1_DISK_TABLE_CONFIGS, RSCACHE_DAT1_CONFIG_VERSION_LIST);
    if( vl_arch )
    {
        struct RSCache_FileListDat* vl_jag =
            RSCache_FileListDatNewFromDecode(vl_arch->data, vl_arch->data_size);
        if( vl_jag )
        {
            struct RSCache_Dat1VersionList* vl =
                RSCache_Dat1VersionListNewFromJagfile(vl_jag);
            if( vl )
            {
                max_archives = vl->anim_version_count;
                if( vl->anim_index_count > max_archives )
                    max_archives = vl->anim_index_count;
                RSCache_Dat1VersionListFree(vl);
            }
            RSCache_FileListDatFree(vl_jag);
        }
        RSCache_Dat1DiskArchiveFree(vl_arch);
    }
    if( max_archives <= 0 )
        max_archives = 2048;

    for( int archive_id = 0; archive_id < max_archives; archive_id++ )
    {
        if( !dat1_archive_present(c, RSCACHE_DAT1_DISK_TABLE_ANIMATIONS, archive_id) )
            continue;
        struct RSCache_Dat1DiskArchive* archive = RSCache_Dat1DiskArchiveNewLoad(
            c->disk, RSCACHE_DAT1_DISK_TABLE_ANIMATIONS, archive_id);
        if( !archive )
            continue;
        struct RSCache_Dat1AnimBaseFrames* abf =
            RSCache_Dat1AnimBaseFramesNewDecode(archive->data, archive->data_size);
        RSCache_Dat1DiskArchiveFree(archive);
        if( !abf )
            continue;
        for( int i = 0; i < abf->frame_count; i++ )
        {
            if( abf->frames[i].id > max_id )
                max_id = abf->frames[i].id;
        }
        RSCache_Dat1AnimBaseFramesFree(abf);
    }
    return max_id;
}

static int
put_config_record(
    struct RSCache_Dat2Edit* edit,
    struct Tool_Dat2Cache* dst,
    enum RSCache_Type type,
    int config_kind,
    int record_id,
    const uint8_t* data,
    uint32_t size)
{
    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(&dst->profile, type);
    if( addr.group_shift == 0 )
    {
        int table = RSCache_Dat2DiskTableId(dst->disk, RSCACHE_DAT2_TABLE_CONFIGS);
        return RSCache_Dat2EditPutFile(edit, table, config_kind, record_id, data, size) ? 0 : 1;
    }
    int table = RSCache_Dat2DiskTableId(dst->disk, addr.table);
    int archive_id = record_id >> addr.group_shift;
    int file_id = record_id & addr.file_mask;
    return RSCache_Dat2EditPutFile(edit, table, archive_id, file_id, data, size) ? 0 : 1;
}

int
tool_port_commit_dat2(
    struct Tool_Dat2Cache* src,
    struct Tool_Dat2Cache* dst,
    const char* dst_directory,
    struct Tool_PortPlan* plan,
    int emit_bas)
{
    assert(src && dst && dst_directory && plan);

    struct RSCache_Dat2Edit* edit = RSCache_Dat2EditNew(dst->disk);
    if( !edit )
        return 1;

    int models_table = RSCache_Dat2DiskTableId(dst->disk, RSCACHE_DAT2_TABLE_MODELS);
    int skel_table = RSCache_Dat2DiskTableId(dst->disk, RSCACHE_DAT2_TABLE_SKELETONS);
    int anim_table = RSCache_Dat2DiskTableId(dst->disk, RSCACHE_DAT2_TABLE_ANIMATIONS);

    /* Models */
    for( int i = 0; i < plan->model_map.count; i++ )
    {
        int src_id = plan->model_map.entries[i].source_id;
        int dst_id = plan->model_map.entries[i].dest_id;
        struct Tool_Bytes bytes;
        int src_table = RSCache_Dat2DiskTableId(src->disk, RSCACHE_DAT2_TABLE_MODELS);
        if( !tool_dat2_archive_bytes(src, src_table, src_id, &bytes) )
        {
            tool_port_plan_add_warning(plan, "failed to load model %d", src_id);
            continue;
        }
        int same_codec =
            RSCache_ModelCodecVersion(&src->profile) == RSCache_ModelCodecVersion(&dst->profile);
        if( same_codec )
        {
            RSCache_Dat2EditPutArchive(edit, models_table, dst_id, bytes.data, (uint32_t)bytes.size);
        }
        else
        {
            struct RSCache_ModelProvenance* prov = NULL;
            struct RSCache_Model* model =
                RSCache_ModelNewDecodeProvenance(bytes.data, bytes.size, &prov);
            if( model )
            {
                uint32_t bound = RSCache_ModelEncodeBound(model, prov);
                uint8_t* enc = malloc(bound);
                if( enc )
                {
                    uint32_t n = RSCache_ModelEncode(&dst->profile, model, prov, enc, bound);
                    if( n )
                        RSCache_Dat2EditPutArchive(edit, models_table, dst_id, enc, n);
                    free(enc);
                }
                RSCache_ModelFree(model);
            }
            RSCache_ModelProvenanceFree(prov);
        }
        tool_bytes_free(&bytes);
    }

    /* Chathead models share the models table. */
    for( int i = 0; i < plan->chathead_map.count; i++ )
    {
        int src_id = plan->chathead_map.entries[i].source_id;
        int dst_id = plan->chathead_map.entries[i].dest_id;
        int already = 0;
        for( int j = 0; j < plan->model_map.count; j++ )
        {
            if( plan->model_map.entries[j].source_id == src_id &&
                plan->model_map.entries[j].dest_id == dst_id )
            {
                already = 1;
                break;
            }
        }
        if( already )
            continue;
        struct Tool_Bytes bytes;
        int src_table = RSCache_Dat2DiskTableId(src->disk, RSCACHE_DAT2_TABLE_MODELS);
        if( !tool_dat2_archive_bytes(src, src_table, src_id, &bytes) )
            continue;
        RSCache_Dat2EditPutArchive(edit, models_table, dst_id, bytes.data, (uint32_t)bytes.size);
        tool_bytes_free(&bytes);
    }

    /* Framemaps */
    for( int i = 0; i < plan->framemap_map.count; i++ )
    {
        int src_id = plan->framemap_map.entries[i].source_id;
        int dst_id = plan->framemap_map.entries[i].dest_id;
        struct Tool_Bytes bytes;
        int src_table = RSCache_Dat2DiskTableId(src->disk, RSCACHE_DAT2_TABLE_SKELETONS);
        if( !tool_dat2_archive_bytes(src, src_table, src_id, &bytes) )
            continue;
        int same =
            RSCache_Dat2FramemapCodecVersion(&src->profile) ==
            RSCache_Dat2FramemapCodecVersion(&dst->profile);
        if( same )
        {
            RSCache_Dat2EditPutArchive(edit, skel_table, dst_id, bytes.data, (uint32_t)bytes.size);
        }
        else
        {
            struct RSCache_Dat2Framemap* fm = RSCache_Dat2FramemapNewDecodeProfile(
                &src->profile, src_id, (char*)bytes.data, bytes.size);
            if( fm )
            {
                fm->id = dst_id;
                uint32_t bound = RSCache_Dat2FramemapEncodeBound(fm);
                uint8_t* enc = malloc(bound);
                if( enc )
                {
                    uint32_t n = RSCache_Dat2FramemapEncode(fm, enc, bound);
                    if( n )
                        RSCache_Dat2EditPutArchive(edit, skel_table, dst_id, enc, n);
                    free(enc);
                }
                RSCache_Dat2FramemapFree(fm);
            }
        }
        tool_bytes_free(&bytes);
    }

    /* Frame archives: copy group, patch framemap ids in each frame file. */
    for( int i = 0; i < plan->frame_archive_map.count; i++ )
    {
        int src_arch = plan->frame_archive_map.entries[i].source_id;
        int dst_arch = plan->frame_archive_map.entries[i].dest_id;
        int src_table = RSCache_Dat2DiskTableId(src->disk, RSCACHE_DAT2_TABLE_ANIMATIONS);
        struct RSCache_Dat2DiskArchive* archive =
            RSCache_Dat2DiskArchiveNewLoad(src->disk, src_table, src_arch);
        if( !archive )
            continue;
        if( !RSCache_Dat2DiskArchiveInitMetadata(src->disk, archive) )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }
        struct RSCache_FileList* files = RSCache_FileListNewFromDecode(
            archive->data, archive->data_size, archive->file_count);
        if( !files )
        {
            RSCache_Dat2DiskArchiveFree(archive);
            continue;
        }

        int same_frame_codec =
            RSCache_Dat2FrameCodecVersion(&src->profile) ==
            RSCache_Dat2FrameCodecVersion(&dst->profile);

        for( int f = 0; f < files->file_count; f++ )
        {
            if( files->file_sizes[f] <= 0 )
                continue;
            int file_id =
                (archive->file_ids && f < archive->file_count) ? archive->file_ids[f] : f;
            int old_fm = RSCache_Dat2FrameFramemapIdFromFileProfile(
                &src->profile, files->files[f], files->file_sizes[f]);
            int new_fm = old_fm;
            tool_id_map_lookup(&plan->framemap_map, old_fm, &new_fm);

            if( same_frame_codec && old_fm == new_fm )
            {
                RSCache_Dat2EditPutFile(
                    edit,
                    anim_table,
                    dst_arch,
                    file_id,
                    (uint8_t*)files->files[f],
                    (uint32_t)files->file_sizes[f]);
            }
            else if( same_frame_codec )
            {
                /* Patch framemap id in-place in a copy. */
                uint8_t* copy = malloc((size_t)files->file_sizes[f]);
                if( !copy )
                    continue;
                memcpy(copy, files->files[f], (size_t)files->file_sizes[f]);
                int off = (RSCache_Dat2FrameCodecVersion(&src->profile) == RSCACHE_CODEC_FRAME_V2)
                              ? 1
                              : 0;
                if( files->file_sizes[f] >= off + 2 )
                {
                    copy[off] = (uint8_t)((new_fm >> 8) & 0xFF);
                    copy[off + 1] = (uint8_t)(new_fm & 0xFF);
                }
                RSCache_Dat2EditPutFile(
                    edit, anim_table, dst_arch, file_id, copy, (uint32_t)files->file_sizes[f]);
                free(copy);
            }
            else
            {
                struct RSCache_Dat2Framemap* fm =
                    tool_dat2_framemap_load(src, old_fm >= 0 ? old_fm : 0);
                struct RSCache_Dat2Frame* frame = RSCache_Dat2FrameNewDecodeProfile(
                    &src->profile, file_id, fm, files->files[f], files->file_sizes[f]);
                if( frame )
                {
                    frame->framemap_id = new_fm;
                    if( fm )
                        fm->id = new_fm;
                    uint32_t bound = RSCache_Dat2FrameEncodeBoundCodec(
                        frame, RSCache_Dat2FrameCodecVersion(&dst->profile));
                    uint8_t* enc = malloc(bound);
                    if( enc )
                    {
                        uint32_t n = RSCache_Dat2FrameEncodeCodec(
                            frame,
                            RSCache_Dat2FrameCodecVersion(&dst->profile),
                            fm,
                            enc,
                            bound);
                        if( n )
                            RSCache_Dat2EditPutFile(edit, anim_table, dst_arch, file_id, enc, n);
                        free(enc);
                    }
                    RSCache_Dat2FrameFree(frame);
                }
                RSCache_Dat2FramemapFree(fm);
            }
        }
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
    }

    /* Sequences */
    for( int i = 0; i < plan->seq_map.count; i++ )
    {
        int src_id = plan->seq_map.entries[i].source_id;
        int dst_id = plan->seq_map.entries[i].dest_id;
        struct RSCache_Dat2ConfigSequence* seq = tool_dat2_seq_load(src, src_id);
        if( !seq )
            continue;
        for( int f = 0; f < seq->frame_count; f++ )
        {
            int old = seq->frame_ids[f];
            int old_arch = (old >> 16) & 0xFFFF;
            int file_id = old & 0xFFFF;
            int new_arch = old_arch;
            tool_id_map_lookup(&plan->frame_archive_map, old_arch, &new_arch);
            seq->frame_ids[f] = (new_arch << 16) | file_id;
        }
        uint32_t bound = 65536;
        uint8_t* enc = malloc(bound);
        if( enc )
        {
            uint32_t n = RSCache_Dat2ConfigSequenceEncode(&dst->profile, seq, enc, bound);
            if( n == 0 )
            {
                free(enc);
                bound = 256 * 1024;
                enc = malloc(bound);
                if( enc )
                    n = RSCache_Dat2ConfigSequenceEncode(&dst->profile, seq, enc, bound);
            }
            if( enc && n )
                put_config_record(
                    edit, dst, RSCACHE_TYPE_SEQUENCE, RSCACHE_DAT2_CONFIG_KIND_SEQUENCE, dst_id, enc, n);
            free(enc);
        }
        RSCache_Dat2ConfigSequenceFree(seq);
    }

    /* BasType */
    if( emit_bas && plan->bas_map.count > 0 )
    {
        for( int i = 0; i < plan->bas_map.count; i++ )
        {
            int dst_bas = plan->bas_map.entries[i].dest_id;
            struct RSCache_Dat2ConfigBas bas;
            memset(&bas, 0, sizeof(bas));
            bas.idle_seq_id = -1;
            bas.walk_seq_id = -1;
            bas.walk_back_seq_id = -1;
            bas.walk_left_seq_id = -1;
            bas.walk_right_seq_id = -1;
            if( plan->npc.anim_present[TOOL_ANIM_IDLE] )
            {
                int d = plan->npc.anim[TOOL_ANIM_IDLE];
                tool_id_map_lookup(&plan->seq_map, plan->npc.anim[TOOL_ANIM_IDLE], &d);
                bas.idle_seq_id = d;
            }
            if( plan->npc.anim_present[TOOL_ANIM_WALK] )
            {
                int d = plan->npc.anim[TOOL_ANIM_WALK];
                tool_id_map_lookup(&plan->seq_map, plan->npc.anim[TOOL_ANIM_WALK], &d);
                bas.walk_seq_id = d;
            }
            if( plan->npc.anim_present[TOOL_ANIM_WALK_BACK] )
            {
                int d = plan->npc.anim[TOOL_ANIM_WALK_BACK];
                tool_id_map_lookup(&plan->seq_map, plan->npc.anim[TOOL_ANIM_WALK_BACK], &d);
                bas.walk_back_seq_id = d;
            }
            if( plan->npc.anim_present[TOOL_ANIM_WALK_LEFT] )
            {
                int d = plan->npc.anim[TOOL_ANIM_WALK_LEFT];
                tool_id_map_lookup(&plan->seq_map, plan->npc.anim[TOOL_ANIM_WALK_LEFT], &d);
                bas.walk_left_seq_id = d;
            }
            if( plan->npc.anim_present[TOOL_ANIM_WALK_RIGHT] )
            {
                int d = plan->npc.anim[TOOL_ANIM_WALK_RIGHT];
                tool_id_map_lookup(&plan->seq_map, plan->npc.anim[TOOL_ANIM_WALK_RIGHT], &d);
                bas.walk_right_seq_id = d;
            }
            uint32_t bound = RSCache_Dat2ConfigBasEncodeBound(&bas);
            uint8_t* enc = malloc(bound ? bound : 64);
            if( enc )
            {
                uint32_t n = RSCache_Dat2ConfigBasEncode(&bas, enc, bound ? bound : 64);
                if( n )
                {
                    int table = RSCache_Dat2DiskTableId(dst->disk, RSCACHE_DAT2_TABLE_CONFIGS);
                    RSCache_Dat2EditPutFile(
                        edit, table, RSCACHE_DAT2_CONFIG_KIND_BAS, dst_bas, enc, n);
                }
                free(enc);
            }
            (void)dst_bas;
        }
    }

    /* NPC record */
    {
        int bas_dest = -1;
        if( emit_bas && plan->bas_map.count > 0 )
            bas_dest = plan->bas_map.entries[0].dest_id;

        struct Tool_NeutralNpc remapped = plan->npc;
        /* Remap model/anim ids in a shallow copy of arrays. */
        int* models = NULL;
        int* heads = NULL;
        if( remapped.models_count > 0 )
        {
            models = malloc((size_t)remapped.models_count * sizeof(int));
            for( int i = 0; i < remapped.models_count; i++ )
            {
                int d = remapped.models[i];
                tool_id_map_lookup(&plan->model_map, remapped.models[i], &d);
                models[i] = d;
            }
            remapped.models = models;
        }
        if( remapped.chathead_models_count > 0 )
        {
            heads = malloc((size_t)remapped.chathead_models_count * sizeof(int));
            for( int i = 0; i < remapped.chathead_models_count; i++ )
            {
                int d = remapped.chathead_models[i];
                tool_id_map_lookup(&plan->chathead_map, remapped.chathead_models[i], &d);
                heads[i] = d;
            }
            remapped.chathead_models = heads;
        }
        for( int s = 0; s < TOOL_ANIM_SLOT_COUNT; s++ )
        {
            if( !remapped.anim_present[s] )
                continue;
            int d = remapped.anim[s];
            tool_id_map_lookup(&plan->seq_map, remapped.anim[s], &d);
            remapped.anim[s] = d;
        }

        int dest_npc_id = plan->npc.source_id;
        tool_id_map_lookup(&plan->model_map, -1, &dest_npc_id); /* no-op guard */
        /* NPC id lives in seq_map? No — use a dedicated lookup. Stored as first
         * related or we keep source_id remapped via a synthetic entry. */
        dest_npc_id = plan->npc.source_id;
        for( int i = 0; i < plan->seq_map.count; i++ )
        {
            /* placeholder — npc dest id is plan->npc.source_id remapped via
             * allocating into npc map; we store it in bas_map unused slot? 
             * Better: look for warning. Actually port_npc stores dest npc in
             * related_seq_ids[-1] hack — instead use chathead_map with key -1.
             * Simplest: dest npc id equals source if free else remapped; store
             * in plan by convention as model_map entry with source -npc?
             * We'll pass dest via plan->npc.source_id already remapped by caller.
             */
            break;
        }
        /* Caller sets plan->npc.source_id to the DESTINATION id before commit
         * when remapped; keep original in model_map? port_npc will set
         * plan->related_seq_count field... Actually port_npc keeps dest npc id
         * in plan by rewriting source_id. Document that. */

        struct RSCache_Dat2ConfigNpc* npc = tool_neutral_npc_to_dat2(
            &remapped, &dst->profile, emit_bas, bas_dest, &plan->warnings, &plan->warning_count);
        if( npc )
        {
            uint8_t enc[4096];
            uint32_t n = RSCache_Dat2ConfigNpcEncodeProfile(&dst->profile, npc, enc, sizeof(enc));
            if( n )
                put_config_record(
                    edit,
                    dst,
                    RSCACHE_TYPE_NPC,
                    RSCACHE_DAT2_CONFIG_KIND_NPC,
                    remapped.source_id,
                    enc,
                    n);
            RSCache_Dat2ConfigNpcFree(npc);
        }
        free(models);
        free(heads);
    }

    bool ok = RSCache_Dat2EditCommit(edit, dst_directory);
    RSCache_Dat2EditFree(edit);
    return ok ? 0 : 1;
}

int
tool_port_commit_dat1(
    struct Tool_Dat2Cache* src_dat2,
    struct Tool_Dat1Cache* src_dat1,
    struct Tool_Dat1Cache* dst,
    const char* dst_directory,
    struct Tool_PortPlan* plan)
{
    assert(dst && dst_directory && plan);
    (void)src_dat1;

    struct RSCache_Dat1Edit* edit = RSCache_Dat1EditNew(dst->disk);
    if( !edit )
        return 1;

    if( src_dat2 )
    {
        /* Body + chathead models. Chatheads that share a body model id are
         * already in model_map; write the rest from chathead_map. */
        for( int pass = 0; pass < 2; pass++ )
        {
            struct Tool_IdMap* map = pass == 0 ? &plan->model_map : &plan->chathead_map;
            for( int i = 0; i < map->count; i++ )
            {
                int src_id = map->entries[i].source_id;
                int dst_id = map->entries[i].dest_id;
                if( pass == 1 )
                {
                    int already;
                    if( tool_id_map_lookup(&plan->model_map, src_id, &already) )
                        continue;
                }
                struct Tool_Bytes bytes;
                int table =
                    RSCache_Dat2DiskTableId(src_dat2->disk, RSCACHE_DAT2_TABLE_MODELS);
                if( !tool_dat2_archive_bytes(src_dat2, table, src_id, &bytes) )
                    continue;
                struct RSCache_ModelProvenance* prov = NULL;
                struct RSCache_Model* model =
                    RSCache_ModelNewDecodeProvenance(bytes.data, bytes.size, &prov);
                if( model )
                {
                    uint32_t ob2_size = 0;
                    uint8_t* ob2 = tool_transcode_model_to_ob2(
                        model, prov, &ob2_size, &plan->warnings, &plan->warning_count);
                    if( ob2 )
                    {
                        RSCache_Dat1EditPutModel(edit, dst_id, ob2, ob2_size, false);
                        free(ob2);
                    }
                    RSCache_ModelFree(model);
                }
                RSCache_ModelProvenanceFree(prov);
                tool_bytes_free(&bytes);
            }
        }

        int anim_table =
            RSCache_Dat2DiskTableId(src_dat2->disk, RSCACHE_DAT2_TABLE_ANIMATIONS);

        /* One dat1 anim archive per source framemap, packed with all ported frames. */
        for( int fi = 0; fi < plan->framemap_map.count; fi++ )
        {
            int src_fm = plan->framemap_map.entries[fi].source_id;
            int dst_arch = plan->framemap_map.entries[fi].dest_id;
            struct RSCache_Dat2Framemap* fm = tool_dat2_framemap_load(src_dat2, src_fm);
            if( !fm )
            {
                tool_port_plan_add_warning(plan, "failed to load framemap %d", src_fm);
                continue;
            }
            struct RSCache_Dat1AnimBase* base = tool_transcode_framemap_to_animbase(
                fm, &plan->warnings, &plan->warning_count);
            if( !base )
            {
                RSCache_Dat2FramemapFree(fm);
                continue;
            }

            /* Collect unique (src composite, dest frame id) for this framemap. */
            int* src_composites = NULL;
            int* dst_frame_ids = NULL;
            int nframes = 0;
            int ncap = 0;
            for( int si = 0; si < plan->seq_map.count; si++ )
            {
                int src_seq = plan->seq_map.entries[si].source_id;
                struct RSCache_Dat2ConfigSequence* seq =
                    tool_dat2_seq_load(src_dat2, src_seq);
                if( !seq )
                    continue;
                int seq_fm = tool_dat2_seq_framemap_id(src_dat2, seq, 0);
                if( seq_fm != src_fm || !seq->frame_ids )
                {
                    RSCache_Dat2ConfigSequenceFree(seq);
                    continue;
                }
                for( int f = 0; f < seq->frame_count; f++ )
                {
                    int composite = seq->frame_ids[f];
                    int dst_fid;
                    if( !tool_id_map_lookup(&plan->frame_id_map, composite, &dst_fid) )
                        continue;
                    int dup = 0;
                    for( int k = 0; k < nframes; k++ )
                    {
                        if( src_composites[k] == composite )
                        {
                            dup = 1;
                            break;
                        }
                    }
                    if( dup )
                        continue;
                    if( nframes >= ncap )
                    {
                        int nc = ncap == 0 ? 64 : ncap * 2;
                        int* ns = realloc(src_composites, (size_t)nc * sizeof(int));
                        int* nd = realloc(dst_frame_ids, (size_t)nc * sizeof(int));
                        if( !ns || !nd )
                        {
                            free(ns);
                            free(nd);
                            break;
                        }
                        src_composites = ns;
                        dst_frame_ids = nd;
                        ncap = nc;
                    }
                    src_composites[nframes] = composite;
                    dst_frame_ids[nframes] = dst_fid;
                    nframes++;
                }
                RSCache_Dat2ConfigSequenceFree(seq);
            }

            struct RSCache_Dat1AnimBaseFrames* abf = calloc(1, sizeof(*abf));
            if( !abf )
            {
                for( int bi = 0; bi < base->length; bi++ )
                    free(base->labels[bi]);
                free(base->types);
                free(base->labels);
                free(base->label_counts);
                free(base);
                free(src_composites);
                free(dst_frame_ids);
                RSCache_Dat2FramemapFree(fm);
                continue;
            }
            abf->base = base;
            if( nframes > 0 )
            {
                abf->frames = calloc((size_t)nframes, sizeof(*abf->frames));
                if( !abf->frames )
                {
                    RSCache_Dat1AnimBaseFramesFree(abf);
                    free(src_composites);
                    free(dst_frame_ids);
                    RSCache_Dat2FramemapFree(fm);
                    continue;
                }
            }

            for( int f = 0; f < nframes; f++ )
            {
                int composite = src_composites[f];
                int arch = (composite >> 16) & 0xFFFF;
                int file_id = composite & 0xFFFF;
                struct RSCache_Dat2DiskArchive* archive =
                    RSCache_Dat2DiskArchiveNewLoad(src_dat2->disk, anim_table, arch);
                if( !archive ||
                    !RSCache_Dat2DiskArchiveInitMetadata(src_dat2->disk, archive) )
                {
                    if( archive )
                        RSCache_Dat2DiskArchiveFree(archive);
                    tool_port_plan_add_warning(
                        plan, "failed to load dat2 frame archive %d", arch);
                    continue;
                }
                struct RSCache_FileList* files = RSCache_FileListNewFromDecode(
                    archive->data, archive->data_size, archive->file_count);
                if( !files )
                {
                    RSCache_Dat2DiskArchiveFree(archive);
                    continue;
                }
                int pos = tool_archive_file_position(archive, file_id);
                if( pos < 0 || pos >= files->file_count || files->file_sizes[pos] <= 0 )
                {
                    RSCache_FileListFree(files);
                    RSCache_Dat2DiskArchiveFree(archive);
                    tool_port_plan_add_warning(
                        plan, "dat2 frame file %d missing in archive %d", file_id, arch);
                    continue;
                }
                struct RSCache_Dat2Frame* frame = RSCache_Dat2FrameNewDecodeProfile(
                    &src_dat2->profile,
                    file_id,
                    fm,
                    files->files[pos],
                    files->file_sizes[pos]);
                RSCache_FileListFree(files);
                RSCache_Dat2DiskArchiveFree(archive);
                if( !frame )
                {
                    tool_port_plan_add_warning(
                        plan, "failed to decode dat2 frame %d", composite);
                    continue;
                }
                if( !tool_transcode_dat2_frame_to_dat1(
                        frame,
                        base,
                        dst_frame_ids[f],
                        0,
                        &abf->frames[abf->frame_count],
                        &plan->warnings,
                        &plan->warning_count) )
                {
                    RSCache_Dat2FrameFree(frame);
                    tool_port_plan_add_warning(
                        plan, "failed to transcode frame %d", composite);
                    continue;
                }
                abf->frame_count++;
                RSCache_Dat2FrameFree(frame);
            }
            free(src_composites);
            free(dst_frame_ids);

            uint32_t bound = RSCache_Dat1AnimBaseFramesEncodeBound(abf);
            if( bound == 0 )
                bound = 64;
            uint8_t* enc = malloc(bound);
            if( enc )
            {
                uint32_t n = RSCache_Dat1AnimBaseFramesEncode(abf, enc, bound);
                if( n )
                    RSCache_Dat1EditPutAnimArchive(edit, dst_arch, enc, n, false);
                else
                    tool_port_plan_add_warning(
                        plan,
                        "dat1 anim archive encode failed for framemap %d (%d frames); "
                        "section lengths must fit in u16 — drop --include-related-anims "
                        "or split across archives",
                        src_fm,
                        abf->frame_count);
                free(enc);
            }
            RSCache_Dat1AnimBaseFramesFree(abf);
            RSCache_Dat2FramemapFree(fm);
        }

        /* Sequences: remap frame composites to dat1 global ids. */
        for( int i = 0; i < plan->seq_map.count; i++ )
        {
            int src_id = plan->seq_map.entries[i].source_id;
            int dst_id = plan->seq_map.entries[i].dest_id;
            struct RSCache_Dat2ConfigSequence* src_seq =
                tool_dat2_seq_load(src_dat2, src_id);
            if( !src_seq )
            {
                tool_port_plan_add_warning(plan, "failed to load source seq %d", src_id);
                continue;
            }

            struct RSCache_Dat1ConfigSeq seq;
            memset(&seq, 0, sizeof(seq));
            seq.loops = -1;
            seq.priority = 5;
            seq.replaceheldleft = -1;
            seq.replaceheldright = -1;
            seq.maxloops = 99;
            seq.preanim_move = -1;
            seq.postanim_move = -1;
            seq.duplicate_behavior = -1;
            seq.stretches = src_seq->stretches;
            if( src_seq->forced_priority > 0 )
                seq.priority = src_seq->forced_priority;
            else if( src_seq->priority > 0 )
                seq.priority = src_seq->priority;
            if( src_seq->max_loops > 0 )
                seq.maxloops = src_seq->max_loops;

            if( src_seq->frame_count > 0 && src_seq->frame_ids )
            {
                seq.frame_count = src_seq->frame_count;
                seq.frames = calloc((size_t)seq.frame_count, sizeof(int));
                seq.iframes = calloc((size_t)seq.frame_count, sizeof(int));
                seq.delay = calloc((size_t)seq.frame_count, sizeof(int));
                if( !seq.frames || !seq.iframes || !seq.delay )
                {
                    free(seq.frames);
                    free(seq.iframes);
                    free(seq.delay);
                    RSCache_Dat2ConfigSequenceFree(src_seq);
                    continue;
                }
                for( int f = 0; f < seq.frame_count; f++ )
                {
                    int dst_fid = 0;
                    if( !tool_id_map_lookup(
                            &plan->frame_id_map, src_seq->frame_ids[f], &dst_fid) )
                    {
                        tool_port_plan_add_warning(
                            plan,
                            "seq %d frame[%d] composite %d missing from frame_id_map",
                            src_id,
                            f,
                            src_seq->frame_ids[f]);
                    }
                    seq.frames[f] = dst_fid;
                    seq.iframes[f] = -1;
                    seq.delay[f] =
                        src_seq->frame_lengths ? src_seq->frame_lengths[f] : 0;
                }
            }

            uint32_t bound = RSCache_Dat1ConfigSeqEncodeBound(&seq);
            uint8_t* enc = malloc(bound ? bound : 16);
            if( enc )
            {
                uint32_t n = RSCache_Dat1ConfigSeqEncode(&seq, enc, bound ? bound : 16);
                if( n )
                    RSCache_Dat1EditPutSeq(edit, dst_id, enc, n);
                else
                    tool_port_plan_add_warning(
                        plan, "dat1 seq encode failed for src %d", src_id);
                free(enc);
            }
            free(seq.frames);
            free(seq.iframes);
            free(seq.delay);
            free(seq.walkmerge);
            RSCache_Dat2ConfigSequenceFree(src_seq);
        }
    }

    /* NPC with remapped models / chatheads / animation slots. */
    {
        struct Tool_NeutralNpc remapped = plan->npc;
        int* models = NULL;
        int* heads = NULL;
        if( remapped.models_count > 0 )
        {
            models = malloc((size_t)remapped.models_count * sizeof(int));
            for( int i = 0; i < remapped.models_count; i++ )
            {
                int d = remapped.models[i];
                tool_id_map_lookup(&plan->model_map, remapped.models[i], &d);
                models[i] = d;
            }
            remapped.models = models;
        }
        if( remapped.chathead_models_count > 0 )
        {
            heads = malloc((size_t)remapped.chathead_models_count * sizeof(int));
            for( int i = 0; i < remapped.chathead_models_count; i++ )
            {
                int d = remapped.chathead_models[i];
                if( !tool_id_map_lookup(&plan->chathead_map, remapped.chathead_models[i], &d) )
                    tool_id_map_lookup(
                        &plan->model_map, remapped.chathead_models[i], &d);
                heads[i] = d;
            }
            remapped.chathead_models = heads;
        }
        for( int s = 0; s < TOOL_ANIM_SLOT_COUNT; s++ )
        {
            if( !remapped.anim_present[s] )
                continue;
            int d = remapped.anim[s];
            tool_id_map_lookup(&plan->seq_map, remapped.anim[s], &d);
            remapped.anim[s] = d;
        }
        struct RSCache_Dat1ConfigNpc* npc =
            tool_neutral_npc_to_dat1(&remapped, &plan->warnings, &plan->warning_count);
        if( npc )
        {
            uint32_t bound = RSCache_Dat1ConfigNpcEncodeBound(npc);
            uint8_t* enc = malloc(bound ? bound : 256);
            if( enc )
            {
                uint32_t n = RSCache_Dat1ConfigNpcEncode(npc, enc, bound ? bound : 256);
                if( n )
                    RSCache_Dat1EditPutNpc(edit, remapped.source_id, enc, n);
                free(enc);
            }
            RSCache_Dat1ConfigNpcFree(npc);
        }
        free(models);
        free(heads);
    }

    bool ok = RSCache_Dat1EditCommit(edit, dst_directory);
    RSCache_Dat1EditFree(edit);
    return ok ? 0 : 1;
}
