#include "world_decode_tile.h"

#include "toridraw/toridraw_model.h"
#include "toridraw/toridraw_types.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OVERLAY_HSL_LIGHTNESS_ONLY -1
#define OVERLAY_HSL_TRANSPARENT -2
#define UNDERLAY_HSL_NONE -1

enum VertexType
{
    VERTEX_TYPE_SW = 1,
    VERTEX_TYPE_SOUTHEDGE_MIDDLE = 2,
    VERTEX_TYPE_SE = 3,
    VERTEX_TYPE_EASTEDGE_MIDDLE = 4,
    VERTEX_TYPE_NE = 5,
    VERTEX_TYPE_NORTHEDGE_MIDDLE = 6,
    VERTEX_TYPE_NW = 7,
    VERTEX_TYPE_WESTEDGE_MIDDLE = 8,
    VERTEX_TYPE_SOUTH_QUARTER_MIDLINE = 9,
    VERTEX_TYPE_EAST_QUARTER_MIDLINE = 10,
    VERTEX_TYPE_NORTH_QUARTER_MIDLINE = 11,
    VERTEX_TYPE_WEST_QUARTER_MIDLINE = 12,
    VERTEX_TYPE_SW_QUARTER_CENTER = 13,
    VERTEX_TYPE_SE_QUARTER_CENTER = 14,
    VERTEX_TYPE_NE_QUARTER_CENTER = 15,
    VERTEX_TYPE_NW_QUARTER_CENTER = 16,
};

/**
 * These specify the subtile position on the x-z plane for the tile vertices.
 */
static int g_tile_shape_vertex_types[15][6] = {
    { 1, 3, 5, 7 },
    { 1, 3, 5, 7 }, // PLAIN_SHAPE
    { 1, 3, 5, 7 }, // DIAGONAL_SHAPE
    { 1, 3, 5, 7, 6 }, // LEFT_SEMI_DIAGONAL_SMALL_SHAPE
    { 1, 3, 5, 7, 6 }, // RIGHT_SEMI_DIAGONAL_SMALL_SHAPE
    { 1, 3, 5, 7, 6 }, // LEFT_SEMI_DIAGONAL_BIG_SHAPE
    { 1, 3, 5, 7, 6 }, // RIGHT_SEMI_DIAGONAL_BIG_SHAPE
    { 1, 3, 5, 7, 2, 6 }, // HALF_SQUARE_SHAPE
    { 1, 3, 5, 7, 2, 8 }, // CORNER_SMALL_SHAPE
    { 1, 3, 5, 7, 2, 8 }, // CORNER_BIG_SHAPE
    { 1, 3, 5, 7, 11, 12 }, // FAN_SMALL_SHAPE
    { 1, 3, 5, 7, 11, 12 }, // FAN_BIG_SHAPE
    { 1, 3, 5, 7, 13, 14 }  // TRAPEZIUM_SHAPE
};

static int g_tile_shape_vertex_types_lengths[15] = {
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

static void
tile_calculate_bounds_cylinder(
    struct ToriDraw_BoundsCylinder* bounds_cylinder,
    int num_vertices,
    vertexint_t* vertex_x,
    vertexint_t* vertex_y,
    vertexint_t* vertex_z)
{
    memset(bounds_cylinder, 0, sizeof(struct ToriDraw_BoundsCylinder));

    int min_y = INT_MAX;
    int max_y = INT_MIN;
    int radius_squared = 0;

    for( int i = 0; i < num_vertices; i++ )
    {
        int x = (int)vertex_x[i];
        int y = (int)vertex_y[i];
        int z = (int)vertex_z[i];
        if( y < min_y )
            min_y = y;
        if( y > max_y )
            max_y = y;
        int radius_squared_vertex = x * x + z * z;
        if( radius_squared_vertex > radius_squared )
            radius_squared = radius_squared_vertex;
    }

    int center_to_bottom_edge = (int)sqrt(radius_squared + min_y * min_y) + 1;
    int center_to_top_edge = (int)sqrt(radius_squared + max_y * max_y) + 1;
    bounds_cylinder->center_to_bottom_edge = center_to_bottom_edge;
    bounds_cylinder->center_to_top_edge = center_to_top_edge;
    bounds_cylinder->min_y = min_y;
    bounds_cylinder->max_y = max_y;
    bounds_cylinder->radius = (int)sqrt(radius_squared);
    bounds_cylinder->min_z_depth_any_rotation =
        center_to_top_edge > center_to_bottom_edge ? center_to_top_edge : center_to_bottom_edge;
}

int
terrain_multiply_lightness(
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

int
terrain_adjust_lightness(
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
    if( hsl_a == INVALID_HSL_COLOR || hsl_b == INVALID_HSL_COLOR )
        return INVALID_HSL_COLOR;

    if( hsl_a == INVALID_HSL_COLOR )
        return hsl_b;
    else if( hsl_b == INVALID_HSL_COLOR )
        return hsl_a;

    int hue = (hsl_a >> 10) & 0x3f;
    int saturation = (hsl_a >> 7) & 0x7;
    int lightness = hsl_a & 0x7f;

    int hue_b = (hsl_b >> 10) & 0x3f;
    int saturation_b = (hsl_b >> 7) & 0x7;
    int lightness_b = hsl_b & 0x7f;

    hue += hue_b;
    saturation += saturation_b;
    lightness += lightness_b;

    hue >>= 1;
    saturation >>= 1;
    lightness >>= 1;

    return (hue << 10) + (saturation << 7) + lightness;
}

// /Users/matthewevers/Documents/git_repos/meteor-client/osrs/src/main/java/SceneTileModel.java
static struct ToriDraw_Model*
decode_tile(
    int shape,
    int rotation,
    int texture_id,
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

    int* vertex_indices = g_tile_shape_vertex_types[shape];
    int vertex_count = g_tile_shape_vertex_types_lengths[shape];

    vertexint_t* vertices_x = (vertexint_t*)malloc((size_t)vertex_count * sizeof(vertexint_t));
    vertexint_t* vertices_y = (vertexint_t*)malloc((size_t)vertex_count * sizeof(vertexint_t));
    vertexint_t* vertices_z = (vertexint_t*)malloc((size_t)vertex_count * sizeof(vertexint_t));

    int* underlay_colors_hsl = (int*)malloc(vertex_count * sizeof(int));
    int* overlay_colors_hsl = (int*)malloc(vertex_count * sizeof(int));

    int underlay_hsl_sw = terrain_multiply_lightness(underlay_hsl16, light_sw);
    int underlay_hsl_se = terrain_multiply_lightness(underlay_hsl16, light_se);
    int underlay_hsl_ne = terrain_multiply_lightness(underlay_hsl16, light_ne);
    int underlay_hsl_nw = terrain_multiply_lightness(underlay_hsl16, light_nw);

    // Assumes the overlay hsl is simply lightness.
    // If overlay_hsl16 is -1, then these are just lightnesses.
    int overlay_hsl_sw = terrain_adjust_lightness(overlay_hsl16, light_sw);
    int overlay_hsl_se = terrain_adjust_lightness(overlay_hsl16, light_se);
    int overlay_hsl_ne = terrain_adjust_lightness(overlay_hsl16, light_ne);
    int overlay_hsl_nw = terrain_adjust_lightness(overlay_hsl16, light_nw);

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

    faceint_t* faces_a = (faceint_t*)malloc(face_count * sizeof(faceint_t));
    faceint_t* faces_b = (faceint_t*)malloc(face_count * sizeof(faceint_t));
    faceint_t* faces_c = (faceint_t*)malloc(face_count * sizeof(faceint_t));

    int* valid_faces = (int*)malloc(face_count * sizeof(int));

    hsl16_t* face_colors_hsl_a = (hsl16_t*)malloc(face_count * sizeof(hsl16_t));
    hsl16_t* face_colors_hsl_b = (hsl16_t*)malloc(face_count * sizeof(hsl16_t));
    hsl16_t* face_colors_hsl_c = (hsl16_t*)malloc(face_count * sizeof(hsl16_t));

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

        face_colors_hsl_a[i] = (hsl16_t)(unsigned)(color_a & 0xffff);
        face_colors_hsl_b[i] = (hsl16_t)(unsigned)(color_b & 0xffff);
        face_colors_hsl_c[i] = (hsl16_t)(unsigned)(color_c & 0xffff);

        if( color_a == INVALID_HSL_COLOR && face_texture_id == -1 )
        {
            valid_faces[i] = 0;
            continue;
        }
        else if(
            color_a == INVALID_HSL_COLOR || color_b == INVALID_HSL_COLOR ||
            color_c == INVALID_HSL_COLOR )
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

    struct ToriDraw_Model* td = calloc(1, sizeof(struct ToriDraw_Model));
    if( !td )
    {
        free(valid_faces);
        free(face_colors_hsl_a);
        free(face_colors_hsl_b);
        free(face_colors_hsl_c);
        free(face_texture_ids);
        free(vertices_x);
        free(vertices_y);
        free(vertices_z);
        free(faces_a);
        free(faces_b);
        free(faces_c);
        return NULL;
    }

    td->vertex_count = vertex_count;
    td->vertices_x = vertices_x;
    td->vertices_y = vertices_y;
    td->vertices_z = vertices_z;

    td->face_count = face_count;
    td->face_indices_a = faces_a;
    td->face_indices_b = faces_b;
    td->face_indices_c = faces_c;

    for( int i = 0; i < face_count; i++ )
    {
        if( !valid_faces[i] )
            face_colors_hsl_c[i] = TORIDRAWHSL16_HIDDEN;
    }

    td->face_colors_a = face_colors_hsl_a;
    td->face_colors_b = face_colors_hsl_b;
    td->face_colors_c = face_colors_hsl_c;

    if( face_texture_ids )
    {
        td->face_textures = (faceint_t*)calloc(face_count, sizeof(faceint_t));
        for( int i = 0; i < face_count; i++ )
            td->face_textures[i] = face_texture_ids[i];
        free(face_texture_ids);

        td->flags |= 0x02u;

        /* Tile overlay UVs use SW/SE/NW corners (vertices 0,1,3), not per-face corners. */
        td->textured_face_count = 1;
        td->textured_p_coordinate = (faceint_t*)malloc(sizeof(faceint_t));
        td->textured_m_coordinate = (faceint_t*)malloc(sizeof(faceint_t));
        td->textured_n_coordinate = (faceint_t*)malloc(sizeof(faceint_t));
        td->face_texture_coords = (faceint_t*)calloc((size_t)face_count, sizeof(faceint_t));
        if( !td->textured_p_coordinate || !td->textured_m_coordinate ||
            !td->textured_n_coordinate || !td->face_texture_coords )
        {
            toridraw_model_free(td);
            return NULL;
        }
        td->textured_p_coordinate[0] = 0;
        td->textured_m_coordinate[0] = 1;
        td->textured_n_coordinate[0] = 3;
    }

    td->flags |= 0x01u;

    td->bounds_cylinder = calloc(1, sizeof(struct ToriDraw_BoundsCylinder));
    if( !td->bounds_cylinder )
    {
        toridraw_model_free(td);
        return NULL;
    }
    tile_calculate_bounds_cylinder(
        td->bounds_cylinder, vertex_count, vertices_x, vertices_y, vertices_z);

    free(valid_faces);

    return td;
}

struct ToriDraw_Model*
world_decode_tile(
    int shape,
    int rotation,
    int texture_id,
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
    return decode_tile(
        shape,
        rotation,
        texture_id,
        height_sw,
        height_se,
        height_ne,
        height_nw,
        light_sw,
        light_se,
        light_ne,
        light_nw,
        underlay_hsl16,
        overlay_hsl16);
}