#include "anim.h"

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
    int* frame_map,
    int frame_map_length,
    int** vertex_groups,
    int* vertex_groups_lengths,
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

        for( int i = 0; i < frame_map_length; ++i )
        {
            int vertex_group_index = frame_map[i];
            if( vertex_group_index < vertex_groups_length )
            {
                int* face_indices = vertex_groups[vertex_group_index];
                int face_indices_length = vertex_groups_lengths[vertex_group_index];

                for( int j = 0; j < face_indices_length; ++j )
                {
                    int face_index = face_indices[j];
                    avg_x += vertices_x[face_index];
                    avg_y += vertices_y[face_index];
                    avg_z += vertices_z[face_index];
                    ++count;
                }
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
    case 1:
    {
        for( int i = 0; i < frame_map_length; ++i )
        {
            int vertex_group_index = frame_map[i];
            if( vertex_group_index < vertex_groups_length )
            {
                int* face_indices = vertex_groups[vertex_group_index];
                int face_indices_length = vertex_groups_lengths[vertex_group_index];

                for( int j = 0; j < face_indices_length; ++j )
                {
                    int face_index = face_indices[j];
                    vertices_x[face_index] += arg_x;
                    vertices_y[face_index] += arg_y;
                    vertices_z[face_index] += arg_z;
                }
            }
        }
        break;
    }
    case 2:
    {
        for( int i = 0; i < frame_map_length; ++i )
        {
            int vertex_group_index = frame_map[i];
            if( vertex_group_index < vertex_groups_length )
            {
                int* face_indices = vertex_groups[vertex_group_index];
                int face_indices_length = vertex_groups_lengths[vertex_group_index];

                for( int j = 0; j < face_indices_length; ++j )
                {
                    int face_index = face_indices[j];
                    vertices_x[face_index] -= transformation->origin_x;
                    vertices_y[face_index] -= transformation->origin_y;
                    vertices_z[face_index] -= transformation->origin_z;
                    int var12 = (arg_x & 255) * 8;
                    int var13 = (arg_y & 255) * 8;
                    int var14 = (arg_z & 255) * 8;
                    int var15;
                    int var16;
                    int var17;
                    if( var14 != 0 )
                    {
                        var15 = g_sin_table[var14];
                        var16 = g_cos_table[var14];
                        var17 =
                            var15 * vertices_y[face_index] + var16 * vertices_x[face_index] >> 16;
                        vertices_y[face_index] =
                            var16 * vertices_y[face_index] - var15 * vertices_x[face_index] >> 16;
                        vertices_x[face_index] = var17;
                    }

                    if( var12 != 0 )
                    {
                        var15 = g_sin_table[var12];
                        var16 = g_cos_table[var12];
                        var17 =
                            (var16 * vertices_y[face_index] - var15 * vertices_z[face_index]) >> 16;
                        vertices_z[face_index] =
                            (var15 * vertices_y[face_index] + var16 * vertices_z[face_index]) >> 16;
                        vertices_y[face_index] = var17;
                    }

                    if( var13 != 0 )
                    {
                        var15 = g_sin_table[var13];
                        var16 = g_cos_table[var13];
                        var17 =
                            (var15 * vertices_z[face_index] + var16 * vertices_x[face_index]) >> 16;
                        vertices_z[face_index] =
                            (var16 * vertices_z[face_index] - var15 * vertices_x[face_index]) >> 16;
                        vertices_x[face_index] = var17;
                    }

                    vertices_x[face_index] += arg_x;
                    vertices_y[face_index] += arg_y;
                    vertices_z[face_index] += arg_z;
                }
            }
        }
    }
    case 3:
    {
        for( int i = 0; i < frame_map_length; ++i )
        {
            int vertex_group_index = frame_map[i];
            if( vertex_group_index < vertex_groups_length )
            {
                int* face_indices = vertex_groups[vertex_group_index];
                int face_indices_length = vertex_groups_lengths[vertex_group_index];

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
    for( int i = 0; i < frame->framemap->length; i++ )
    {
        animate(
            &transformation,
            frame->framemap->types[i],
            0,
            frame->index_frame_ids,
            frame->translator_count,
            frame->framemap->vertex_groups,
            frame->framemap->vertex_group_lengths,
            frame->framemap->length,
            frame->translator_arg_x[i],
            frame->translator_arg_y[i],
            frame->translator_arg_z[i],
            vertices_x,
            vertices_y,
            vertices_z);
    }
}
