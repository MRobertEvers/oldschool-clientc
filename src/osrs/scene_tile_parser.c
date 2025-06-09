#include "blend_underlays.h"
#include "osrs/tables/config_floortype.h"
#include "palette.h"
#include "scene_tile.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Structure to match the binary format
typedef struct
{
    int32_t level;
    int32_t x;
    int32_t y;
    int32_t shape;
    int32_t rotation;
    int32_t textureId;
    int32_t underlayRgb;
    int32_t overlayRgb;
} TileMetadata;

struct SceneTile*
parse_tiles_data(const char* filename, int* tile_count)
{
    FILE* file = fopen(filename, "rb");
    if( !file )
    {
        fprintf(stderr, "Failed to open file: %s\n", filename);
        return NULL;
    }

    // Read number of tiles
    int32_t num_tiles;
    if( fread(&num_tiles, sizeof(int32_t), 1, file) != 1 )
    {
        fprintf(stderr, "Failed to read tile count\n");
        fclose(file);
        return NULL;
    }

    // Allocate array of SceneTiles
    struct SceneTile* tiles = (struct SceneTile*)malloc(num_tiles * sizeof(struct SceneTile));
    for( int i = 0; i < num_tiles; i++ )
        memset(&tiles[i], 0, sizeof(struct SceneTile));

    if( !tiles )
    {
        fprintf(stderr, "Failed to allocate memory for tiles\n");
        fclose(file);
        return NULL;
    }

    // Read each tile
    for( int i = 0; i < num_tiles; i++ )
    {
        // Read tile metadata
        TileMetadata metadata;
        if( fread(&metadata, sizeof(TileMetadata), 1, file) != 1 )
        {
            fprintf(stderr, "Failed to read tile metadata for tile %d\n", i);
            goto cleanup;
        }

        // Read vertex count
        int32_t vertex_count;
        if( fread(&vertex_count, sizeof(int32_t), 1, file) != 1 )
        {
            fprintf(stderr, "Failed to read vertex count for tile %d\n", i);
            goto cleanup;
        }

        // Allocate memory for vertices
        tiles[i].vertex_x = (int*)malloc(vertex_count * sizeof(int));
        tiles[i].vertex_y = (int*)malloc(vertex_count * sizeof(int));
        tiles[i].vertex_z = (int*)malloc(vertex_count * sizeof(int));

        if( !tiles[i].vertex_x || !tiles[i].vertex_y || !tiles[i].vertex_z )
        {
            fprintf(stderr, "Failed to allocate memory for vertices in tile %d\n", i);
            goto cleanup;
        }

        tiles[i].vertex_count = vertex_count;

        // Read vertices
        for( int j = 0; j < vertex_count; j++ )
        {
            int vertex_x, vertex_y, vertex_z;
            if( fread(&vertex_x, sizeof(int32_t), 1, file) != 1 ||
                fread(&vertex_y, sizeof(int32_t), 1, file) != 1 ||
                fread(&vertex_z, sizeof(int32_t), 1, file) != 1 )
            {
                fprintf(stderr, "Failed to read vertex data for tile %d, vertex %d\n", i, j);
                goto cleanup;
            }
            tiles[i].vertex_x[j] = vertex_x;
            tiles[i].vertex_y[j] = vertex_y;
            tiles[i].vertex_z[j] = vertex_z;
        }

        // Calculate face count (assuming 3 vertices per face)
        int face_count;
        fread(&face_count, sizeof(int32_t), 1, file);
        tiles[i].faces_a = (int*)malloc(face_count * sizeof(int));
        tiles[i].faces_b = (int*)malloc(face_count * sizeof(int));
        tiles[i].faces_c = (int*)malloc(face_count * sizeof(int));
        tiles[i].face_color_hsl = (int*)malloc(face_count * sizeof(int));

        if( !tiles[i].faces_a || !tiles[i].faces_b || !tiles[i].faces_c ||
            !tiles[i].face_color_hsl )
        {
            fprintf(stderr, "Failed to allocate memory for faces in tile %d\n", i);
            goto cleanup;
        }

        tiles[i].face_count = face_count;

        // Read face data
        for( int j = 0; j < face_count; j++ )
        {
            int face_a, face_b, face_c, face_color_hsl;
            if( fread(&face_a, sizeof(int32_t), 1, file) != 1 ||
                fread(&face_b, sizeof(int32_t), 1, file) != 1 ||
                fread(&face_c, sizeof(int32_t), 1, file) != 1 ||
                fread(&face_color_hsl, sizeof(int32_t), 1, file) != 1 )
            {
                fprintf(stderr, "Failed to read face data for tile %d, face %d\n", i, j);
                goto cleanup;
            }
            tiles[i].faces_a[j] = face_a;
            tiles[i].faces_b[j] = face_b;
            tiles[i].faces_c[j] = face_c;
            tiles[i].face_color_hsl[j] = face_color_hsl;
        }
    }

    *tile_count = num_tiles;
    fclose(file);
    return tiles;

cleanup:
    // Free all allocated memory
    for( int i = 0; i < num_tiles; i++ )
    {
        free(tiles[i].vertex_x);
        free(tiles[i].vertex_y);
        free(tiles[i].vertex_z);
        free(tiles[i].faces_a);
        free(tiles[i].faces_b);
        free(tiles[i].faces_c);
        free(tiles[i].face_color_hsl);
    }
    free(tiles);
    fclose(file);
    return NULL;
}

void
free_tiles(struct SceneTile* tiles, int tile_count)
{
    if( !tiles )
        return;

    for( int i = 0; i < tile_count; i++ )
    {
        free(tiles[i].vertex_x);
        free(tiles[i].vertex_y);
        free(tiles[i].vertex_z);
        free(tiles[i].faces_a);
        free(tiles[i].faces_b);
        free(tiles[i].faces_c);
        free(tiles[i].face_color_hsl);
    }
    free(tiles);
}

// TODO: Lookup table for overlay ids and underlay ids.

static int tile_shape_vertex_indices[15][6] = {
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

static int tile_shape_vertex_indices_lengths[15] = {
    4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6,
};

static int tile_shape_faces[15][25] = {
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

static int tile_shape_face_counts[15] = {
    8, 8, 8, 12, 12, 12, 12, 16, 16, 16, 24, 24, 24,
};

#define TILE_SIZE 128
#define LEVEL_HEIGHT 240

static int
get_index(int* ids, int count, int id)
{
    for( int i = 0; i < count; i++ )
    {
        if( ids[i] == id )
            return i;
    }
    return -1;
}

// export function adjustUnderlayLight(hsl: number, light: number) {
//     if (hsl === -1) {
//         return INVALID_HSL_COLOR;
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

#define INVALID_HSL_COLOR 12345678

static int
adjust_underlay_light(int hsl, int light)
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
adjust_overlay_light(int hsl, int light)
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
mix_hsl(int hsl_a, int hsl_b)
{
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

static bool
decode_tile(
    struct SceneTile* tile,
    int shape,
    int rotation,
    int tile_coord_x,
    int tile_coord_y,
    int tile_coord_z,
    int height_sw,
    int height_se,
    int height_ne,
    int height_nw,
    int light_sw,
    int light_se,
    int light_ne,
    int light_nw,
    int blended_underlay_hsl_sw,
    int blended_underlay_hsl_se,
    int blended_underlay_hsl_ne,
    int blended_underlay_hsl_nw,
    int overlay_hsl)
{
    // memset(tile, 0, sizeof(struct SceneTile));
    int tile_x = tile_coord_x * TILE_SIZE;
    int tile_y = tile_coord_y * TILE_SIZE;

    int* vertex_indices = tile_shape_vertex_indices[shape];
    int vertex_count = tile_shape_vertex_indices_lengths[shape];

    int* vertex_x = (int*)malloc(vertex_count * sizeof(int));
    int* vertex_y = (int*)malloc(vertex_count * sizeof(int));
    int* vertex_z = (int*)malloc(vertex_count * sizeof(int));

    int* underlay_colors_hsl = (int*)malloc(vertex_count * sizeof(int));
    int* overlay_colors_hsl = (int*)malloc(vertex_count * sizeof(int));

    blended_underlay_hsl_sw = adjust_underlay_light(blended_underlay_hsl_sw, light_sw);
    blended_underlay_hsl_se = adjust_underlay_light(blended_underlay_hsl_se, light_se);
    blended_underlay_hsl_ne = adjust_underlay_light(blended_underlay_hsl_ne, light_ne);
    blended_underlay_hsl_nw = adjust_underlay_light(blended_underlay_hsl_nw, light_nw);

    int overlay_hsl_sw = adjust_overlay_light(overlay_hsl, light_sw);
    int overlay_hsl_se = adjust_overlay_light(overlay_hsl, light_se);
    int overlay_hsl_ne = adjust_overlay_light(overlay_hsl, light_ne);
    int overlay_hsl_nw = adjust_overlay_light(overlay_hsl, light_nw);

    for( int i = 0; i < vertex_count; i++ )
    {
        int vertex_index = vertex_indices[i];
        if( (vertex_index & 1) == 0 && vertex_index <= 8 )
        {
            vertex_index = ((vertex_index - rotation - rotation - 1) & 7) + 1;
        }

        if( vertex_index > 8 && vertex_index <= 12 )
        {
            vertex_index = ((vertex_index - 9 - rotation) & 3) + 9;
        }

        if( vertex_index > 12 && vertex_index <= 16 )
        {
            vertex_index = ((vertex_index - 13 - rotation) & 3) + 13;
        }

        int vert_x = 0;
        int vert_z = 0;
        int vert_y = 0;
        int vert_underlay_color_hsl = blended_underlay_hsl_sw;
        int vert_overlay_color_hsl = overlay_hsl;

        if( vertex_index == 1 )
        {
            vert_x = tile_x;
            vert_z = tile_y;
            vert_y = height_sw;

            vert_underlay_color_hsl = blended_underlay_hsl_sw;
            vert_overlay_color_hsl = overlay_hsl_sw;
            // vert_underlay_color_hsl = blended_underlay_hsl_sw;
            // vert_overlay_color_hsl = overlay_hsl;
        }
        else if( vertex_index == 2 )
        {
            vert_x = tile_x + TILE_SIZE / 2;
            vert_z = tile_y;
            vert_y = (height_se + height_sw) >> 1;
            // vert_y = height_sw;

            vert_underlay_color_hsl = mix_hsl(blended_underlay_hsl_se, blended_underlay_hsl_sw);
            vert_overlay_color_hsl = (overlay_hsl_se + overlay_hsl_sw) >> 1;
        }
        else if( vertex_index == 3 )
        {
            vert_x = tile_x + TILE_SIZE;
            vert_z = tile_y;
            vert_y = height_se;

            vert_underlay_color_hsl = blended_underlay_hsl_se;
            vert_overlay_color_hsl = overlay_hsl_se;
        }
        else if( vertex_index == 4 )
        {
            vert_x = tile_x + TILE_SIZE;
            vert_z = tile_y + TILE_SIZE / 2;
            vert_y = (height_ne + height_se) >> 1;
            // vert_y = height_sw;

            vert_underlay_color_hsl = mix_hsl(blended_underlay_hsl_se, blended_underlay_hsl_ne);
            vert_overlay_color_hsl = (overlay_hsl_ne + overlay_hsl_se) >> 1;
        }
        else if( vertex_index == 5 )
        {
            vert_x = tile_x + TILE_SIZE;
            vert_z = tile_y + TILE_SIZE;
            vert_y = height_ne;

            vert_underlay_color_hsl = blended_underlay_hsl_ne;
            vert_overlay_color_hsl = overlay_hsl_ne;
        }
        else if( vertex_index == 6 )
        {
            vert_x = tile_x + TILE_SIZE / 2;
            vert_z = tile_y + TILE_SIZE;
            vert_y = (height_ne + height_nw) >> 1;
            // vert_y = height_sw;

            vert_underlay_color_hsl = mix_hsl(blended_underlay_hsl_ne, blended_underlay_hsl_nw);
            vert_overlay_color_hsl = (overlay_hsl_ne + overlay_hsl_nw) >> 1;
        }
        else if( vertex_index == 7 )
        {
            vert_x = tile_x;
            vert_z = tile_y + TILE_SIZE;
            vert_y = height_nw;

            vert_underlay_color_hsl = blended_underlay_hsl_nw;
            vert_overlay_color_hsl = overlay_hsl_nw;
        }
        else if( vertex_index == 8 )
        {
            vert_x = tile_x;
            vert_z = tile_y + TILE_SIZE / 2;
            vert_y = (height_nw + height_sw) >> 1;
            // vert_y = height_sw;

            vert_underlay_color_hsl = mix_hsl(blended_underlay_hsl_nw, blended_underlay_hsl_sw);
            vert_overlay_color_hsl = (overlay_hsl_nw + overlay_hsl_sw) >> 1;
        }
        else if( vertex_index == 9 )
        {
            vert_x = tile_x + TILE_SIZE / 2;
            vert_z = tile_y + TILE_SIZE / 4;
            vert_y = (height_sw + height_se) >> 1;

            vert_underlay_color_hsl = mix_hsl(blended_underlay_hsl_sw, blended_underlay_hsl_se);
            vert_overlay_color_hsl = (overlay_hsl_sw + overlay_hsl_se) >> 1;
        }
        else if( vertex_index == 10 )
        {
            vert_x = tile_x + TILE_SIZE * 3 / 4;
            vert_z = tile_y + TILE_SIZE / 2;
            vert_y = (height_se + height_ne) >> 1;

            vert_underlay_color_hsl = mix_hsl(blended_underlay_hsl_se, blended_underlay_hsl_ne);
            vert_overlay_color_hsl = (overlay_hsl_se + overlay_hsl_ne) >> 1;
        }
        else if( vertex_index == 11 )
        {
            vert_x = tile_x + TILE_SIZE / 2;
            vert_z = tile_y + TILE_SIZE * 3 / 4;

            vert_y = (height_ne + height_nw) >> 1;

            vert_underlay_color_hsl = mix_hsl(blended_underlay_hsl_ne, blended_underlay_hsl_nw);
            vert_overlay_color_hsl = (overlay_hsl_ne + overlay_hsl_nw) >> 1;
        }
        else if( vertex_index == 12 )
        {
            vert_x = tile_x + TILE_SIZE / 4;
            vert_z = tile_y + TILE_SIZE / 2;
            vert_y = (height_nw + height_sw) >> 1;

            vert_underlay_color_hsl = mix_hsl(blended_underlay_hsl_nw, blended_underlay_hsl_sw);
            vert_overlay_color_hsl = (overlay_hsl_nw + overlay_hsl_sw) >> 1;
        }
        else if( vertex_index == 13 )
        {
            vert_x = tile_x + TILE_SIZE / 4;
            vert_z = tile_y + TILE_SIZE / 4;
            vert_y = height_sw;

            vert_underlay_color_hsl = blended_underlay_hsl_sw;
            vert_overlay_color_hsl = overlay_hsl_sw;
        }
        else if( vertex_index == 14 )
        {
            vert_x = tile_x + TILE_SIZE * 3 / 4;
            vert_z = tile_y + TILE_SIZE / 4;
            vert_y = height_se;

            vert_underlay_color_hsl = blended_underlay_hsl_se;
            vert_overlay_color_hsl = overlay_hsl_se;
        }
        else if( vertex_index == 15 )
        {
            vert_x = tile_x + TILE_SIZE * 3 / 4;
            vert_z = tile_y + TILE_SIZE * 3 / 4;
            vert_y = height_ne;

            vert_underlay_color_hsl = blended_underlay_hsl_ne;
            vert_overlay_color_hsl = overlay_hsl_ne;
        }
        else
        {
            vert_x = tile_x + TILE_SIZE / 4;
            vert_z = tile_y + TILE_SIZE * 3 / 4;
            vert_y = height_nw;

            vert_underlay_color_hsl = blended_underlay_hsl_nw;
            vert_overlay_color_hsl = overlay_hsl_nw;
        }

        // TODO: For the target level, subtract LEVEL_HEIGHT from the vertex y.
        vertex_x[i] = vert_x;
        vertex_y[i] = vert_y;
        vertex_z[i] = vert_z;

        underlay_colors_hsl[i] = vert_underlay_color_hsl;
        overlay_colors_hsl[i] = vert_overlay_color_hsl;
    }

    int* face_indices = tile_shape_faces[shape];
    int face_count = tile_shape_face_counts[shape] / 4;

    int* faces_a = (int*)malloc(face_count * sizeof(int));
    int* faces_b = (int*)malloc(face_count * sizeof(int));
    int* faces_c = (int*)malloc(face_count * sizeof(int));

    int* valid_faces = (int*)malloc(face_count * sizeof(int));

    int* face_colors_hsl_a = (int*)malloc(face_count * sizeof(int));
    int* face_colors_hsl_b = (int*)malloc(face_count * sizeof(int));
    int* face_colors_hsl_c = (int*)malloc(face_count * sizeof(int));

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
        if( is_overlay )
        {
            color_a = overlay_colors_hsl[a];
            color_b = overlay_colors_hsl[b];
            color_c = overlay_colors_hsl[c];
        }
        else
        {
            color_a = underlay_colors_hsl[a];
            color_b = underlay_colors_hsl[b];
            color_c = underlay_colors_hsl[c];
        }

        face_colors_hsl_a[i] = color_a;
        face_colors_hsl_b[i] = color_b;
        face_colors_hsl_c[i] = color_c;

        if( color_a == INVALID_HSL_COLOR )
            valid_faces[i] = 0;
        else
            valid_faces[i] = 1;
    }

    free(underlay_colors_hsl);
    free(overlay_colors_hsl);

    tile->vertex_count = vertex_count;
    tile->vertex_x = vertex_x;
    tile->vertex_y = vertex_y;
    tile->vertex_z = vertex_z;
    tile->face_count = face_count;
    tile->faces_a = faces_a;
    tile->faces_b = faces_b;
    tile->faces_c = faces_c;

    tile->valid_faces = valid_faces;
    tile->face_color_hsl_a = face_colors_hsl_a;
    tile->face_color_hsl_b = face_colors_hsl_b;
    tile->face_color_hsl_c = face_colors_hsl_c;
    return true;
error:
    free(valid_faces);
    free(vertex_x);
    free(vertex_y);
    free(vertex_z);
    free(underlay_colors_hsl);
    free(overlay_colors_hsl);
    free(faces_a);
    free(faces_b);
    free(faces_c);
    free(face_colors_hsl_a);
    free(face_colors_hsl_b);
    free(face_colors_hsl_c);
    return false;
}
extern int g_cos_table[2048];

static int
interpolate(int i, int i_4_, int i_5_, int freq)
{
    int i_8_ = (65536 - g_cos_table[(i_5_ * 1024) / freq]) >> 1;
    return ((i_8_ * i_4_) >> 16) + (((65536 - i_8_) * i) >> 16);
}

static int
noise(int x, int y)
{
    int n = y * 57 + x;
    n = (n << 13) ^ n;
    int n2 = (n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff;
    return (n2 >> 19) & 0xff;
}

static int
smoothedNoise1(int x, int y)
{
    int corners =
        noise(x - 1, y - 1) + noise(x + 1, y - 1) + noise(x - 1, y + 1) + noise(x + 1, y + 1);
    int sides = noise(x - 1, y) + noise(x + 1, y) + noise(x, y - 1) + noise(x, y + 1);
    int center = noise(x, y);
    return (center / 4) + (sides / 8) + (corners / 16);
}

static int
interpolateNoise(int x, int y, int freq)
{
    int intX = x / freq;
    int fracX = x & (freq - 1);
    int intY = y / freq;
    int fracY = y & (freq - 1);
    int v1 = smoothedNoise1(intX, intY);
    int v2 = smoothedNoise1(intX + 1, intY);
    int v3 = smoothedNoise1(intX, intY + 1);
    int v4 = smoothedNoise1(intX + 1, intY + 1);
    int i1 = interpolate(v1, v2, fracX, freq);
    int i2 = interpolate(v3, v4, fracX, freq);
    return interpolate(i1, i2, fracY, freq);
}

static int
generateHeight(int x, int y)
{
    int n = interpolateNoise(x + 45365, y + 91923, 4) - 128 +
            ((interpolateNoise(x + 10294, y + 37821, 2) - 128) >> 1) +
            ((interpolateNoise(x, y, 1) - 128) >> 2);
    n = (int)(0.3 * n) + 35;
    if( n < 10 )
    {
        n = 10;
    }
    else if( n > 60 )
    {
        n = 60;
    }
    return n;
}

/**
 * Normally, some of this calculation is done in the map terrain loader.
 *
 * The deob meteor client and rs map viewer do this calculation there.
 * src/rs/scene/SceneBuilder.ts decodeTerrainTile
 */
static void
fix_terrain_tile(struct MapTerrain* map_terrain, int world_scene_origin_x, int world_scene_origin_y)
{
    for( int z = 0; z < MAP_TERRAIN_Z; z++ )
    {
        for( int y = 0; y < MAP_TERRAIN_Y - 1; y++ )
        {
            for( int x = 0; x < MAP_TERRAIN_X - 1; x++ )
            {
                struct MapTile* map = &map_terrain->tiles_xyz[MAP_TILE_COORD(x, y, z)];

                if( map->height == 0 )
                {
                    if( z == 0 )
                    {
                        int worldX = world_scene_origin_x + (-58) + 932731;
                        int worldY = world_scene_origin_y + (-58) + 556238;
                        map->height = -generateHeight(worldX, worldY) * MAP_UNITS_TILE_HEIGHT_BASIS;
                    }
                    else
                    {
                        int lower = map_terrain->tiles_xyz[MAP_TILE_COORD(x, y, z - 1)].height;
                        map->height = lower - MAP_UNITS_LEVEL_HEIGHT;
                    }
                }
                else
                {
                    if( map->height == 1 )
                        map->height = 0;

                    if( z == 0 )
                    {
                        map->height = -map->height * MAP_UNITS_TILE_HEIGHT_BASIS;
                    }
                    else
                    {
                        map->height = map_terrain->tiles_xyz[MAP_TILE_COORD(x, y, z - 1)].height -
                                      map->height * MAP_UNITS_TILE_HEIGHT_BASIS;
                    }
                }
            }
        }
    }
}

#define LIGHT_DIR_X -50
#define LIGHT_DIR_Y -10
#define LIGHT_DIR_Z -50
#define LIGHT_INTENSITY_BASE 96
#define LIGHT_INTENSITY_FACTOR 768
#define HEIGHT_SCALE 65536

static int*
calculate_lights(struct MapTerrain* map_terrain, int level)
{
    int* lights = (int*)malloc(MAP_TERRAIN_X * MAP_TERRAIN_Y * sizeof(int));
    memset(lights, 0, MAP_TERRAIN_X * MAP_TERRAIN_Y * sizeof(int));

    int magnitude =
        sqrt(LIGHT_DIR_X * LIGHT_DIR_X + LIGHT_DIR_Y * LIGHT_DIR_Y + LIGHT_DIR_Z * LIGHT_DIR_Z);
    int intensity = (magnitude * LIGHT_INTENSITY_FACTOR) >> 8;

    for( int y = 1; y < MAP_TERRAIN_Y - 1; y++ )
    {
        for( int x = 1; x < MAP_TERRAIN_X - 1; x++ )
        {
            // First we need to calculate the normals for each tile.
            // This is typically by doing a cross product on the tangent vectors which can be
            // derived from the differences in height between adjacent tiles. The code below seems
            // to be calculating the normals directly by skipping the cross product.

            int height_delta_x = map_terrain->tiles_xyz[MAP_TILE_COORD(x + 1, y, level)].height -
                                 map_terrain->tiles_xyz[MAP_TILE_COORD(x - 1, y, level)].height;
            int height_delta_y = map_terrain->tiles_xyz[MAP_TILE_COORD(x, y + 1, level)].height -
                                 map_terrain->tiles_xyz[MAP_TILE_COORD(x, y - 1, level)].height;
            // const tileNormalLength =
            //                 Math.sqrt(
            //                     heightDeltaY * heightDeltaY + heightDeltaX * heightDeltaX +
            //                     HEIGHT_SCALE,
            //                 ) | 0;

            //             const normalizedTileNormalX = ((heightDeltaX << 8) / tileNormalLength) |
            //             0; const normalizedTileNormalY = (HEIGHT_SCALE / tileNormalLength) | 0;
            //             const normalizedTileNormalZ = ((heightDeltaY << 8) / tileNormalLength) |
            //             0;

            int tile_normal_length = sqrt(
                height_delta_y * height_delta_y + height_delta_x * height_delta_x + HEIGHT_SCALE);

            int normalized_tile_normal_x = (height_delta_x << 8) / tile_normal_length;
            int normalized_tile_normal_y = HEIGHT_SCALE / tile_normal_length;
            int normalized_tile_normal_z = (height_delta_y << 8) / tile_normal_length;

            // Now we calculate the light contribution based on a simplified Phong model,
            // specifically
            // we ignore the material coefficients and there are no specular contributions.

            // For reference, this is the standard Phong model:
            //  I = Ia * Ka + Id * Kd * (N dot L)
            //  I: Total intensity of light at a point on the surface.
            //  Ia: Intensity of ambient light in the scene (constant and uniform).
            //  Ka: Ambient reflection coefficient of the material.
            //  Id: Intensity of the directional (diffuse) light source.
            //  Kd: Diffuse reflection coefficient of the material.
            //  N: Normalized surface normal vector at the point.
            //  L: Normalized direction vector from the point to the light source.
            //  (N dot L): Dot product between the surface normal vector and the light direction
            //  vector.

            // const dot =
            //     normalizedTileNormalX * LIGHT_DIR_X +
            //     normalizedTileNormalY * LIGHT_DIR_Y +
            //     normalizedTileNormalZ * LIGHT_DIR_Z;
            // const sunLight = (dot / lightIntensity + LIGHT_INTENSITY_BASE) | 0;

            int dot = normalized_tile_normal_x * LIGHT_DIR_X +
                      normalized_tile_normal_y * LIGHT_DIR_Y +
                      normalized_tile_normal_z * LIGHT_DIR_Z;
            int sunlight = (dot / intensity + LIGHT_INTENSITY_BASE) | 0;

            lights[MAP_TILE_COORD(x, y, 0)] = sunlight;
        }
    }

    return lights;
}

struct SceneTile*
scene_tiles_new_from_map_terrain(
    struct MapTerrain* map_terrain,
    struct Overlay* overlays,
    int* overlay_ids,
    int overlays_count,
    struct Underlay* underlays,
    int* underlay_ids,
    int underlays_count)
{
    struct Underlay* underlay = NULL;
    struct Overlay* overlay = NULL;
    printf("MAP_TILE_COUNT: %d\n", MAP_TILE_COUNT);
    struct SceneTile* tiles = (struct SceneTile*)malloc(MAP_TILE_COUNT * sizeof(struct SceneTile));

    fix_terrain_tile(map_terrain, 50 * MAP_CHUNK_SIZE, 50 * MAP_CHUNK_SIZE);

    if( !tiles )
    {
        fprintf(stderr, "Failed to allocate memory for tiles\n");
        return NULL;
    }

    // Blend underlays and calculate lights

    for( int i = 0; i < MAP_TILE_COUNT; i++ )
        memset(&tiles[i], 0, sizeof(struct SceneTile));

    for( int z = 0; z < 1; z++ )
    {
        int* blended_underlays =
            blend_underlays(map_terrain, underlays, underlay_ids, underlays_count, z);
        int* lights = calculate_lights(map_terrain, z);

        for( int y = 0; y < MAP_TERRAIN_Y - 1; y++ )
        {
            for( int x = 0; x < MAP_TERRAIN_X - 1; x++ )
            {
                struct MapTile* map = &map_terrain->tiles_xyz[MAP_TILE_COORD(x, y, z)];

                struct SceneTile* scene_tile = &tiles[MAP_TILE_COORD(x, y, z)];
                int underlay_id = map->underlay_id - 1;

                int overlay_id = map->overlay_id - 1;

                if( underlay_id == -1 && overlay_id == -1 )
                    continue;

                int height_sw = map_terrain->tiles_xyz[MAP_TILE_COORD(x, y, z)].height;
                int height_se = map_terrain->tiles_xyz[MAP_TILE_COORD(x + 1, y, z)].height;
                int height_ne = map_terrain->tiles_xyz[MAP_TILE_COORD(x + 1, y + 1, z)].height;
                int height_nw = map_terrain->tiles_xyz[MAP_TILE_COORD(x, y + 1, z)].height;

                int light_sw = lights[MAP_TILE_COORD(x, y, 0)];
                int light_se = lights[MAP_TILE_COORD(x + 1, y, 0)];
                int light_ne = lights[MAP_TILE_COORD(x + 1, y + 1, 0)];
                int light_nw = lights[MAP_TILE_COORD(x, y + 1, 0)];

                // Just get the underlay color for now.

                int underlay_hsl_sw = -1;
                int underlay_hsl_se = -1;
                int underlay_hsl_ne = -1;
                int underlay_hsl_nw = -1;
                int overlay_hsl = 0;

                if( underlay_id != -1 )
                {
                    int underlay_index = get_index(underlay_ids, underlays_count, underlay_id);
                    assert(underlay_index != -1);

                    underlay = &underlays[underlay_index];
                    underlay_hsl_sw = blended_underlays[COLOR_COORD(x, y)];
                    underlay_hsl_se = blended_underlays[COLOR_COORD(x + 1, y)];
                    underlay_hsl_ne = blended_underlays[COLOR_COORD(x + 1, y + 1)];
                    underlay_hsl_nw = blended_underlays[COLOR_COORD(x, y + 1)];

                    if( underlay_hsl_se == -1 )
                        underlay_hsl_se = underlay_hsl_sw;
                    if( underlay_hsl_ne == -1 )
                        underlay_hsl_ne = underlay_hsl_sw;
                    if( underlay_hsl_nw == -1 )
                        underlay_hsl_nw = underlay_hsl_sw;
                }

                if( overlay_id != -1 )
                {
                    int overlay_index = get_index(overlay_ids, overlays_count, overlay_id);
                    assert(overlay_index != -1);

                    overlay = &overlays[overlay_index];

                    if( overlay->texture != -1 )
                        overlay_hsl = -1;
                    else
                        overlay_hsl = palette_rgb_to_hsl16(overlay->rgb_color);
                }

                int shape = overlay_id == -1 ? 0 : (map->shape + 1);
                int rotation = overlay_id == -1 ? 0 : map->rotation;

                bool success = decode_tile(
                    scene_tile,
                    shape,
                    rotation,
                    x,
                    y,
                    z,
                    height_sw,
                    height_se,
                    height_ne,
                    height_nw,
                    light_sw,
                    light_se,
                    light_ne,
                    light_nw,
                    underlay_hsl_sw,
                    underlay_hsl_se,
                    underlay_hsl_ne,
                    underlay_hsl_nw,
                    // Overlay color.
                    overlay_hsl);

                assert(success);
            }
        }

        free(blended_underlays);
        free(lights);
    }

    return tiles;
}