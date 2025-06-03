#include "scene_tile.h"

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
    if( !tiles )
    {
        fprintf(stderr, "Failed to allocate memory for tiles\n");
        fclose(file);
        return NULL;
    }

    // Initialize all pointers to NULL
    for( int i = 0; i < num_tiles; i++ )
    {
        tiles[i].vertex_x = NULL;
        tiles[i].vertex_y = NULL;
        tiles[i].vertex_z = NULL;
        tiles[i].faces_a = NULL;
        tiles[i].faces_b = NULL;
        tiles[i].faces_c = NULL;
        tiles[i].face_color_hsl = NULL;
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