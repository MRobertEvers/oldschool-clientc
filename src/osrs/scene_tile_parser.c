#include "blend_underlays.h"
#include "osrs/tables/config_floortype.h"
#include "palette.h"
#include "scene_tile.h"

#include <assert.h>
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
    4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6,
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

            // vert_underlay_color_hsl = blended_underlay_hsl_sw;
            // vert_overlay_color_hsl = overlay_hsl;
        }
        else if( vertex_index == 2 )
        {
            vert_x = tile_x + TILE_SIZE / 2;
            vert_z = tile_y;
            vert_y = (height_se + height_sw) >> 1;
            // vert_y = height_sw;
        }
        else if( vertex_index == 3 )
        {
            vert_x = tile_x + TILE_SIZE;
            vert_z = tile_y;
            vert_y = height_se;
        }
        else if( vertex_index == 4 )
        {
            vert_x = tile_x + TILE_SIZE;
            vert_z = tile_y + TILE_SIZE / 2;
            vert_y = (height_ne + height_se) >> 1;
            // vert_y = height_sw;
        }
        else if( vertex_index == 5 )
        {
            vert_x = tile_x + TILE_SIZE;
            vert_z = tile_y + TILE_SIZE;
            vert_y = height_ne;
        }
        else if( vertex_index == 6 )
        {
            vert_x = tile_x + TILE_SIZE / 2;
            vert_z = tile_y + TILE_SIZE;
            vert_y = (height_ne + height_nw) >> 1;
            // vert_y = height_sw;
        }
        else if( vertex_index == 7 )
        {
            vert_x = tile_x;
            vert_z = tile_y + TILE_SIZE;
            vert_y = height_nw;
        }
        else if( vertex_index == 8 )
        {
            vert_x = tile_x;
            vert_z = tile_y + TILE_SIZE / 2;
            vert_y = (height_nw + height_sw) >> 1;
            // vert_y = height_sw;
        }
        else if( vertex_index == 9 )
        {
            vert_x = tile_x + TILE_SIZE / 2;
            vert_z = tile_y + TILE_SIZE / 4;
            vert_y = (height_sw + height_se) >> 1;
        }
        else if( vertex_index == 10 )
        {
            vert_x = tile_x + TILE_SIZE * 3 / 4;
            vert_z = tile_y + TILE_SIZE / 2;
            vert_y = (height_se + height_ne) >> 1;
        }
        else if( vertex_index == 11 )
        {
            vert_x = tile_x + TILE_SIZE / 2;
            vert_z = tile_y + TILE_SIZE * 3 / 4;

            vert_y = (height_ne + height_nw) >> 1;
        }
        else if( vertex_index == 12 )
        {
            vert_x = tile_x + TILE_SIZE / 4;
            vert_z = tile_y + TILE_SIZE / 2;
            vert_y = (height_nw + height_sw) >> 1;
        }
        else if( vertex_index == 13 )
        {
            vert_x = tile_x + TILE_SIZE / 4;
            vert_z = tile_y + TILE_SIZE / 4;
        }
        else if( vertex_index == 14 )
        {
            vert_x = tile_x + TILE_SIZE * 3 / 4;
            vert_z = tile_y + TILE_SIZE / 4;
            vert_y = height_se;
        }
        else if( vertex_index == 15 )
        {
            vert_x = tile_x + TILE_SIZE * 3 / 4;
            vert_z = tile_y + TILE_SIZE * 3 / 4;
            vert_y = height_ne;
        }
        else
        {
            vert_x = tile_x + TILE_SIZE / 4;
            vert_z = tile_y + TILE_SIZE * 3 / 4;
            vert_y = height_nw;
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

        int color_a = is_overlay ? overlay_colors_hsl[a] : underlay_colors_hsl[a];
        int color_b = is_overlay ? overlay_colors_hsl[b] : underlay_colors_hsl[b];
        int color_c = is_overlay ? overlay_colors_hsl[c] : underlay_colors_hsl[c];

        face_colors_hsl_a[i] = color_a;
        face_colors_hsl_b[i] = color_b;
        face_colors_hsl_c[i] = color_c;
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

    tile->face_color_hsl_a = face_colors_hsl_a;
    tile->face_color_hsl_b = face_colors_hsl_b;
    tile->face_color_hsl_c = face_colors_hsl_c;
    return true;
error:
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

    int* blended_underlays = blend_underlays(map_terrain, underlays, underlay_ids, underlays_count);

    for( int i = 0; i < MAP_TILE_COUNT; i++ )
        memset(&tiles[i], 0, sizeof(struct SceneTile));

    for( int z = 0; z < MAP_TERRAIN_Z; z++ )
    {
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

                // Just get the underlay color for now.

                int underlay_hsl_sw = 0;
                int underlay_hsl_se = 0;
                int underlay_hsl_ne = 0;
                int underlay_hsl_nw = 0;
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

                    overlay_hsl = underlay_hsl_sw;
                }

                if( overlay_id != -1 )
                {
                    int overlay_index = get_index(overlay_ids, overlays_count, overlay_id);
                    assert(overlay_index != -1);

                    overlay = &overlays[overlay_index];

                    overlay_hsl = palette_rgb_to_hsl16(overlay->rgb_color);
                }

                int shape = map->overlay_id == -1 ? 0 : map->shape + 1;
                int rotation = map->overlay_id == -1 ? 0 : map->rotation;

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
                    underlay_hsl_sw,
                    underlay_hsl_se,
                    underlay_hsl_ne,
                    underlay_hsl_nw,
                    // Overlay color.
                    overlay_hsl);

                assert(success);
            }
        }
    }

    return tiles;
}