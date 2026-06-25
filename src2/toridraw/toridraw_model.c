#include "toridraw_model.h"

#include "osrs/palette.h"
#include "toridraw_animation.h"
#include "toridraw_lighting.h"
#include "toridraw_math.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct ToriDraw_AnimTransform
{
    int origin_x;
    int origin_y;
    int origin_z;
};

static vertexint_t
ToriDraw_AnimVertexintClamp(int v)
{
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
                x = arg_x * x / 128;
                y = arg_y * y / 128;
                z = arg_z * z / 128;
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
    if( !src || src->bones_count <= 0 || !src->bones || !src->bones_sizes )
        return NULL;

    struct ToriDraw_Bones* dst = calloc(1, sizeof(struct ToriDraw_Bones));
    if( !dst )
        return NULL;

    dst->bones_count = src->bones_count;
    dst->bones = calloc((size_t)src->bones_count, sizeof(boneint_t*));
    dst->bones_sizes = malloc((size_t)src->bones_count * sizeof(boneint_t));
    if( !dst->bones || !dst->bones_sizes )
    {
        ToriDraw_BonesFree(dst);
        return NULL;
    }

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
        if( !dst->bones[i] )
        {
            ToriDraw_BonesFree(dst);
            return NULL;
        }
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

void
ToriDraw_ModelFree(struct ToriDraw_Model* model)
{
    if( !model )
        return;
    ToriDraw_ModelFree_arrays(model);
    free(model);
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
        if( (fi[i] & 0x3) == 1 )
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
    size_t const vc = (size_t)model->vertex_count;
    model->original_vertices_x = malloc(vc * sizeof(vertexint_t));
    model->original_vertices_y = malloc(vc * sizeof(vertexint_t));
    model->original_vertices_z = malloc(vc * sizeof(vertexint_t));

    memcpy(model->original_vertices_x, model->vertices_x, vc * sizeof(vertexint_t));
    memcpy(model->original_vertices_y, model->vertices_y, vc * sizeof(vertexint_t));
    memcpy(model->original_vertices_z, model->vertices_z, vc * sizeof(vertexint_t));

    if( model->face_alphas && model->face_count > 0 )
    {
        size_t const fc = (size_t)model->face_count;
        model->original_face_alphas = malloc(fc * sizeof(alphaint_t));
        if( model->original_face_alphas )
            memcpy(model->original_face_alphas, model->face_alphas, fc * sizeof(alphaint_t));
    }
}

void
ToriDraw_ModelAnimateReset(struct ToriDraw_Model* model)
{
    if( !model || !model->original_vertices_x )
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
    if( !model || !base || !frame || frame->length <= 0 )
        return;
    if( !frame->groups || !frame->x || !frame->y || !frame->z )
        return;

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

void
ToriDraw_ModelAnimateSkeletal(
    struct ToriDraw_Model* model,
    const struct ToriDraw_SkeletalAnim* skeletal,
    int frame_index)
{
    if( !model || !skeletal || frame_index < 0 || frame_index >= skeletal->frame_count )
        return;
    if( !model->original_vertices_x || !model->original_vertices_y ||
        !model->original_vertices_z )
        return;
    if( !model->animaya_group_counts || !model->animaya_groups || !model->animaya_scales )
        return;
    if( !skeletal->matrices )
        return;

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

        float ox = (float)model->original_vertices_x[vi];
        float oy = (float)model->original_vertices_y[vi];
        float oz = (float)model->original_vertices_z[vi];

        float bx = 0.0f, by = 0.0f, bz = 0.0f;
        float total_weight = 0.0f;

        for( int j = 0; j < cnt; j++ )
        {
            int bone   = model->animaya_groups[vi] ? (int)model->animaya_groups[vi][j] : 0;
            int weight = model->animaya_scales[vi]  ? (int)model->animaya_scales[vi][j]  : 0;
            if( bone < 0 || bone >= skeletal->bone_count || weight == 0 )
                continue;

            /* Column-major mat4 multiply: out = M * [ox, oy, oz, 1] */
            const float* M = &frame_matrices[bone * 16];
            float w = (float)weight;
            bx += w * (M[0]*ox + M[4]*oy + M[8]*oz  + M[12]);
            by += w * (M[1]*ox + M[5]*oy + M[9]*oz  + M[13]);
            bz += w * (M[2]*ox + M[6]*oy + M[10]*oz + M[14]);
            total_weight += w;
        }

        if( total_weight > 0.0f )
        {
            model->vertices_x[vi] = (vertexint_t)(bx / total_weight);
            model->vertices_y[vi] = (vertexint_t)(by / total_weight);
            model->vertices_z[vi] = (vertexint_t)(bz / total_weight);
        }
        else
        {
            model->vertices_x[vi] = (vertexint_t)ox;
            model->vertices_y[vi] = (vertexint_t)oy;
            model->vertices_z[vi] = (vertexint_t)oz;
        }
    }
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
        if( tex->animation_direction == TORIDRAW_TEXANIM_DIRECTION_NONE || tex->animation_speed == 0 )
            continue;

        int const pixel_count = tex->width * tex->height;
        if( pixel_count <= 0 || pixel_count > (int)(sizeof(scratch) / sizeof(scratch[0])) )
            continue;

        ToriDraw_TextureAnimate(tex, cycles, scratch);
    }
}
