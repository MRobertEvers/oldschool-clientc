#ifndef TERRAIN_DECODE_TILE_U_C
#define TERRAIN_DECODE_TILE_U_C

#include "graphics/dash.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tile_shape_vertex_indices[15][6] = {
    { 1, 3, 5, 7 },
    { 1, 3, 5, 7 },
    { 1, 3, 5, 7 },
    { 1, 3, 5, 7, 6 },
    { 1, 3, 5, 7, 6 },
    { 1, 3, 5, 7, 6 },
    { 1, 3, 5, 7, 6 },
    { 1, 3, 5, 7, 2, 6 },
    { 1, 3, 5, 7, 2, 8 },
    { 1, 3, 5, 7, 2, 8 },
    { 1, 3, 5, 7, 11, 12 },
    { 1, 3, 5, 7, 11, 12 },
    { 1, 3, 5, 7, 13, 14 },
};

static int g_tile_shape_vertex_indices_lengths[15] = {
    4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6,
};

static int g_tile_shape_faces[15][25] = {
    { 0, 1, 2, 3, 0, 0, 1, 3 },
    { 1, 1, 2, 3, 1, 0, 1, 3 },
    { 0, 1, 2, 3, 1, 0, 1, 3 },
    { 0, 0, 1, 2, 0, 0, 2, 4, 1, 0, 4, 3 },
    { 0, 0, 1, 4, 0, 0, 4, 3, 1, 1, 2, 4 },
    { 0, 0, 4, 3, 1, 0, 1, 2, 1, 0, 2, 4 },
    { 0, 1, 2, 4, 1, 0, 1, 4, 1, 0, 4, 3 },
    { 0, 4, 1, 2, 0, 4, 2, 5, 1, 0, 4, 5, 1, 0, 5, 3 },
    { 0, 4, 1, 2, 0, 4, 2, 3, 0, 4, 3, 5, 1, 0, 4, 5 },
    { 0, 0, 4, 5, 1, 4, 1, 2, 1, 4, 2, 3, 1, 4, 3, 5 },
    { 0, 0, 1, 5, 0, 1, 4, 5, 0, 1, 2, 4, 1, 0, 5, 3, 1, 5, 4, 3, 1, 4, 2, 3 },
    { 1, 0, 1, 5, 1, 1, 4, 5, 1, 1, 2, 4, 0, 0, 5, 3, 0, 5, 4, 3, 0, 4, 2, 3 },
    { 1, 0, 5, 4, 1, 0, 1, 5, 0, 0, 4, 3, 0, 4, 5, 3, 0, 5, 2, 3, 0, 1, 2, 5 },
};

static int g_tile_shape_face_counts[15] = {
    8, 8, 8, 12, 12, 12, 12, 16, 16, 16, 24, 24, 24,
};

#define TILE_SIZE 128
#define LEVEL_HEIGHT 240
#define INVALID_HSL_COLOR 12345678

static int
multiply_lightness(
    int hsl,
    int light)
{
    if( hsl == -1 )
        return INVALID_HSL_COLOR;

    light = ((hsl & 127) * light) >> 7;
    if( light < 2 )
        light = 2;
    else if( light > 126 )
        light = 126;

    return (hsl & 0xff80) + light;
}

// export function adjustOverlayLight(hsl: number, light: number) {
//     if (hsl === -2) {
//         return INVALID_HSL_COLOR;
//     } else if (hsl === -1) {
//         if (light < 2) {
//             light = 2;
//         } else if (light > 126) {
//             light = 126;
//         }

//         return light;
//     } else {
//         light = ((hsl & 127) * light) >> 7;
//         if (light < 2) {
//             light = 2;
//         } else if (light > 126) {
//             light = 126;
//         }

//         return (hsl & 0xff80) + light;
//     }
// }

static int
adjust_lightness(
    int hsl,
    int light)
{
    if( hsl == -2 )
        return INVALID_HSL_COLOR;

    if( hsl == -1 )
    {
        if( light < 2 )
            light = 2;
        else if( light > 126 )
            light = 126;

        return light;
    }

    light = ((hsl & 127) * light) >> 7;
    if( light < 2 )
        light = 2;
    else if( light > 126 )
        light = 126;

    return (hsl & 0xff80) + light;
}

// export function mixHsl(hslA: number, hslB: number): number {
//     if (hslA === INVALID_HSL_COLOR || hslB === INVALID_HSL_COLOR) {
//         return INVALID_HSL_COLOR;
//     }
//     if (hslA === -1) {
//         return hslB;
//     } else if (hslB === -1) {
//         return hslA;
//     } else {
//         let hue = (hslA >> 10) & 0x3f;
//         let saturation = (hslA >> 7) & 0x7;
//         let lightness = hslA & 0x7f;

//         let hueB = (hslB >> 10) & 0x3f;
//         let saturationB = (hslB >> 7) & 0x7;
//         let lightnessB = hslB & 0x7f;

//         hue += hueB;
//         saturation += saturationB;
//         lightness += lightnessB;

//         hue >>= 1;
//         saturation >>= 1;
//         lightness >>= 1;

//         return (hue << 10) + (saturation << 7) + lightness;
//     }
// }

static int
mix_hsl(
    int hsl_a,
    int hsl_b)
{
    return (hsl_a + hsl_b) >> 1;
    // if( hsl_a == INVALID_HSL_COLOR || hsl_b == INVALID_HSL_COLOR )
    //     return INVALID_HSL_COLOR;

    // if( hsl_a == INVALID_HSL_COLOR )
    //     return hsl_b;
    // else if( hsl_b == INVALID_HSL_COLOR )
    //     return hsl_a;

    // int hue = (hsl_a >> 10) & 0x3f;
    // int saturation = (hsl_a >> 7) & 0x7;
    // int lightness = hsl_a & 0x7f;

    // int hue_b = (hsl_b >> 10) & 0x3f;
    // int saturation_b = (hsl_b >> 7) & 0x7;
    // int lightness_b = hsl_b & 0x7f;

    // hue += hue_b;
    // saturation += saturation_b;
    // lightness += lightness_b;

    // hue >>= 1;
    // saturation >>= 1;
    // lightness >>= 1;

    // return (hue << 10) + (saturation << 7) + lightness;
}

// /Users/matthewevers/Documents/git_repos/meteor-client/osrs/src/main/java/SceneTileModel.java
static struct DashModel*
decode_tile(
    int shape,
    int rotation,
    int texture_id,
    int tile_coord_x,
    int tile_coord_z,
    int tile_coord_level,
    int height_sw,
    int height_se,
    int height_ne,
    int height_nw,
    int light_sw,
    int light_se,
    int light_ne,
    int light_nw,
    int underlay_hsl16,
    int overlay_hsl16)
{
    // memset(tile, 0, sizeof(struct SceneTile));
    int tile_x = 0; // tile_coord_x * TILE_SIZE;
    int tile_z = 0; // tile_coord_z * TILE_SIZE;

    int* vertex_indices = g_tile_shape_vertex_indices[shape];
    int vertex_count = g_tile_shape_vertex_indices_lengths[shape];

    int* vertices_x = (int*)malloc(vertex_count * sizeof(int));
    int* vertices_y = (int*)malloc(vertex_count * sizeof(int));
    int* vertices_z = (int*)malloc(vertex_count * sizeof(int));

    int* underlay_colors_hsl = (int*)malloc(vertex_count * sizeof(int));
    int* overlay_colors_hsl = (int*)malloc(vertex_count * sizeof(int));

    // Assumes the overlay hsl is simply lightness.
    int underlay_hsl_sw = multiply_lightness(underlay_hsl16, light_sw);
    int underlay_hsl_se = multiply_lightness(underlay_hsl16, light_se);
    int underlay_hsl_ne = multiply_lightness(underlay_hsl16, light_ne);
    int underlay_hsl_nw = multiply_lightness(underlay_hsl16, light_nw);

    // Assumes the overlay hsl is simply lightness.
    int overlay_hsl_sw = adjust_lightness(overlay_hsl16, light_sw);
    int overlay_hsl_se = adjust_lightness(overlay_hsl16, light_se);
    int overlay_hsl_ne = adjust_lightness(overlay_hsl16, light_ne);
    int overlay_hsl_nw = adjust_lightness(overlay_hsl16, light_nw);

    for( int i = 0; i < vertex_count; i++ )
    {
        int vertex_type = vertex_indices[i];
        if( (vertex_type & 1) == 0 && vertex_type <= 8 )
        {
            vertex_type = ((vertex_type - rotation - rotation - 1) & 7) + 1;
        }

        if( vertex_type > 8 && vertex_type <= 12 )
        {
            vertex_type = ((vertex_type - 9 - rotation) & 3) + 9;
        }

        if( vertex_type > 12 && vertex_type <= 16 )
        {
            vertex_type = ((vertex_type - 13 - rotation) & 3) + 13;
        }

        int vert_x = 0;
        int vert_z = 0;
        int vert_y = 0;
        int vert_underlay_color_hsl = underlay_hsl_sw;
        int vert_overlay_color_hsl = overlay_hsl_sw;

        if( vertex_type == 1 )
        {
            vert_x = tile_x;
            vert_z = tile_z;
            vert_y = height_sw;

            vert_underlay_color_hsl = underlay_hsl_sw;
            vert_overlay_color_hsl = overlay_hsl_sw;
        }
        else if( vertex_type == 2 )
        {
            vert_x = tile_x + TILE_SIZE / 2;
            vert_z = tile_z;
            vert_y = (height_se + height_sw) >> 1;
            // vert_y = height_sw;

            vert_underlay_color_hsl = mix_hsl(underlay_hsl_se, underlay_hsl_sw);
            vert_overlay_color_hsl = (overlay_hsl_se + overlay_hsl_sw) >> 1;
        }
        else if( vertex_type == 3 )
        {
            vert_x = tile_x + TILE_SIZE;
            vert_z = tile_z;
            vert_y = height_se;

            vert_underlay_color_hsl = underlay_hsl_se;
            vert_overlay_color_hsl = overlay_hsl_se;
        }
        else if( vertex_type == 4 )
        {
            vert_x = tile_x + TILE_SIZE;
            vert_z = tile_z + TILE_SIZE / 2;
            vert_y = (height_ne + height_se) >> 1;

            vert_underlay_color_hsl = mix_hsl(underlay_hsl_se, underlay_hsl_ne);
            vert_overlay_color_hsl = (overlay_hsl_ne + overlay_hsl_se) >> 1;
        }
        else if( vertex_type == 5 )
        {
            vert_x = tile_x + TILE_SIZE;
            vert_z = tile_z + TILE_SIZE;
            vert_y = height_ne;

            vert_underlay_color_hsl = underlay_hsl_ne;
            vert_overlay_color_hsl = overlay_hsl_ne;
        }
        else if( vertex_type == 6 )
        {
            vert_x = tile_x + TILE_SIZE / 2;
            vert_z = tile_z + TILE_SIZE;
            vert_y = (height_ne + height_nw) >> 1;
            // vert_y = height_sw;

            vert_underlay_color_hsl = mix_hsl(underlay_hsl_ne, underlay_hsl_nw);
            vert_overlay_color_hsl = (overlay_hsl_ne + overlay_hsl_nw) >> 1;
        }
        else if( vertex_type == 7 )
        {
            vert_x = tile_x;
            vert_z = tile_z + TILE_SIZE;
            vert_y = height_nw;

            vert_underlay_color_hsl = underlay_hsl_nw;
            vert_overlay_color_hsl = overlay_hsl_nw;
        }
        else if( vertex_type == 8 )
        {
            vert_x = tile_x;
            vert_z = tile_z + TILE_SIZE / 2;
            vert_y = (height_nw + height_sw) >> 1;
            // vert_y = height_sw;

            vert_underlay_color_hsl = mix_hsl(underlay_hsl_nw, underlay_hsl_sw);
            vert_overlay_color_hsl = (overlay_hsl_nw + overlay_hsl_sw) >> 1;
        }
        else if( vertex_type == 9 )
        {
            vert_x = tile_x + TILE_SIZE / 2;
            vert_z = tile_z + TILE_SIZE / 4;
            vert_y = (height_sw + height_se) >> 1;

            vert_underlay_color_hsl = mix_hsl(underlay_hsl_sw, underlay_hsl_se);
            vert_overlay_color_hsl = (overlay_hsl_sw + overlay_hsl_se) >> 1;
        }
        else if( vertex_type == 10 )
        {
            vert_x = tile_x + TILE_SIZE * 3 / 4;
            vert_z = tile_z + TILE_SIZE / 2;
            vert_y = (height_se + height_ne) >> 1;

            vert_underlay_color_hsl = mix_hsl(underlay_hsl_se, underlay_hsl_ne);
            vert_overlay_color_hsl = (overlay_hsl_se + overlay_hsl_ne) >> 1;
        }
        else if( vertex_type == 11 )
        {
            vert_x = tile_x + TILE_SIZE / 2;
            vert_z = tile_z + TILE_SIZE * 3 / 4;

            vert_y = (height_ne + height_nw) >> 1;

            vert_underlay_color_hsl = mix_hsl(underlay_hsl_ne, underlay_hsl_nw);
            vert_overlay_color_hsl = (overlay_hsl_ne + overlay_hsl_nw) >> 1;
        }
        else if( vertex_type == 12 )
        {
            vert_x = tile_x + TILE_SIZE / 4;
            vert_z = tile_z + TILE_SIZE / 2;
            vert_y = (height_nw + height_sw) >> 1;

            vert_underlay_color_hsl = mix_hsl(underlay_hsl_nw, underlay_hsl_sw);
            vert_overlay_color_hsl = (overlay_hsl_nw + overlay_hsl_sw) >> 1;
        }
        else if( vertex_type == 13 )
        {
            vert_x = tile_x + TILE_SIZE / 4;
            vert_z = tile_z + TILE_SIZE / 4;
            vert_y = height_sw;

            vert_underlay_color_hsl = underlay_hsl_sw;
            vert_overlay_color_hsl = overlay_hsl_sw;
        }
        else if( vertex_type == 14 )
        {
            vert_x = tile_x + TILE_SIZE * 3 / 4;
            vert_z = tile_z + TILE_SIZE / 4;
            vert_y = height_se;

            vert_underlay_color_hsl = underlay_hsl_se;
            vert_overlay_color_hsl = overlay_hsl_se;
        }
        else if( vertex_type == 15 )
        {
            vert_x = tile_x + TILE_SIZE * 3 / 4;
            vert_z = tile_z + TILE_SIZE * 3 / 4;
            vert_y = height_ne;

            vert_underlay_color_hsl = underlay_hsl_ne;
            vert_overlay_color_hsl = overlay_hsl_ne;
        }
        else
        {
            vert_x = tile_x + TILE_SIZE / 4;
            vert_z = tile_z + TILE_SIZE * 3 / 4;
            vert_y = height_nw;

            vert_underlay_color_hsl = underlay_hsl_nw;
            vert_overlay_color_hsl = overlay_hsl_nw;
        }

        // TODO: For the target level, subtract LEVEL_HEIGHT from the vertex y.
        vertices_x[i] = vert_x;
        vertices_y[i] = vert_y;
        vertices_z[i] = vert_z;

        underlay_colors_hsl[i] = vert_underlay_color_hsl;
        overlay_colors_hsl[i] = vert_overlay_color_hsl;
    }

    int* face_indices = g_tile_shape_faces[shape];
    int face_count = g_tile_shape_face_counts[shape] / 4;

    int* faces_a = (int*)malloc(face_count * sizeof(int));
    int* faces_b = (int*)malloc(face_count * sizeof(int));
    int* faces_c = (int*)malloc(face_count * sizeof(int));

    int* valid_faces = (int*)malloc(face_count * sizeof(int));

    int* face_colors_hsl_a = (int*)malloc(face_count * sizeof(int));
    int* face_colors_hsl_b = (int*)malloc(face_count * sizeof(int));
    int* face_colors_hsl_c = (int*)malloc(face_count * sizeof(int));

    int* face_texture_ids = NULL;
    // int* face_texture_u_a = NULL;
    // int* face_texture_v_a = NULL;
    // int* face_texture_u_b = NULL;
    // int* face_texture_v_b = NULL;
    // int* face_texture_u_c = NULL;
    // int* face_texture_v_c = NULL;
    if( texture_id != -1 )
    {
        face_texture_ids = (int*)malloc(face_count * sizeof(int));
        //     face_texture_u_a = (int*)malloc(face_count * sizeof(int));
        //     face_texture_v_a = (int*)malloc(face_count * sizeof(int));
        //     face_texture_u_b = (int*)malloc(face_count * sizeof(int));
        //     face_texture_v_b = (int*)malloc(face_count * sizeof(int));
        //     face_texture_u_c = (int*)malloc(face_count * sizeof(int));
        //     face_texture_v_c = (int*)malloc(face_count * sizeof(int));
    }

    for( int i = 0; i < face_count; i++ )
    {
        bool is_overlay = face_indices[i * 4] == 1;
        int a = face_indices[i * 4 + 1];
        int b = face_indices[i * 4 + 2];
        int c = face_indices[i * 4 + 3];

        if( a < 4 )
            a = (a - rotation) & 3;

        if( b < 4 )
            b = (b - rotation) & 3;

        if( c < 4 )
            c = (c - rotation) & 3;

        faces_a[i] = a;
        faces_b[i] = b;
        faces_c[i] = c;

        int color_a;
        int color_b;
        int color_c;
        int face_texture_id = -1;

        if( is_overlay )
        {
            color_a = overlay_colors_hsl[a];
            color_b = overlay_colors_hsl[b];
            color_c = overlay_colors_hsl[c];

            face_texture_id = texture_id;
            if( texture_id != -1 )
                face_texture_ids[i] = texture_id;
        }
        else
        {
            color_a = underlay_colors_hsl[a];
            color_b = underlay_colors_hsl[b];
            color_c = underlay_colors_hsl[c];

            face_texture_id = -1;
            if( texture_id != -1 )
                face_texture_ids[i] = -1;
        }

        face_colors_hsl_a[i] = color_a;
        face_colors_hsl_b[i] = color_b;
        face_colors_hsl_c[i] = color_c;

        // TODO: Skip texture rendering right now

        if( color_a == INVALID_HSL_COLOR && face_texture_id == -1 )
        {
            valid_faces[i] = 0;
            continue;
        }
        else
            valid_faces[i] = 1;

        // int u0 = (vertex_x[a] - tile_x) / TILE_SIZE;
        // int v0 = (vertex_z[a] - tile_z) / TILE_SIZE;

        // int u1 = (vertex_x[b] - tile_x) / TILE_SIZE;
        // int v1 = (vertex_z[b] - tile_z) / TILE_SIZE;

        // int u2 = (vertex_x[c] - tile_x) / TILE_SIZE;
        // int v2 = (vertex_z[c] - tile_z) / TILE_SIZE;

        // if( texture_id != -1 )
        // {
        //     face_texture_u_a[i] = u0;
        //     face_texture_v_a[i] = v0;

        //     face_texture_u_b[i] = u1;
        //     face_texture_v_b[i] = v1;

        //     face_texture_u_c[i] = u2;
        //     face_texture_v_c[i] = v2;
        // }
    }

    free(underlay_colors_hsl);
    free(overlay_colors_hsl);

    struct DashModel* dash_model = (struct DashModel*)malloc(sizeof(struct DashModel));
    memset(dash_model, 0, sizeof(struct DashModel));

    dash_model->vertex_count = vertex_count;
    dash_model->vertices_x = vertices_x;
    dash_model->vertices_y = vertices_y;
    dash_model->vertices_z = vertices_z;
    dash_model->face_count = face_count;
    dash_model->face_indices_a = faces_a;
    dash_model->face_indices_b = faces_b;
    dash_model->face_indices_c = faces_c;

    dash_model->face_textures = face_texture_ids;

    dash_model->face_alphas = valid_faces;
    dash_model->face_colors = face_colors_hsl_a;

    dash_model->lighting = dashmodel_lighting_new(face_count);
    dash_model->lighting->face_colors_hsl_a = face_colors_hsl_a;
    dash_model->lighting->face_colors_hsl_b = face_colors_hsl_b;
    dash_model->lighting->face_colors_hsl_c = face_colors_hsl_c;

    dash_model->bounds_cylinder = dashmodel_bounds_cylinder_new();

    dash3d_calculate_bounds_cylinder(
        dash_model->bounds_cylinder, vertex_count, vertices_x, vertices_y, vertices_z);

    return dash_model;
    // error:;
    free(valid_faces);
    free(vertices_x);
    free(vertices_y);
    free(vertices_z);
    free(underlay_colors_hsl);
    free(overlay_colors_hsl);
    free(faces_a);
    free(faces_b);
    free(faces_c);
    free(face_colors_hsl_a);
    free(face_colors_hsl_b);
    free(face_colors_hsl_c);
    free(face_texture_ids);
    return NULL;
}

#endif