#include "anim.h"

#include <string.h>

extern int g_cos_table[2048];
extern int g_sin_table[2048];
extern int g_tan_table[2048];

static void
animate(
    struct Transformation* transformation,
    int type,
    int frame_id,
    /**
     * Frame map contains indexes of vertex groups.
     * [0] => Vertex Group 4
     * [1] => Vertex Group 5
     * [2] => Vertex Group 9 etc.
     */
    int* vertex_group,
    int vertex_groups_length,
    int arg_x,
    int arg_y,
    int arg_z,
    int* vertices_x,
    int* vertices_y,
    int* vertices_z)
{
    switch( type )
    {
    case 0:
    {
        int avg_x = 0;
        int avg_y = 0;
        int avg_z = 0;
        int count = 0;

        int* face_indices = vertex_group;
        int face_indices_length = vertex_groups_length;

        for( int j = 0; j < face_indices_length; ++j )
        {
            int face_index = face_indices[j];
            avg_x += vertices_x[face_index];
            avg_y += vertices_y[face_index];
            avg_z += vertices_z[face_index];
            ++count;
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
    case 1:
    {
        int* face_indices = vertex_group;
        int face_indices_length = vertex_groups_length;

        for( int j = 0; j < face_indices_length; ++j )
        {
            int face_index = face_indices[j];
            vertices_x[face_index] += arg_x;
            vertices_y[face_index] += arg_y;
            vertices_z[face_index] += arg_z;
        }
        break;
    }
    case 2:
    {
        int* face_indices = vertex_group;
        int face_indices_length = vertex_groups_length;

        for( int j = 0; j < face_indices_length; ++j )
        {
            int face_index = face_indices[j];
            vertices_x[face_index] -= transformation->origin_x;
            vertices_y[face_index] -= transformation->origin_y;
            vertices_z[face_index] -= transformation->origin_z;
            int pitch = (arg_x & 255) * 8;
            int yaw = (arg_y & 255) * 8;
            int roll = (arg_z & 255) * 8;
            int var17;
            if( roll != 0 )
            {
                int sin_roll = g_sin_table[roll];
                int cos_roll = g_cos_table[roll];
                var17 = sin_roll * vertices_y[face_index] + cos_roll * vertices_x[face_index] >> 16;
                vertices_y[face_index] =
                    cos_roll * vertices_y[face_index] - sin_roll * vertices_x[face_index] >> 16;
                vertices_x[face_index] = var17;
            }

            if( pitch != 0 )
            {
                int sin_pitch = g_sin_table[pitch];
                int cos_pitch = g_cos_table[pitch];
                var17 =
                    (cos_pitch * vertices_y[face_index] - sin_pitch * vertices_z[face_index]) >> 16;
                vertices_z[face_index] =
                    (sin_pitch * vertices_y[face_index] + cos_pitch * vertices_z[face_index]) >> 16;
                vertices_y[face_index] = var17;
            }

            if( yaw != 0 )
            {
                int sin_yaw = g_sin_table[yaw];
                int cos_yaw = g_cos_table[yaw];
                var17 = (sin_yaw * vertices_z[face_index] + cos_yaw * vertices_x[face_index]) >> 16;
                vertices_z[face_index] =
                    (cos_yaw * vertices_z[face_index] - sin_yaw * vertices_x[face_index]) >> 16;
                vertices_x[face_index] = var17;
            }

            vertices_x[face_index] += transformation->origin_x;
            vertices_y[face_index] += transformation->origin_y;
            vertices_z[face_index] += transformation->origin_z;
        }
    }
    case 3:
    {
        break;
        int* face_indices = vertex_group;
        int face_indices_length = vertex_groups_length;

        for( int j = 0; j < face_indices_length; ++j )
        {
            int face_index = face_indices[j];
            vertices_x[face_index] -= transformation->origin_x;
            vertices_y[face_index] -= transformation->origin_y;
            vertices_z[face_index] -= transformation->origin_z;
            vertices_x[face_index] = arg_x * vertices_x[face_index] / 128;
            vertices_y[face_index] = arg_y * vertices_y[face_index] / 128;
            vertices_z[face_index] = arg_z * vertices_z[face_index] / 128;
            vertices_x[face_index] += transformation->origin_x;
            vertices_y[face_index] += transformation->origin_y;
            vertices_z[face_index] += transformation->origin_z;
        }
    }
    case 5:
        // TODO: Alpha
        break;
    }
}

int
anim_frame_apply(struct FrameDefinition* frame, int* vertices_x, int* vertices_y, int* vertices_z)
{
    struct Transformation transformation = { 0 };
    for( int i = 0; i < frame->translator_count; i++ )
    {
        memset(&transformation, 0, sizeof(struct Transformation));

        int x = frame->translator_arg_x[i];
        int y = frame->translator_arg_y[i];
        int z = frame->translator_arg_z[i];

        int index = frame->index_frame_ids[i];
        for( int j = 0; j < frame->framemap->length; j++ )
        {
            int* bones = frame->framemap->vertex_groups[index];
            int bones_length = frame->framemap->vertex_group_lengths[index];

            int type = frame->framemap->types[index];

            int frame_id = 0;

            // cache/src/main/java/net/runelite/cache/definitions/ModelDefinition.java
            animate(
                &transformation,
                type,
                frame_id,
                bones,
                bones_length,
                x,
                y,
                z,
                vertices_x,
                vertices_y,
                vertices_z);
        }
    }
}
