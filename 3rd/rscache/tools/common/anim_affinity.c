#include "anim_affinity.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
dat1_anim_archive_present(
    struct Tool_Dat1Cache* c,
    int archive_id)
{
    char path[1024];
    snprintf(
        path,
        sizeof(path),
        "%s/main_file_cache.idx%d",
        c->disk->directory,
        RSCACHE_DAT1_DISK_TABLE_ANIMATIONS);
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

void
tool_anim_seeds_init(struct Tool_AnimSeeds* s)
{
    memset(s, 0, sizeof(*s));
    s->bas_type_id = -1;
}

void
tool_anim_seeds_free(struct Tool_AnimSeeds* s)
{
    if( !s )
        return;
    free(s->seq_ids);
    memset(s, 0, sizeof(*s));
}

int
tool_anim_seeds_add(
    struct Tool_AnimSeeds* s,
    int seq_id)
{
    if( seq_id < 0 )
        return 1;
    for( int i = 0; i < s->count; i++ )
    {
        if( s->seq_ids[i] == seq_id )
            return 1;
    }
    if( s->count >= s->capacity )
    {
        int cap = s->capacity == 0 ? 16 : s->capacity * 2;
        int* n = realloc(s->seq_ids, (size_t)cap * sizeof(*n));
        if( !n )
            return 0;
        s->seq_ids = n;
        s->capacity = cap;
    }
    s->seq_ids[s->count++] = seq_id;
    return 1;
}

static void
add_if_present(
    struct Tool_AnimSeeds* s,
    int seq_id)
{
    /* Dat2 calloc defaults leave absent anims as 0; treat 0 as "maybe present"
     * only when the caller already decided to include it. For seeds we skip
     * non-positive unless they came from BasType (-1 means absent there). */
    if( seq_id > 0 )
        tool_anim_seeds_add(s, seq_id);
}

int
tool_dat2_npc_anim_seeds(
    struct Tool_Dat2Cache* c,
    const struct RSCache_Dat2ConfigNpc* npc,
    struct Tool_AnimSeeds* out)
{
    assert(c && npc && out);
    tool_anim_seeds_init(out);

    add_if_present(out, npc->standing_animation);
    add_if_present(out, npc->walking_animation);
    add_if_present(out, npc->idle_rotate_left_animation);
    add_if_present(out, npc->idle_rotate_right_animation);
    add_if_present(out, npc->rotate180_animation);
    add_if_present(out, npc->rotate_left_animation);
    add_if_present(out, npc->rotate_right_animation);
    add_if_present(out, npc->run_animation);
    add_if_present(out, npc->run_rotate180_animation);
    add_if_present(out, npc->run_rotate_left_animation);
    add_if_present(out, npc->run_rotate_right_animation);
    add_if_present(out, npc->crawl_animation);
    add_if_present(out, npc->crawl_rotate180_animation);
    add_if_present(out, npc->crawl_rotate_left_animation);
    add_if_present(out, npc->crawl_rotate_right_animation);

    out->bas_type_id = npc->bas_type_id;
    if( npc->bas_type_id >= 0 && RSCache_IsRs2Dat2(&c->profile) )
    {
        struct RSCache_Dat2ConfigBas* bas = tool_dat2_bas_load(c, npc->bas_type_id);
        if( bas )
        {
            if( bas->idle_seq_id > 0 )
                tool_anim_seeds_add(out, bas->idle_seq_id);
            if( bas->walk_seq_id > 0 )
                tool_anim_seeds_add(out, bas->walk_seq_id);
            if( bas->walk_back_seq_id > 0 )
                tool_anim_seeds_add(out, bas->walk_back_seq_id);
            if( bas->walk_left_seq_id > 0 )
                tool_anim_seeds_add(out, bas->walk_left_seq_id);
            if( bas->walk_right_seq_id > 0 )
                tool_anim_seeds_add(out, bas->walk_right_seq_id);
            RSCache_Dat2ConfigBasFree(bas);
        }
    }
    return 1;
}

int
tool_dat1_npc_anim_seeds(
    const struct RSCache_Dat1ConfigNpc* npc,
    struct Tool_AnimSeeds* out)
{
    assert(npc && out);
    tool_anim_seeds_init(out);
    if( npc->readyanim >= 0 )
        tool_anim_seeds_add(out, npc->readyanim);
    if( npc->walkanim >= 0 )
        tool_anim_seeds_add(out, npc->walkanim);
    if( npc->walkanim_b >= 0 )
        tool_anim_seeds_add(out, npc->walkanim_b);
    if( npc->walkanim_r >= 0 )
        tool_anim_seeds_add(out, npc->walkanim_r);
    if( npc->walkanim_l >= 0 )
        tool_anim_seeds_add(out, npc->walkanim_l);
    return 1;
}

void
tool_framemap_index_free(struct Tool_FramemapIndex* idx)
{
    if( !idx )
        return;
    free(idx->entries);
    free(idx->archive_fm);
    memset(idx, 0, sizeof(*idx));
}

static int
archive_fm_lookup(
    struct Tool_Dat2Cache* c,
    struct Tool_FramemapIndex* idx,
    int archive_id)
{
    if( archive_id < 0 )
        return -1;
    if( archive_id >= idx->archive_fm_cap )
    {
        int new_cap = archive_id + 1;
        if( new_cap < 64 )
            new_cap = 64;
        int* n = realloc(idx->archive_fm, (size_t)new_cap * sizeof(*n));
        if( !n )
            return -1;
        for( int i = idx->archive_fm_cap; i < new_cap; i++ )
            n[i] = -2; /* -2 = not yet resolved */
        idx->archive_fm = n;
        idx->archive_fm_cap = new_cap;
    }
    if( idx->archive_fm[archive_id] != -2 )
        return idx->archive_fm[archive_id];

    int anim_table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_ANIMATIONS);
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(c->disk, anim_table, archive_id);
    int fm = -1;
    if( archive && RSCache_Dat2DiskArchiveInitMetadata(c->disk, archive) &&
        archive->file_count > 0 )
    {
        struct RSCache_FileList* files = RSCache_FileListNewFromDecode(
            archive->data, archive->data_size, archive->file_count);
        if( files )
        {
            /* Prefer file_id 0 or the first non-empty file. */
            for( int i = 0; i < files->file_count; i++ )
            {
                if( files->file_sizes[i] <= 0 )
                    continue;
                fm = RSCache_Dat2FrameFramemapIdFromFileProfile(
                    &c->profile, files->files[i], files->file_sizes[i]);
                break;
            }
            RSCache_FileListFree(files);
        }
    }
    if( archive )
        RSCache_Dat2DiskArchiveFree(archive);
    idx->archive_fm[archive_id] = fm;
    return fm;
}

int
tool_dat2_build_framemap_index(
    struct Tool_Dat2Cache* c,
    struct Tool_FramemapIndex* out)
{
    assert(c && out);
    memset(out, 0, sizeof(*out));

    struct RSCache_Dat2ConfigSequence** seqs = NULL;
    int* ids = NULL;
    int count = 0;
    if( !tool_dat2_seq_load_all(c, &seqs, &ids, &count) )
        return 0;

    out->entries = calloc((size_t)count, sizeof(*out->entries));
    if( !out->entries && count > 0 )
    {
        for( int i = 0; i < count; i++ )
            RSCache_Dat2ConfigSequenceFree(seqs[i]);
        free(seqs);
        free(ids);
        return 0;
    }

    for( int i = 0; i < count; i++ )
    {
        struct RSCache_Dat2ConfigSequence* seq = seqs[i];
        int fm = -1;
        int skeletal = 0;
        if( seq->frame_ids && seq->frame_count > 0 )
        {
            int archive_id = (seq->frame_ids[0] >> 16) & 0xFFFF;
            /* Prefer archive-memoised framemap (all frames in an archive share
             * one). Fall back to per-sequence resolution for sparse archives. */
            fm = archive_fm_lookup(c, out, archive_id);
            if( fm < 0 )
                fm = tool_dat2_seq_framemap_id(c, seq, 0);
        }
        else
        {
            /* Skeletal (Animaya): no frame list at all, so the rig comes from
             * the curve set's base id. Without this every modern OldSchool npc
             * — the DT2 bosses and everything after — has an empty rig set. */
            fm = tool_dat2_seq_rig_id(c, seq, &skeletal);
        }
        /* A skeletal sequence has no frame list, so `frame_count` would read 0
         * for every one of them. Its playable length is the config's own maya
         * range, which is what a caller means by "how long is this". A range of
         * 0 means "the whole bake", and only the bake knows that — left at 0
         * rather than guessed. */
        int frames = seq->frame_count;
        if( skeletal && seq->anim_maya_end > seq->anim_maya_start )
            frames = seq->anim_maya_end - seq->anim_maya_start;

        out->entries[out->count].seq_id = ids[i];
        out->entries[out->count].framemap_id = fm;
        out->entries[out->count].frame_count = frames;
        out->entries[out->count].skeletal = skeletal;
        out->count++;
        RSCache_Dat2ConfigSequenceFree(seq);
    }
    free(seqs);
    free(ids);
    return 1;
}

int
tool_framemap_index_query(
    const struct Tool_FramemapIndex* idx,
    const int* seed_fms,
    int seed_fm_count,
    int** out_seq_ids,
    int* out_count)
{
    assert(idx && out_seq_ids && out_count);
    *out_seq_ids = NULL;
    *out_count = 0;
    if( seed_fm_count <= 0 )
        return 1;

    int cap = 0;
    int n = 0;
    int* ids = NULL;
    for( int i = 0; i < idx->count; i++ )
    {
        int fm = idx->entries[i].framemap_id;
        if( fm < 0 )
            continue;
        int match = 0;
        for( int j = 0; j < seed_fm_count; j++ )
        {
            if( seed_fms[j] == fm )
            {
                match = 1;
                break;
            }
        }
        if( !match )
            continue;
        if( n >= cap )
        {
            int nc = cap == 0 ? 64 : cap * 2;
            int* p = realloc(ids, (size_t)nc * sizeof(*p));
            if( !p )
            {
                free(ids);
                return 0;
            }
            ids = p;
            cap = nc;
        }
        ids[n++] = idx->entries[i].seq_id;
    }
    *out_seq_ids = ids;
    *out_count = n;
    return 1;
}

/* ---- rig identity ------------------------------------------------------- */

/* Framemap ids are allocation addresses, not rig identities. Independent
 * backport ledgers can mint several destination ids from one source framemap;
 * the Summoning cohorts and their special moves do exactly that. Compare the
 * decoded payload and canonicalise exact copies before building either side of
 * the npc-rig/sequence join. */
static uint64_t
rig_hash_i32(uint64_t h, int v)
{
    uint32_t u = (uint32_t)v;
    for( int i = 0; i < 4; i++ )
    {
        h ^= (uint8_t)(u >> (i * 8));
        h *= UINT64_C(1099511628211);
    }
    return h;
}

static uint64_t
framemap_hash(const struct RSCache_Dat2Framemap* fm)
{
    uint64_t h = UINT64_C(1469598103934665603);
    h = rig_hash_i32(h, fm->length);
    for( int i = 0; i < fm->length; i++ )
    {
        int group_count = fm->bone_groups_lengths ? fm->bone_groups_lengths[i] : 0;
        h = rig_hash_i32(h, fm->types ? fm->types[i] : 0);
        h = rig_hash_i32(h, group_count);
        for( int j = 0; j < group_count; j++ )
            h = rig_hash_i32(h, fm->bone_groups && fm->bone_groups[i] ?
                                    fm->bone_groups[i][j] : 0);
    }

    h = rig_hash_i32(h, fm->has_transform_actor ? 1 : 0);
    if( fm->has_transform_actor )
        for( int i = 0; i < fm->length; i++ )
            h = rig_hash_i32(h, fm->transform_actor ? fm->transform_actor[i] : 0);

    h = rig_hash_i32(h, fm->has_masks ? 1 : 0);
    if( fm->has_masks )
        for( int i = 0; i < fm->length; i++ )
            h = rig_hash_i32(h, fm->masks ? fm->masks[i] : 0);

    h = rig_hash_i32(h, fm->tail_size);
    for( int i = 0; i < fm->tail_size; i++ )
    {
        h ^= fm->tail ? fm->tail[i] : 0;
        h *= UINT64_C(1099511628211);
    }
    return h;
}

static int
framemap_equal(
    const struct RSCache_Dat2Framemap* a,
    const struct RSCache_Dat2Framemap* b)
{
    if( a->length != b->length ||
        a->has_transform_actor != b->has_transform_actor ||
        a->has_masks != b->has_masks || a->tail_size != b->tail_size )
        return 0;

    for( int i = 0; i < a->length; i++ )
    {
        int at = a->types ? a->types[i] : 0;
        int bt = b->types ? b->types[i] : 0;
        int an = a->bone_groups_lengths ? a->bone_groups_lengths[i] : 0;
        int bn = b->bone_groups_lengths ? b->bone_groups_lengths[i] : 0;
        if( at != bt || an != bn )
            return 0;
        for( int j = 0; j < an; j++ )
        {
            int ag = a->bone_groups && a->bone_groups[i] ? a->bone_groups[i][j] : 0;
            int bg = b->bone_groups && b->bone_groups[i] ? b->bone_groups[i][j] : 0;
            if( ag != bg )
                return 0;
        }
        if( a->has_transform_actor )
        {
            int av = a->transform_actor ? a->transform_actor[i] : 0;
            int bv = b->transform_actor ? b->transform_actor[i] : 0;
            if( av != bv )
                return 0;
        }
        if( a->has_masks )
        {
            int av = a->masks ? a->masks[i] : 0;
            int bv = b->masks ? b->masks[i] : 0;
            if( av != bv )
                return 0;
        }
    }

    if( a->tail_size > 0 )
    {
        if( !a->tail || !b->tail )
            return a->tail == b->tail;
        if( memcmp(a->tail, b->tail, (size_t)a->tail_size) != 0 )
            return 0;
    }
    return 1;
}

void
tool_dat2_canonicalise_framemap_index(
    struct Tool_Dat2Cache* cache,
    struct Tool_FramemapIndex* index,
    int max_framemap,
    int* out_distinct,
    int* out_aliases)
{
    assert(cache);
    assert(index);
    assert(out_distinct);
    assert(out_aliases);

    int n = max_framemap + 1;
    uint8_t* referenced = calloc((size_t)n, 1);
    int* canonical = malloc((size_t)n * sizeof(*canonical));
    uint64_t* hashes = calloc((size_t)n, sizeof(*hashes));
    struct RSCache_Dat2Framemap** maps = calloc((size_t)n, sizeof(*maps));
    assert(referenced);
    assert(canonical);
    assert(hashes);
    assert(maps);

    for( int id = 0; id < n; id++ )
        canonical[id] = -1;

    int referenced_count = 0;
    for( int i = 0; i < index->count; i++ )
    {
        int id = index->entries[i].framemap_id;
        if( id >= 0 && id < n && !referenced[id] )
        {
            referenced[id] = 1;
            referenced_count++;
        }
    }

    int table_cap = 16;
    while( table_cap < referenced_count * 2 )
        table_cap *= 2;
    int* table = malloc((size_t)table_cap * sizeof(*table));
    assert(table);
    for( int i = 0; i < table_cap; i++ )
        table[i] = -1;

    int distinct = 0;
    int aliases = 0;
    for( int id = 0; id < n; id++ )
    {
        if( !referenced[id] )
            continue;

        struct RSCache_Dat2Framemap* fm = tool_dat2_framemap_load(cache, id);
        if( !fm )
        {
            canonical[id] = id;
            distinct++;
            continue;
        }

        uint64_t hash = framemap_hash(fm);
        int slot = (int)(hash & (uint64_t)(table_cap - 1));
        int matched = 0;
        while( table[slot] >= 0 )
        {
            int other = table[slot];
            if( hashes[other] == hash && framemap_equal(maps[other], fm) )
            {
                canonical[id] = other;
                aliases++;
                matched = 1;
                break;
            }
            slot = (slot + 1) & (table_cap - 1);
        }

        if( matched )
            RSCache_Dat2FramemapFree(fm);
        else
        {
            canonical[id] = id;
            hashes[id] = hash;
            maps[id] = fm;
            table[slot] = id;
            distinct++;
        }
    }

    for( int i = 0; i < index->count; i++ )
    {
        int id = index->entries[i].framemap_id;
        if( id >= 0 && id < n && canonical[id] >= 0 )
            index->entries[i].framemap_id = canonical[id];
    }

    for( int id = 0; id < n; id++ )
        RSCache_Dat2FramemapFree(maps[id]);
    free(table);
    free(maps);
    free(hashes);
    free(canonical);
    free(referenced);
    *out_distinct = distinct;
    *out_aliases = aliases;
}

void
tool_framemap_label_set(
    const struct RSCache_Dat2Framemap* fm,
    uint8_t present[256])
{
    memset(present, 0, 256);
    if( !fm )
        return;
    for( int i = 0; i < fm->length; i++ )
    {
        for( int j = 0; j < fm->bone_groups_lengths[i]; j++ )
        {
            int label = fm->bone_groups[i][j];
            if( label >= 0 && label < 256 )
                present[label] = 1;
        }
    }
}

int
tool_framemap_covers_model(
    const struct RSCache_Dat2Framemap* fm,
    const struct RSCache_Model* model)
{
    if( !model || !model->vertex_bone_map || model->vertex_count <= 0 )
        return 1;
    uint8_t present[256];
    tool_framemap_label_set(fm, present);
    for( int i = 0; i < model->vertex_count; i++ )
    {
        int label = model->vertex_bone_map[i];
        if( label > 0 && label < 256 && !present[label] )
            return 0;
    }
    return 1;
}

uint64_t
tool_dat1_animbase_hash(const struct RSCache_Dat1AnimBase* base)
{
    /* FNV-1a over types + labels. */
    uint64_t h = 14695981039346656037ull;
    if( !base )
        return h;
    h ^= (uint64_t)base->length;
    h *= 1099511628211ull;
    for( int i = 0; i < base->length; i++ )
    {
        h ^= base->types[i];
        h *= 1099511628211ull;
        h ^= base->label_counts[i];
        h *= 1099511628211ull;
        for( int j = 0; j < base->label_counts[i]; j++ )
        {
            h ^= base->labels[i][j];
            h *= 1099511628211ull;
        }
    }
    return h;
}

void
tool_dat1_anim_affinity_free(struct Tool_Dat1AnimAffinity* a)
{
    if( !a )
        return;
    free(a->seq_base_hash);
    free(a->frame_archive);
    free(a->archive_hash);
    memset(a, 0, sizeof(*a));
}

static int
ensure_int_cap(int** arr, int* cap, int need, int fill)
{
    if( need <= *cap )
        return 1;
    int nc = need;
    if( nc < 64 )
        nc = 64;
    int* n = realloc(*arr, (size_t)nc * sizeof(*n));
    if( !n )
        return 0;
    for( int i = *cap; i < nc; i++ )
        n[i] = fill;
    *arr = n;
    *cap = nc;
    return 1;
}

static int
ensure_u64_cap(uint64_t** arr, int* cap, int need, uint64_t fill)
{
    if( need <= *cap )
        return 1;
    int nc = need;
    if( nc < 64 )
        nc = 64;
    uint64_t* n = realloc(*arr, (size_t)nc * sizeof(*n));
    if( !n )
        return 0;
    for( int i = *cap; i < nc; i++ )
        n[i] = fill;
    *arr = n;
    *cap = nc;
    return 1;
}

int
tool_dat1_build_anim_affinity(
    struct Tool_Dat1Cache* c,
    const struct RSCache_Dat1ConfigSeqList* seqs,
    struct Tool_Dat1AnimAffinity* out)
{
    assert(c && seqs && out);
    memset(out, 0, sizeof(*out));

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
        max_archives = 2048; /* fallback probe bound */

    for( int archive_id = 0; archive_id < max_archives; archive_id++ )
    {
        if( !dat1_anim_archive_present(c, archive_id) )
            continue;
        struct RSCache_Dat1DiskArchive* archive = RSCache_Dat1DiskArchiveNewLoad(
            c->disk, RSCACHE_DAT1_DISK_TABLE_ANIMATIONS, archive_id);
        if( !archive )
            continue;

        struct RSCache_Dat1AnimBaseFrames* abf =
            RSCache_Dat1AnimBaseFramesNewDecode(archive->data, archive->data_size);
        RSCache_Dat1DiskArchiveFree(archive);
        if( !abf || !abf->base )
        {
            if( abf )
                RSCache_Dat1AnimBaseFramesFree(abf);
            continue;
        }

        uint64_t hash = tool_dat1_animbase_hash(abf->base);
        if( !ensure_u64_cap(&out->archive_hash, &out->archive_hash_cap, archive_id + 1, 0) )
        {
            RSCache_Dat1AnimBaseFramesFree(abf);
            tool_dat1_anim_affinity_free(out);
            return 0;
        }
        out->archive_hash[archive_id] = hash;

        for( int i = 0; i < abf->frame_count; i++ )
        {
            int fid = abf->frames[i].id;
            if( fid < 0 )
                continue;
            if( !ensure_int_cap(&out->frame_archive, &out->frame_archive_cap, fid + 1, -1) )
            {
                RSCache_Dat1AnimBaseFramesFree(abf);
                tool_dat1_anim_affinity_free(out);
                return 0;
            }
            out->frame_archive[fid] = archive_id;
        }
        RSCache_Dat1AnimBaseFramesFree(abf);
    }

    out->seq_count = seqs->seqs_count;
    out->seq_base_hash = calloc((size_t)out->seq_count, sizeof(*out->seq_base_hash));
    if( !out->seq_base_hash && out->seq_count > 0 )
    {
        tool_dat1_anim_affinity_free(out);
        return 0;
    }

    for( int i = 0; i < seqs->seqs_count; i++ )
    {
        struct RSCache_Dat1ConfigSeq* seq = &seqs->seqs[i];
        uint64_t h = 0;
        if( seq->frames && seq->frame_count > 0 )
        {
            int fid = seq->frames[0];
            if( fid >= 0 && fid < out->frame_archive_cap )
            {
                int aid = out->frame_archive[fid];
                if( aid >= 0 && aid < out->archive_hash_cap )
                    h = out->archive_hash[aid];
            }
        }
        out->seq_base_hash[i] = h;
    }
    return 1;
}

int
tool_dat1_affinity_query(
    const struct Tool_Dat1AnimAffinity* a,
    const uint64_t* seed_hashes,
    int seed_hash_count,
    int** out_seq_ids,
    int* out_count)
{
    assert(a && out_seq_ids && out_count);
    *out_seq_ids = NULL;
    *out_count = 0;
    if( seed_hash_count <= 0 )
        return 1;

    int cap = 0;
    int n = 0;
    int* ids = NULL;
    for( int i = 0; i < a->seq_count; i++ )
    {
        uint64_t h = a->seq_base_hash[i];
        if( h == 0 )
            continue;
        int match = 0;
        for( int j = 0; j < seed_hash_count; j++ )
        {
            if( seed_hashes[j] == h )
            {
                match = 1;
                break;
            }
        }
        if( !match )
            continue;
        if( n >= cap )
        {
            int nc = cap == 0 ? 64 : cap * 2;
            int* p = realloc(ids, (size_t)nc * sizeof(*p));
            if( !p )
            {
                free(ids);
                return 0;
            }
            ids = p;
            cap = nc;
        }
        ids[n++] = i;
    }
    *out_seq_ids = ids;
    *out_count = n;
    return 1;
}
