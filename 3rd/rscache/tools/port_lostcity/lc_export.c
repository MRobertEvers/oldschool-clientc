/*
 * Exporters: dat2 records -> LostCity source files.
 *
 * Every exporter is idempotent through the id maps and through lc_pack_alloc:
 * asking twice for the same source asset returns the same LostCity id and emits
 * nothing new. That is what lets the closure be walked naively — an npc asks for
 * its sequences, a sequence asks for its frames, a spotanim asks for both — with
 * no ordering constraints beyond "flush the animsets last".
 */

#include "lc_export.h"

#include "transcode.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- id map ------------------------------------------------------------- */

int
lc_id_map_get(
    const struct LC_IdMap* map,
    int src,
    int* out_dst)
{
    assert(map);
    for( int i = 0; i < map->count; i++ )
    {
        if( map->src[i] != src )
            continue;
        if( out_dst )
            *out_dst = map->dst[i];
        return 1;
    }
    return 0;
}

int
lc_id_map_put(
    struct LC_IdMap* map,
    int src,
    int dst)
{
    assert(map);
    if( map->count >= map->cap )
    {
        int cap = map->cap == 0 ? 32 : map->cap * 2;
        int* s = realloc(map->src, (size_t)cap * sizeof(int));
        int* d = realloc(map->dst, (size_t)cap * sizeof(int));
        if( !s || !d )
        {
            free(s);
            free(d);
            return 0;
        }
        map->src = s;
        map->dst = d;
        map->cap = cap;
    }
    map->src[map->count] = src;
    map->dst[map->count] = dst;
    map->count++;
    return 1;
}

void
lc_id_map_free(struct LC_IdMap* map)
{
    if( !map )
        return;
    free(map->src);
    free(map->dst);
    memset(map, 0, sizeof(*map));
}

/* ---- config records ----------------------------------------------------- */

int
lc_config_record(
    struct LC_Ctx* ctx,
    enum RSCache_Type type,
    int config_kind,
    int id,
    struct Tool_Bytes* out,
    int* out_group_revision)
{
    assert(ctx && out);
    memset(out, 0, sizeof(*out));
    if( out_group_revision )
        *out_group_revision = 0;

    struct RSCache_RecordAddress addr = RSCache_RecordAddressFor(&ctx->src->profile, type);
    int table;
    int archive_id;
    if( addr.group_shift == 0 )
    {
        table = RSCache_Dat2DiskTableId(ctx->src->disk, RSCACHE_DAT2_TABLE_CONFIGS);
        archive_id = config_kind;
    }
    else
    {
        table = RSCache_Dat2DiskTableId(ctx->src->disk, addr.table);
        archive_id = id >> addr.group_shift;
    }

    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(ctx->src->disk, table, archive_id);
    if( !archive )
        return 0;
    if( !RSCache_Dat2DiskArchiveInitMetadata(ctx->src->disk, archive) ||
        archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }
    if( out_group_revision )
        *out_group_revision = archive->revision;

    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return 0;
    }

    int found = 0;
    for( int i = 0; i < files->file_count; i++ )
    {
        if( files->file_sizes[i] <= 0 )
            continue;
        int file_id = (archive->file_ids && i < archive->file_count) ? archive->file_ids[i] : i;
        int global = addr.group_shift == 0
                         ? file_id
                         : ((archive->archive_id << addr.group_shift) + file_id);
        if( global != id )
            continue;
        out->data = malloc((size_t)files->file_sizes[i]);
        if( out->data )
        {
            memcpy(out->data, files->files[i], (size_t)files->file_sizes[i]);
            out->size = files->file_sizes[i];
            found = 1;
        }
        break;
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return found;
}

/* ---- texture average colours -------------------------------------------- */

/*
 * OB2 can carry textures, but its texture ids index the destination cache's own
 * table, and rev 254 has ~50 textures against OSRS's ~90 materials — there is no
 * shared numbering to remap onto. So textured faces are downgraded to flat
 * colour, and the colour that keeps a model recognisable is the texture's own
 * average, which the dat2 texture record stores outright.
 */
static void
load_texture_hsl(struct LC_Ctx* ctx)
{
    int table = RSCache_Dat2DiskTableId(ctx->src->disk, RSCACHE_DAT2_TABLE_TEXTURES);
    if( table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return;
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(ctx->src->disk, table, 0);
    if( !archive )
        return;
    if( !RSCache_Dat2DiskArchiveInitMetadata(ctx->src->disk, archive) ||
        archive->file_count <= 0 )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return;
    }

    struct RSCache local = ctx->src->profile;
    RSCache_ProfileSetGroupRevision(&local, RSCACHE_TYPE_TEXTURE, archive->revision);

    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( !files )
    {
        RSCache_Dat2DiskArchiveFree(archive);
        return;
    }

    int max_id = 0;
    for( int i = 0; i < archive->file_count; i++ )
    {
        int file_id = archive->file_ids ? archive->file_ids[i] : i;
        if( file_id > max_id )
            max_id = file_id;
    }
    ctx->texture_hsl = calloc((size_t)max_id + 1, sizeof(int));
    if( !ctx->texture_hsl )
    {
        RSCache_FileListFree(files);
        RSCache_Dat2DiskArchiveFree(archive);
        return;
    }
    ctx->texture_hsl_count = max_id + 1;
    for( int i = 0; i < ctx->texture_hsl_count; i++ )
        ctx->texture_hsl[i] = -1;

    for( int i = 0; i < files->file_count; i++ )
    {
        if( files->file_sizes[i] <= 0 )
            continue;
        int file_id = (archive->file_ids && i < archive->file_count) ? archive->file_ids[i] : i;
        struct RSCache_Dat2Texture* tex = RSCache_Dat2TextureNewDecodeProfile(
            &local, files->files[i], files->file_sizes[i]);
        if( !tex )
            continue;
        if( file_id >= 0 && file_id < ctx->texture_hsl_count )
            ctx->texture_hsl[file_id] = tex->average_hsl;
        RSCache_Dat2TextureFree(tex);
    }

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
}

/* ---- context ------------------------------------------------------------ */

int
lc_ctx_init(
    struct LC_Ctx* ctx,
    struct Tool_Dat2Cache* src,
    struct LC_Packs* packs,
    struct LC_Out* out)
{
    assert(ctx && src && packs && out);
    memset(ctx, 0, sizeof(*ctx));
    ctx->src = src;
    ctx->packs = packs;
    ctx->out = out;
    ctx->max_npc_size = 5;
    /* A mid grey: visible, and neutral enough that it reads as "untextured"
     * rather than as a wrong colour. Only reached when a face names a texture
     * the source cache does not hold. */
    ctx->texture_fallback_hsl = 4675;
    load_texture_hsl(ctx);
    return 1;
}

void
lc_ctx_free(struct LC_Ctx* ctx)
{
    if( !ctx )
        return;
    lc_id_map_free(&ctx->npc_map);
    lc_id_map_free(&ctx->seq_map);
    lc_id_map_free(&ctx->spotanim_map);
    lc_id_map_free(&ctx->loc_map);
    lc_id_map_free(&ctx->model_map);
    lc_id_map_free(&ctx->frame_map);
    lc_id_map_free(&ctx->underlay_map);
    lc_id_map_free(&ctx->overlay_map);
    for( int i = 0; i < ctx->animset_count; i++ )
    {
        free(ctx->animsets[i].src_frames);
        free(ctx->animsets[i].lc_frames);
        free(ctx->animsets[i].delays);
    }
    free(ctx->animsets);
    free(ctx->texture_hsl);
    memset(ctx, 0, sizeof(*ctx));
}

/* ---- models ------------------------------------------------------------- */

static const char* const LOC_SHAPE_SUFFIX[23] = {
    "_1", "_2", "_3", "_4", "_q", "_w", "_r", "_e", "_t", "_5", "_8", "_9",
    "_a", "_s", "_d", "_f", "_g", "_h", "_z", "_x", "_c", "_v", "_0",
};

const char*
lc_loc_shape_suffix(int shape)
{
    if( shape < 0 || shape >= 23 )
        return "_8";
    return LOC_SHAPE_SUFFIX[shape];
}

/**
 * Drop every texture reference, replacing it with the texture's average colour.
 *
 * Done on the decoded model rather than after encoding because OB2 expresses a
 * textured face as `info & 2` plus the texture id stored *in the colour slot*,
 * so the colour and the flag have to move together. Clearing `face_textures`
 * outright is what makes the encoder take the untextured branch for every face.
 */
static void
strip_textures(
    struct LC_Ctx* ctx,
    struct RSCache_Model* model,
    int* out_dropped)
{
    int dropped = 0;
    if( model->face_textures )
    {
        for( int i = 0; i < model->face_count; i++ )
        {
            if( model->face_textures[i] == -1 )
                continue;
            int tex = model->face_textures[i];
            int hsl = ctx->texture_fallback_hsl;
            if( tex >= 0 && tex < ctx->texture_hsl_count && ctx->texture_hsl[tex] >= 0 )
                hsl = ctx->texture_hsl[tex];
            if( model->face_colors )
                model->face_colors[i] = (uint16_t)hsl;
            if( model->face_infos )
                model->face_infos[i] = (uint8_t)(model->face_infos[i] & 1);
            dropped++;
        }
        free(model->face_textures);
        model->face_textures = NULL;
    }
    free(model->face_texture_coords);
    model->face_texture_coords = NULL;
    free(model->texture_render_types);
    model->texture_render_types = NULL;
    model->textured_face_count = 0;

    if( out_dropped )
        *out_dropped = dropped;
}

int
lc_export_model(
    struct LC_Ctx* ctx,
    int src_model_id,
    int shape,
    char* out_base,
    int out_base_size)
{
    assert(ctx);
    char base[128];
    if( shape < 0 )
        snprintf(base, sizeof(base), "%s_model_%d", ctx->out->prefix, src_model_id);
    else
        snprintf(base, sizeof(base), "%s_locmodel_%d", ctx->out->prefix, src_model_id);
    if( out_base && out_base_size > 0 )
        snprintf(out_base, (size_t)out_base_size, "%s", base);

    int key = (src_model_id << 8) | (shape + 1);
    int existing;
    if( lc_id_map_get(&ctx->model_map, key, &existing) )
        return existing;

    char name[160];
    if( shape < 0 )
        snprintf(name, sizeof(name), "%s", base);
    else
        snprintf(name, sizeof(name), "%s%s", base, lc_loc_shape_suffix(shape));

    struct Tool_Bytes bytes;
    int table = RSCache_Dat2DiskTableId(ctx->src->disk, RSCACHE_DAT2_TABLE_MODELS);
    if( !tool_dat2_archive_bytes(ctx->src, table, src_model_id, &bytes) )
    {
        lc_out_warn(ctx->out, "model %d: not in source cache", src_model_id);
        return -1;
    }

    struct RSCache_Model* model = RSCache_ModelNewDecode(bytes.data, bytes.size);
    tool_bytes_free(&bytes);
    if( !model )
    {
        lc_out_warn(ctx->out, "model %d: decode failed", src_model_id);
        return -1;
    }

    int dropped = 0;
    strip_textures(ctx, model, &dropped);
    if( dropped > 0 )
        lc_out_warn(
            ctx->out,
            "model %d: %d textured faces flattened to their texture's average colour",
            src_model_id,
            dropped);

    /*
     * Encoded without provenance on purpose. Provenance mirrors the source's own
     * counts, and stripping textures changes `textured_face_count`, so handing it
     * back would be rejected as describing a different model. Byte-exactness is
     * not on offer here anyway — the target format is a different one.
     */
    uint32_t size = 0;
    uint8_t* ob2 =
        tool_transcode_model_to_ob2(model, NULL, &size, &ctx->out->warnings, &ctx->out->warning_count);
    RSCache_ModelFree(model);
    if( !ob2 || size == 0 )
    {
        free(ob2);
        lc_out_warn(ctx->out, "model %d: OB2 encode failed", src_model_id);
        return -1;
    }

    int lc_id = lc_pack_alloc(&ctx->packs->packs[LC_PACK_MODEL], name);
    if( lc_id < 0 )
    {
        free(ob2);
        return -1;
    }

    char rel[512];
    snprintf(rel, sizeof(rel), "models/%s/%s.ob2", ctx->out->prefix, name);
    int ok = lc_out_write_file(ctx->out, rel, ob2, size);
    free(ob2);
    if( !ok )
        return -1;

    lc_id_map_put(&ctx->model_map, key, lc_id);
    return lc_id;
}

/* ---- animsets ----------------------------------------------------------- */

static struct LC_AnimSet*
animset_for(
    struct LC_Ctx* ctx,
    int framemap_id)
{
    struct LC_AnimSet* last = NULL;
    for( int i = 0; i < ctx->animset_count; i++ )
    {
        if( ctx->animsets[i].src_framemap_id != framemap_id )
            continue;
        if( !last || ctx->animsets[i].part > last->part )
            last = &ctx->animsets[i];
    }
    if( last && last->frame_count < LC_ANIMSET_MAX_FRAMES )
        return last;

    if( ctx->animset_count >= ctx->animset_cap )
    {
        int cap = ctx->animset_cap == 0 ? 16 : ctx->animset_cap * 2;
        struct LC_AnimSet* n = realloc(ctx->animsets, (size_t)cap * sizeof(*n));
        if( !n )
            return NULL;
        memset(n + ctx->animset_cap, 0, (size_t)(cap - ctx->animset_cap) * sizeof(*n));
        ctx->animsets = n;
        ctx->animset_cap = cap;
        /* `last` pointed into the old block. */
        last = NULL;
        for( int i = 0; i < ctx->animset_count; i++ )
        {
            if( ctx->animsets[i].src_framemap_id != framemap_id )
                continue;
            if( !last || ctx->animsets[i].part > last->part )
                last = &ctx->animsets[i];
        }
    }

    struct LC_AnimSet* set = &ctx->animsets[ctx->animset_count];
    memset(set, 0, sizeof(*set));
    set->src_framemap_id = framemap_id;
    set->part = last ? last->part + 1 : 0;
    if( set->part == 0 )
        snprintf(set->name, sizeof(set->name), "%s_animset_%d", ctx->out->prefix, framemap_id);
    else
        snprintf(
            set->name,
            sizeof(set->name),
            "%s_animset_%d_p%d",
            ctx->out->prefix,
            framemap_id,
            set->part);

    set->lc_animset_id = lc_pack_alloc(&ctx->packs->packs[LC_PACK_ANIMSET], set->name);
    set->lc_base_id = lc_pack_alloc(&ctx->packs->packs[LC_PACK_BASE], set->name);
    if( set->lc_animset_id < 0 || set->lc_base_id < 0 )
        return NULL;
    ctx->animset_count++;
    return set;
}

static int
animset_add_frame(
    struct LC_AnimSet* set,
    int composite,
    int lc_frame_id,
    int delay)
{
    for( int i = 0; i < set->frame_count; i++ )
    {
        if( set->src_frames[i] == composite )
            return 1;
    }
    if( set->frame_count >= set->frame_cap )
    {
        int cap = set->frame_cap == 0 ? LC_ANIMSET_MAX_FRAMES : set->frame_cap * 2;
        int* s = realloc(set->src_frames, (size_t)cap * sizeof(int));
        int* l = realloc(set->lc_frames, (size_t)cap * sizeof(int));
        int* d = realloc(set->delays, (size_t)cap * sizeof(int));
        if( !s || !l || !d )
        {
            free(s);
            free(l);
            free(d);
            return 0;
        }
        set->src_frames = s;
        set->lc_frames = l;
        set->delays = d;
        set->frame_cap = cap;
    }
    set->src_frames[set->frame_count] = composite;
    set->lc_frames[set->frame_count] = lc_frame_id;
    set->delays[set->frame_count] = delay;
    set->frame_count++;
    return 1;
}

/** Allocate (or find) the LostCity global frame id for a source frame. */
static int
frame_id_for(
    struct LC_Ctx* ctx,
    int composite)
{
    int existing;
    if( lc_id_map_get(&ctx->frame_map, composite, &existing) )
        return existing;
    char name[96];
    snprintf(
        name,
        sizeof(name),
        "%s_frame_%d_%d",
        ctx->out->prefix,
        (composite >> 16) & 0xFFFF,
        composite & 0xFFFF);
    int id = lc_pack_alloc(&ctx->packs->packs[LC_PACK_ANIM], name);
    if( id < 0 )
        return -1;
    lc_id_map_put(&ctx->frame_map, composite, id);
    return id;
}

static const char*
frame_name(
    struct LC_Ctx* ctx,
    int lc_frame_id)
{
    const struct LC_Pack* pack = &ctx->packs->packs[LC_PACK_ANIM];
    if( lc_frame_id < 0 || lc_frame_id >= pack->capacity )
        return NULL;
    return pack->names[lc_frame_id];
}

int
lc_export_flush_animsets(struct LC_Ctx* ctx)
{
    assert(ctx);
    int anim_table = RSCache_Dat2DiskTableId(ctx->src->disk, RSCACHE_DAT2_TABLE_ANIMATIONS);
    int ok = 1;

    for( int si = 0; si < ctx->animset_count; si++ )
    {
        struct LC_AnimSet* set = &ctx->animsets[si];
        if( set->frame_count == 0 )
            continue;

        struct RSCache_Dat2Framemap* fm =
            tool_dat2_framemap_load(ctx->src, set->src_framemap_id);
        if( !fm )
        {
            lc_out_warn(ctx->out, "framemap %d: load failed", set->src_framemap_id);
            ok = 0;
            continue;
        }
        struct RSCache_Dat1AnimBase* base = tool_transcode_framemap_to_animbase(
            fm, &ctx->out->warnings, &ctx->out->warning_count);
        if( !base )
        {
            RSCache_Dat2FramemapFree(fm);
            lc_out_warn(ctx->out, "framemap %d: transcode failed", set->src_framemap_id);
            ok = 0;
            continue;
        }

        struct RSCache_Dat1AnimBaseFrames* abf = calloc(1, sizeof(*abf));
        if( !abf )
        {
            RSCache_Dat2FramemapFree(fm);
            return 0;
        }
        abf->base = base;
        abf->frames = calloc((size_t)set->frame_count, sizeof(*abf->frames));
        if( !abf->frames )
        {
            RSCache_Dat1AnimBaseFramesFree(abf);
            RSCache_Dat2FramemapFree(fm);
            return 0;
        }

        for( int f = 0; f < set->frame_count; f++ )
        {
            int composite = set->src_frames[f];
            int arch = (composite >> 16) & 0xFFFF;
            int file_id = composite & 0xFFFF;

            struct RSCache_Dat2DiskArchive* archive =
                RSCache_Dat2DiskArchiveNewLoad(ctx->src->disk, anim_table, arch);
            if( !archive || !RSCache_Dat2DiskArchiveInitMetadata(ctx->src->disk, archive) )
            {
                if( archive )
                    RSCache_Dat2DiskArchiveFree(archive);
                lc_out_warn(ctx->out, "frame archive %d: load failed", arch);
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
                lc_out_warn(ctx->out, "frame %d/%d: missing in archive", arch, file_id);
                RSCache_FileListFree(files);
                RSCache_Dat2DiskArchiveFree(archive);
                continue;
            }
            struct RSCache_Dat2Frame* frame = RSCache_Dat2FrameNewDecodeProfile(
                &ctx->src->profile, file_id, fm, files->files[pos], files->file_sizes[pos]);
            RSCache_FileListFree(files);
            RSCache_Dat2DiskArchiveFree(archive);
            if( !frame )
            {
                lc_out_warn(ctx->out, "frame %d/%d: decode failed", arch, file_id);
                continue;
            }
            if( tool_transcode_dat2_frame_to_dat1(
                    frame,
                    base,
                    set->lc_frames[f],
                    set->delays[f],
                    &abf->frames[abf->frame_count],
                    &ctx->out->warnings,
                    &ctx->out->warning_count) )
                abf->frame_count++;
            else
                lc_out_warn(ctx->out, "frame %d/%d: transcode failed", arch, file_id);
            RSCache_Dat2FrameFree(frame);
        }

        uint32_t bound = RSCache_Dat1AnimBaseFramesEncodeBound(abf);
        if( bound == 0 )
            bound = 64;
        uint8_t* enc = malloc(bound);
        if( enc )
        {
            uint32_t n = RSCache_Dat1AnimBaseFramesEncode(abf, enc, bound);
            if( n )
            {
                char rel[512];
                snprintf(rel, sizeof(rel), "models/%s/%s.anim", ctx->out->prefix, set->name);
                if( !lc_out_write_file(ctx->out, rel, enc, n) )
                    ok = 0;
            }
            else
            {
                lc_out_warn(
                    ctx->out,
                    "animset %s: encode failed (%d frames); a section length "
                    "overflowed u16, lower LC_ANIMSET_MAX_FRAMES",
                    set->name,
                    abf->frame_count);
                ok = 0;
            }
            free(enc);
        }

        RSCache_Dat1AnimBaseFramesFree(abf);
        RSCache_Dat2FramemapFree(fm);
    }
    return ok;
}

/* ---- sequences ---------------------------------------------------------- */

int
lc_export_seq(
    struct LC_Ctx* ctx,
    int src_seq_id,
    const char* name)
{
    assert(ctx);
    if( src_seq_id < 0 )
        return -1;
    int existing;
    if( lc_id_map_get(&ctx->seq_map, src_seq_id, &existing) )
        return existing;

    char auto_name[96];
    if( !name )
    {
        snprintf(auto_name, sizeof(auto_name), "%s_seq_%d", ctx->out->prefix, src_seq_id);
        name = auto_name;
    }

    struct RSCache_Dat2ConfigSequence* seq = tool_dat2_seq_load(ctx->src, src_seq_id);
    if( !seq )
    {
        lc_out_warn(ctx->out, "seq %d: not in source cache", src_seq_id);
        return -1;
    }

    int lc_id = lc_pack_alloc(&ctx->packs->packs[LC_PACK_SEQ], name);
    if( lc_id < 0 )
    {
        RSCache_Dat2ConfigSequenceFree(seq);
        return -1;
    }
    /* Registered before the frames are walked so a sequence that somehow reaches
     * itself terminates. */
    lc_id_map_put(&ctx->seq_map, src_seq_id, lc_id);

    int framemap = tool_dat2_seq_framemap_id(ctx->src, seq, 0);
    if( framemap < 0 && seq->frame_count > 0 )
        lc_out_warn(ctx->out, "seq %d: no framemap; frames dropped", src_seq_id);

    struct LC_Str* cfg = &ctx->out->seq_cfg;
    lc_str_addf(cfg, "[%s]\n", name);

    int emitted = 0;
    if( framemap >= 0 )
    {
        for( int i = 0; i < seq->frame_count; i++ )
        {
            int composite = seq->frame_ids[i];
            int delay = seq->frame_lengths ? seq->frame_lengths[i] : 0;
            int fid = frame_id_for(ctx, composite);
            if( fid < 0 )
                continue;
            struct LC_AnimSet* set = animset_for(ctx, framemap);
            if( !set )
                continue;
            if( !animset_add_frame(set, composite, fid, delay) )
                continue;
            const char* fname = frame_name(ctx, fid);
            if( !fname )
                continue;
            emitted++;
            lc_str_addf(cfg, "frame%d=%s\n", emitted, fname);
            if( delay > 0 )
                lc_str_addf(cfg, "delay%d=%d\n", emitted, delay);
            if( emitted >= 255 )
            {
                lc_out_warn(
                    ctx->out, "seq %d: truncated at 255 frames (LostCity limit)", src_seq_id);
                break;
            }
        }
    }

    /*
     * The dat1 sequence opcodes line up one-for-one with the dat2 ones for every
     * field LostCity's text format exposes, so these are direct copies rather
     * than a mapping. The decoder establishes no reference defaults, though, so
     * an unset field reads as 0 and "0" has to be treated as absent.
     */
    if( seq->frame_step > 0 )
        lc_str_addf(cfg, "loops=%d\n", seq->frame_step);
    if( seq->stretches )
        lc_str_addf(cfg, "stretches=yes\n");
    if( seq->forced_priority > 0 && seq->forced_priority <= 10 )
        lc_str_addf(cfg, "priority=%d\n", seq->forced_priority);
    if( seq->max_loops > 0 && seq->max_loops <= 255 )
        lc_str_addf(cfg, "maxloops=%d\n", seq->max_loops);
    lc_str_addf(cfg, "\n");

    RSCache_Dat2ConfigSequenceFree(seq);
    return lc_id;
}

/* ---- npcs --------------------------------------------------------------- */

int
lc_export_npc(
    struct LC_Ctx* ctx,
    int src_npc_id,
    const char* name)
{
    assert(ctx && name);
    int existing;
    if( lc_id_map_get(&ctx->npc_map, src_npc_id, &existing) )
        return existing;

    int exact = 0;
    struct RSCache_Dat2ConfigNpc* npc =
        tool_dat2_npc_load_checked(ctx->src, src_npc_id, &exact);
    if( !npc )
    {
        lc_out_warn(ctx->out, "npc %d: not in source cache", src_npc_id);
        return -1;
    }
    if( !exact )
    {
        /*
         * A short consume is the decoder saying it stopped on an opcode it does
         * not know, which leaves every field after that point either unset or
         * read out of the wrong bytes. Exporting it would bake that corruption
         * into the config, so refuse the record rather than the revision.
         */
        lc_out_warn(
            ctx->out,
            "npc %d: record does not decode exactly (unknown opcode); skipped",
            src_npc_id);
        RSCache_Dat2ConfigNpcFree(npc);
        return -1;
    }

    int lc_id = lc_pack_alloc(&ctx->packs->packs[LC_PACK_NPC], name);
    if( lc_id < 0 )
    {
        RSCache_Dat2ConfigNpcFree(npc);
        return -1;
    }
    lc_id_map_put(&ctx->npc_map, src_npc_id, lc_id);

    struct LC_Str* cfg = &ctx->out->npc_cfg;
    lc_str_addf(cfg, "[%s]\n", name);
    if( npc->name && npc->name[0] )
        lc_str_addf(cfg, "name=%s\n", npc->name);

    for( int i = 0; i < npc->models_count; i++ )
    {
        char base[160];
        if( lc_export_model(ctx, npc->models[i], -1, base, sizeof(base)) < 0 )
            continue;
        lc_str_addf(cfg, "model%d=%s\n", i + 1, base);
    }
    for( int i = 0; i < npc->chathead_models_count; i++ )
    {
        char base[160];
        if( lc_export_model(ctx, npc->chathead_models[i], -1, base, sizeof(base)) < 0 )
            continue;
        lc_str_addf(cfg, "head%d=%s\n", i + 1, base);
    }

    for( int i = 0; i < npc->recolor_count && i < 6; i++ )
    {
        lc_str_addf(cfg, "recol%ds=%d\n", i + 1, (int)(uint16_t)npc->recolor_to_find[i]);
        lc_str_addf(cfg, "recol%dd=%d\n", i + 1, (int)(uint16_t)npc->recolor_to_replace[i]);
    }

    int size = npc->size;
    if( size < 1 )
        size = 1;
    if( size > ctx->max_npc_size )
    {
        lc_out_warn(
            ctx->out,
            "npc %d (%s): size %d clamped to %d; LostCity npc size is capped",
            src_npc_id,
            name,
            npc->size,
            ctx->max_npc_size);
        size = ctx->max_npc_size;
    }
    lc_str_addf(cfg, "size=%d\n", size);

    if( npc->standing_animation > 0 )
    {
        int seq = lc_export_seq(ctx, npc->standing_animation, NULL);
        if( seq >= 0 )
            lc_str_addf(
                cfg,
                "readyanim=%s\n",
                ctx->packs->packs[LC_PACK_SEQ].names[seq]);
    }
    if( npc->walking_animation > 0 )
    {
        int seq = lc_export_seq(ctx, npc->walking_animation, NULL);
        if( seq >= 0 )
            lc_str_addf(
                cfg,
                "walkanim=%s\n",
                ctx->packs->packs[LC_PACK_SEQ].names[seq]);
    }

    if( npc->combat_level > 0 )
        lc_str_addf(cfg, "vislevel=%d\n", npc->combat_level);
    else
        lc_str_addf(cfg, "vislevel=hide\n");

    if( npc->width_scale > 0 && npc->width_scale != 128 && npc->width_scale <= 512 )
        lc_str_addf(cfg, "resizeh=%d\n", npc->width_scale);
    if( npc->height_scale > 0 && npc->height_scale != 128 && npc->height_scale <= 512 )
        lc_str_addf(cfg, "resizev=%d\n", npc->height_scale);
    if( npc->ambient != 0 )
        lc_str_addf(cfg, "ambient=%d\n", npc->ambient);
    if( npc->contrast != 0 )
        lc_str_addf(cfg, "contrast=%d\n", npc->contrast);

    for( int i = 0; i < 5; i++ )
    {
        if( npc->actions[i] && npc->actions[i][0] )
            lc_str_addf(cfg, "op%d=%s\n", i + 1, npc->actions[i]);
    }
    lc_str_addf(cfg, "\n");

    RSCache_Dat2ConfigNpcFree(npc);
    return lc_id;
}

/* ---- spotanims ---------------------------------------------------------- */

int
lc_hsl16_to_rgb15(int hsl)
{
    double hue = (double)((hsl >> 10) & 0x3F) / 64.0 + 0.0078125;
    double sat = (double)((hsl >> 7) & 0x7) / 8.0 + 0.0625;
    double lum = (double)(hsl & 0x7F) / 128.0;

    double r = lum;
    double g = lum;
    double b = lum;
    if( sat != 0.0 )
    {
        double q = lum < 0.5 ? lum * (1.0 + sat) : lum + sat - lum * sat;
        double p = 2.0 * lum - q;
        double t[3] = { hue + 1.0 / 3.0, hue, hue - 1.0 / 3.0 };
        double o[3];
        for( int i = 0; i < 3; i++ )
        {
            double v = t[i];
            if( v < 0.0 )
                v += 1.0;
            if( v > 1.0 )
                v -= 1.0;
            if( 6.0 * v < 1.0 )
                o[i] = p + (q - p) * 6.0 * v;
            else if( 2.0 * v < 1.0 )
                o[i] = q;
            else if( 3.0 * v < 2.0 )
                o[i] = p + (q - p) * (2.0 / 3.0 - v) * 6.0;
            else
                o[i] = p;
        }
        r = o[0];
        g = o[1];
        b = o[2];
    }

    int ri = (int)(r * 31.0 + 0.5);
    int gi = (int)(g * 31.0 + 0.5);
    int bi = (int)(b * 31.0 + 0.5);
    if( ri > 31 )
        ri = 31;
    if( gi > 31 )
        gi = 31;
    if( bi > 31 )
        bi = 31;
    if( ri < 0 )
        ri = 0;
    if( gi < 0 )
        gi = 0;
    if( bi < 0 )
        bi = 0;
    return (ri << 10) | (gi << 5) | bi;
}

int
lc_export_spotanim(
    struct LC_Ctx* ctx,
    int src_spotanim_id,
    const char* name)
{
    assert(ctx);
    int existing;
    if( lc_id_map_get(&ctx->spotanim_map, src_spotanim_id, &existing) )
        return existing;

    char auto_name[96];
    if( !name )
    {
        snprintf(
            auto_name, sizeof(auto_name), "%s_spotanim_%d", ctx->out->prefix, src_spotanim_id);
        name = auto_name;
    }

    struct Tool_Bytes bytes;
    int revision = 0;
    if( !lc_config_record(
            ctx,
            RSCACHE_TYPE_SPOTANIM,
            RSCACHE_DAT2_CONFIG_KIND_SPOTANIM,
            src_spotanim_id,
            &bytes,
            &revision) )
    {
        lc_out_warn(ctx->out, "spotanim %d: not in source cache", src_spotanim_id);
        return -1;
    }

    struct RSCache_Dat2ConfigSpotanim* spot = RSCache_Dat2ConfigSpotanimNewDecode(
        ctx->src->profile.revision, (char*)bytes.data, bytes.size);
    if( spot && spot->_consumed != bytes.size )
    {
        lc_out_warn(
            ctx->out,
            "spotanim %d: record does not decode exactly (%d of %d bytes); skipped",
            src_spotanim_id,
            spot->_consumed,
            bytes.size);
        RSCache_Dat2ConfigSpotanimFree(spot);
        spot = NULL;
    }
    tool_bytes_free(&bytes);
    if( !spot )
        return -1;

    int lc_id = lc_pack_alloc(&ctx->packs->packs[LC_PACK_SPOTANIM], name);
    if( lc_id < 0 )
    {
        RSCache_Dat2ConfigSpotanimFree(spot);
        return -1;
    }
    lc_id_map_put(&ctx->spotanim_map, src_spotanim_id, lc_id);

    struct LC_Str* cfg = &ctx->out->spotanim_cfg;
    lc_str_addf(cfg, "[%s]\n", name);

    char base[160];
    if( lc_export_model(ctx, spot->model, -1, base, sizeof(base)) >= 0 )
        lc_str_addf(cfg, "model=%s\n", base);

    if( spot->anim > 0 )
    {
        int seq = lc_export_seq(ctx, spot->anim, NULL);
        if( seq >= 0 )
            lc_str_addf(cfg, "anim=%s\n", ctx->packs->packs[LC_PACK_SEQ].names[seq]);
    }

    if( spot->resizeh > 0 && spot->resizeh != 128 && spot->resizeh <= 512 )
        lc_str_addf(cfg, "resizeh=%d\n", spot->resizeh);
    if( spot->resizev > 0 && spot->resizev != 128 && spot->resizev <= 512 )
        lc_str_addf(cfg, "resizev=%d\n", spot->resizev);
    if( spot->angle > 0 && spot->angle <= 360 )
        lc_str_addf(cfg, "angle=%d\n", spot->angle);
    if( spot->ambient != 0 )
        lc_str_addf(cfg, "ambient=%d\n", spot->ambient);
    if( spot->contrast != 0 )
        lc_str_addf(cfg, "contrast=%d\n", spot->contrast);

    for( int i = 0; i < spot->recol_count && i < RSCACHE_SPOTANIM_COLOUR_SLOTS; i++ )
    {
        lc_str_addf(cfg, "recol%ds=%d\n", i + 1, lc_hsl16_to_rgb15(spot->recol_s[i]));
        lc_str_addf(cfg, "recol%dd=%d\n", i + 1, lc_hsl16_to_rgb15(spot->recol_d[i]));
    }
    if( spot->retex_count > 0 )
        lc_out_warn(
            ctx->out,
            "spotanim %d: %d retextures dropped (models are exported untextured)",
            src_spotanim_id,
            spot->retex_count);
    lc_str_addf(cfg, "\n");

    RSCache_Dat2ConfigSpotanimFree(spot);
    return lc_id;
}
