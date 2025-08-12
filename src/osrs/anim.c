#include "anim.h"

#include <string.h>

extern int g_cos_table[2048];
extern int g_sin_table[2048];
extern int g_tan_table[2048];

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
    int* bone_group,
    int bone_group_length,
    int arg_x,
    int arg_y,
    int arg_z,
    int vertex_bones_count,
    int** vertex_bones,
    int* vertex_bones_sizes,
    int face_bones_count,
    int** face_bones,
    int* face_bones_sizes,
    int* vertices_x,
    int* vertices_y,
    int* vertices_z,
    int* face_alphas)
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
        int avg_x = 0;
        int avg_y = 0;
        int avg_z = 0;
        int count = 0;

        for( int i = 0; i < bone_group_length; i++ )
        {
            int bone_index = bone_group[i];
            if( bone_index >= vertex_bones_count )
                continue;

            int* bone = vertex_bones[bone_index];
            int bone_length = vertex_bones_sizes[bone_index];

            for( int j = 0; j < bone_length; j++ )
            {
                int face_index = bone[j];
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
        for( int i = 0; i < bone_group_length; i++ )
        {
            int bone_index = bone_group[i];
            if( bone_index >= vertex_bones_count )
                continue;

            int* bone = vertex_bones[bone_index];
            int bone_length = vertex_bones_sizes[bone_index];

            for( int j = 0; j < bone_length; ++j )
            {
                int vertex_index = bone[j];
                vertices_x[vertex_index] += arg_x;
                vertices_y[vertex_index] += arg_y;
                vertices_z[vertex_index] += arg_z;
            }
        }
        break;
    }
    // ROTATE
    case 2:
    {
        for( int i = 0; i < bone_group_length; i++ )
        {
            int bone_index = bone_group[i];
            if( bone_index >= vertex_bones_count )
                continue;

            int* bone = vertex_bones[bone_index];
            int bone_length = vertex_bones_sizes[bone_index];

            for( int j = 0; j < bone_length; ++j )
            {
                int vertex_index = bone[j];
                vertices_x[vertex_index] -= transformation->origin_x;
                vertices_y[vertex_index] -= transformation->origin_y;
                vertices_z[vertex_index] -= transformation->origin_z;
                int pitch = (arg_x & 255) * 8;
                int yaw = (arg_y & 255) * 8;
                int roll = (arg_z & 255) * 8;
                int var17;
                if( roll != 0 )
                {
                    int sin_roll = g_sin_table[roll];
                    int cos_roll = g_cos_table[roll];
                    var17 =
                        sin_roll * vertices_y[vertex_index] + cos_roll * vertices_x[vertex_index] >>
                        16;
                    vertices_y[vertex_index] = (cos_roll * vertices_y[vertex_index] -
                                                sin_roll * vertices_x[vertex_index]) >>
                                               16;
                    vertices_x[vertex_index] = var17;
                }

                if( pitch != 0 )
                {
                    int sin_pitch = g_sin_table[pitch];
                    int cos_pitch = g_cos_table[pitch];
                    var17 = (cos_pitch * vertices_y[vertex_index] -
                             sin_pitch * vertices_z[vertex_index]) >>
                            16;
                    vertices_z[vertex_index] = (sin_pitch * vertices_y[vertex_index] +
                                                cos_pitch * vertices_z[vertex_index]) >>
                                               16;
                    vertices_y[vertex_index] = var17;
                }

                if( yaw != 0 )
                {
                    int sin_yaw = g_sin_table[yaw];
                    int cos_yaw = g_cos_table[yaw];
                    var17 =
                        (sin_yaw * vertices_z[vertex_index] + cos_yaw * vertices_x[vertex_index]) >>
                        16;
                    vertices_z[vertex_index] =
                        (cos_yaw * vertices_z[vertex_index] - sin_yaw * vertices_x[vertex_index]) >>
                        16;
                    vertices_x[vertex_index] = var17;
                }

                vertices_x[vertex_index] += transformation->origin_x;
                vertices_y[vertex_index] += transformation->origin_y;
                vertices_z[vertex_index] += transformation->origin_z;
            }
        }
        break;
    }
    case 3:
    {
        for( int i = 0; i < bone_group_length; i++ )
        {
            int bone_index = bone_group[i];
            if( bone_index >= vertex_bones_count )
                continue;

            int* bone = vertex_bones[bone_index];
            int bone_length = vertex_bones_sizes[bone_index];

            for( int j = 0; j < bone_length; ++j )
            {
                int vertex_index = bone[j];
                vertices_x[vertex_index] -= transformation->origin_x;
                vertices_y[vertex_index] -= transformation->origin_y;
                vertices_z[vertex_index] -= transformation->origin_z;
                vertices_x[vertex_index] = arg_x * vertices_x[vertex_index] / 128;
                vertices_y[vertex_index] = arg_y * vertices_y[vertex_index] / 128;
                vertices_z[vertex_index] = arg_z * vertices_z[vertex_index] / 128;
                vertices_x[vertex_index] += transformation->origin_x;
                vertices_y[vertex_index] += transformation->origin_y;
                vertices_z[vertex_index] += transformation->origin_z;
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

            int* bone = face_bones[bone_index];
            int bone_length = face_bones_sizes[bone_index];

            for( int j = 0; j < bone_length; ++j )
            {
                int face_index = bone[j];

                face_alphas[face_index] += arg_x * 8;
                if( face_alphas[face_index] < 0 )
                    face_alphas[face_index] = 0;

                if( face_alphas[face_index] > 255 )
                    face_alphas[face_index] = 255;
            }
        }
    }
    break;
    }
}

int
anim_frame_apply(
    struct CacheFrame* frame,
    struct CacheFramemap* framemap,
    int* vertices_x,
    int* vertices_y,
    int* vertices_z,
    int* face_alphas,
    // These are the bones of the model. They are defined with the model.
    int vertex_bones_count,
    int** vertex_bones,
    int* vertex_bones_sizes,
    int face_bones_count,
    int** face_bones,
    int* face_bones_sizes)
{
    struct Transformation transformation = { 0 };
    for( int i = 0; i < frame->translator_count; i++ )
    {
        int x = frame->translator_arg_x[i];
        int y = frame->translator_arg_y[i];
        int z = frame->translator_arg_z[i];

        int index = frame->index_frame_ids[i];

        int* bone_group = framemap->bone_groups[index];
        int bone_group_length = framemap->bone_groups_lengths[index];
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
