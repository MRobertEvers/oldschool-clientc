#include "toridraw_model.h"

#include "osrs/palette.h"
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
toridraw_anim_vertexint_clamp(int v)
{
    return (vertexint_t)v;
}

static void
toridraw_anim_apply_transform(
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
                    toridraw_anim_vertexint_clamp((int)vertices_x[vertex_index] + arg_x);
                vertices_y[vertex_index] =
                    toridraw_anim_vertexint_clamp((int)vertices_y[vertex_index] + arg_y);
                vertices_z[vertex_index] =
                    toridraw_anim_vertexint_clamp((int)vertices_z[vertex_index] + arg_z);
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
                    int sin_roll = toridraw_sin(roll);
                    int cos_roll = toridraw_cos(roll);
                    var17 = (sin_roll * y + cos_roll * x) >> 16;
                    y = (cos_roll * y - sin_roll * x) >> 16;
                    x = var17;
                }

                if( pitch != 0 )
                {
                    int sin_pitch = toridraw_sin(pitch);
                    int cos_pitch = toridraw_cos(pitch);
                    var17 = (cos_pitch * y - sin_pitch * z) >> 16;
                    z = (sin_pitch * y + cos_pitch * z) >> 16;
                    y = var17;
                }

                if( yaw != 0 )
                {
                    int sin_yaw = toridraw_sin(yaw);
                    int cos_yaw = toridraw_cos(yaw);
                    var17 = (sin_yaw * z + cos_yaw * x) >> 16;
                    z = (cos_yaw * z - sin_yaw * x) >> 16;
                    x = var17;
                }

                vertices_x[vertex_index] = toridraw_anim_vertexint_clamp(x + transform->origin_x);
                vertices_y[vertex_index] = toridraw_anim_vertexint_clamp(y + transform->origin_y);
                vertices_z[vertex_index] = toridraw_anim_vertexint_clamp(z + transform->origin_z);
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
                vertices_x[vertex_index] = toridraw_anim_vertexint_clamp(x + transform->origin_x);
                vertices_y[vertex_index] = toridraw_anim_vertexint_clamp(y + transform->origin_y);
                vertices_z[vertex_index] = toridraw_anim_vertexint_clamp(z + transform->origin_z);
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
toridraw_normals_new(
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
toridraw_normals_free(struct ToriDraw_Normals* normals)
{
    if( !normals )
        return;
    free(normals->vertex_normals);
    free(normals->face_normals);
    free(normals);
}

void
toridraw_bones_free(struct ToriDraw_Bones* bones)
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
toridraw_bones_copy(const struct ToriDraw_Bones* src)
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
        toridraw_bones_free(dst);
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
            toridraw_bones_free(dst);
            return NULL;
        }
        memcpy(dst->bones[i], src->bones[i], (size_t)bone_length * sizeof(boneint_t));
    }

    return dst;
}

static void
toridraw_model_free_arrays(struct ToriDraw_Model* m)
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
    toridraw_normals_free(m->normals);
    toridraw_normals_free(m->merged_normals);
    toridraw_bones_free(m->vertex_bones);
    toridraw_bones_free(m->face_bones);
    free(m->bounds_cylinder);
}

void
toridraw_model_free(struct ToriDraw_Model* model)
{
    if( !model )
        return;
    toridraw_model_free_arrays(model);
    free(model);
}

static int
toridraw_model_needs_face_normals(struct ToriDraw_Model* model)
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
toridraw_model_alloc_normals(struct ToriDraw_Model* model)
{
    assert(model);
    if( model->normals )
        return;
    int face_n = toridraw_model_needs_face_normals(model) ? model->face_count : 0;
    model->normals = toridraw_normals_new(model->vertex_count, face_n);
}

void
toridraw_model_alloc_merged_normals(struct ToriDraw_Model* model)
{
    assert(model);
    if( model->merged_normals )
        return;
    model->merged_normals = toridraw_normals_new(model->vertex_count, 0);
}

void
toridraw_model_calculate_vertex_normals(struct ToriDraw_Model* model)
{
    assert(model);
    toridraw_model_alloc_normals(model);
    struct ToriDraw_Normals* nm = model->normals;
    assert(nm);

    int vc = model->vertex_count;
    int fc = model->face_count;
    toridraw_calculate_vertex_normals(
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
toridraw_model_free_normals(struct ToriDraw_Model* model)
{
    assert(model);
    if( !model->normals )
        return;
    toridraw_normals_free(model->normals);
    toridraw_normals_free(model->merged_normals);
    model->normals = NULL;
    model->merged_normals = NULL;
}

void
toridraw_context_set_texture(
    struct ToriDraw_Context* ctx,
    int id,
    struct ToriDraw_Texture* texture)
{
    if( !ctx || id < 0 || id >= 256 )
        return;

    struct ToriDraw_TextureMap* map = &ctx->texture_map;
    struct ToriDraw_Texture* const old = map->textures[id];

    if( old == texture )
        return;

    if( old )
    {
        toridraw_eventqueue_push(&ctx->events, TORIDRAW_EVENT_TEX_UNLOAD, id, NULL);
        toridraw_texture_free(old);
        map->textures[id] = NULL;
    }

    if( texture )
    {
        map->textures[id] = texture;
        toridraw_eventqueue_push(&ctx->events, TORIDRAW_EVENT_TEX_LOAD, id, texture);
        if( id >= map->count )
            map->count = id + 1;
    }
    else if( id == map->count - 1 )
    {
        while( map->count > 0 && !map->textures[map->count - 1] )
            map->count--;
    }
}

void
toridraw_model_capture_original_vertices(struct ToriDraw_Model* model)
{
    if( !model || !model->vertices_x || model->vertex_count <= 0 )
        return;
    if( model->original_vertices_x )
        return;

    size_t const vc = (size_t)model->vertex_count;
    model->original_vertices_x = malloc(vc * sizeof(vertexint_t));
    model->original_vertices_y = malloc(vc * sizeof(vertexint_t));
    model->original_vertices_z = malloc(vc * sizeof(vertexint_t));
    if( !model->original_vertices_x || !model->original_vertices_y || !model->original_vertices_z )
    {
        free(model->original_vertices_x);
        free(model->original_vertices_y);
        free(model->original_vertices_z);
        model->original_vertices_x = NULL;
        model->original_vertices_y = NULL;
        model->original_vertices_z = NULL;
        return;
    }

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
toridraw_model_animate_reset(struct ToriDraw_Model* model)
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
toridraw_model_animate_frame(
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

        toridraw_anim_apply_transform(
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
toridraw_texture_average_hsl16(const struct ToriDraw_Texture* texture)
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
