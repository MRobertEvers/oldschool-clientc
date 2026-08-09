#include "ev_build.h"

#include "toridraw.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- RSCache_Model -> ToriDraw_Model ------------------------------------ */

static void*
dup_as(const void* src, int count, size_t src_elem, size_t dst_elem, int is_signed)
{
    if( !src || count <= 0 )
        return NULL;
    uint8_t* out = calloc((size_t)count, dst_elem);
    if( !out )
        return NULL;
    for( int i = 0; i < count; i++ )
    {
        long v = 0;
        const uint8_t* s = (const uint8_t*)src + (size_t)i * src_elem;
        switch( src_elem )
        {
        case 1:
            v = is_signed ? *(const int8_t*)s : *(const uint8_t*)s;
            break;
        case 2:
            v = is_signed ? *(const int16_t*)s : *(const uint16_t*)s;
            break;
        default:
            v = *(const int32_t*)s;
            break;
        }
        uint8_t* d = out + (size_t)i * dst_elem;
        switch( dst_elem )
        {
        case 1:
            *(uint8_t*)d = (uint8_t)v;
            break;
        case 2:
            *(uint16_t*)d = (uint16_t)v;
            break;
        default:
            *(int32_t*)d = (int32_t)v;
            break;
        }
    }
    return out;
}

/**
 * Invert a per-element bone label map into the label -> elements lists the
 * animator walks.
 *
 * A frame says "translate transform group 7"; the model has to answer "which
 * vertices is that". The cache stores the opposite direction — one label per
 * vertex — so this inversion is the whole reason an unrigged model ignores an
 * animation rather than mis-animating.
 */
static struct ToriDraw_Bones*
build_bones(const uint8_t* map, int count)
{
    if( !map || count <= 0 )
        return NULL;

    struct RSCache_ModelBones* src = RSCache_ModelBonesNewDecode(map, count);
    if( !src )
        return NULL;

    struct ToriDraw_Bones* out = calloc(1, sizeof(*out));
    if( !out )
    {
        RSCache_ModelBonesFree(src);
        return NULL;
    }

    out->bones_count = src->bones_count;
    int n = out->bones_count > 0 ? out->bones_count : 1;
    out->bones = calloc((size_t)n, sizeof(boneint_t*));
    out->bones_sizes = calloc((size_t)n, sizeof(boneint_t));
    if( !out->bones || !out->bones_sizes )
    {
        RSCache_ModelBonesFree(src);
        ToriDraw_BonesFree(out);
        return NULL;
    }

    for( int i = 0; i < out->bones_count; i++ )
    {
        int size = src->bones_sizes[i];
        out->bones_sizes[i] = (boneint_t)size;
        out->bones[i] = calloc((size_t)(size > 0 ? size : 1), sizeof(boneint_t));
        if( !out->bones[i] )
        {
            RSCache_ModelBonesFree(src);
            ToriDraw_BonesFree(out);
            return NULL;
        }
        for( int j = 0; j < size; j++ )
            out->bones[i][j] = (boneint_t)src->bones[i][j];
    }

    RSCache_ModelBonesFree(src);
    return out;
}

static struct ToriDraw_Model*
model_from_rscache(const struct RSCache_Model* src)
{
    if( !src || src->vertex_count <= 0 || src->face_count <= 0 )
        return NULL;

    struct ToriDraw_Model* m = ToriDraw_ModelNew(src->vertex_count, src->face_count, 0);
    if( !m )
        return NULL;

    m->model_priority = src->model_priority;

    m->vertices_x = dup_as(src->vertices_x, src->vertex_count, 4, sizeof(vertexint_t), 1);
    m->vertices_y = dup_as(src->vertices_y, src->vertex_count, 4, sizeof(vertexint_t), 1);
    m->vertices_z = dup_as(src->vertices_z, src->vertex_count, 4, sizeof(vertexint_t), 1);
    m->face_indices_a = dup_as(src->face_indices_a, src->face_count, 4, sizeof(faceint_t), 1);
    m->face_indices_b = dup_as(src->face_indices_b, src->face_count, 4, sizeof(faceint_t), 1);
    m->face_indices_c = dup_as(src->face_indices_c, src->face_count, 4, sizeof(faceint_t), 1);
    if( !m->vertices_x || !m->vertices_y || !m->vertices_z || !m->face_indices_a ||
        !m->face_indices_b || !m->face_indices_c )
    {
        ToriDraw_ModelFree(m);
        return NULL;
    }

    m->face_colors = dup_as(src->face_colors, src->face_count, 2, sizeof(hsl16_t), 0);

    /*
     * The three per-corner colour arrays lighting writes into.
     *
     * They hold no source data — the cache has one colour per face — so they
     * look optional, and leaving them NULL is exactly what made every .model
     * request kill the server: ToriDraw_ApplyLighting writes through them
     * unconditionally. They must exist before the model is lit.
     */
    {
        size_t n = (size_t)(src->face_count > 0 ? src->face_count : 1);
        m->face_colors_a = calloc(n, sizeof(hsl16_t));
        m->face_colors_b = calloc(n, sizeof(hsl16_t));
        m->face_colors_c = calloc(n, sizeof(hsl16_t));
        if( !m->face_colors_a || !m->face_colors_b || !m->face_colors_c )
        {
            ToriDraw_ModelFree(m);
            return NULL;
        }
    }
    m->face_alphas = dup_as(src->face_alphas, src->face_count, 1, sizeof(alphaint_t), 0);
    m->face_infos = dup_as(src->face_infos, src->face_count, 1, sizeof(int), 0);
    m->face_textures = dup_as(src->face_textures, src->face_count, 2, sizeof(faceint_t), 1);
    m->face_texture_coords =
        dup_as(src->face_texture_coords, src->face_count, 2, sizeof(faceint_t), 1);

    /* Two 4-bit priorities per byte on both sides, so this one copies verbatim. */
    if( src->face_priorities )
    {
        size_t nbytes = (size_t)((src->face_count + 1) / 2);
        m->face_priorities = malloc(nbytes);
        if( m->face_priorities )
            memcpy(m->face_priorities, src->face_priorities, nbytes);
    }

    m->textured_face_count = src->textured_face_count;
    m->textured_p_coordinate =
        dup_as(src->textured_p_coordinate, src->textured_face_count, 2, sizeof(faceint_t), 0);
    m->textured_m_coordinate =
        dup_as(src->textured_m_coordinate, src->textured_face_count, 2, sizeof(faceint_t), 0);
    m->textured_n_coordinate =
        dup_as(src->textured_n_coordinate, src->textured_face_count, 2, sizeof(faceint_t), 0);

    m->vertex_bones = build_bones(src->vertex_bone_map, src->vertex_count);
    m->face_bones = build_bones(src->face_bone_map, src->face_count);

    return m;
}

struct ToriDraw_Model*
ev_build_npc_model(
    struct Tool_Dat2Cache* c,
    int npc_id)
{
    struct RSCache_Dat2ConfigNpc* npc = tool_dat2_npc_load(c, npc_id);
    if( !npc )
        return NULL;
    if( npc->models_count <= 0 )
    {
        RSCache_Dat2ConfigNpcFree(npc);
        return NULL;
    }

    struct ToriDraw_Model** parts = calloc((size_t)npc->models_count, sizeof(*parts));
    if( !parts )
    {
        RSCache_Dat2ConfigNpcFree(npc);
        return NULL;
    }

    int part_count = 0;
    for( int i = 0; i < npc->models_count; i++ )
    {
        struct RSCache_Model* rs = tool_dat2_model_load(c, npc->models[i]);
        if( !rs )
            continue;
        struct ToriDraw_Model* part = model_from_rscache(rs);
        RSCache_ModelFree(rs);
        if( part )
            parts[part_count++] = part;
    }

    struct ToriDraw_Model* merged = NULL;
    if( part_count == 1 )
        merged = parts[0];
    else if( part_count > 1 )
    {
        merged = ToriDraw_ModelNewMerge(parts, part_count);
        for( int i = 0; i < part_count; i++ )
            ToriDraw_ModelFree(parts[i]);
    }
    free(parts);

    if( !merged )
    {
        RSCache_Dat2ConfigNpcFree(npc);
        return NULL;
    }

    /* Recolour and retexture after the merge, exactly where the client does it:
     * the pairs address colours across the whole npc, not per part. */
    for( int i = 0; i < npc->recolor_count; i++ )
        ToriDraw_ModelRecolor(merged, npc->recolor_to_find[i], npc->recolor_to_replace[i]);
    for( int i = 0; i < npc->retexture_count; i++ )
        ToriDraw_ModelRetexture(merged, npc->retexture_to_find[i], npc->retexture_to_replace[i]);

    struct ToriDraw_ModelHandle hnd;
    memset(&hnd, 0, sizeof(hnd));
    hnd.kind = TORIDRAWMK_MODEL;
    hnd.u.model.model = merged;

    /* The actor light profile, and the npc's own signed ambient/contrast
     * offsets. Scene light would wash an npc out — these are the two profiles
     * the client keeps apart, and an npc is an actor. */
    ToriDraw_LightModelActor(hnd, npc->contrast, npc->ambient);

    /* The projection culls anything without one. Harmless on the wire path
     * (the reader recomputes it) but needed by any native caller. */
    ToriDraw_ModelSetBoundsCylinder(merged);

    RSCache_Dat2ConfigNpcFree(npc);
    return merged;
}

/* ---- sequence -> animation ---------------------------------------------- */

/** One frame archive, decoded far enough to pull a single frame out of it. */
static struct RSCache_Dat2Frame*
load_frame(
    struct Tool_Dat2Cache* c,
    struct RSCache_Dat2Framemap* framemap,
    int packed_frame_id)
{
    int archive_id = (packed_frame_id >> 16) & 0xFFFF;
    int file_id = packed_frame_id & 0xFFFF;
    int anim_table = RSCache_Dat2DiskTableId(c->disk, RSCACHE_DAT2_TABLE_ANIMATIONS);

    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(c->disk, anim_table, archive_id);
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

    struct RSCache_Dat2Frame* frame = NULL;
    int pos = tool_archive_file_position(archive, file_id);
    if( pos >= 0 && pos < files->file_count && files->file_sizes[pos] > 0 )
        frame = RSCache_Dat2FrameNewDecodeProfile(
            &c->profile, packed_frame_id, framemap, files->files[pos], files->file_sizes[pos]);

    RSCache_FileListFree(files);
    RSCache_Dat2DiskArchiveFree(archive);
    return frame;
}

void
ev_build_free_anim(struct ToriDraw_Animation* anim)
{
    if( !anim )
        return;
    if( anim->base )
    {
        for( int i = 0; i < anim->base->length; i++ )
            free(anim->base->bone_groups ? anim->base->bone_groups[i] : NULL);
        free(anim->base->bone_groups);
        free(anim->base->bone_group_lengths);
        free(anim->base->types);
        free(anim->base);
    }
    for( int i = 0; i < anim->frame_count; i++ )
    {
        free(anim->frames[i].groups);
        free(anim->frames[i].x);
        free(anim->frames[i].y);
        free(anim->frames[i].z);
    }
    free(anim->frames);
    free(anim);
}

struct ToriDraw_Animation*
ev_build_seq_anim(
    struct Tool_Dat2Cache* c,
    int seq_id,
    int* out_framemap_id)
{
    if( out_framemap_id )
        *out_framemap_id = -1;

    struct RSCache_Dat2ConfigSequence* seq = tool_dat2_seq_load(c, seq_id);
    if( !seq )
        return NULL;
    if( !seq->frame_ids || seq->frame_count <= 0 )
    {
        RSCache_Dat2ConfigSequenceFree(seq);
        return NULL;
    }

    int framemap_id = tool_dat2_seq_framemap_id(c, seq, 0);
    if( framemap_id < 0 )
    {
        RSCache_Dat2ConfigSequenceFree(seq);
        return NULL;
    }
    if( out_framemap_id )
        *out_framemap_id = framemap_id;

    struct RSCache_Dat2Framemap* fm = tool_dat2_framemap_load(c, framemap_id);
    if( !fm )
    {
        RSCache_Dat2ConfigSequenceFree(seq);
        return NULL;
    }

    struct ToriDraw_Animation* anim = calloc(1, sizeof(*anim));
    if( !anim )
        goto fail;

    anim->frame_step = seq->frame_step;
    anim->base = calloc(1, sizeof(*anim->base));
    if( !anim->base )
        goto fail;

    anim->base->length = fm->length;
    {
        int n = fm->length > 0 ? fm->length : 1;
        anim->base->types = calloc((size_t)n, sizeof(uint8_t));
        anim->base->bone_group_lengths = calloc((size_t)n, sizeof(uint16_t));
        anim->base->bone_groups = calloc((size_t)n, sizeof(uint8_t*));
        if( !anim->base->types || !anim->base->bone_group_lengths || !anim->base->bone_groups )
            goto fail;

        for( int i = 0; i < fm->length; i++ )
        {
            anim->base->types[i] = (uint8_t)fm->types[i];
            int glen = fm->bone_groups_lengths[i];
            anim->base->bone_group_lengths[i] = (uint16_t)glen;
            anim->base->bone_groups[i] = calloc((size_t)(glen > 0 ? glen : 1), sizeof(uint8_t));
            if( !anim->base->bone_groups[i] )
                goto fail;
            for( int j = 0; j < glen; j++ )
                anim->base->bone_groups[i][j] = (uint8_t)fm->bone_groups[i][j];
        }
    }

    anim->frames = calloc((size_t)seq->frame_count, sizeof(*anim->frames));
    if( !anim->frames )
        goto fail;

    for( int i = 0; i < seq->frame_count; i++ )
    {
        struct RSCache_Dat2Frame* src = load_frame(c, fm, seq->frame_ids[i]);
        struct ToriDraw_AnimFrame* dst = &anim->frames[anim->frame_count];

        /* A frame that will not decode is skipped rather than left as a hole:
         * an empty frame would hold the model in bind pose for its duration,
         * which reads as a bug in the animation rather than a missing frame. */
        if( !src )
            continue;

        dst->id = seq->frame_ids[i];
        dst->length = src->translator_count;
        dst->delay = seq->frame_lengths ? seq->frame_lengths[i] : 1;

        int n = dst->length > 0 ? dst->length : 1;
        dst->groups = calloc((size_t)n, sizeof(int16_t));
        dst->x = calloc((size_t)n, sizeof(int16_t));
        dst->y = calloc((size_t)n, sizeof(int16_t));
        dst->z = calloc((size_t)n, sizeof(int16_t));
        if( !dst->groups || !dst->x || !dst->y || !dst->z )
        {
            RSCache_Dat2FrameFree(src);
            goto fail;
        }
        for( int j = 0; j < dst->length; j++ )
        {
            dst->groups[j] = (int16_t)src->index_frame_ids[j];
            dst->x[j] = (int16_t)src->translator_arg_x[j];
            dst->y[j] = (int16_t)src->translator_arg_y[j];
            dst->z[j] = (int16_t)src->translator_arg_z[j];
        }
        anim->frame_count++;
        RSCache_Dat2FrameFree(src);
    }

    RSCache_Dat2FramemapFree(fm);
    RSCache_Dat2ConfigSequenceFree(seq);

    if( anim->frame_count == 0 )
    {
        ev_build_free_anim(anim);
        return NULL;
    }
    return anim;

fail:
    ev_build_free_anim(anim);
    RSCache_Dat2FramemapFree(fm);
    RSCache_Dat2ConfigSequenceFree(seq);
    return NULL;
}
