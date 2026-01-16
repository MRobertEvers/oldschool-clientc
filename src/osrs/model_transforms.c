#include "model_transforms.h"

#include "graphics/lighting.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

void
model_transform_mirror(struct CacheModel* model)
{
    int* vertices_z_alias = model->vertices_z;

    int* face_indices_a_alias = model->face_indices_a;
    int* face_indices_c_alias = model->face_indices_c;

    for( int v = 0; v < model->vertex_count; v++ )
    {
        vertices_z_alias[v] = -vertices_z_alias[v];
    }

    for( int f = 0; f < model->face_count; f++ )
    {
        int temp = face_indices_a_alias[f];
        face_indices_a_alias[f] = face_indices_c_alias[f];
        face_indices_c_alias[f] = temp;
    }
}

void
model_transform_hillskew(
    struct CacheModel* model,
    int sw_height,
    int se_height,
    int ne_height,
    int nw_height)
{
    int average_height = (sw_height + se_height + ne_height + nw_height) / 4;

    for( int i = 0; i < model->vertex_count; i++ )
    {
        int x = model->vertices_x[i];
        int z = model->vertices_z[i];

        int s_height = sw_height + (se_height - sw_height) * (x + 64) / 128;
        int n_height = nw_height + (ne_height - nw_height) * (x + 64) / 128;
        int y = s_height + (n_height - s_height) * (z + 64) / 128;

        model->vertices_y[i] += y - average_height;
    }
}

void
model_transform_recolor(
    struct CacheModel* model,
    int color_src,
    int color_dst)
{
    int* face_colors_alias = model->face_colors;

    // This check is present in the deob. Some locs specify recolors on models without face_colors.
    if( !face_colors_alias )
        return;

    for( int f = 0; f < model->face_count; f++ )
    {
        if( face_colors_alias[f] == color_src )
            face_colors_alias[f] = color_dst;
    }
}

void
model_transform_rotate(struct CacheModel* model)
{
    // TODO: Implement
}

void
model_transform_retexture(
    struct CacheModel* model,
    int texture_src,
    int texture_dst)
{
    int* face_textures_alias = model->face_textures;

    // This check is present in the deob. Some locs specify retextures on models without
    // face_textures.
    if( !face_textures_alias )
        return;

    for( int f = 0; f < model->face_count; f++ )
    {
        if( face_textures_alias[f] == texture_src )
            face_textures_alias[f] = texture_dst;
    }
}

void
model_transform_scale(
    struct CacheModel* model,
    int x,
    int z,
    int height)
{
    int* vertices_x_alias = model->vertices_x;
    int* vertices_y_alias = model->vertices_y;
    int* vertices_z_alias = model->vertices_z;

    for( int i = 0; i < model->vertex_count; i++ )
    {
        vertices_x_alias[i] = vertices_x_alias[i] * x / 128;
        vertices_y_alias[i] = vertices_y_alias[i] * height / 128;
        vertices_z_alias[i] = vertices_z_alias[i] * z / 128;
    }
}

void
model_transform_orient(
    struct CacheModel* model,
    int orientation)
{
    orientation &= 3;
    while( orientation-- > 0 )
    {
        // This is a counter clockwise rotation. 1536.
        for( int v = 0; v < model->vertex_count; v++ )
        {
            int tmp = model->vertices_x[v];
            model->vertices_x[v] = model->vertices_z[v];
            model->vertices_z[v] = -tmp;
        }
    }
}

void
model_transform_translate(
    struct CacheModel* model,
    int x,
    int y,
    int z)
{
    int* vertices_x_alias = model->vertices_x;
    int* vertices_y_alias = model->vertices_y;
    int* vertices_z_alias = model->vertices_z;

    for( int i = 0; i < model->vertex_count; i++ )
    {
        vertices_x_alias[i] += x;
        vertices_y_alias[i] += y;
        vertices_z_alias[i] += z;
    }
}
