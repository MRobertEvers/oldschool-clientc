#include "toridraw_model.h"

#include "osrs/palette.h"
#include "toridraw_animation.h"
#include "toridraw_lighting.h"
#include "toridraw_math.h"
#include "toridraw_model_transform.h"

#include <assert.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ToriDraw_AnimTransform
{
    int origin_x;
    int origin_y;
    int origin_z;
};

/*
 * Clamp, as the name has always claimed -- it used to be a bare truncating cast.
 *
 * `vertexint_t` is int16_t, so a transform that drives a coordinate past 32767
 * WRAPS to the opposite extreme, and every face touching that vertex becomes a
 * screen-crossing shard. Clamping keeps the damage local to the vertex that
 * overflowed instead of teleporting it, which is the difference between a
 * visibly distorted limb and geometry sprayed across the arena.
 *
 * This is containment, not a fix: a model reaching these values is already
 * wrong upstream (the QBD hit radius 32,969 and a depth bias of 38,548 against
 * a 16,384-level table, which is what made her vanish). Clamping stops the
 * wrap; it does not stop the explosion.
 */
static vertexint_t
ToriDraw_AnimVertexintClamp(int v)
{
    if( v > 32767 )
        return (vertexint_t)32767;
    if( v < -32768 )
        return (vertexint_t)(-32768);
    return (vertexint_t)v;
}

static void
ToriDraw_AnimApplyTransform(
    struct ToriDraw_AnimTransform* transform,
    int type,
    const uint8_t* bone_group,
    int bone_group_length,
    int arg_x,
    int arg_y,
    int arg_z,
    struct ToriDraw_Model* model)
{
    struct ToriDraw_Bones* vertex_bones = model->vertex_bones;
    struct ToriDraw_Bones* face_bones = model->face_bones;
    vertexint_t* vertices_x = model->vertices_x;
    vertexint_t* vertices_y = model->vertices_y;
    vertexint_t* vertices_z = model->vertices_z;
    alphaint_t* face_alphas = model->face_alphas;

    switch( type )
    {
    case 0:
    {
        if( !vertex_bones || !vertex_bones->bones )
            return;

        int avg_x = 0;
        int avg_y = 0;
        int avg_z = 0;
        int count = 0;

        for( int i = 0; i < bone_group_length; i++ )
        {
            int bone_index = bone_group[i];
            if( bone_index >= vertex_bones->bones_count )
                continue;

            boneint_t* bone = vertex_bones->bones[bone_index];
            int bone_length = vertex_bones->bones_sizes[bone_index];

            for( int j = 0; j < bone_length; j++ )
            {
                boneint_t vertex_index = bone[j];
                avg_x += vertices_x[vertex_index];
                avg_y += vertices_y[vertex_index];
                avg_z += vertices_z[vertex_index];
                count++;
            }
        }

        if( count > 0 )
        {
            transform->origin_x = arg_x + avg_x / count;
            transform->origin_y = arg_y + avg_y / count;
            transform->origin_z = arg_z + avg_z / count;
        }
        else
        {
            transform->origin_x = arg_x;
            transform->origin_y = arg_y;
            transform->origin_z = arg_z;
        }

        /*
         * TORIRS_ORIGIN_PROBE: an ORIGIN (type 0) op that lands far outside the
         * model it pivots.
         *
         * Every later rotate in the frame is relative to this point, so a bad
         * pivot is what turns a plausible keyframe into geometry at the far
         * rail -- and it is the difference between "this animation asks for a
         * big pose" and "this frame decoded wrong". `arg_*` is the frame's own
         * decoded value; the averaged term is the centroid of the bone group it
         * names. Printing both says which half is unreasonable: a wild `arg`
         * means the frame data, a wild centroid means the bone group (a label
         * naming vertices this model does not have).
         */
        if( getenv("TORIRS_ORIGIN_PROBE") )
        {
            int const ox = transform->origin_x;
            int const oy = transform->origin_y;
            int const oz = transform->origin_z;
            int const worst = model->bounds_cylinder ? model->bounds_cylinder->radius : 0;
            int const limit = worst > 4096 ? worst * 4 : 16384;

            if( ox > limit || ox < -limit || oy > limit || oy < -limit || oz > limit ||
                oz < -limit )
                fprintf(
                    stderr,
                    "origin_probe: model=%p vc=%d ORIGIN=(%d,%d,%d) from arg=(%d,%d,%d) + "
                    "centroid=(%d,%d,%d) over %d verts in %d bone group(s) [model radius %d]\n",
                    (void*)model, model->vertex_count, ox, oy, oz, arg_x, arg_y, arg_z,
                    avg_x / count, avg_y / count, avg_z / count, count, bone_group_length,
                    worst);
        }
        break;
    }
    case 1:
    {
        if( !vertex_bones || !vertex_bones->bones )
            return;

        for( int i = 0; i < bone_group_length; i++ )
        {
            int bone_index = bone_group[i];
            if( bone_index >= vertex_bones->bones_count )
                continue;

            boneint_t* bone = vertex_bones->bones[bone_index];
            int bone_length = vertex_bones->bones_sizes[bone_index];

            for( int j = 0; j < bone_length; j++ )
            {
                int vertex_index = bone[j];
                vertices_x[vertex_index] =
                    ToriDraw_AnimVertexintClamp((int)vertices_x[vertex_index] + arg_x);
                vertices_y[vertex_index] =
                    ToriDraw_AnimVertexintClamp((int)vertices_y[vertex_index] + arg_y);
                vertices_z[vertex_index] =
                    ToriDraw_AnimVertexintClamp((int)vertices_z[vertex_index] + arg_z);
            }
        }
        break;
    }
    case 2:
    {
        if( !vertex_bones || !vertex_bones->bones )
            return;

        for( int i = 0; i < bone_group_length; i++ )
        {
            int bone_index = bone_group[i];
            if( bone_index >= vertex_bones->bones_count )
                continue;

            boneint_t* bone = vertex_bones->bones[bone_index];
            int bone_length = vertex_bones->bones_sizes[bone_index];

            for( int j = 0; j < bone_length; j++ )
            {
                int vertex_index = bone[j];
                int x = (int)vertices_x[vertex_index] - transform->origin_x;
                int y = (int)vertices_y[vertex_index] - transform->origin_y;
                int z = (int)vertices_z[vertex_index] - transform->origin_z;
                int pitch = (arg_x & 255) * 8;
                int yaw = (arg_y & 255) * 8;
                int roll = (arg_z & 255) * 8;
                int var17;

                /* 32-bit hazard: these coordinates are relative to
                 * transform->origin_*, so a distant pivot makes the operand
                 * large even when every stored vertex is in range, and the
                 * 16.16 product overflows past ~+/-32,000. Only reachable with
                 * geometry that is already wrong (see ToriDraw_ModelCopy);
                 * guard by range once per model, not by widening every vertex. */
                if( roll != 0 )
                {
                    int sin_roll = ToriDraw_Sin(roll);
                    int cos_roll = ToriDraw_Cos(roll);
                    var17 = (sin_roll * y + cos_roll * x) >> 16;
                    y = (cos_roll * y - sin_roll * x) >> 16;
                    x = var17;
                }

                if( pitch != 0 )
                {
                    int sin_pitch = ToriDraw_Sin(pitch);
                    int cos_pitch = ToriDraw_Cos(pitch);
                    var17 = (cos_pitch * y - sin_pitch * z) >> 16;
                    z = (sin_pitch * y + cos_pitch * z) >> 16;
                    y = var17;
                }

                if( yaw != 0 )
                {
                    int sin_yaw = ToriDraw_Sin(yaw);
                    int cos_yaw = ToriDraw_Cos(yaw);
                    var17 = (sin_yaw * z + cos_yaw * x) >> 16;
                    z = (cos_yaw * z - sin_yaw * x) >> 16;
                    x = var17;
                }

                vertices_x[vertex_index] = ToriDraw_AnimVertexintClamp(x + transform->origin_x);
                vertices_y[vertex_index] = ToriDraw_AnimVertexintClamp(y + transform->origin_y);
                vertices_z[vertex_index] = ToriDraw_AnimVertexintClamp(z + transform->origin_z);
            }
        }
        break;
    }
    case 3:
    {
        if( !vertex_bones || !vertex_bones->bones )
            return;

        for( int i = 0; i < bone_group_length; i++ )
        {
            int bone_index = bone_group[i];
            if( bone_index >= vertex_bones->bones_count )
                continue;

            boneint_t* bone = vertex_bones->bones[bone_index];
            int bone_length = vertex_bones->bones_sizes[bone_index];

            for( int j = 0; j < bone_length; j++ )
            {
                int vertex_index = bone[j];
                int x = (int)vertices_x[vertex_index] - transform->origin_x;
                int y = (int)vertices_y[vertex_index] - transform->origin_y;
                int z = (int)vertices_z[vertex_index] - transform->origin_z;
                /* Same 32-bit hazard as the rotate above, same reasoning. */
                x = (arg_x * x) / 128;
                y = (arg_y * y) / 128;
                z = (arg_z * z) / 128;
                vertices_x[vertex_index] = ToriDraw_AnimVertexintClamp(x + transform->origin_x);
                vertices_y[vertex_index] = ToriDraw_AnimVertexintClamp(y + transform->origin_y);
                vertices_z[vertex_index] = ToriDraw_AnimVertexintClamp(z + transform->origin_z);
            }
        }
        break;
    }
    case 5:
    {
        if( !face_alphas || !face_bones || !face_bones->bones )
            return;

        for( int i = 0; i < bone_group_length; i++ )
        {
            int bone_index = bone_group[i];
            if( bone_index >= face_bones->bones_count )
                continue;

            boneint_t* bone = face_bones->bones[bone_index];
            int bone_length = face_bones->bones_sizes[bone_index];

            for( int j = 0; j < bone_length; j++ )
            {
                int face_index = bone[j];
                int alpha = face_alphas[face_index];
                alpha += arg_x * 8;
                if( alpha < 0 )
                    alpha = 0;
                if( alpha > 255 )
                    alpha = 255;
                face_alphas[face_index] = (alphaint_t)alpha;
            }
        }
        break;
    }
    default:
        break;
    }
}

struct ToriDraw_Normals*
ToriDraw_NormalsNew(
    int vertex_count,
    int face_count)
{
    struct ToriDraw_Normals* normals = malloc(sizeof(struct ToriDraw_Normals));
    memset(normals, 0, sizeof(struct ToriDraw_Normals));
    normals->vertex_normals = malloc(sizeof(struct ToriDraw_Normal) * (size_t)vertex_count);
    memset(normals->vertex_normals, 0, sizeof(struct ToriDraw_Normal) * (size_t)vertex_count);
    normals->vertex_normals_count = vertex_count;
    if( face_count > 0 )
    {
        normals->face_normals = malloc(sizeof(struct ToriDraw_Normal) * (size_t)face_count);
        memset(normals->face_normals, 0, sizeof(struct ToriDraw_Normal) * (size_t)face_count);
        normals->face_normals_count = face_count;
    }
    return normals;
}

void
ToriDraw_NormalsFree(struct ToriDraw_Normals* normals)
{
    if( !normals )
        return;
    free(normals->vertex_normals);
    free(normals->face_normals);
    free(normals);
}

void
ToriDraw_BonesFree(struct ToriDraw_Bones* bones)
{
    if( !bones )
        return;
    if( bones->bones )
    {
        for( int i = 0; i < bones->bones_count; i++ )
            free(bones->bones[i]);
        free(bones->bones);
    }
    free(bones->bones_sizes);
    free(bones);
}

struct ToriDraw_Bones*
ToriDraw_BonesCopy(const struct ToriDraw_Bones* src)
{
    /* Whether a model HAS bones is the caller's question to answer -- an
     * unrigged model simply does not call this. Once called, a bone set that
     * is absent, empty, or half-built is a broken one. */
    assert(src);
    assert(src->bones_count > 0);
    assert(src->bones);
    assert(src->bones_sizes);

    struct ToriDraw_Bones* dst = calloc(1, sizeof(struct ToriDraw_Bones));
    assert(dst);

    dst->bones_count = src->bones_count;
    dst->bones = calloc((size_t)src->bones_count, sizeof(boneint_t*));
    dst->bones_sizes = malloc((size_t)src->bones_count * sizeof(boneint_t));
    assert(dst->bones);
    assert(dst->bones_sizes);

    memcpy(dst->bones_sizes, src->bones_sizes, (size_t)src->bones_count * sizeof(boneint_t));

    for( int i = 0; i < src->bones_count; i++ )
    {
        int const bone_length = (int)src->bones_sizes[i];
        if( bone_length <= 0 || !src->bones[i] )
        {
            dst->bones[i] = NULL;
            continue;
        }

        dst->bones[i] = malloc((size_t)bone_length * sizeof(boneint_t));
        assert(dst->bones[i]);
        memcpy(dst->bones[i], src->bones[i], (size_t)bone_length * sizeof(boneint_t));
    }

    return dst;
}

static void
ToriDraw_ModelFree_arrays(struct ToriDraw_Model* m)
{
    free(m->vertices_x);
    free(m->vertices_y);
    free(m->vertices_z);
    free(m->face_colors_a);
    free(m->face_colors_b);
    free(m->face_colors_c);
    free(m->face_indices_a);
    free(m->face_indices_b);
    free(m->face_indices_c);
    free(m->face_textures);
    free(m->original_vertices_x);
    free(m->original_vertices_y);
    free(m->original_vertices_z);
    free(m->face_alphas);
    free(m->original_face_alphas);
    free(m->face_infos);
    free(m->face_priorities);
    free(m->face_colors);
    free(m->textured_p_coordinate);
    free(m->textured_m_coordinate);
    free(m->textured_n_coordinate);
    free(m->texture_render_types);
    free(m->face_texture_coords);
    ToriDraw_NormalsFree(m->normals);
    ToriDraw_NormalsFree(m->merged_normals);
    ToriDraw_BonesFree(m->vertex_bones);
    ToriDraw_BonesFree(m->face_bones);
    free(m->bounds_cylinder);

    if( m->animaya_groups )
    {
        for( int i = 0; i < m->animaya_vertex_count; i++ )
            free(m->animaya_groups[i]);
        free(m->animaya_groups);
    }
    if( m->animaya_scales )
    {
        for( int i = 0; i < m->animaya_vertex_count; i++ )
            free(m->animaya_scales[i]);
        free(m->animaya_scales);
    }
    free(m->animaya_group_counts);
}

struct ToriDraw_ModelHD*
ToriDraw_ModelHDFromModel(struct ToriDraw_Model* model)
{
    struct ToriDraw_ModelHD* hd;

    if( !model )
        return NULL;

    hd = (struct ToriDraw_ModelHD*)calloc(1, sizeof(*hd));
    assert(hd);

    /* By value: the arrays are pointers and move with the struct, so freeing
     * the shell afterwards must not go through ToriDraw_ModelFree — that would
     * release the arrays the HD model now owns. */
    hd->base = *model;
    free(model);
    return hd;
}

void
ToriDraw_ModelHDFree(struct ToriDraw_ModelHD* hd)
{
    if( !hd )
        return;
    free(hd->texture_mappings);
    hd->texture_mappings = NULL;
    /* The base is embedded by value, so its arrays are released here and the
     * single allocation covering both is freed once. */
    ToriDraw_ModelFree_arrays(&hd->base);
    free(hd);
}

void
ToriDraw_ModelFree(struct ToriDraw_Model* model)
{
    if( !model )
        return;
    ToriDraw_ModelFree_arrays(model);
    free(model);
}

void
ToriDraw_ModelAssertPnmTextureInvariant(struct ToriDraw_Model const* model)
{
    assert(model);

    if( !model->face_texture_coords || model->face_count <= 0 )
        return;

    for( int i = 0; i < model->face_count; i++ )
    {
        const int texture_face = model->face_texture_coords[i];
        if( texture_face == -1 )
            continue;

        assert(model->textured_face_count > 0);
        assert(model->textured_p_coordinate != NULL);
        assert(model->textured_m_coordinate != NULL);
        assert(model->textured_n_coordinate != NULL);
        assert(texture_face >= 0);
        assert(texture_face < model->textured_face_count);

        /* Only render type 0 stores vertex indices here; types 1-3 store a raw
         * projection axis, which is not an index and is normally out of range.
         * See the field comment in the cache decoder. */
        const int render_type =
            model->texture_render_types ? (model->texture_render_types[texture_face] & 0xFF) : 0;
        if( render_type != 0 )
            continue;

        const int p = model->textured_p_coordinate[texture_face];
        const int m = model->textured_m_coordinate[texture_face];
        const int n = model->textured_n_coordinate[texture_face];
        assert(p >= 0 && p < model->vertex_count);
        assert(m >= 0 && m < model->vertex_count);
        assert(n >= 0 && n < model->vertex_count);
    }
}

static int
ToriDraw_ModelNeedsFaceNormals(struct ToriDraw_Model* model)
{
    const int* fi = model->face_infos;
    if( !fi )
        return 0;
    int fc = model->face_count;
    for( int i = 0; i < fc; i++ )
    {
        if( (fi[i] == 1) )
            return 1;
    }
    return 0;
}

void
ToriDraw_ModelAllocNormals(struct ToriDraw_Model* model)
{
    assert(model);
    if( model->normals )
        return;
    int face_n = ToriDraw_ModelNeedsFaceNormals(model) ? model->face_count : 0;
    model->normals = ToriDraw_NormalsNew(model->vertex_count, face_n);
}

void
ToriDraw_ModelAllocMergedNormals(struct ToriDraw_Model* model)
{
    assert(model);
    if( model->merged_normals )
        return;
    model->merged_normals = ToriDraw_NormalsNew(model->vertex_count, 0);
}

void
ToriDraw_ModelCalculateVertexNormals(struct ToriDraw_Model* model)
{
    assert(model);
    ToriDraw_ModelAllocNormals(model);
    struct ToriDraw_Normals* nm = model->normals;
    assert(nm);

    int vc = model->vertex_count;
    int fc = model->face_count;
    ToriDraw_CalculateVertexNormals(
        nm->vertex_normals,
        nm->face_normals,
        vc,
        model->face_indices_a,
        model->face_indices_b,
        model->face_indices_c,
        model->vertices_x,
        model->vertices_y,
        model->vertices_z,
        fc);

    struct ToriDraw_Normals* mm = model->merged_normals;
    if( mm && mm->vertex_normals )
        memcpy(mm->vertex_normals, nm->vertex_normals, sizeof(struct ToriDraw_Normal) * (size_t)vc);
}

void
ToriDraw_ModelFreeNormals(struct ToriDraw_Model* model)
{
    assert(model);
    if( !model->normals )
        return;
    ToriDraw_NormalsFree(model->normals);
    ToriDraw_NormalsFree(model->merged_normals);
    model->normals = NULL;
    model->merged_normals = NULL;
}

void
ToriDraw_ModelCaptureOriginalVertices(struct ToriDraw_Model* model)
{
    assert(model);
    size_t const vc = (size_t)model->vertex_count;

    /*
     * TORIRS_ANIM_RECAPTURE: catch a capture that overwrites a live bind pose.
     *
     * The originals ARE the bind pose -- the renderer restores them and applies
     * the keyframe to them, every frame. Capturing while the model is posed
     * makes the posed geometry the new bind, so the next keyframe stacks on top
     * of the last one and the model inflates without bound. Nothing errors; the
     * only visible sign is the geometry growing, which is why this reports the
     * overwrite itself rather than waiting for the size to become absurd.
     *
     * Comparing against the EXISTING originals is what makes it specific: a
     * re-capture of an unposed model is a harmless no-op and stays silent.
     *
     * The env is probed once per process: this runs for every scene element a
     * map rebuild creates, and per-call getenv was hot in a rebuild profile.
     */
    static int recapture_env = -1;
    if( recapture_env < 0 )
        recapture_env = getenv("TORIRS_ANIM_RECAPTURE") != NULL;
    if( recapture_env && model->original_vertices_x &&
        model->original_vertices_y && model->original_vertices_z && vc > 0 )
    {
        size_t differ = 0;
        int worst = 0;
        for( size_t i = 0; i < vc; i++ )
        {
            int dx = (int)model->vertices_x[i] - (int)model->original_vertices_x[i];
            int dy = (int)model->vertices_y[i] - (int)model->original_vertices_y[i];
            int dz = (int)model->vertices_z[i] - (int)model->original_vertices_z[i];
            int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy) + (dz < 0 ? -dz : dz);
            if( d )
            {
                differ++;
                if( d > worst )
                    worst = d;
            }
        }
        if( differ )
            fprintf(
                stderr,
                "anim_recapture: model=%p verts=%d POSED vertices captured as bind "
                "(%zu/%zu moved, worst delta %d) -- the next keyframe will stack "
                "on this pose\n",
                (void*)model, model->vertex_count, differ, vc, worst);
    }

    /* Re-capture, not first capture: this runs again on every SetModel /
     * SetAnimation / SetAnimationSeq, and a bind takes two of those in a row
     * (app_world_try_bind_seq). Dropping the previous buffers on the floor leaks
     * 6 bytes per vertex per call, which on a 7,837-vertex import like the QBD is
     * ~46 KB every time its sequence changes -- once per attack, per phase, per
     * retype -- plus the face-alpha copy. Free before reallocating. */
    free(model->original_vertices_x);
    free(model->original_vertices_y);
    free(model->original_vertices_z);
    free(model->original_face_alphas);
    model->original_vertices_x = NULL;
    model->original_vertices_y = NULL;
    model->original_vertices_z = NULL;
    model->original_face_alphas = NULL;

    if( vc == 0 )
        return;

    model->original_vertices_x = malloc(vc * sizeof(vertexint_t));
    model->original_vertices_y = malloc(vc * sizeof(vertexint_t));
    model->original_vertices_z = malloc(vc * sizeof(vertexint_t));
    /* ToriDraw_ModelAnimateReset gates on original_vertices_x alone, so a
     * partial capture would let it memcpy from a NULL y/z. All three or none. */
    assert(model->original_vertices_x);
    assert(model->original_vertices_y);
    assert(model->original_vertices_z);

    memcpy(model->original_vertices_x, model->vertices_x, vc * sizeof(vertexint_t));
    memcpy(model->original_vertices_y, model->vertices_y, vc * sizeof(vertexint_t));
    memcpy(model->original_vertices_z, model->vertices_z, vc * sizeof(vertexint_t));

    if( model->face_alphas && model->face_count > 0 )
    {
        size_t const fc = (size_t)model->face_count;
        model->original_face_alphas = malloc(fc * sizeof(alphaint_t));
        assert(model->original_face_alphas);
        memcpy(model->original_face_alphas, model->face_alphas, fc * sizeof(alphaint_t));
    }
}

void
ToriDraw_ModelAnimateReset(struct ToriDraw_Model* model)
{
    assert(model);
    if( !model->original_vertices_x )
        return;

    size_t const vc = (size_t)model->vertex_count;
    memcpy(model->vertices_x, model->original_vertices_x, vc * sizeof(vertexint_t));
    memcpy(model->vertices_y, model->original_vertices_y, vc * sizeof(vertexint_t));
    memcpy(model->vertices_z, model->original_vertices_z, vc * sizeof(vertexint_t));

    if( model->face_alphas && model->original_face_alphas )
    {
        size_t const fc = (size_t)model->face_count;
        memcpy(model->face_alphas, model->original_face_alphas, fc * sizeof(alphaint_t));
    }
}

void
ToriDraw_ModelAnimateFrame(
    struct ToriDraw_Model* model,
    const struct ToriDraw_AnimBase* base,
    const struct ToriDraw_AnimFrame* frame)
{
    assert(model && base && frame && frame->length > 0);
    assert(frame->groups && frame->x && frame->y && frame->z);

    struct ToriDraw_AnimTransform transform = { 0 };
    for( int i = 0; i < frame->length; i++ )
    {
        int group_index = frame->groups[i];
        if( group_index < 0 || group_index >= base->length )
            continue;
        if( !base->bone_groups || !base->types )
            continue;

        uint8_t* bone_group = base->bone_groups[group_index];
        int bone_group_length =
            base->bone_group_lengths ? (int)base->bone_group_lengths[group_index] : 0;
        int type = base->types[group_index];

        ToriDraw_AnimApplyTransform(
            &transform,
            type,
            bone_group,
            bone_group_length,
            frame->x[i],
            frame->y[i],
            frame->z[i],
            model);
    }

    /* Animate, THEN resize -- the reference's order (NpcType.getModel,
     * MapSpotAnim.getModel). The bind pose the ops above were applied to is the
     * model at its authored size; this is what puts the finished pose into
     * render scale. */
    ToriDraw_ModelApplyPostTransforms(model);

    /* Projection culling and the face-sort depth bias both consume this
     * cylinder.  Keeping the bind-pose cylinder on a large deformation can
     * make valid depths negative even when the scene's depth table is large
     * enough (animated QBD reaches radius 4,791 from a bind radius of 1,899). */
    ToriDraw_ModelSetBoundsCylinder(model);
}

/* One masked pass of Model.ts maskAnimate: apply `frame`'s ops to groups
 * whose membership in the ascending `mask` matches `apply_in_mask`. ORIGIN
 * ops (type 0) always apply — they set the pivot the following rotate/scale
 * ops need. Both frame->groups and mask are ascending; the 9999999 sentinel
 * terminates the mask. */
static void
model_animate_frame_mask_pass(
    struct ToriDraw_Model* model,
    const struct ToriDraw_AnimBase* base,
    const struct ToriDraw_AnimFrame* frame,
    const int* mask,
    int apply_in_mask)
{
    struct ToriDraw_AnimTransform transform = { 0 };
    int cursor = 0;
    int mask_group = mask[0];

    for( int i = 0; i < frame->length; i++ )
    {
        int group_index = frame->groups[i];
        int type;
        int in_mask;
        if( group_index < 0 || group_index >= base->length )
            continue;
        if( !base->bone_groups || !base->types )
            continue;

        while( group_index > mask_group )
            mask_group = mask[++cursor];
        in_mask = (group_index == mask_group);
        type = base->types[group_index];

        if( type != 0 && in_mask != apply_in_mask )
            continue;

        ToriDraw_AnimApplyTransform(
            &transform,
            type,
            base->bone_groups[group_index],
            base->bone_group_lengths ? (int)base->bone_group_lengths[group_index] : 0,
            frame->x[i],
            frame->y[i],
            frame->z[i],
            model);
    }
}

void
ToriDraw_ModelAnimateFrameMasked(
    struct ToriDraw_Model* model,
    const struct ToriDraw_AnimBase* base,
    const struct ToriDraw_AnimFrame* primary,
    const struct ToriDraw_AnimFrame* secondary,
    const int* walkmerge)
{
    /* Reference Model.maskAnimate: walkmerge lists the transform groups the
     * SECONDARY (walk) seq keeps driving while the primary (action) seq plays
     * — pass 1 applies the primary to every group NOT in the mask, pass 2
     * applies the secondary to exactly the masked groups. */
    assert(model && base && primary && primary->length > 0);
    if( !walkmerge || !secondary || secondary->length <= 0 )
    {
        ToriDraw_ModelAnimateFrame(model, base, primary);
        return;
    }
    model_animate_frame_mask_pass(model, base, primary, walkmerge, 0);
    model_animate_frame_mask_pass(model, base, secondary, walkmerge, 1);
    ToriDraw_ModelApplyPostTransforms(model);
    ToriDraw_ModelSetBoundsCylinder(model);
}

int
ToriDraw_TextureAverageHsl16(const struct ToriDraw_Texture* texture)
{
    if( !texture || !texture->texels || texture->width <= 0 || texture->height <= 0 )
        return 0;

    int red = 0;
    int green = 0;
    int blue = 0;
    int colour_count = texture->width * texture->height;
    for( int i = 0; i < colour_count; i++ )
    {
        int pixel = texture->texels[i];
        red += (pixel >> 16) & 0xff;
        green += (pixel >> 8) & 0xff;
        blue += pixel & 0xff;
    }

    int average_rgb =
        ((red / colour_count) << 16) + ((green / colour_count) << 8) + (blue / colour_count);
    return palette_rgb_to_hsl16(average_rgb);
}

static int
tori_skeletal_matrix_is_finite(const float* M)
{
    for( int k = 0; k < 16; k++ )
    {
        if( !isfinite(M[k]) )
            return 0;
    }
    return 1;
}

void
ToriDraw_ModelAnimateSkeletal(
    struct ToriDraw_Model* model,
    const struct ToriDraw_SkeletalAnim* skeletal,
    int frame_index)
{
    assert(model && skeletal && frame_index >= 0 && frame_index < skeletal->frame_count);
    assert(model->original_vertices_x && model->original_vertices_y && model->original_vertices_z);
    assert(model->animaya_group_counts && model->animaya_groups && model->animaya_scales);
    assert(skeletal->matrices);

    /* Pointer to the 4x4 skinning matrices for this frame:
     *   frame_matrices[(bone) * 16 .. +15] is the column-major mat4 for that bone */
    const float* frame_matrices =
        &skeletal->matrices[(size_t)frame_index * (size_t)skeletal->bone_count * 16];

    int vc = model->animaya_vertex_count;
    if( vc <= 0 )
        vc = model->vertex_count;

    for( int vi = 0; vi < vc && vi < model->vertex_count; vi++ )
    {
        int cnt = model->animaya_group_counts[vi];
        if( cnt <= 0 )
        {
            model->vertices_x[vi] = model->original_vertices_x[vi];
            model->vertices_y[vi] = model->original_vertices_y[vi];
            model->vertices_z[vi] = model->original_vertices_z[vi];
            continue;
        }

        /* RS model space is Y-down; skeletal matrices are Y-up (rs-map-viewer transformVertex). */
        float ox = (float)model->original_vertices_x[vi];
        float oy = -(float)model->original_vertices_y[vi];
        float oz = -(float)model->original_vertices_z[vi];

        float bx = 0.0f, by = 0.0f, bz = 0.0f;
        int contributed = 0;

        for( int j = 0; j < cnt; j++ )
        {
            int bone = model->animaya_groups[vi] ? (int)model->animaya_groups[vi][j] : 0;
            int scale = model->animaya_scales[vi] ? (int)model->animaya_scales[vi][j] : 0;
            if( bone < 0 || bone >= skeletal->bone_count || scale == 0 )
                continue;

            /* Column-major mat4 multiply: out = (scale/255) * M * [ox, oy, oz, 1] */
            const float* M = &frame_matrices[bone * 16];
            if( !tori_skeletal_matrix_is_finite(M) )
                continue;

            float w = (float)scale / 255.0f;
            bx += w * (M[0] * ox + M[4] * oy + M[8] * oz + M[12]);
            by += w * (M[1] * ox + M[5] * oy + M[9] * oz + M[13]);
            bz += w * (M[2] * ox + M[6] * oy + M[10] * oz + M[14]);
            contributed = 1;
        }

        if( contributed )
        {
            model->vertices_x[vi] = (vertexint_t)lroundf(bx);
            model->vertices_y[vi] = (vertexint_t)-lroundf(by);
            model->vertices_z[vi] = (vertexint_t)-lroundf(bz);
        }
        else
        {
            model->vertices_x[vi] = model->original_vertices_x[vi];
            model->vertices_y[vi] = model->original_vertices_y[vi];
            model->vertices_z[vi] = model->original_vertices_z[vi];
        }
    }

    /* Same order as the classic path above: the palette poses the authored
     * bind, the resize follows it. */
    ToriDraw_ModelApplyPostTransforms(model);
    ToriDraw_ModelSetBoundsCylinder(model);
}

void
ToriDraw_TextureAnimate(
    struct ToriDraw_Texture* tex,
    int cycles,
    int* scratch)
{
    if( !tex || !tex->texels || !scratch || cycles <= 0 )
        return;
    if( tex->animation_direction == TORIDRAW_TEXANIM_DIRECTION_NONE || tex->animation_speed == 0 )
        return;

    int const pixel_count = tex->width * tex->height;
    if( pixel_count <= 0 )
        return;

    int const dim = (pixel_count == 4096) ? 64 : 128;
    int const mask = pixel_count - 1;

    if( tex->animation_direction == TORIDRAW_TEXANIM_DIRECTION_V_DOWN ||
        tex->animation_direction == TORIDRAW_TEXANIM_DIRECTION_V_UP )
    {
        int shift = cycles * dim * tex->animation_speed;
        if( tex->animation_direction == TORIDRAW_TEXANIM_DIRECTION_V_DOWN )
            shift = -shift;

        for( int i = 0; i < pixel_count; i++ )
            scratch[i] = tex->texels[(shift + i) & mask];

        memcpy(tex->texels, scratch, (size_t)pixel_count * sizeof(int));
        return;
    }

    if( tex->animation_direction == TORIDRAW_TEXANIM_DIRECTION_U_DOWN ||
        tex->animation_direction == TORIDRAW_TEXANIM_DIRECTION_U_UP )
    {
        int const row_mask = dim - 1;
        int shift = tex->animation_speed * cycles;
        if( tex->animation_direction == TORIDRAW_TEXANIM_DIRECTION_U_DOWN )
            shift = -shift;

        for( int row = 0; row < pixel_count; row += dim )
        {
            for( int col = 0; col < dim; col++ )
            {
                int const dst = row + col;
                int const src = ((shift + col) & row_mask) + row;
                scratch[dst] = tex->texels[src];
            }
        }

        memcpy(tex->texels, scratch, (size_t)pixel_count * sizeof(int));
    }
}

void
ToriDraw_TextureMapAnimate(
    struct ToriDraw_TextureMap* map,
    int cycles)
{
    if( !map || cycles <= 0 )
        return;

    static int scratch[128 * 128];

    for( int i = 0; i < map->count; i++ )
    {
        struct ToriDraw_Texture* tex = map->textures[i];
        if( !tex || !tex->texels )
            continue;
        if( tex->animation_direction == TORIDRAW_TEXANIM_DIRECTION_NONE ||
            tex->animation_speed == 0 )
            continue;

        int const pixel_count = tex->width * tex->height;
        if( pixel_count <= 0 || pixel_count > (int)(sizeof(scratch) / sizeof(scratch[0])) )
            continue;

        ToriDraw_TextureAnimate(tex, cycles, scratch);
    }
}
