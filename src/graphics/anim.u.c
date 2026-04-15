#ifndef ANIM_U_C
#define ANIM_U_C

#include "dash_alphaint.h"
#include "dash_anim.h"
#include "dash_boneint.h"
#include "dash_vertexint.h"

#include <stdint.h>
#include <string.h>

extern int g_cos_table[2048];
extern int g_sin_table[2048];
extern int g_tan_table[2048];

struct Transformation
{
    int origin_x;
    int origin_y;
    int origin_z;
};

/** Store animated vertex coords in int16 without relying on impl-defined int16 overflow. */
static inline vertexint_t
anim_vertexint_clamp(int v)
{
    // if( v > INT16_MAX )
    //     return (vertexint_t)INT16_MAX;
    // if( v < INT16_MIN )
    //     return (vertexint_t)INT16_MIN;
    return (vertexint_t)v;
}

static void
animate(
    struct Transformation* transformation,
    int type,
    /**
     * Frame map contains indexes of vertex groups.
     * [0] => Bone Group 4
     * [1] => Bone Group 5
     * [2] => Bone Group 9 etc.
     */
    const uint8_t* bone_group,
    int bone_group_length,
    int arg_x,
    int arg_y,
    int arg_z,
    int vertex_bones_count,
    boneint_t** vertex_bones,
    boneint_t* vertex_bones_sizes,
    int face_bones_count,
    boneint_t** face_bones,
    boneint_t* face_bones_sizes,
    vertexint_t* vertices_x,
    vertexint_t* vertices_y,
    vertexint_t* vertices_z,
    alphaint_t* face_alphas)
{
    // src/rs/model/seq/SeqTransformType.ts
    // export enum SeqTransformType {
    //     ORIGIN = 0,
    //     TRANSLATE = 1,
    //     ROTATE = 2,
    //     SCALE = 3,
    //     ALPHA = 5,
    //     TYPE_6 = 6,
    //     LIGHT = 7,
    //     TYPE_10 = 10,
    // }

    switch( type )
    {
        // ORIGIN
    case 0:
    {
        if( !vertex_bones || !vertex_bones_sizes )
            return;

        int avg_x = 0;
        int avg_y = 0;
        int avg_z = 0;
        int count = 0;

        for( int i = 0; i < bone_group_length; i++ )
        {
            int bone_index = bone_group[i];
            if( bone_index >= vertex_bones_count )
                continue;

            boneint_t* bone = vertex_bones[bone_index];
            int bone_length = vertex_bones_sizes[bone_index];

            for( int j = 0; j < bone_length; j++ )
            {
                boneint_t face_index = bone[j];
                avg_x += vertices_x[face_index];
                avg_y += vertices_y[face_index];
                avg_z += vertices_z[face_index];
                count++;
            }
        }

        if( count > 0 )
        {
            transformation->origin_x = arg_x + avg_x / count;
            transformation->origin_y = arg_y + avg_y / count;
            transformation->origin_z = arg_z + avg_z / count;
        }
        else
        {
            transformation->origin_x = arg_x;
            transformation->origin_y = arg_y;
            transformation->origin_z = arg_z;
        }
        break;
    }
    // TRANSLATE
    case 1:
    {
        if( !vertex_bones || !vertex_bones_sizes )
            return;

        for( int i = 0; i < bone_group_length; i++ )
        {
            int bone_index = bone_group[i];
            if( bone_index >= vertex_bones_count )
                continue;

            boneint_t* bone = vertex_bones[bone_index];
            int bone_length = vertex_bones_sizes[bone_index];

            for( int j = 0; j < bone_length; ++j )
            {
                int vertex_index = bone[j];
                int vx = (int)vertices_x[vertex_index] + arg_x;
                int vy = (int)vertices_y[vertex_index] + arg_y;
                int vz = (int)vertices_z[vertex_index] + arg_z;
                vertices_x[vertex_index] = anim_vertexint_clamp(vx);
                vertices_y[vertex_index] = anim_vertexint_clamp(vy);
                vertices_z[vertex_index] = anim_vertexint_clamp(vz);
            }
        }
        break;
    }
    // ROTATE
    case 2:
    {
        if( !vertex_bones || !vertex_bones_sizes )
            return;

        for( int i = 0; i < bone_group_length; i++ )
        {
            int bone_index = bone_group[i];
            if( bone_index >= vertex_bones_count )
                continue;

            boneint_t* bone = vertex_bones[bone_index];
            int bone_length = vertex_bones_sizes[bone_index];

            for( int j = 0; j < bone_length; ++j )
            {
                int vertex_index = bone[j];
                int x = (int)vertices_x[vertex_index] - transformation->origin_x;
                int y = (int)vertices_y[vertex_index] - transformation->origin_y;
                int z = (int)vertices_z[vertex_index] - transformation->origin_z;
                int pitch = (arg_x & 255) * 8;
                int yaw = (arg_y & 255) * 8;
                int roll = (arg_z & 255) * 8;
                int var17;
                if( roll != 0 )
                {
                    int sin_roll = g_sin_table[roll];
                    int cos_roll = g_cos_table[roll];
                    var17 = (sin_roll * y + cos_roll * x) >> 16;
                    y = (cos_roll * y - sin_roll * x) >> 16;
                    x = var17;
                }

                if( pitch != 0 )
                {
                    int sin_pitch = g_sin_table[pitch];
                    int cos_pitch = g_cos_table[pitch];
                    var17 = (cos_pitch * y - sin_pitch * z) >> 16;
                    z = (sin_pitch * y + cos_pitch * z) >> 16;
                    y = var17;
                }

                if( yaw != 0 )
                {
                    int sin_yaw = g_sin_table[yaw];
                    int cos_yaw = g_cos_table[yaw];
                    var17 = (sin_yaw * z + cos_yaw * x) >> 16;
                    z = (cos_yaw * z - sin_yaw * x) >> 16;
                    x = var17;
                }

                vertices_x[vertex_index] = anim_vertexint_clamp(x + transformation->origin_x);
                vertices_y[vertex_index] = anim_vertexint_clamp(y + transformation->origin_y);
                vertices_z[vertex_index] = anim_vertexint_clamp(z + transformation->origin_z);
            }
        }
        break;
    }
    case 3:
    {
        if( !vertex_bones || !vertex_bones_sizes )
            return;

        for( int i = 0; i < bone_group_length; i++ )
        {
            int bone_index = bone_group[i];
            if( bone_index >= vertex_bones_count )
                continue;

            boneint_t* bone = vertex_bones[bone_index];
            int bone_length = vertex_bones_sizes[bone_index];

            for( int j = 0; j < bone_length; ++j )
            {
                int vertex_index = bone[j];
                int x = (int)vertices_x[vertex_index] - transformation->origin_x;
                int y = (int)vertices_y[vertex_index] - transformation->origin_y;
                int z = (int)vertices_z[vertex_index] - transformation->origin_z;
                x = arg_x * x / 128;
                y = arg_y * y / 128;
                z = arg_z * z / 128;
                vertices_x[vertex_index] = anim_vertexint_clamp(x + transformation->origin_x);
                vertices_y[vertex_index] = anim_vertexint_clamp(y + transformation->origin_y);
                vertices_z[vertex_index] = anim_vertexint_clamp(z + transformation->origin_z);
            }
        }
        break;
    }
    // TODO: This actually uses face_labels rather than vertex_labels.
    case 5:
    {
        if( !face_alphas || !face_bones )
            return;

        for( int i = 0; i < bone_group_length; i++ )
        {
            int bone_index = bone_group[i];
            if( bone_index >= face_bones_count )
                continue;

            boneint_t* bone = face_bones[bone_index];
            int bone_length = face_bones_sizes[bone_index];

            for( int j = 0; j < bone_length; ++j )
            {
                int face_index = bone[j];

                int alpha = face_alphas[face_index];
                alpha += arg_x * 8;
                if( alpha < 0 )
                    alpha = 0;

                if( alpha > 255 )
                    alpha = 255;

                face_alphas[face_index] = alpha;
            }
        }
    }
    break;
    }
}

static inline void
anim_frame_apply(
    struct DashFrame* frame,
    struct DashFramemap* framemap,
    vertexint_t* vertices_x,
    vertexint_t* vertices_y,
    vertexint_t* vertices_z,
    alphaint_t* face_alphas,
    int vertex_bones_count,
    boneint_t** vertex_bones,
    boneint_t* vertex_bones_sizes,
    int face_bones_count,
    boneint_t** face_bones,
    boneint_t* face_bones_sizes)
{
    struct Transformation transformation = { 0 };
    for( int i = 0; i < frame->translator_count; i++ )
    {
        int x = frame->translator_arg_x[i];
        int y = frame->translator_arg_y[i];
        int z = frame->translator_arg_z[i];

        int index = frame->index_frame_ids[i];

        uint8_t* bone_group = framemap->bone_groups[index];
        int bone_group_length = (int)framemap->bone_groups_lengths[index];
        int type = framemap->types[index];

        // cache/src/main/java/net/runelite/cache/definitions/ModelDefinition.java
        animate(
            &transformation,
            type,
            bone_group,
            bone_group_length,
            x,
            y,
            z,
            vertex_bones_count,
            vertex_bones,
            vertex_bones_sizes,
            face_bones_count,
            face_bones,
            face_bones_sizes,
            vertices_x,
            vertices_y,
            vertices_z,
            face_alphas);
    }
}

/* Client.ts Model.maskAnimate: blend primary and secondary frames using walkmerge mask.
 * Primary provides bones where base !== maskBase (or type 0). Secondary provides bones where base
 * === maskBase (or type 0). */
static inline void
anim_frame_apply_mask(
    struct DashFrame* primary_frame,
    struct DashFrame* secondary_frame,
    struct DashFramemap* framemap,
    int* walkmerge,
    vertexint_t* vertices_x,
    vertexint_t* vertices_y,
    vertexint_t* vertices_z,
    alphaint_t* face_alphas,
    int vertex_bones_count,
    boneint_t** vertex_bones,
    boneint_t* vertex_bones_sizes,
    int face_bones_count,
    boneint_t** face_bones,
    boneint_t* face_bones_sizes)
{
    struct Transformation transformation = { 0 };
    int walkmerge_len = 0;
    if( walkmerge )
    {
        while( walkmerge[walkmerge_len] != 9999999 )
            walkmerge_len++;
    }

    /* Primary: apply bones where base !== maskBase || type === 0 */
    int mask_idx = 0;
    int mask_base = walkmerge_len > 0 ? walkmerge[0] : 9999999;
    for( int i = 0; i < primary_frame->translator_count; i++ )
    {
        int base = primary_frame->index_frame_ids[i];
        while( walkmerge && mask_idx < walkmerge_len && base > mask_base )
            mask_base = walkmerge[++mask_idx];
        int type = base < framemap->length ? framemap->types[base] : 0;
        int use_primary = (base != mask_base || type == 0);
        if( !use_primary || base >= framemap->length )
            continue;

        uint8_t* bone_group = framemap->bone_groups[base];
        int bone_group_length = (int)framemap->bone_groups_lengths[base];
        animate(
            &transformation,
            type,
            bone_group,
            bone_group_length,
            primary_frame->translator_arg_x[i],
            primary_frame->translator_arg_y[i],
            primary_frame->translator_arg_z[i],
            vertex_bones_count,
            vertex_bones,
            vertex_bones_sizes,
            face_bones_count,
            face_bones,
            face_bones_sizes,
            vertices_x,
            vertices_y,
            vertices_z,
            face_alphas);
    }

    /* Secondary: apply bones where base === maskBase || type === 0 */
    transformation.origin_x = 0;
    transformation.origin_y = 0;
    transformation.origin_z = 0;
    mask_idx = 0;
    mask_base = walkmerge_len > 0 ? walkmerge[0] : 9999999;
    for( int i = 0; i < secondary_frame->translator_count; i++ )
    {
        int base = secondary_frame->index_frame_ids[i];
        while( walkmerge && mask_idx < walkmerge_len && base > mask_base )
            mask_base = walkmerge[++mask_idx];
        int type = base < framemap->length ? framemap->types[base] : 0;
        int use_secondary = (base == mask_base || type == 0);
        if( !use_secondary || base >= framemap->length )
            continue;

        uint8_t* bone_group = framemap->bone_groups[base];
        int bone_group_length = (int)framemap->bone_groups_lengths[base];
        animate(
            &transformation,
            type,
            bone_group,
            bone_group_length,
            secondary_frame->translator_arg_x[i],
            secondary_frame->translator_arg_y[i],
            secondary_frame->translator_arg_z[i],
            vertex_bones_count,
            vertex_bones,
            vertex_bones_sizes,
            face_bones_count,
            face_bones,
            face_bones_sizes,
            vertices_x,
            vertices_y,
            vertices_z,
            face_alphas);
    }
}

#endif