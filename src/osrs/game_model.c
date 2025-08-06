#include "game_model.h"

#include "lighting.h"

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
    struct CacheModel* model, int sw_height, int se_height, int ne_height, int nw_height)
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
model_transform_recolor(struct CacheModel* model, int color_src, int color_dst)
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
model_transform_retexture(struct CacheModel* model, int texture_src, int texture_dst)
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
model_transform_scale(struct CacheModel* model, int x, int y, int z)
{
    int* vertices_x_alias = model->vertices_x;
    int* vertices_y_alias = model->vertices_y;
    int* vertices_z_alias = model->vertices_z;

    for( int i = 0; i < model->vertex_count; i++ )
    {
        vertices_x_alias[i] = vertices_x_alias[i] * x / 128;
        vertices_y_alias[i] = vertices_y_alias[i] * y / 128;
        vertices_z_alias[i] = vertices_z_alias[i] * z / 128;
    }
}

void
model_transform_orient(struct CacheModel* model, int orientation)
{
    while( orientation-- > 0 )
    {
        for( int v = 0; v < model->vertex_count; v++ )
        {
            int tmp = model->vertices_x[v];
            model->vertices_x[v] = model->vertices_z[v];
            model->vertices_z[v] = -tmp;
        }
    }
}

void
model_transform_translate(struct CacheModel* model, int x, int y, int z)
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

enum BlendMode
{
    BLEND_VERTEX = 0,
    BLEND_FACE = 1,
    HIDDEN_FACE = 2,
    FLAT_BLACK = 3,
};

// src/main/java/jagex3/dash3d/ModelLit.java
enum LightingMode
model_face_lighting_mode(struct CacheModel* model, int face)
{
    if( model->_id == 2080 )
    {
        int iii = 0;
    }

    int alpha = 0;
    if( model->face_alphas )
        alpha = model->face_alphas[face];

    enum BlendMode blend = BLEND_VERTEX;
    switch( alpha )
    {
    case -2:
        blend = FLAT_BLACK;
        break;
    case -1:
        blend = HIDDEN_FACE;
        break;
    default:
    {
        if( model->face_infos )
            blend = model->face_infos[face] & 0x3;
        break;
    }
    }

    int texture_id = model->face_textures ? model->face_textures[face] : -1;

    if( texture_id == -1 )
    {
        switch( blend )
        {
        case BLEND_VERTEX:
            return LM_TEXTURED_VERTEX;
        case BLEND_FACE:
            return LM_TEXTURED_FACE;
        case FLAT_BLACK:
            return LM_TEXTURED_FLAT_BLACK;
        case HIDDEN_FACE:
            /**
             * For textures, these are usually PNM face coordinates.
             *
             */
            return LM_HIDDEN_FACE;
        }
    }
    else
    {
        switch( blend )
        {
        case BLEND_VERTEX:
            return LM_VERTEX;
        case BLEND_FACE:
            return LM_FACE;
        case FLAT_BLACK:
        case HIDDEN_FACE:
            return LM_HIDDEN_FACE;
        }
    }

    assert(0);
    return LM_HIDDEN_FACE;
}

enum FaceDrawMode
model_face_draw_mode(struct CacheModel* model, int face)
{
    if( model->face_textures && model->face_textures[face] != -1 )
    {
        if( model->face_texture_coords && model->face_texture_coords[face] != -1 )
            return DRAW_TEXTURED_PMN;
        else
            return DRAW_TEXTURED;
    }
    else
    {
        if( model->face_infos && model->face_infos[face] & 0x3 == 0 )
            return DRAW_FLAT;
    }
}

// void
// model_apply_lighting(
//     struct CacheModel* model,
//     int face,
//     int* face_colors_a_hsl16,
//     int* face_colors_b_hsl16,
//     int* face_colors_c_hsl16)
// {
//     struct VertexNormal* vertex_normals = NULL;

//     int light_ambient = 64;
//     int light_attenuation = 850;
//     int lightsrc_x = -30;
//     int lightsrc_y = -50;
//     int lightsrc_z = -30;
//     int light_magnitude =
//         (int)sqrt(lightsrc_x * lightsrc_x + lightsrc_y * lightsrc_y + lightsrc_z * lightsrc_z);
//     int attenuation = light_attenuation * light_magnitude >> 8;

//     enum LightingMode lighting_mode = 0;
//     for( int face = 0; face < model->face_count; face++ )
//     {
//         lighting_mode = model_face_lighting_mode(model, face);

//         switch( lighting_mode )
//         {
//         case LM_TEXTURED_VERTEX:
//         {
//             vertex_normals =
//                 (struct VertexNormal*)malloc(model->vertex_count * sizeof(struct VertexNormal));
//             memset(vertex_normals, 0, model->vertex_count * sizeof(struct VertexNormal));

//             calculate_vertex_normals(
//                 vertex_normals,
//                 model->vertex_count,
//                 model->face_indices_a,
//                 model->face_indices_b,
//                 model->face_indices_c,
//                 model->vertices_x,
//                 model->vertices_y,
//                 model->vertices_z,
//                 model->face_count);

//             apply_lighting(
//                 face_colors_a_hsl16,
//                 face_colors_b_hsl16,
//                 face_colors_c_hsl16,
//                 vertex_normals,
//                 model->face_indices_a,
//                 model->face_indices_b,
//                 model->face_indices_c,
//                 model->face_count,
//                 model->vertices_x,
//                 model->vertices_y,
//                 model->vertices_z,
//                 model->face_colors,
//                 model->face_infos,
//                 light_ambient,
//                 attenuation,
//                 lightsrc_x,
//                 lightsrc_y,
//                 lightsrc_z);

//             free(vertex_normals);
//         }
//         break;
//         case LM_TEXTURED_FACE:
//             // TODO:
//             break;
//         case LM_TEXTURED_FLAT_BLACK:
//             break;
//         case LM_VERTEX:
//             break;
//         case LM_FACE:
//             break;
//         case LM_HIDDEN_FACE:
//             break;
//         }
//     }
// }